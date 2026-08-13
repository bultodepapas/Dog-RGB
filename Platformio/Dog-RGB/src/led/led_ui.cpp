#include "led/led_ui.h"

#include <Arduino.h>
#include <WiFi.h>

#include "config/runtime_config.h"
#include "config.h"
#include "geofence/home.h"
#include "gps/gps.h"
#include "led/effect_registry.h"
#include "led/led_bus.h"
#include "led/led_compositor.h"
#include "led/led_policy.h"
#include "led/palette_registry.h"
#include "led/scene_runtime.h"
#include "pins.h"
#include "power/day_mode.h"
#include "wifi/wifi_mgr.h"

namespace led_ui {
namespace {
static_assert(EFFECT_COUNT == led::EFFECT_REGISTRY_COUNT,
              "Persisted effect IDs and registry entries must stay aligned");
// LED strip configuration is defined in config.h.
unsigned long last_led_update_ms = 0;

unsigned long last_ok_ms = 0;
unsigned long gps_fix_ms = 0;
unsigned long last_gps_fix_ms = 0;

const led::LedLayoutConfig LED_LAYOUT_CONFIG = {
    LED_STRIP_COUNT,
    LED_STATUS_COUNT,
    LED_STRIP_MODE == 2,
    LED_BUS_A_REVERSED ? led::LedOrientation::Reverse
                       : led::LedOrientation::Forward,
    LED_BUS_B_REVERSED ? led::LedOrientation::Reverse
                       : led::LedOrientation::Forward,
    LED_LAYOUT_MIRROR_DEFAULT};

// Effects render into logical branch coordinates. The compositor is the only
// owner of orientation, mirroring, semantic regions and body transitions.
led::LedFrame render_frame = {};
led::LedFrame led_frame = {};
Rgb *const leds_a = render_frame.bus_a;
Rgb *const leds_b = render_frame.bus_b;
Rgb *const physical_leds_a = led_frame.bus_a;
Rgb *const physical_leds_b = led_frame.bus_b;
uint8_t heat_a[LED_STRIP_COUNT];
uint8_t heat_b[LED_STRIP_COUNT];

led::LedBus led_bus(LED_STRIP_COUNT, PIN_LED_A_DATA, PIN_LED_B_DATA,
                    LED_STRIP_MODE == 2);
led::LedCompositor compositor(LED_LAYOUT_CONFIG);

using EffectState = led::EffectRuntime;

EffectState state_a;
EffectState state_b;
bool led_transport_enabled = true;
uint32_t effect_random_state = 0xD06F00D5UL;
led::LedPolicyEngine policy_engine;
led::LedState active_led_state = {};
led::LedState previous_visual_state = {};
bool has_previous_visual_state = false;

struct WelcomeState {
  bool active = false;
  uint8_t color_index = 0;
  uint8_t laps_done = 0;
};

WelcomeState welcome;
EffectState welcome_state_a;
EffectState welcome_state_b;
const uint8_t WELCOME_LAPS = 5;
const uint8_t WELCOME_SPEED = 32;
// fade_amt per-frame: 160 → head + ~2 dim trailing LEDs visible (short comet tail).
const uint8_t WELCOME_FADE_AMT = 160;
const Rgb WELCOME_COLORS[5] = {
  {255, 0, 0},     // rojo
  {255, 255, 255}, // blanco
  {255, 0, 128},   // rosado
  {0, 0, 255},     // azul
  {0, 255, 0}      // verde
};
} // namespace
static uint8_t clamp_u8(int value) {
  if (value < 0) {
    return 0;
  }
  if (value > 255) {
    return 255;
  }
  return static_cast<uint8_t>(value);
}

static Rgb make_rgb(uint8_t r, uint8_t g, uint8_t b) {
  return Rgb{r, g, b};
}

static uint8_t scale8(uint8_t value, uint8_t scale) {
  return static_cast<uint8_t>((static_cast<uint16_t>(value) * scale) / 255);
}

static Rgb scale_rgb(const Rgb &c, float scale) {
  const uint8_t s = clamp_u8(static_cast<int>(scale * 255.0f));
  return make_rgb(scale8(c.r, s), scale8(c.g, s), scale8(c.b, s));
}

static Rgb scale_rgb8(const Rgb &c, uint8_t scale) {
  return make_rgb(scale8(c.r, scale), scale8(c.g, scale), scale8(c.b, scale));
}

static void fade_rgb(Rgb &c, uint8_t amount) {
  const uint8_t scale = static_cast<uint8_t>(255 - amount);
  c.r = scale8(c.r, scale);
  c.g = scale8(c.g, scale);
  c.b = scale8(c.b, scale);
}

static float pulse_scale(unsigned long period_ms) {
  const unsigned long now_ms = millis();
  const float phase = static_cast<float>(now_ms % period_ms) / static_cast<float>(period_ms);
  if (phase < 0.5f) {
    return phase * 2.0f;
  }
  return (1.0f - phase) * 2.0f;
}

static float double_pulse_scale(unsigned long period_ms, unsigned long pulse_ms) {
  const unsigned long t = millis() % period_ms;
  if (t < pulse_ms) {
    return static_cast<float>(t) / static_cast<float>(pulse_ms);
  }
  if (t < pulse_ms * 2) {
    return static_cast<float>((pulse_ms * 2) - t) / static_cast<float>(pulse_ms);
  }
  if (t < pulse_ms * 3) {
    return static_cast<float>(t - (pulse_ms * 2)) / static_cast<float>(pulse_ms);
  }
  if (t < pulse_ms * 4) {
    return static_cast<float>((pulse_ms * 4) - t) / static_cast<float>(pulse_ms);
  }
  return 0.0f;
}

static uint8_t effective_brightness(uint8_t brightness) {
  return LED_DEBUG_BRIGHTNESS_ENABLED ? LED_DEBUG_BRIGHTNESS : brightness;
}

static led::PowerLimitConfig power_config_from(const RuntimeConfig &cfg) {
  return led::PowerLimitConfig{cfg.led_power_limit_enabled,
                               cfg.led_power_budget_ma,
                               cfg.led_base_current_ma,
                               cfg.led_rgb_channel_ma,
                               cfg.led_white_channel_ma};
}

static void led_begin() {
  led_bus.configure_power(power_config_from(config::get()));
  led_bus.begin(effective_brightness(config::get().brightness));
}

static void show_leds() {
  if (!led_transport_enabled) {
    return;
  }
#if defined(DOG_RGB_WOKWI_LED_SHOW_MS)
  static unsigned long last_transport_ms = 0;
  const unsigned long now_ms = millis();
  if (last_transport_ms != 0 &&
      now_ms - last_transport_ms < DOG_RGB_WOKWI_LED_SHOW_MS) {
    return;
  }
  last_transport_ms = now_ms;
#endif
  led_bus.show(led_frame);
  scene_runtime::note_led_frame(micros());
}

static void fill_range(Rgb *leds, int start, int count, const Rgb &color) {
  for (int i = start; i < start + count; ++i) {
    leds[i] = color;
  }
}

static void fade_range(Rgb *leds, int start, int count, uint8_t amount) {
  for (int i = start; i < start + count; ++i) {
    fade_rgb(leds[i], amount);
  }
}

static uint8_t step_from_speed(uint8_t speed, uint8_t divisor) {
  const uint8_t step = speed / divisor;
  return step < 1 ? 1 : step;
}

static led::LedMode led_mode_from(uint8_t mode) {
  switch (mode) {
    case MODE_GEOFENCE: return led::LedMode::Geofence;
    case MODE_SHOW: return led::LedMode::Show;
    case MODE_SIMPLE: return led::LedMode::Simple;
    default: return led::LedMode::Speed;
  }
}

static led::LedPolicyConfig policy_config_from(const RuntimeConfig &cfg) {
  led::LedPolicyConfig adapted = {};
  adapted.brightness = cfg.brightness;
  adapted.transition_ms = LED_TRANSITION_MS;
  adapted.mirror_equal_effects =
      LED_LAYOUT_MIRROR_DEFAULT && LED_STRIP_MODE == 2;
  for (uint8_t i = 0; i < 9; ++i) {
    adapted.speed_ranges_kph[i] = cfg.ranges[i];
  }
  for (uint8_t i = 0; i < 10; ++i) {
    adapted.range_effects[i] = {cfg.effects[i].effect_a,
                                cfg.effects[i].effect_b,
                                cfg.effects[i].speed,
                                cfg.effects[i].intensity};
  }
  adapted.simple = {cfg.single.effect_id, cfg.single.effect_id,
                    cfg.single.speed, cfg.single.intensity};
  adapted.simple_base = {cfg.single.base_r, cfg.single.base_g,
                         cfg.single.base_b};
  return adapted;
}

static led::LedState evaluate_policy(unsigned long now_ms, bool gps_ok,
                                     bool critical_error) {
  const RuntimeConfig &cfg = config::get();
  const led::LedPolicyConfig adapted = policy_config_from(cfg);
  const bool home_set = geofence::is_set();
  const float distance_m = home_set ? geofence::distance_to_home_m() : -1.0f;
  uint8_t geofence_range = 1;
  if (distance_m >= 0.0f) {
    geofence_range = geofence::geofence_range(distance_m);
    geofence_range = geofence::apply_hysteresis(geofence_range, distance_m);
  }
  led::LedPolicyInput input = {};
  input.mode = led_mode_from(cfg.mode);
  input.config = &adapted;
  input.welcome_active = welcome.active;
  input.day_mode_active = day_mode::active_now();
  input.gps_ok = gps_ok;
  input.critical_error = critical_error;
  input.geofence_alert = cfg.mode == MODE_GEOFENCE && gps_ok && home_set &&
                          distance_m >= static_cast<float>(cfg.fence_max_m);
  input.wifi_off = wifi_mgr::wifi_off();
  input.home_set = home_set;
  input.geofence_distance_valid = distance_m >= 0.0f;
  input.homogeneous_ready = gps_fix_ms >= WIFI_OFF_GPS_FIX_MS;
  input.speed_kph = gps::last_speed_kph();
  input.geofence_range = geofence_range;
  const led::ScenePlayer &player = scene_runtime::player();
  input.scene = player.active_scene();
  input.scene_manual = player.playback() == led::ScenePlayback::Manual;
  input.scene_activation_revision = player.activation_revision();
  (void)now_ms;
  return policy_engine.evaluate(input);
}

uint8_t speed_range(float kph) {
  const led::LedPolicyConfig adapted = policy_config_from(config::get());
  return led::policy_speed_range(adapted, kph);
}

void get_range_config(uint8_t range,
                             int &effect_a,
                             int &effect_b,
                             uint8_t &speed,
                             uint8_t &intensity) {
  const uint8_t idx = (range > 0 && range <= 10) ? static_cast<uint8_t>(range - 1) : 0;
  effect_a = config::get().effects[idx].effect_a;
  effect_b = config::get().effects[idx].effect_b;
  speed = config::get().effects[idx].speed;
  intensity = config::get().effects[idx].intensity;
}

Rgb base_color_for_range(uint8_t range) {
  return led::policy_range_base_color(range);
}

const char *effect_name(uint8_t effect_id) {
  const led::EffectDescriptor *descriptor = led::effect_descriptor(effect_id);
  return descriptor == nullptr ? "UNKNOWN" : descriptor->label;
}

static void apply_effect(int effect_id,
                         Rgb *leds,
                         uint8_t *heat,
                         int start,
                         int count,
                         const Rgb &base,
                         const Rgb &accent,
                         uint8_t palette_id,
                         uint8_t speed,
                         uint8_t intensity,
                         EffectState &state) {
  led::EffectRenderContext context = {
      leds, heat, static_cast<uint16_t>(start), static_cast<uint16_t>(count),
      base, accent, palette_id, speed, intensity, millis(),
      &effect_random_state, &state};
  led::render_effect(static_cast<uint8_t>(effect_id), context);
}

void start_welcome() {
  welcome.active = true;
  welcome.color_index = 0;
  welcome.laps_done = 0;
  welcome_state_a = {};
  welcome_state_b = {};
  compositor.cancel_transition();
  led_bus.set_brightness(effective_brightness(255));
  fill_range(physical_leds_a, 0, LED_STRIP_COUNT, make_rgb(0, 0, 0));
  if (LED_STRIP_MODE == 2) {
    fill_range(physical_leds_b, 0, LED_STRIP_COUNT, make_rgb(0, 0, 0));
  }
  active_led_state = evaluate_policy(millis(), gps::has_fix(), false);
  previous_visual_state = active_led_state;
  has_previous_visual_state = true;
  show_leds();
}

static void apply_welcome_chase(Rgb *physical,
                                led::LedBusId bus,
                                int count,
                                const Rgb &base,
                                uint8_t speed,
                                EffectState &state) {
  if (count <= 0) {
    return;
  }
  fade_range(physical, 0, count, WELCOME_FADE_AMT);
  state.pos = (state.pos + step_from_speed(speed, 32)) % count;
  const uint16_t write_pos =
      compositor.layout().physical_full_index(bus, state.pos);
  physical[write_pos] = base;
}

static void update_welcome(unsigned long now_ms) {
  if (now_ms - last_led_update_ms < LED_UPDATE_MS) {
    return;
  }
  last_led_update_ms = now_ms;

  const Rgb base = WELCOME_COLORS[welcome.color_index];
  const uint16_t prev_pos = welcome_state_a.pos;

  apply_welcome_chase(physical_leds_a, led::LedBusId::A, LED_STRIP_COUNT,
                      base, WELCOME_SPEED, welcome_state_a);
  if (LED_STRIP_MODE == 2) {
    apply_welcome_chase(physical_leds_b, led::LedBusId::B, LED_STRIP_COUNT,
                        base, WELCOME_SPEED, welcome_state_b);
  }
  show_leds();

  if (welcome_state_a.pos < prev_pos) {
    welcome.laps_done++;
    if (welcome.laps_done >= WELCOME_LAPS) {
      welcome.active = false;
      led_bus.set_brightness(effective_brightness(config::get().brightness));
    } else {
      welcome.color_index = static_cast<uint8_t>(welcome.color_index + 1);
    }
  }
}

static void update_gps_fix_timer(unsigned long now_ms, bool gps_ok) {
  if (last_gps_fix_ms == 0) {
    last_gps_fix_ms = now_ms;
  }
  const unsigned long fix_dt = now_ms - last_gps_fix_ms;
  last_gps_fix_ms = now_ms;
  if (gps_ok) {
    gps_fix_ms = (gps_fix_ms + fix_dt > WIFI_OFF_GPS_FIX_MS) ? WIFI_OFF_GPS_FIX_MS : (gps_fix_ms + fix_dt);
  } else {
    gps_fix_ms = 0;
  }
}

static bool compute_critical_error(unsigned long now_ms, bool gps_ok, bool sta_ok) {
  if (gps_ok || sta_ok) {
    last_ok_ms = now_ms;
  }
  return (!gps_ok && !sta_ok && (now_ms - last_ok_ms) > CRITICAL_NO_OK_MS);
}

static void paint_status_leds(unsigned long now_ms,
                              bool gps_ok,
                              bool sta_ok,
                              bool sta_try) {
  const Rgb wifi_base = make_rgb(0, 60, 0);
  const Rgb ap_base = make_rgb(60, 45, 0);
  const Rgb gps_base = make_rgb(0, 0, 60);
  const Rgb err_base = make_rgb(60, 0, 0);

  Rgb wifi_color = make_rgb(0, 0, 0);
  Rgb gps_color = make_rgb(0, 0, 0);

  if (wifi_mgr::wifi_off()) {
    const float pulse = double_pulse_scale(AP_OFF_PULSE_PERIOD_MS, AP_OFF_PULSE_MS);
    wifi_color = scale_rgb(ap_base, pulse);
  } else if (!sta_ok && wifi_mgr::ssid().length() > 0 && wifi_mgr::ap_enabled() && wifi_mgr::is_ap_mode()) {
    wifi_color = err_base;
  } else if (sta_ok) {
    wifi_color = wifi_base;
  } else if (sta_try) {
    wifi_color = scale_rgb(wifi_base, pulse_scale(1500));
  } else if (wifi_mgr::ap_enabled()) {
    if (wifi_mgr::ap_station_count() > 0) {
      wifi_color = scale_rgb(ap_base, pulse_scale(1500));
    } else {
      wifi_color = ap_base;
    }
  }

  if (gps_ok) {
    gps_color = gps_base;
  } else {
    gps_color = scale_rgb(gps_base, pulse_scale(1500));
  }

  if (LED_STATUS_COUNT > 0) {
    leds_a[0] = wifi_color;
    if (LED_STRIP_MODE == 2) {
      leds_b[0] = wifi_color;
    }
  }
  if (LED_STATUS_COUNT > 1) {
    leds_a[1] = gps_color;
    if (LED_STRIP_MODE == 2) {
      leds_b[1] = gps_color;
    }
  }
  for (int i = 2; i < LED_STATUS_COUNT; ++i) {
    leds_a[i] = make_rgb(0, 0, 0);
    if (LED_STRIP_MODE == 2) {
      leds_b[i] = make_rgb(0, 0, 0);
    }
  }
}

static void clear_body_leds() {
  const int seg_start = LED_STATUS_COUNT;
  const int seg_count = LED_STRIP_COUNT - LED_STATUS_COUNT;
  if (seg_count <= 0) {
    return;
  }
  fill_range(leds_a, seg_start, seg_count, make_rgb(0, 0, 0));
  if (LED_STRIP_MODE == 2) {
    fill_range(leds_b, seg_start, seg_count, make_rgb(0, 0, 0));
  }
}

static void clear_status_leds() {
  const int status_count =
      LED_STATUS_COUNT < LED_STRIP_COUNT ? LED_STATUS_COUNT : LED_STRIP_COUNT;
  fill_range(leds_a, 0, status_count, make_rgb(0, 0, 0));
  if (LED_STRIP_MODE == 2) {
    fill_range(leds_b, 0, status_count, make_rgb(0, 0, 0));
  }
}

static Rgb alert_color(unsigned long now_ms, led::LedAlert alert) {
  if (alert == led::LedAlert::System) {
    return (now_ms / 200UL) % 2UL == 0UL ? make_rgb(80, 0, 0)
                                          : make_rgb(20, 0, 0);
  }
  if (alert == led::LedAlert::Geofence) {
    const uint8_t pulse = static_cast<uint8_t>(
        80U + static_cast<uint8_t>(pulse_scale(900) * 175.0f));
    return scale_rgb8(make_rgb(180, 0, 0), pulse);
  }
  return make_rgb(0, 0, 0);
}

static void reset_render_runtime(EffectState &runtime_a,
                                 EffectState &runtime_b) {
  runtime_a = {};
  runtime_b = {};
  for (int i = 0; i < LED_STRIP_COUNT; ++i) {
    heat_a[i] = 0;
    heat_b[i] = 0;
  }
  clear_body_leds();
}

static void accept_led_state(const led::LedState &next,
                             unsigned long now_ms,
                             EffectState &runtime_a,
                             EffectState &runtime_b) {
  const bool visual_changed = !has_previous_visual_state ||
      !led::led_visual_state_equal(previous_visual_state, next);

  if (next.alert != led::LedAlert::None) {
    compositor.interrupt_for_alert();
  } else if (has_previous_visual_state && visual_changed &&
             next.body_enabled) {
    compositor.begin_transition(led_frame, now_ms, next.transition_ms);
  } else if (visual_changed && !next.body_enabled) {
    compositor.cancel_transition();
  }

  if (visual_changed) {
    reset_render_runtime(runtime_a, runtime_b);
  }
  active_led_state = next;
  previous_visual_state = next;
  has_previous_visual_state = true;
}

static void render_composed_frame(unsigned long now_ms, bool gps_ok,
                                  bool sta_ok, bool sta_try,
                                  EffectState &runtime_a,
                                  EffectState &runtime_b) {
  const int body_start = LED_STATUS_COUNT;
  const int body_count = LED_STRIP_COUNT - LED_STATUS_COUNT;
  if (!active_led_state.body_enabled || body_count <= 0) {
    clear_body_leds();
  } else {
    apply_effect(active_led_state.effect_a, leds_a, heat_a, body_start,
                 body_count, active_led_state.base,
                 active_led_state.accent, active_led_state.palette_a,
                 active_led_state.speed, active_led_state.intensity,
                 runtime_a);
    if (LED_STRIP_MODE == 2 && !active_led_state.mirror) {
      apply_effect(active_led_state.effect_b, leds_b, heat_b, body_start,
                   body_count, active_led_state.base,
                   active_led_state.accent, active_led_state.palette_b,
                   active_led_state.speed, active_led_state.intensity,
                   runtime_b);
    }
  }

  if (active_led_state.status_enabled) {
    paint_status_leds(now_ms, gps_ok, sta_ok, sta_try);
  } else {
    clear_status_leds();
  }
  compositor.compose(render_frame, led_frame, now_ms,
                     active_led_state.mirror,
                     active_led_state.status_enabled,
                     active_led_state.alert != led::LedAlert::None,
                     alert_color(now_ms, active_led_state.alert),
                     active_led_state.body_level);
  show_leds();
}

static void update_led_ui() {
  const unsigned long now_ms = millis();
  const RuntimeConfig &cfg = config::get();
  const bool body_permitted =
      LED_UI_ENABLED && !welcome.active && !day_mode::active_now();
  scene_runtime::tick(now_ms, cfg.mode, cfg.mode == MODE_SHOW,
                      body_permitted);
  if (!LED_UI_ENABLED) {
    return;
  }
  if (welcome.active) {
    update_welcome(now_ms);
    return;
  }
  if (now_ms - last_led_update_ms < LED_UPDATE_MS) {
    return;
  }
  last_led_update_ms = now_ms;

  const bool gps_ok = gps::has_fix();
  const bool sta_ok = (wifi_mgr::sta_connected() && WiFi.status() == WL_CONNECTED);
  const bool sta_try = (!sta_ok && wifi_mgr::sta_connecting());
  update_gps_fix_timer(now_ms, gps_ok);
  const bool critical_error = compute_critical_error(now_ms, gps_ok, sta_ok);
  const led::LedState next = evaluate_policy(now_ms, gps_ok, critical_error);
  accept_led_state(next, now_ms, state_a, state_b);
  render_composed_frame(now_ms, gps_ok, sta_ok, sta_try, state_a, state_b);
}

void begin() {
  // The pure renderer advances an explicit PRNG state so tests can replay a
  // fixed seed. Production still starts stochastic effects from fresh ESP32
  // hardware entropy instead of repeating the same sequence after every boot.
  effect_random_state = static_cast<uint32_t>(random(1, 0x7FFFFFFFL));
  led_begin();
}

void tick() {
  update_led_ui();
}

void apply_brightness(uint8_t brightness) {
  led_bus.set_brightness(effective_brightness(brightness));
}

void apply_power_config(bool enabled, uint16_t budget_ma,
                        uint16_t base_current_ma, uint8_t rgb_channel_ma,
                        uint8_t white_channel_ma) {
  led_bus.configure_power(led::PowerLimitConfig{enabled, budget_ma,
                                                base_current_ma,
                                                rgb_channel_ma,
                                                white_channel_ma});
}

const led::PowerDiagnostics &power_diagnostics() {
  return led_bus.power_diagnostics();
}

const led::TransitionDiagnostics &transition_diagnostics() {
  return compositor.diagnostics();
}

const led::LedLayout &layout() {
  return compositor.layout();
}

const led::LedState &current_state() {
  return active_led_state;
}

void set_transport_enabled(bool enabled) {
  led_transport_enabled = enabled;
}

bool transport_enabled() {
  return led_transport_enabled;
}

uint8_t current_show_effect() {
  const led::ScenePlayer &player = scene_runtime::player();
  const led::SceneV1 *scene = player.active_scene();
  return player.playback() == led::ScenePlayback::Show && scene != nullptr
             ? scene->effect_a
             : 0U;
}
} // namespace led_ui
