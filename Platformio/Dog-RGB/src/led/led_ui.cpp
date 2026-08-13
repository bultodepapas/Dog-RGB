#include "led/led_ui.h"

#include <Arduino.h>
#include <WiFi.h>

#include "config/runtime_config.h"
#include "config.h"
#include "geofence/home.h"
#include "gps/gps.h"
#include "led/effect_registry.h"
#include "led/led_bus.h"
#include "led/led_policy.h"
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

led::LedFrame led_frame = {};
Rgb *const leds_a = led_frame.bus_a;
Rgb *const leds_b = led_frame.bus_b;
uint8_t heat_a[LED_STRIP_COUNT];
uint8_t heat_b[LED_STRIP_COUNT];

led::LedBus led_bus(LED_STRIP_COUNT, PIN_LED_A_DATA, PIN_LED_B_DATA,
                    LED_STRIP_MODE == 2);

using EffectState = led::EffectRuntime;

EffectState state_a;
EffectState state_b;
uint8_t body_idle_hue = 0;
EffectState show_state_a;
EffectState show_state_b;
uint8_t show_effect_id = 0;
unsigned long show_effect_since_ms = 0;
Rgb show_base = {0, 0, 0};
Rgb show_next_base = {0, 0, 0};
uint8_t show_speed = SHOW_SPEED;
uint8_t show_intensity = SHOW_INTENSITY;
bool show_first_tick = true;
uint8_t show_effect_order[EFFECT_COUNT];
uint8_t show_effect_order_index = EFFECT_COUNT;
uint8_t show_last_effect_id = 255;
EffectState simple_state_a;
EffectState simple_state_b;
bool simple_first_tick = true;
bool led_transport_enabled = true;
uint8_t last_simple_effect = SINGLE_EFFECT_DEFAULT;
uint8_t last_simple_speed = SINGLE_SPEED_DEFAULT;
uint8_t last_simple_intensity = SINGLE_INTENSITY_DEFAULT;
uint8_t last_simple_r = SINGLE_R_DEFAULT;
uint8_t last_simple_g = SINGLE_G_DEFAULT;
uint8_t last_simple_b = SINGLE_B_DEFAULT;
uint8_t last_mode = MODE_SPEED;
uint32_t effect_random_state = 0xD06F00D5UL;
led::LedPolicyEngine policy_engine;
led::LedState active_led_state = {
    led::LedMode::Speed, led::LedIntent::Idle, 20, LED_BRIGHTNESS, true, true,
    false, false, -1, 9, 9, 80, 140, {0, 60, 60}};

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
const unsigned long SHOW_TRANSITION_MS = 500;
const Rgb WELCOME_COLORS[5] = {
  {255, 0, 0},     // rojo
  {255, 255, 255}, // blanco
  {255, 0, 128},   // rosado
  {0, 0, 255},     // azul
  {0, 255, 0}      // verde
};
const Rgb SHOW_PALETTE[] = {
  {255, 0, 80},
  {0, 180, 255},
  {0, 255, 120},
  {255, 180, 0},
  {150, 0, 255},
  {255, 255, 255},
  {255, 70, 0},
  {0, 70, 255},
  {255, 0, 190},
  {70, 255, 220},
  {255, 230, 60},
  {120, 255, 0}
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

static uint8_t blend_u8(uint8_t a, uint8_t b, uint8_t amount) {
  const uint16_t inv = static_cast<uint16_t>(255 - amount);
  return static_cast<uint8_t>(((static_cast<uint16_t>(a) * inv) +
                               (static_cast<uint16_t>(b) * amount)) / 255);
}

static Rgb blend_rgb(const Rgb &a, const Rgb &b, uint8_t amount) {
  return make_rgb(blend_u8(a.r, b.r, amount),
                  blend_u8(a.g, b.g, amount),
                  blend_u8(a.b, b.b, amount));
}

static void fade_rgb(Rgb &c, uint8_t amount) {
  const uint8_t scale = static_cast<uint8_t>(255 - amount);
  c.r = scale8(c.r, scale);
  c.g = scale8(c.g, scale);
  c.b = scale8(c.b, scale);
}

static uint8_t random8(uint8_t max_val = 255) {
  return static_cast<uint8_t>(random(0, static_cast<long>(max_val) + 1));
}

static uint8_t random8(uint8_t min_val, uint8_t max_val) {
  if (max_val <= min_val) {
    return min_val;
  }
  return static_cast<uint8_t>(random(min_val, static_cast<long>(max_val) + 1));
}

static Rgb hsv_to_rgb(uint8_t hue, uint8_t sat, uint8_t val) {
  const uint8_t region = hue / 43;
  const uint8_t remainder = (hue - (region * 43)) * 6;

  const uint8_t p = scale8(val, 255 - sat);
  const uint8_t q = scale8(val, 255 - scale8(sat, remainder));
  const uint8_t t = scale8(val, 255 - scale8(sat, 255 - remainder));

  switch (region) {
    case 0: return make_rgb(val, t, p);
    case 1: return make_rgb(q, val, p);
    case 2: return make_rgb(p, val, t);
    case 3: return make_rgb(p, q, val);
    case 4: return make_rgb(t, p, val);
    default: return make_rgb(val, p, q);
  }
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
                                     bool critical_error,
                                     const Rgb &active_show_base) {
  const RuntimeConfig &cfg = config::get();
  const led::LedPolicyConfig adapted = policy_config_from(cfg);
  const bool home_set = geofence::is_set();
  const float distance_m = home_set ? geofence::distance_to_home_m() : -1.0f;
  uint8_t geofence_range = 1;
  if (distance_m >= 0.0f) {
    geofence_range = geofence::geofence_range(distance_m);
    geofence_range = geofence::apply_hysteresis(geofence_range, distance_m);
  }
  const led::LedPolicyInput input = {
      led_mode_from(cfg.mode),
      &adapted,
      welcome.active,
      day_mode::active_now(),
      gps_ok,
      critical_error,
      wifi_mgr::wifi_off(),
      home_set,
      distance_m >= 0.0f,
      gps_fix_ms >= WIFI_OFF_GPS_FIX_MS,
      gps::last_speed_kph(),
      geofence_range,
      show_effect_id,
      show_speed,
      show_intensity,
      active_show_base};
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
                         uint8_t speed,
                         uint8_t intensity,
                         EffectState &state) {
  led::EffectRenderContext context = {
      leds, heat, static_cast<uint16_t>(start), static_cast<uint16_t>(count),
      base, speed, intensity, millis(), &effect_random_state, &state};
  led::render_effect(static_cast<uint8_t>(effect_id), context);
}

void start_welcome() {
  welcome.active = true;
  welcome.color_index = 0;
  welcome.laps_done = 0;
  welcome_state_a = {};
  welcome_state_b = {};
  led_bus.set_brightness(effective_brightness(255));
  fill_range(leds_a, 0, LED_STRIP_COUNT, make_rgb(0, 0, 0));
  if (LED_STRIP_MODE == 2) {
    fill_range(leds_b, 0, LED_STRIP_COUNT, make_rgb(0, 0, 0));
  }
  active_led_state = evaluate_policy(millis(), gps::has_fix(), false,
                                     show_base);
  show_leds();
}

static void apply_welcome_chase(Rgb *leds,
                                int start,
                                int count,
                                const Rgb &base,
                                uint8_t speed,
                                EffectState &state,
                                bool reverse) {
  if (count <= 0) {
    return;
  }
  fade_range(leds, start, count, WELCOME_FADE_AMT);
  state.pos = (state.pos + step_from_speed(speed, 32)) % count;
  const uint16_t write_pos = reverse ? static_cast<uint16_t>(count - 1 - state.pos) : state.pos;
  leds[start + write_pos] = base;
}

static void update_welcome(unsigned long now_ms) {
  if (now_ms - last_led_update_ms < LED_UPDATE_MS) {
    return;
  }
  last_led_update_ms = now_ms;

  const Rgb base = WELCOME_COLORS[welcome.color_index];
  const uint16_t prev_pos = welcome_state_a.pos;

  apply_welcome_chase(leds_a, 0, LED_STRIP_COUNT, base,
                      WELCOME_SPEED, welcome_state_a, false);
  if (LED_STRIP_MODE == 2) {
    apply_welcome_chase(leds_b, 0, LED_STRIP_COUNT, base,
                        WELCOME_SPEED, welcome_state_b, true);
  }
  show_leds();

  if (welcome_state_a.pos < prev_pos) {
    welcome.laps_done++;
    if (welcome.laps_done >= WELCOME_LAPS) {
      welcome.active = false;
      led_bus.set_brightness(effective_brightness(config::get().brightness));
      fill_range(leds_a, 0, LED_STRIP_COUNT, make_rgb(0, 0, 0));
      if (LED_STRIP_MODE == 2) {
        fill_range(leds_b, 0, LED_STRIP_COUNT, make_rgb(0, 0, 0));
      }
      show_leds();
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
                              bool sta_try,
                              bool critical_error) {
  const Rgb wifi_base = make_rgb(0, 60, 0);
  const Rgb ap_base = make_rgb(60, 45, 0);
  const Rgb gps_base = make_rgb(0, 0, 60);
  const Rgb err_base = make_rgb(60, 0, 0);

  Rgb wifi_color = make_rgb(0, 0, 0);
  Rgb gps_color = make_rgb(0, 0, 0);

  if (critical_error) {
    const float blink = (now_ms / 200) % 2 ? 1.0f : 0.0f;
    wifi_color = scale_rgb(err_base, blink);
    gps_color = wifi_color;
  } else {
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

static void render_day_mode_status(unsigned long now_ms,
                                   bool gps_ok,
                                   bool sta_ok,
                                   bool sta_try,
                                   bool critical_error) {
  clear_body_leds();
  paint_status_leds(now_ms, gps_ok, sta_ok, sta_try, critical_error);
  show_leds();
}

static Rgb random_show_color() {
  const size_t palette_size = sizeof(SHOW_PALETTE) / sizeof(SHOW_PALETTE[0]);
  Rgb color = SHOW_PALETTE[random8(static_cast<uint8_t>(palette_size - 1))];
  color.r = clamp_u8(static_cast<int>(color.r) + static_cast<int>(random8(0, 40)) - 20);
  color.g = clamp_u8(static_cast<int>(color.g) + static_cast<int>(random8(0, 40)) - 20);
  color.b = clamp_u8(static_cast<int>(color.b) + static_cast<int>(random8(0, 40)) - 20);
  return color;
}

static void clear_show_buffers() {
  fill_range(leds_a, 0, LED_STRIP_COUNT, make_rgb(0, 0, 0));
  if (LED_STRIP_MODE == 2) {
    fill_range(leds_b, 0, LED_STRIP_COUNT, make_rgb(0, 0, 0));
  }
}

static void shuffle_show_effect_order() {
  for (uint8_t i = 0; i < EFFECT_COUNT; ++i) {
    show_effect_order[i] = i;
  }
  for (int i = EFFECT_COUNT - 1; i > 0; --i) {
    const int j = random8(static_cast<uint8_t>(i));
    const uint8_t tmp = show_effect_order[i];
    show_effect_order[i] = show_effect_order[j];
    show_effect_order[j] = tmp;
  }
  if (EFFECT_COUNT > 1 && show_last_effect_id < EFFECT_COUNT &&
      show_effect_order[0] == show_last_effect_id) {
    const uint8_t swap_idx = random8(1, EFFECT_COUNT - 1);
    show_effect_order[0] = show_effect_order[swap_idx];
    show_effect_order[swap_idx] = show_last_effect_id;
  }
  show_effect_order_index = 0;
}

static uint8_t next_show_effect() {
  if (show_effect_order_index >= EFFECT_COUNT) {
    shuffle_show_effect_order();
  }
  const uint8_t effect_id = show_effect_order[show_effect_order_index++];
  show_last_effect_id = effect_id;
  return effect_id;
}

static uint8_t show_elapsed_amount(unsigned long now_ms) {
  const unsigned long elapsed_ms = now_ms - show_effect_since_ms;
  if (elapsed_ms >= SHOW_EFFECT_MS) {
    return 255;
  }
  return static_cast<uint8_t>((elapsed_ms * 255UL) / SHOW_EFFECT_MS);
}

static Rgb current_show_base(unsigned long now_ms) {
  return blend_rgb(show_base, show_next_base, show_elapsed_amount(now_ms));
}

static uint8_t show_transition_scale(unsigned long now_ms) {
  if (SHOW_TRANSITION_MS == 0) {
    return 255;
  }
  const unsigned long elapsed_ms = now_ms - show_effect_since_ms;
  const unsigned long remaining_ms = (elapsed_ms < SHOW_EFFECT_MS) ? (SHOW_EFFECT_MS - elapsed_ms) : 0;
  uint8_t scale = 255;
  if (elapsed_ms < SHOW_TRANSITION_MS) {
    scale = static_cast<uint8_t>((elapsed_ms * 255UL) / SHOW_TRANSITION_MS);
  }
  if (remaining_ms < SHOW_TRANSITION_MS) {
    const uint8_t fade_out = static_cast<uint8_t>((remaining_ms * 255UL) / SHOW_TRANSITION_MS);
    if (fade_out < scale) {
      scale = fade_out;
    }
  }
  return scale;
}

static void scale_show_range(Rgb *leds, int start, int count, uint8_t scale) {
  if (scale == 255) {
    return;
  }
  for (int i = start; i < start + count; ++i) {
    leds[i] = scale_rgb8(leds[i], scale);
  }
}

static void prepare_show_effect() {
  show_base = random_show_color();
  show_next_base = random_show_color();
  show_speed = clamp_u8(static_cast<int>(SHOW_SPEED) + static_cast<int>(random8(0, 50)) - 25);
  show_intensity = clamp_u8(static_cast<int>(SHOW_INTENSITY) + static_cast<int>(random8(0, 50)) - 25);
  if (show_effect_id == 10) { // FIRE
    show_speed = random8(130, 180);
    show_intensity = random8(175, 220);
  }
  show_state_a = {};
  show_state_b = {};
  if (show_effect_id == 9 || show_effect_id == 11) { // RAINBOW / GRADIENT_WAVE
    const uint8_t hue = random8();
    show_state_a.hue = hue;
    show_state_b.hue = hue;
  }
  if (show_effect_id == 10) { // FIRE
    for (int i = 0; i < LED_STRIP_COUNT; ++i) {
      heat_a[i] = 0;
      heat_b[i] = 0;
    }
  }
  clear_show_buffers();
}

static void update_show_mode(unsigned long now_ms) {
  if (show_first_tick) {
    show_first_tick = false;
    shuffle_show_effect_order();
    show_effect_id = next_show_effect();
    show_effect_since_ms = now_ms;
    prepare_show_effect();
  }

  if (now_ms - show_effect_since_ms >= SHOW_EFFECT_MS) {
    show_effect_id = next_show_effect();
    show_effect_since_ms = now_ms;
    prepare_show_effect();
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
  const Rgb active_show_base = current_show_base(now_ms);
  active_led_state = evaluate_policy(now_ms, gps_ok, critical_error,
                                     active_show_base);
  if (!active_led_state.body_enabled) {
    render_day_mode_status(now_ms, gps_ok, sta_ok, sta_try, critical_error);
    return;
  }
  const uint8_t transition_scale = show_transition_scale(now_ms);

  if (active_led_state.homogeneous) {
    apply_effect(active_led_state.effect_a, leds_a, heat_a, 0, LED_STRIP_COUNT,
                 active_led_state.base, active_led_state.speed,
                 active_led_state.intensity,
                 show_state_a);
    scale_show_range(leds_a, 0, LED_STRIP_COUNT, transition_scale);
    if (LED_STRIP_MODE == 2) {
      apply_effect(active_led_state.effect_b, leds_b, heat_b, 0,
                   LED_STRIP_COUNT, active_led_state.base,
                   active_led_state.speed, active_led_state.intensity,
                   show_state_b);
      scale_show_range(leds_b, 0, LED_STRIP_COUNT, transition_scale);
    }
    show_leds();
    return;
  }

  const int seg_start = LED_STATUS_COUNT;
  const int seg_count = LED_STRIP_COUNT - LED_STATUS_COUNT;
  if (seg_count > 0) {
    apply_effect(active_led_state.effect_a, leds_a, heat_a, seg_start,
                 seg_count, active_led_state.base, active_led_state.speed,
                 active_led_state.intensity,
                 show_state_a);
    scale_show_range(leds_a, seg_start, seg_count, transition_scale);
    if (LED_STRIP_MODE == 2) {
      apply_effect(active_led_state.effect_b, leds_b, heat_b, seg_start,
                   seg_count, active_led_state.base, active_led_state.speed,
                   active_led_state.intensity,
                   show_state_b);
      scale_show_range(leds_b, seg_start, seg_count, transition_scale);
    }
  } else {
    apply_effect(active_led_state.effect_a, leds_a, heat_a, 0,
                 LED_STRIP_COUNT, active_led_state.base,
                 active_led_state.speed, active_led_state.intensity,
                 show_state_a);
    scale_show_range(leds_a, 0, LED_STRIP_COUNT, transition_scale);
    if (LED_STRIP_MODE == 2) {
      apply_effect(active_led_state.effect_b, leds_b, heat_b, 0,
                   LED_STRIP_COUNT, active_led_state.base,
                   active_led_state.speed, active_led_state.intensity,
                   show_state_b);
      scale_show_range(leds_b, 0, LED_STRIP_COUNT, transition_scale);
    }
  }

  paint_status_leds(now_ms, gps_ok, sta_ok, sta_try, critical_error);
  show_leds();
}

static void reset_simple_state_if_needed() {
  if (!simple_first_tick &&
      config::get().single.effect_id == last_simple_effect &&
      config::get().single.speed == last_simple_speed &&
      config::get().single.intensity == last_simple_intensity &&
      config::get().single.base_r == last_simple_r &&
      config::get().single.base_g == last_simple_g &&
      config::get().single.base_b == last_simple_b) {
    return;
  }
  simple_first_tick = false;
  simple_state_a = {};
  simple_state_b = {};
  if (config::get().single.effect_id == 10) { // FIRE
    for (int i = 0; i < LED_STRIP_COUNT; ++i) {
      heat_a[i] = 0;
      heat_b[i] = 0;
    }
  }
  last_simple_effect = config::get().single.effect_id;
  last_simple_speed = config::get().single.speed;
  last_simple_intensity = config::get().single.intensity;
  last_simple_r = config::get().single.base_r;
  last_simple_g = config::get().single.base_g;
  last_simple_b = config::get().single.base_b;
}

static void update_simple_mode(unsigned long now_ms) {
  reset_simple_state_if_needed();
  if (now_ms - last_led_update_ms < LED_UPDATE_MS) {
    return;
  }
  last_led_update_ms = now_ms;

  const bool gps_ok = gps::has_fix();
  const bool sta_ok = (wifi_mgr::sta_connected() && WiFi.status() == WL_CONNECTED);
  const bool sta_try = (!sta_ok && wifi_mgr::sta_connecting());
  update_gps_fix_timer(now_ms, gps_ok);
  const bool critical_error = compute_critical_error(now_ms, gps_ok, sta_ok);
  active_led_state = evaluate_policy(now_ms, gps_ok, critical_error,
                                     show_base);
  if (!active_led_state.body_enabled) {
    render_day_mode_status(now_ms, gps_ok, sta_ok, sta_try, critical_error);
    return;
  }

  apply_effect(active_led_state.effect_a, leds_a, heat_a, 0, LED_STRIP_COUNT,
               active_led_state.base, active_led_state.speed,
               active_led_state.intensity, simple_state_a);
  if (LED_STRIP_MODE == 2) {
    apply_effect(active_led_state.effect_b, leds_b, heat_b, 0,
                 LED_STRIP_COUNT, active_led_state.base,
                 active_led_state.speed, active_led_state.intensity,
                 simple_state_b);
  }
  show_leds();
}

static void update_led_ui() {
  if (!LED_UI_ENABLED) {
    return;
  }
  const unsigned long now_ms = millis();
  if (welcome.active) {
    update_welcome(now_ms);
    return;
  }
  if (config::get().mode != last_mode) {
    if (config::get().mode == MODE_SHOW) {
      show_first_tick = true;
    }
    if (config::get().mode == MODE_SIMPLE) {
      simple_first_tick = true;
    }
    last_mode = config::get().mode;
  }
  if (config::get().mode == MODE_SHOW) {
    update_show_mode(now_ms);
    return;
  }
  if (config::get().mode == MODE_SIMPLE) {
    update_simple_mode(now_ms);
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
  active_led_state = evaluate_policy(now_ms, gps_ok, critical_error,
                                     show_base);
  if (!active_led_state.body_enabled) {
    render_day_mode_status(now_ms, gps_ok, sta_ok, sta_try, critical_error);
    return;
  }

  const int seg_start = LED_STATUS_COUNT;
  const int seg_count = LED_STRIP_COUNT - LED_STATUS_COUNT;
  const bool home_missing =
      active_led_state.intent == led::LedIntent::HomeMissing;
  const bool has_range = active_led_state.intent == led::LedIntent::Range &&
                         active_led_state.range >= 1;

  if (active_led_state.homogeneous && has_range) {
    apply_effect(active_led_state.effect_a, leds_a, heat_a, 0,
                 LED_STRIP_COUNT, active_led_state.base,
                 active_led_state.speed, active_led_state.intensity, state_a);
    if (LED_STRIP_MODE == 2) {
      apply_effect(active_led_state.effect_b, leds_b, heat_b, 0,
                   LED_STRIP_COUNT, active_led_state.base,
                   active_led_state.speed, active_led_state.intensity, state_b);
    }
    show_leds();
    return;
  }

  if (home_missing && seg_count > 0) {
    apply_effect(active_led_state.effect_a, leds_a, heat_a, seg_start,
                 seg_count, active_led_state.base, active_led_state.speed,
                 active_led_state.intensity, state_a);
    if (LED_STRIP_MODE == 2) {
      apply_effect(active_led_state.effect_b, leds_b, heat_b, seg_start,
                   seg_count, active_led_state.base, active_led_state.speed,
                   active_led_state.intensity, state_b);
    }
  } else if (has_range && seg_count > 0) {
    apply_effect(active_led_state.effect_a, leds_a, heat_a, seg_start,
                 seg_count, active_led_state.base, active_led_state.speed,
                 active_led_state.intensity, state_a);
    if (LED_STRIP_MODE == 2) {
      apply_effect(active_led_state.effect_b, leds_b, heat_b, seg_start,
                   seg_count, active_led_state.base, active_led_state.speed,
                   active_led_state.intensity, state_b);
    }
  } else if (seg_count > 0) {
    body_idle_hue = static_cast<uint8_t>(body_idle_hue + 2);
    for (int i = 0; i < seg_count; ++i) {
      leds_a[seg_start + i] = hsv_to_rgb(static_cast<uint8_t>(body_idle_hue + (i * 7)), 255, 255);
    }
    if (LED_STRIP_MODE == 2) {
      for (int i = 0; i < seg_count; ++i) {
        leds_b[seg_start + i] = hsv_to_rgb(static_cast<uint8_t>(body_idle_hue + (i * 7)), 255, 255);
      }
    }
  }

  paint_status_leds(now_ms, gps_ok, sta_ok, sta_try, critical_error);
  show_leds();
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
  return show_effect_id;
}
} // namespace led_ui
