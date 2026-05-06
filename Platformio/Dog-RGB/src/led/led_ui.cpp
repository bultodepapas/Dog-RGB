#include "led/led_ui.h"

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <WiFi.h>

#include "config/runtime_config.h"
#include "config.h"
#include "geofence/home.h"
#include "gps/gps.h"
#include "pins.h"
#include "power/day_mode.h"
#include "wifi/wifi_mgr.h"

namespace led_ui {
namespace {
// LED strip configuration is defined in config.h.
unsigned long last_led_update_ms = 0;

unsigned long last_ok_ms = 0;
unsigned long gps_fix_ms = 0;
unsigned long last_gps_fix_ms = 0;

Rgb leds_a[LED_STRIP_COUNT];
Rgb leds_b[LED_STRIP_COUNT];
uint8_t heat_a[LED_STRIP_COUNT];
uint8_t heat_b[LED_STRIP_COUNT];

Adafruit_NeoPixel strip_a(LED_STRIP_COUNT, PIN_LED_A_DATA, NEO_GRBW + NEO_KHZ800);
Adafruit_NeoPixel strip_b(LED_STRIP_COUNT, PIN_LED_B_DATA, NEO_GRBW + NEO_KHZ800);

struct EffectState {
  uint8_t hue = 0;
  uint16_t pos = 0;
};

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
uint8_t last_simple_effect = SINGLE_EFFECT_DEFAULT;
uint8_t last_simple_speed = SINGLE_SPEED_DEFAULT;
uint8_t last_simple_intensity = SINGLE_INTENSITY_DEFAULT;
uint8_t last_simple_r = SINGLE_R_DEFAULT;
uint8_t last_simple_g = SINGLE_G_DEFAULT;
uint8_t last_simple_b = SINGLE_B_DEFAULT;
uint8_t last_mode = MODE_SPEED;

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

static void add_rgb(Rgb &dst, const Rgb &src) {
  dst.r = clamp_u8(dst.r + src.r);
  dst.g = clamp_u8(dst.g + src.g);
  dst.b = clamp_u8(dst.b + src.b);
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

static uint8_t qsub8(uint8_t i, uint8_t j) {
  return (i > j) ? static_cast<uint8_t>(i - j) : 0;
}

static uint8_t qadd8(uint8_t i, uint8_t j) {
  const uint16_t sum = static_cast<uint16_t>(i) + j;
  return (sum > 255) ? 255 : static_cast<uint8_t>(sum);
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

static uint8_t beat8(uint8_t bpm, uint8_t low, uint8_t high) {
  const float t = millis() / 1000.0f;
  const float phase = sinf(2.0f * 3.14159265f * (static_cast<float>(bpm) / 60.0f) * t);
  const float norm = (phase + 1.0f) * 0.5f;
  return static_cast<uint8_t>(low + (high - low) * norm);
}

static uint16_t beat16(uint16_t bpm, uint16_t low, uint16_t high) {
  const float t = millis() / 1000.0f;
  const float phase = sinf(2.0f * 3.14159265f * (static_cast<float>(bpm) / 60.0f) * t);
  const float norm = (phase + 1.0f) * 0.5f;
  return static_cast<uint16_t>(low + (high - low) * norm);
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

static void led_begin() {
  strip_a.begin();
  strip_a.setBrightness(effective_brightness(config::get().brightness));
  strip_a.clear();
  strip_a.show();
  if (LED_STRIP_MODE == 2) {
    strip_b.begin();
    strip_b.setBrightness(effective_brightness(config::get().brightness));
    strip_b.clear();
    strip_b.show();
  }
}

static void show_leds() {
  for (int i = 0; i < LED_STRIP_COUNT; ++i) {
    const uint8_t r = leds_a[i].r;
    const uint8_t g = leds_a[i].g;
    const uint8_t b = leds_a[i].b;
    const uint8_t w = min(r, min(g, b));
    strip_a.setPixelColor(i, strip_a.Color(r - w, g - w, b - w, w));
  }
  strip_a.show();
  if (LED_STRIP_MODE == 2) {
    for (int i = 0; i < LED_STRIP_COUNT; ++i) {
      const uint8_t r = leds_b[i].r;
      const uint8_t g = leds_b[i].g;
      const uint8_t b = leds_b[i].b;
      const uint8_t w = min(r, min(g, b));
      strip_b.setPixelColor(i, strip_b.Color(r - w, g - w, b - w, w));
    }
    strip_b.show();
  }
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

uint8_t speed_range(float kph) {
  if (kph <= config::get().ranges[0]) return 1;
  if (kph <= config::get().ranges[1]) return 2;
  if (kph <= config::get().ranges[2]) return 3;
  if (kph <= config::get().ranges[3]) return 4;
  if (kph <= config::get().ranges[4]) return 5;
  if (kph <= config::get().ranges[5]) return 6;
  if (kph <= config::get().ranges[6]) return 7;
  if (kph <= config::get().ranges[7]) return 8;
  if (kph <= config::get().ranges[8]) return 9;
  return 10;
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
  switch (range) {
    case 1:
      return make_rgb(0, 60, 60);
    case 2:
      return make_rgb(0, 60, 35);
    case 3:
      return make_rgb(0, 60, 0);
    case 4:
      return make_rgb(25, 60, 0);
    case 5:
      return make_rgb(60, 60, 0);
    case 6:
      return make_rgb(60, 45, 0);
    case 7:
      return make_rgb(60, 30, 0);
    case 8:
      return make_rgb(60, 20, 0);
    case 9:
      return make_rgb(60, 10, 0);
    default:
      return make_rgb(60, 0, 0);
  }
}

const char *effect_name(uint8_t effect_id) {
  switch (effect_id) {
    case 0: return "SOLID";
    case 1: return "PULSE";
    case 2: return "BREATH";
    case 3: return "CHASE";
    case 4: return "COMET";
    case 5: return "SINELON";
    case 6: return "CONFETTI";
    case 7: return "JUGGLE";
    case 8: return "BPM";
    case 9: return "RAINBOW";
    case 10: return "FIRE";
    case 11: return "GRADIENT_WAVE";
    default: return "UNKNOWN";
  }
}

static Rgb heat_color(uint8_t temperature) {
  const uint8_t t192 = static_cast<uint8_t>((temperature * 191) / 255);
  const uint8_t heatramp = (t192 & 0x3F) << 2;
  if (t192 > 0x80) {
    return make_rgb(255, 255, heatramp);
  }
  if (t192 > 0x40) {
    return make_rgb(255, heatramp, 0);
  }
  return make_rgb(heatramp, 0, 0);
}

static void apply_fire(Rgb *leds,
                       uint8_t *heat,
                       int start,
                       int count,
                       uint8_t intensity,
                       uint8_t speed) {
  const uint8_t cooling = map(255 - intensity, 0, 255, 20, 80);
  const uint8_t sparking = map(intensity, 0, 255, 20, 120);
  for (int i = start; i < start + count; ++i) {
    heat[i] = qsub8(heat[i], random8(0, ((cooling * 10) / count) + 2));
  }
  for (int k = start + count - 1; k >= start + 2; --k) {
    heat[k] = static_cast<uint8_t>((heat[k - 1] + heat[k - 2] + heat[k - 2]) / 3);
  }
  if (random8() < sparking) {
    const int y = start + random8(min(count, 7));
    heat[y] = qadd8(heat[y], random8(160, 255));
  }
  for (int j = start; j < start + count; ++j) {
    leds[j] = heat_color(heat[j]);
  }
  (void)speed;
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
  const uint8_t fade_amt = map(255 - intensity, 0, 255, 10, 80);
  const uint8_t bpm = map(speed, 0, 255, 10, 90);

  switch (effect_id) {
    case 0: // SOLID
      fill_range(leds, start, count, base);
      break;
    case 1: { // PULSE
      const uint8_t beat = beat8(bpm, 10, 255);
      Rgb c = base;
      c.r = scale8(c.r, beat);
      c.g = scale8(c.g, beat);
      c.b = scale8(c.b, beat);
      fill_range(leds, start, count, c);
      break;
    }
    case 2: { // BREATH
      const uint8_t beat = beat8(bpm, 20, 200);
      Rgb c = base;
      c.r = scale8(c.r, beat);
      c.g = scale8(c.g, beat);
      c.b = scale8(c.b, beat);
      fill_range(leds, start, count, c);
      break;
    }
    case 3: { // CHASE
      fade_range(leds, start, count, fade_amt);
      state.pos = (state.pos + step_from_speed(speed, 32)) % count;
      leds[start + state.pos] = base;
      break;
    }
    case 4: { // COMET
      fade_range(leds, start, count, fade_amt);
      state.pos = (state.pos + step_from_speed(speed, 24)) % count;
      leds[start + state.pos] = base;
      break;
    }
    case 5: { // SINELON
      fade_range(leds, start, count, fade_amt);
      const uint16_t pos = beat16(bpm, 0, count - 1);
      add_rgb(leds[start + pos], base);
      break;
    }
    case 6: { // CONFETTI
      fade_range(leds, start, count, fade_amt);
      const int pos = start + random8(count - 1);
      add_rgb(leds[pos], base);
      break;
    }
    case 7: { // JUGGLE
      fade_range(leds, start, count, fade_amt);
      for (int i = 0; i < 4; ++i) {
        const uint16_t pos = beat16(bpm + i * 2, 0, count - 1);
        add_rgb(leds[start + pos], base);
      }
      break;
    }
    case 8: { // BPM
      const uint8_t beat = beat8(bpm, 64, 255);
      for (int i = start; i < start + count; ++i) {
        leds[i] = base;
        leds[i].r = scale8(leds[i].r, beat);
        leds[i].g = scale8(leds[i].g, beat);
        leds[i].b = scale8(leds[i].b, beat);
      }
      break;
    }
    case 9: { // RAINBOW
      state.hue += step_from_speed(speed, 16);
      for (int i = start; i < start + count; ++i) {
        leds[i] = hsv_to_rgb(static_cast<uint8_t>(state.hue + (i * 7)), 255, 255);
      }
      break;
    }
    case 10: // FIRE
      apply_fire(leds, heat, start, count, intensity, speed);
      break;
    case 11: { // GRADIENT_WAVE
      state.hue += step_from_speed(speed, 24);
      for (int i = start; i < start + count; ++i) {
        leds[i] = hsv_to_rgb(static_cast<uint8_t>(state.hue + (i * 8)), 200, 255);
      }
      break;
    }
    default:
      fill_range(leds, start, count, base);
      break;
  }
}

void start_welcome() {
  welcome.active = true;
  welcome.color_index = 0;
  welcome.laps_done = 0;
  welcome_state_a = {};
  welcome_state_b = {};
  strip_a.setBrightness(effective_brightness(255));
  if (LED_STRIP_MODE == 2) {
    strip_b.setBrightness(effective_brightness(255));
  }
  fill_range(leds_a, 0, LED_STRIP_COUNT, make_rgb(0, 0, 0));
  if (LED_STRIP_MODE == 2) {
    fill_range(leds_b, 0, LED_STRIP_COUNT, make_rgb(0, 0, 0));
  }
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
      strip_a.setBrightness(effective_brightness(config::get().brightness));
      if (LED_STRIP_MODE == 2) {
        strip_b.setBrightness(effective_brightness(config::get().brightness));
      }
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
    } else if (!sta_ok && wifi_mgr::ssid().length() > 0 && wifi_mgr::ap_enabled() && WiFi.getMode() == WIFI_AP) {
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
  const bool day_active = day_mode::active_now();
  if (day_active) {
    render_day_mode_status(now_ms, gps_ok, sta_ok, sta_try, critical_error);
    return;
  }
  const bool homogeneous_mode = (wifi_mgr::wifi_off() && gps_fix_ms >= WIFI_OFF_GPS_FIX_MS);
  const Rgb active_show_base = current_show_base(now_ms);
  const uint8_t transition_scale = show_transition_scale(now_ms);

  if (homogeneous_mode) {
    apply_effect(show_effect_id, leds_a, heat_a, 0, LED_STRIP_COUNT, active_show_base, show_speed, show_intensity,
                 show_state_a);
    scale_show_range(leds_a, 0, LED_STRIP_COUNT, transition_scale);
    if (LED_STRIP_MODE == 2) {
      apply_effect(show_effect_id, leds_b, heat_b, 0, LED_STRIP_COUNT, active_show_base, show_speed, show_intensity,
                   show_state_b);
      scale_show_range(leds_b, 0, LED_STRIP_COUNT, transition_scale);
    }
    show_leds();
    return;
  }

  const int seg_start = LED_STATUS_COUNT;
  const int seg_count = LED_STRIP_COUNT - LED_STATUS_COUNT;
  if (seg_count > 0) {
    apply_effect(show_effect_id, leds_a, heat_a, seg_start, seg_count, active_show_base, show_speed, show_intensity,
                 show_state_a);
    scale_show_range(leds_a, seg_start, seg_count, transition_scale);
    if (LED_STRIP_MODE == 2) {
      apply_effect(show_effect_id, leds_b, heat_b, seg_start, seg_count, active_show_base, show_speed, show_intensity,
                   show_state_b);
      scale_show_range(leds_b, seg_start, seg_count, transition_scale);
    }
  } else {
    apply_effect(show_effect_id, leds_a, heat_a, 0, LED_STRIP_COUNT, active_show_base, show_speed, show_intensity,
                 show_state_a);
    scale_show_range(leds_a, 0, LED_STRIP_COUNT, transition_scale);
    if (LED_STRIP_MODE == 2) {
      apply_effect(show_effect_id, leds_b, heat_b, 0, LED_STRIP_COUNT, active_show_base, show_speed, show_intensity,
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
  const bool day_active = day_mode::active_now();
  if (day_active) {
    render_day_mode_status(now_ms, gps_ok, sta_ok, sta_try, critical_error);
    return;
  }

  const Rgb base = make_rgb(config::get().single.base_r, config::get().single.base_g, config::get().single.base_b);
  apply_effect(config::get().single.effect_id, leds_a, heat_a, 0, LED_STRIP_COUNT, base,
               config::get().single.speed, config::get().single.intensity, simple_state_a);
  if (LED_STRIP_MODE == 2) {
    apply_effect(config::get().single.effect_id, leds_b, heat_b, 0, LED_STRIP_COUNT, base,
                 config::get().single.speed, config::get().single.intensity, simple_state_b);
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
  const bool day_active = day_mode::active_now();
  if (day_active) {
    render_day_mode_status(now_ms, gps_ok, sta_ok, sta_try, critical_error);
    return;
  }
  const bool homogeneous_mode = (wifi_mgr::wifi_off() && gps_fix_ms >= WIFI_OFF_GPS_FIX_MS);

  const int seg_start = LED_STATUS_COUNT;
  const int seg_count = LED_STRIP_COUNT - LED_STATUS_COUNT;
  bool body_idle = false;
  bool home_missing = false;
  uint8_t range = 1;
  float geofence_dist_m = -1.0f;

  if (config::get().mode == MODE_GEOFENCE) {
    if (!gps_ok) {
      body_idle = true;
    } else if (!geofence::is_set()) {
      home_missing = true;
    } else {
      geofence_dist_m = geofence::distance_to_home_m();
      if (geofence_dist_m < 0.0f) {
        body_idle = true;
      } else {
        range = geofence::geofence_range(geofence_dist_m);
        range = geofence::apply_hysteresis(range, geofence_dist_m);
      }
    }
  } else {
    if (!gps_ok) {
      body_idle = true;
    } else {
      range = speed_range(gps::last_speed_kph());
    }
  }

  const bool has_range = (!body_idle && !home_missing);
  int effect_a = RANGE_1_EFFECT_A;
  int effect_b = RANGE_1_EFFECT_B;
  uint8_t eff_speed = RANGE_1_SPEED;
  uint8_t eff_intensity = RANGE_1_INTENSITY;
  if (has_range) {
    get_range_config(range, effect_a, effect_b, eff_speed, eff_intensity);
  }
  const Rgb base = base_color_for_range(range);

  if (homogeneous_mode && has_range) {
    apply_effect(effect_a, leds_a, heat_a, 0, LED_STRIP_COUNT, base, eff_speed, eff_intensity, state_a);
    if (LED_STRIP_MODE == 2) {
      apply_effect(effect_b, leds_b, heat_b, 0, LED_STRIP_COUNT, base, eff_speed, eff_intensity, state_b);
    }
    show_leds();
    return;
  }

  if (home_missing && seg_count > 0) {
    const Rgb home_base = make_rgb(60, 45, 0);
    apply_effect(2, leds_a, heat_a, seg_start, seg_count, home_base, 60, 120, state_a);
    if (LED_STRIP_MODE == 2) {
      apply_effect(2, leds_b, heat_b, seg_start, seg_count, home_base, 60, 120, state_b);
    }
  } else if (has_range && seg_count > 0) {
    apply_effect(effect_a, leds_a, heat_a, seg_start, seg_count, base, eff_speed, eff_intensity, state_a);
    if (LED_STRIP_MODE == 2) {
      apply_effect(effect_b, leds_b, heat_b, seg_start, seg_count, base, eff_speed, eff_intensity, state_b);
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
  led_begin();
}

void tick() {
  update_led_ui();
}

void apply_brightness(uint8_t brightness) {
  strip_a.setBrightness(effective_brightness(brightness));
  if (LED_STRIP_MODE == 2) {
    strip_b.setBrightness(effective_brightness(brightness));
  }
}

uint8_t current_show_effect() {
  return show_effect_id;
}
} // namespace led_ui
