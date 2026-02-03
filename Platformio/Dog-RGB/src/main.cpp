/*
  Dog-RGB GPS-first firmware (ESP32-S3 / XIAO ESP32-S3).

  Purpose:
  - Read GNSS (RMC) and compute distance/avg/max speed.
  - Serve local Wi-Fi portal (AP/STA) with daily summary JSON.
  - Provide BLE read-only summary (future).
  - Drive SK6812 LED strips as system UI.

  Supported hardware:
  - MCU: Seeed Studio XIAO ESP32-S3
  - GNSS: EBYTE E108-GN02 (UART 9600)
  - LEDs: SK6812 RGBW (single-wire)

  Pin table (XIAO ESP32-S3):
  - GNSS RX: D7 / GPIO44
  - GNSS TX: D6 / GPIO43
  - Status LED: D2 / GPIO3
  - LED A data: D0 / GPIO1
  - LED B data: D1 / GPIO2

  Dependencies:
  - Adafruit_NeoPixel (RGBW output + effects)
  - ArduinoJson
  - ESP32 Arduino core (WiFi, WebServer, ESPmDNS)

  Build/flash (PlatformIO):
  - pio run -e esp32s3
  - pio run -e esp32s3 -t upload
  - pio device monitor -e esp32s3

  Power/safety notes:
  - SK6812 requires 5V and good decoupling; avoid brownouts.
  - Keep brightness low (30%) to reduce heat and battery draw.
  - Runtime parameters can be adjusted via /config on the portal.
*/

#include <Arduino.h>
#include <math.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>
#include "pins.h"
#include "config.h"

// Heartbeat for status LED and periodic serial logs.
static const unsigned long HEARTBEAT_MS = 1000;
static const unsigned long LOG_MS = 3000;
static const unsigned long GPS_NO_DATA_MS = 3000;
static unsigned long last_heartbeat_ms = 0;
static unsigned long last_log_ms = 0;
static bool led_state = false;

// GPS UART settings are defined in config.h.
static HardwareSerial GPS(1);
static Preferences prefs;
static Preferences prefs_cfg;
static BLECharacteristic *summary_char = nullptr;
static WebServer server(80);

// NMEA line buffer for incoming GPS sentences.
static char nmea_line[128];
static size_t nmea_len = 0;

// Latest GPS state.
static bool has_gps_fix = false;
static float last_speed_kph = 0.0f;
static unsigned long last_gps_ms = 0;
static unsigned long gps_bytes_rx = 0;
static unsigned long gps_sentences_rx = 0;
static unsigned long gps_rmc_seen = 0;
static unsigned long gps_rmc_valid = 0;
static unsigned long gps_gga_seen = 0;
static unsigned long gps_overflow = 0;
static unsigned long gps_last_byte_ms = 0;
static unsigned long gps_last_sentence_ms = 0;
static unsigned long gps_last_rmc_ms = 0;
static unsigned long gps_last_gga_ms = 0;
static unsigned long gps_last_fix_ms = 0;
static unsigned long gps_last_bytes_log = 0;
static unsigned long gps_last_sentences_log = 0;
static unsigned long gps_last_rmc_log = 0;
static uint8_t gps_sats = 0;
static uint8_t gps_fix_quality = 0;

// Behavior thresholds and sampling are defined in config.h.

// Rolling metrics for the current day.
static unsigned long last_sample_ms = 0;
static unsigned long active_time_ms = 0;
static float total_distance_m = 0.0f;
static float max_speed_kph = 0.0f;
static uint16_t last_update_min = 0;

// Last position for distance calculation.
static bool has_last_point = false;
static float last_lat_deg = 0.0f;
static float last_lon_deg = 0.0f;
static float current_lat_deg = 0.0f;
static float current_lon_deg = 0.0f;
static bool has_current_fix = false;

// Daily reset date (YYYYMMDD from GPS).
static uint32_t current_date_yyyymmdd = 0;
static unsigned long last_save_ms = 0;

// BLE identifiers for the daily summary.
static const char *BLE_DEVICE_NAME = "Dog-Collar";
static const char *BLE_SERVICE_UUID = "8b4c0001-6c1d-4f3c-a5b0-1e0c5a00a101";
static const char *BLE_CHAR_UUID = "8b4c0002-6c1d-4f3c-a5b0-1e0c5a00a101";

// Wi-Fi settings are defined in config.h.

static String wifi_ssid;
static String wifi_pass;
static bool wifi_sta_connected = false;
static bool wifi_sta_connecting = false;
static unsigned long wifi_sta_start_ms = 0;
static unsigned long last_wifi_check_ms = 0;
static bool ap_enabled = true;
static bool wifi_off = false;
static unsigned long last_ap_client_ms = 0;
static unsigned long last_ap_poll_ms = 0;
static uint8_t ap_station_count = 0;
static unsigned long stationary_ms = 0;
static unsigned long last_stationary_ms = 0;
static unsigned long gps_fix_ms = 0;
static unsigned long last_gps_fix_ms = 0;

// Home (geofence) state.
static bool home_set = false;
static float home_lat_deg = 0.0f;
static float home_lon_deg = 0.0f;
static uint8_t home_source = 0; // 0=none, 1=auto, 2=manual
static unsigned long fix_stable_ms = 0;
static unsigned long last_fix_check_ms = 0;
static bool last_fix_state = false;

// LED strip configuration is defined in config.h.
static unsigned long last_led_update_ms = 0;

static unsigned long last_ok_ms = 0;

// Speed-to-color ranges are defined in config.h.

struct Rgb {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

static Rgb leds_a[LED_STRIP_COUNT];
static Rgb leds_b[LED_STRIP_COUNT];
static uint8_t heat_a[LED_STRIP_COUNT];
static uint8_t heat_b[LED_STRIP_COUNT];

static Adafruit_NeoPixel strip_a(LED_STRIP_COUNT, PIN_LED_A_DATA, NEO_GRBW + NEO_KHZ800);
static Adafruit_NeoPixel strip_b(LED_STRIP_COUNT, PIN_LED_B_DATA, NEO_GRBW + NEO_KHZ800);


struct EffectState {
  uint8_t hue = 0;
  uint16_t pos = 0;
};

static EffectState state_a;
static EffectState state_b;
static uint8_t body_idle_hue = 0;
static uint8_t last_geofence_range = 1;
static float last_geofence_distance_m = 0.0f;
static EffectState show_state_a;
static EffectState show_state_b;
static uint8_t show_effect_id = 0;
static unsigned long show_effect_since_ms = 0;
static Rgb show_base = {0, 0, 0};
static bool show_first_tick = true;
static uint8_t last_mode = MODE_SPEED;

struct WelcomeState {
  bool active = false;
  uint8_t color_index = 0;
  uint8_t laps_done = 0;
};

static WelcomeState welcome;
static EffectState welcome_state_a;
static EffectState welcome_state_b;
static const uint8_t WELCOME_LAPS = 5;
static const uint8_t WELCOME_SPEED = 32;
static const uint8_t WELCOME_INTENSITY = 255;
static const Rgb WELCOME_COLORS[5] = {
  {255, 0, 0},     // rojo
  {255, 255, 255}, // blanco
  {255, 0, 128},   // rosado
  {0, 0, 255},     // azul
  {0, 255, 0}      // verde
};

struct RangeEffect {
  uint8_t effect_a;
  uint8_t effect_b;
  uint8_t speed;
  uint8_t intensity;
};

struct RuntimeConfig {
  uint8_t brightness;
  float ranges[9];
  RangeEffect effects[10];
  String ap_ssid;
  String ap_pass;
  String mdns;
  uint8_t mode;
  uint16_t fence_max_m;
};

static RuntimeConfig g_cfg;
static const uint8_t CONFIG_VERSION = 3;
static bool pending_ap_restart = false;
static unsigned long pending_ap_at_ms = 0;
static const unsigned long AP_RESTART_DELAY_MS = 500;

// Forward declarations for Wi-Fi control helpers used before definition.
static void start_ap_mode();
static void start_sta_mode();
static void enable_ap();
static void disable_ap();
static void set_wifi_off(bool off);
static void update_ap_policy(unsigned long now_ms);
static bool valid_mdns(const String &value);

static float knots_to_kph(float knots) {
  return knots * 1.852f;
}

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

// Convert NMEA degree-minute format to decimal degrees.
static float nmea_to_decimal_degrees(const char *value, char hemi) {
  // NMEA format: DDMM.MMMM (lat) or DDDMM.MMMM (lon)
  const float raw = strtof(value, nullptr);
  const int deg = static_cast<int>(raw / 100.0f);
  const float minutes = raw - (deg * 100.0f);
  float dec = static_cast<float>(deg) + (minutes / 60.0f);
  if (hemi == 'S' || hemi == 'W') {
    dec = -dec;
  }
  return dec;
}

// Haversine distance between two lat/lon points in meters.
static float haversine_m(float lat1, float lon1, float lat2, float lon2) {
  const float r = 6371000.0f;
  const float to_rad = 0.01745329252f;
  const float dlat = (lat2 - lat1) * to_rad;
  const float dlon = (lon2 - lon1) * to_rad;
  const float a = sinf(dlat * 0.5f) * sinf(dlat * 0.5f) +
                  cosf(lat1 * to_rad) * cosf(lat2 * to_rad) *
                  sinf(dlon * 0.5f) * sinf(dlon * 0.5f);
  const float c = 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));
  return r * c;
}

// Parse RMC sentence for position, speed, fix status, and date/time.
static bool parse_rmc(const char *line,
                      float *lat_deg,
                      float *lon_deg,
                      float *speed_kph,
                      bool *valid_fix,
                      uint32_t *date_yyyymmdd,
                      uint16_t *time_min) {
  if (strncmp(line, "$GPRMC,", 7) != 0 && strncmp(line, "$GNRMC,", 7) != 0) {
    return false;
  }

  // RMC fields:
  // 1 time, 2 status (A/V), 3 lat, 4 N/S, 5 lon, 6 E/W, 7 speed (knots), 9 date (ddmmyy)
  int field = 0;
  float knots = 0.0f;
  char status = 'V';
  char time_buf[8] = {0};
  char lat_buf[16] = {0};
  char lon_buf[16] = {0};
  char speed_buf[12] = {0};
  char ns = 'N';
  char ew = 'E';
  char date_buf[8] = {0};
  int time_len = 0;
  int lat_len = 0;
  int lon_len = 0;
  int speed_len = 0;
  int date_len = 0;

  for (const char *p = line; *p != '\0' && *p != '*'; ++p) {
    if (*p == ',') {
      field++;
      continue;
    }
    if (field == 1 && time_len < 6) {
      time_buf[time_len++] = *p;
    }
    if (field == 2 && status == 'V') {
      status = *p;
    }
    if (field == 3 && lat_len < 15) {
      lat_buf[lat_len++] = *p;
    }
    if (field == 4) {
      ns = *p;
    }
    if (field == 5 && lon_len < 15) {
      lon_buf[lon_len++] = *p;
    }
    if (field == 6) {
      ew = *p;
    }
    if (field == 7 && speed_len < 11) {
      speed_buf[speed_len++] = *p;
    }
    if (field == 9 && date_len < 6) {
      date_buf[date_len++] = *p;
    }
  }

  *valid_fix = (status == 'A');
  if (speed_len > 0) {
    knots = strtof(speed_buf, nullptr);
  }
  *speed_kph = knots_to_kph(knots);
  if (lat_len > 0 && lon_len > 0) {
    *lat_deg = nmea_to_decimal_degrees(lat_buf, ns);
    *lon_deg = nmea_to_decimal_degrees(lon_buf, ew);
  }
  if (date_len == 6) {
    const int day = (date_buf[0] - '0') * 10 + (date_buf[1] - '0');
    const int mon = (date_buf[2] - '0') * 10 + (date_buf[3] - '0');
    const int year = (date_buf[4] - '0') * 10 + (date_buf[5] - '0');
    *date_yyyymmdd = static_cast<uint32_t>(2000 + year) * 10000 +
                     static_cast<uint32_t>(mon) * 100 +
                     static_cast<uint32_t>(day);
  }
  if (time_len >= 4) {
    const int hour = (time_buf[0] - '0') * 10 + (time_buf[1] - '0');
    const int min = (time_buf[2] - '0') * 10 + (time_buf[3] - '0');
    *time_min = static_cast<uint16_t>(hour * 60 + min);
  }
  return true;
}

// Parse GGA sentence for fix quality and satellites.
static bool parse_gga(const char *line, uint8_t *fix_quality, uint8_t *sats) {
  if (strncmp(line, "$GPGGA,", 7) != 0 && strncmp(line, "$GNGGA,", 7) != 0) {
    return false;
  }

  // GGA fields: 1 time, 2 lat, 3 N/S, 4 lon, 5 E/W, 6 fix quality, 7 satellites
  int field = 0;
  char fix_buf[4] = {0};
  char sat_buf[4] = {0};
  int fix_len = 0;
  int sat_len = 0;

  for (const char *p = line; *p != '\0' && *p != '*'; ++p) {
    if (*p == ',') {
      field++;
      continue;
    }
    if (field == 6 && fix_len < 3) {
      fix_buf[fix_len++] = *p;
    }
    if (field == 7 && sat_len < 3) {
      sat_buf[sat_len++] = *p;
    }
  }

  if (fix_len > 0) {
    *fix_quality = static_cast<uint8_t>(atoi(fix_buf));
  }
  if (sat_len > 0) {
    *sats = static_cast<uint8_t>(atoi(sat_buf));
  }
  return true;
}

// Persist daily metrics to NVS (throttled by SAVE_INTERVAL_MS).
static void save_metrics() {
  prefs.putUInt("date", current_date_yyyymmdd);
  prefs.putFloat("dist_m", total_distance_m);
  prefs.putULong("active_ms", active_time_ms);
  prefs.putFloat("max_kph", max_speed_kph);
  prefs.putUShort("upd_min", last_update_min);
}

// Restore persisted metrics from NVS on boot.
static void load_metrics() {
  current_date_yyyymmdd = prefs.getUInt("date", 0);
  total_distance_m = prefs.getFloat("dist_m", 0.0f);
  active_time_ms = prefs.getULong("active_ms", 0);
  max_speed_kph = prefs.getFloat("max_kph", 0.0f);
  last_update_min = prefs.getUShort("upd_min", 0);
}

static void load_wifi_creds() {
  wifi_ssid = prefs.getString("wifi_ssid", "");
  wifi_pass = prefs.getString("wifi_pass", "");
}

static void save_wifi_creds(const String &ssid, const String &pass) {
  prefs.putString("wifi_ssid", ssid);
  prefs.putString("wifi_pass", pass);
  wifi_ssid = ssid;
  wifi_pass = pass;
}

// Build the 16-byte payload for BLE read.
static void build_summary_payload(uint8_t *out, size_t len) {
  if (len < 16) {
    return;
  }

  const float avg_speed_kph = (active_time_ms > 0)
                                  ? (total_distance_m / (active_time_ms / 1000.0f)) * 3.6f
                                  : 0.0f;
  const uint32_t distance_m = static_cast<uint32_t>(total_distance_m + 0.5f);
  const uint16_t avg_speed_cmps = static_cast<uint16_t>(avg_speed_kph * 27.7778f);
  const uint16_t max_speed_cmps = static_cast<uint16_t>(max_speed_kph * 27.7778f);

  memset(out, 0, len);
  out[0] = static_cast<uint8_t>(current_date_yyyymmdd & 0xFF);
  out[1] = static_cast<uint8_t>((current_date_yyyymmdd >> 8) & 0xFF);
  out[2] = static_cast<uint8_t>((current_date_yyyymmdd >> 16) & 0xFF);
  out[3] = static_cast<uint8_t>((current_date_yyyymmdd >> 24) & 0xFF);

  out[4] = static_cast<uint8_t>(distance_m & 0xFF);
  out[5] = static_cast<uint8_t>((distance_m >> 8) & 0xFF);
  out[6] = static_cast<uint8_t>((distance_m >> 16) & 0xFF);
  out[7] = static_cast<uint8_t>((distance_m >> 24) & 0xFF);

  out[8] = static_cast<uint8_t>(avg_speed_cmps & 0xFF);
  out[9] = static_cast<uint8_t>((avg_speed_cmps >> 8) & 0xFF);
  out[10] = static_cast<uint8_t>(max_speed_cmps & 0xFF);
  out[11] = static_cast<uint8_t>((max_speed_cmps >> 8) & 0xFF);

  out[12] = static_cast<uint8_t>(last_update_min & 0xFF);
  out[13] = static_cast<uint8_t>((last_update_min >> 8) & 0xFF);

  uint8_t flags = 0;
  if (has_gps_fix) {
    flags |= 0x01;
  }
  if (current_date_yyyymmdd != 0) {
    flags |= 0x02;
  }
  out[14] = flags;

  uint8_t checksum = 0;
  for (size_t i = 0; i < 15; ++i) {
    checksum ^= out[i];
  }
  out[15] = checksum;
}

static String build_summary_json() {
  const float avg_speed_kph = (active_time_ms > 0)
                                  ? (total_distance_m / (active_time_ms / 1000.0f)) * 3.6f
                                  : 0.0f;
  const uint32_t distance_m = static_cast<uint32_t>(total_distance_m + 0.5f);
  const uint16_t avg_speed_cmps = static_cast<uint16_t>(avg_speed_kph * 27.7778f);
  const uint16_t max_speed_cmps = static_cast<uint16_t>(max_speed_kph * 27.7778f);
  const bool has_data = (current_date_yyyymmdd != 0);

  String json = "{";
  json += "\"date\":" + String(current_date_yyyymmdd);
  json += ",\"distance_m\":" + String(distance_m);
  json += ",\"avg_speed_cmps\":" + String(avg_speed_cmps);
  json += ",\"max_speed_cmps\":" + String(max_speed_cmps);
  json += ",\"last_update_min\":" + String(last_update_min);
  json += ",\"gps_fix\":" + String(has_gps_fix ? "true" : "false");
  json += ",\"has_data\":" + String(has_data ? "true" : "false");
  json += "}";
  return json;
}

static bool validate_ranges(const float *ranges) {
  for (int i = 1; i < 9; ++i) {
    if (!(ranges[i] > ranges[i - 1])) {
      return false;
    }
  }
  return true;
}

static bool validate_effects(const RangeEffect *effects) {
  for (int i = 0; i < 10; ++i) {
    if (effects[i].effect_a > 11 || effects[i].effect_b > 11) {
      return false;
    }
  }
  return true;
}

static const char *mode_name(uint8_t mode) {
  switch (mode) {
    case MODE_GEOFENCE:
      return "geofence";
    case MODE_SHOW:
      return "show";
    case MODE_SPEED:
    default:
      return "speed";
  }
}

static bool parse_mode(const char *value, uint8_t &mode_out) {
  if (value == nullptr) {
    return false;
  }
  if (strcmp(value, "speed") == 0) {
    mode_out = MODE_SPEED;
    return true;
  }
  if (strcmp(value, "geofence") == 0) {
    mode_out = MODE_GEOFENCE;
    return true;
  }
  if (strcmp(value, "show") == 0) {
    mode_out = MODE_SHOW;
    return true;
  }
  return false;
}

static bool validate_mode(uint8_t mode) {
  return (mode == MODE_SPEED || mode == MODE_GEOFENCE || mode == MODE_SHOW);
}

static uint16_t clamp_fence_max(int value) {
  if (value < static_cast<int>(GEOFENCE_MAX_M_MIN)) {
    return GEOFENCE_MAX_M_MIN;
  }
  if (value > static_cast<int>(GEOFENCE_MAX_M_MAX)) {
    return GEOFENCE_MAX_M_MAX;
  }
  return static_cast<uint16_t>(value);
}

static void set_default_config() {
  g_cfg.brightness = LED_BRIGHTNESS;
  g_cfg.ranges[0] = SPEED_RANGE_1_KPH;
  g_cfg.ranges[1] = SPEED_RANGE_2_KPH;
  g_cfg.ranges[2] = SPEED_RANGE_3_KPH;
  g_cfg.ranges[3] = SPEED_RANGE_4_KPH;
  g_cfg.ranges[4] = SPEED_RANGE_5_KPH;
  g_cfg.ranges[5] = SPEED_RANGE_6_KPH;
  g_cfg.ranges[6] = SPEED_RANGE_7_KPH;
  g_cfg.ranges[7] = SPEED_RANGE_8_KPH;
  g_cfg.ranges[8] = SPEED_RANGE_9_KPH;

  g_cfg.effects[0] = {static_cast<uint8_t>(RANGE_1_EFFECT_A), static_cast<uint8_t>(RANGE_1_EFFECT_B),
                      RANGE_1_SPEED, RANGE_1_INTENSITY};
  g_cfg.effects[1] = {static_cast<uint8_t>(RANGE_2_EFFECT_A), static_cast<uint8_t>(RANGE_2_EFFECT_B),
                      RANGE_2_SPEED, RANGE_2_INTENSITY};
  g_cfg.effects[2] = {static_cast<uint8_t>(RANGE_3_EFFECT_A), static_cast<uint8_t>(RANGE_3_EFFECT_B),
                      RANGE_3_SPEED, RANGE_3_INTENSITY};
  g_cfg.effects[3] = {static_cast<uint8_t>(RANGE_4_EFFECT_A), static_cast<uint8_t>(RANGE_4_EFFECT_B),
                      RANGE_4_SPEED, RANGE_4_INTENSITY};
  g_cfg.effects[4] = {static_cast<uint8_t>(RANGE_5_EFFECT_A), static_cast<uint8_t>(RANGE_5_EFFECT_B),
                      RANGE_5_SPEED, RANGE_5_INTENSITY};
  g_cfg.effects[5] = {static_cast<uint8_t>(RANGE_6_EFFECT_A), static_cast<uint8_t>(RANGE_6_EFFECT_B),
                      RANGE_6_SPEED, RANGE_6_INTENSITY};
  g_cfg.effects[6] = {static_cast<uint8_t>(RANGE_7_EFFECT_A), static_cast<uint8_t>(RANGE_7_EFFECT_B),
                      RANGE_7_SPEED, RANGE_7_INTENSITY};
  g_cfg.effects[7] = {static_cast<uint8_t>(RANGE_8_EFFECT_A), static_cast<uint8_t>(RANGE_8_EFFECT_B),
                      RANGE_8_SPEED, RANGE_8_INTENSITY};
  g_cfg.effects[8] = {static_cast<uint8_t>(RANGE_9_EFFECT_A), static_cast<uint8_t>(RANGE_9_EFFECT_B),
                      RANGE_9_SPEED, RANGE_9_INTENSITY};
  g_cfg.effects[9] = {static_cast<uint8_t>(RANGE_10_EFFECT_A), static_cast<uint8_t>(RANGE_10_EFFECT_B),
                      RANGE_10_SPEED, RANGE_10_INTENSITY};

  g_cfg.ap_ssid = AP_SSID;
  g_cfg.ap_pass = AP_PASS;
  g_cfg.mdns = MDNS_NAME;
  g_cfg.mode = MODE_SPEED;
  g_cfg.fence_max_m = GEOFENCE_MAX_M_DEFAULT;
}

static void save_config() {
  prefs_cfg.putUChar("ver", CONFIG_VERSION);
  prefs_cfg.putUChar("brightness", g_cfg.brightness);
  prefs_cfg.putBytes("ranges", g_cfg.ranges, sizeof(g_cfg.ranges));
  prefs_cfg.putBytes("effects", g_cfg.effects, sizeof(g_cfg.effects));
  prefs_cfg.putString("ap_ssid", g_cfg.ap_ssid);
  prefs_cfg.putString("ap_pass", g_cfg.ap_pass);
  prefs_cfg.putString("mdns", g_cfg.mdns);
  prefs_cfg.putUChar("mode", g_cfg.mode);
  prefs_cfg.putUShort("fence_max", g_cfg.fence_max_m);
}

static bool read_common_config(RuntimeConfig &cfg) {
  cfg.brightness = prefs_cfg.getUChar("brightness", LED_BRIGHTNESS);
  if (cfg.brightness < 1) {
    return false;
  }
  if (prefs_cfg.getBytes("ranges", cfg.ranges, sizeof(cfg.ranges)) != sizeof(cfg.ranges)) {
    return false;
  }
  if (prefs_cfg.getBytes("effects", cfg.effects, sizeof(cfg.effects)) != sizeof(cfg.effects)) {
    return false;
  }
  cfg.ap_ssid = prefs_cfg.getString("ap_ssid", AP_SSID);
  cfg.ap_pass = prefs_cfg.getString("ap_pass", AP_PASS);
  cfg.mdns = prefs_cfg.getString("mdns", MDNS_NAME);
  if (!validate_ranges(cfg.ranges) || !validate_effects(cfg.effects)) {
    return false;
  }
  return true;
}

static void load_config() {
  const uint8_t ver = prefs_cfg.getUChar("ver", 0);
  if (ver == CONFIG_VERSION) {
    RuntimeConfig next = g_cfg;
    if (!read_common_config(next)) {
      set_default_config();
      save_config();
      return;
    }
    next.mode = prefs_cfg.getUChar("mode", MODE_SPEED);
    next.fence_max_m = prefs_cfg.getUShort("fence_max", GEOFENCE_MAX_M_DEFAULT);
    if (!validate_mode(next.mode)) {
      next.mode = MODE_SPEED;
    }
    next.fence_max_m = clamp_fence_max(next.fence_max_m);
    g_cfg = next;
    return;
  }

  if (ver == 2) {
    RuntimeConfig migrated = g_cfg;
    set_default_config();
    migrated = g_cfg;
    if (read_common_config(migrated)) {
      migrated.mode = MODE_SPEED;
      migrated.fence_max_m = GEOFENCE_MAX_M_DEFAULT;
      g_cfg = migrated;
      save_config();
      return;
    }
  }

  set_default_config();
  save_config();
}

static void apply_config(const RuntimeConfig &previous) {
  if (LED_UI_ENABLED) {
    strip_a.setBrightness(g_cfg.brightness);
    if (LED_STRIP_MODE == 2) {
      strip_b.setBrightness(g_cfg.brightness);
    }
  }
  if (g_cfg.mdns != previous.mdns) {
    if (wifi_sta_connected) {
      MDNS.end();
      MDNS.begin(g_cfg.mdns.c_str());
    }
  }
}

static void load_home() {
  home_set = (prefs_cfg.getUChar("home_set", 0) == 1);
  home_lat_deg = prefs_cfg.getFloat("home_lat", 0.0f);
  home_lon_deg = prefs_cfg.getFloat("home_lon", 0.0f);
  home_source = prefs_cfg.getUChar("home_src", 0);
  if (!home_set) {
    home_source = 0;
  }
}

static void save_home() {
  prefs_cfg.putUChar("home_set", home_set ? 1 : 0);
  prefs_cfg.putUChar("home_src", home_source);
  if (home_set) {
    prefs_cfg.putFloat("home_lat", home_lat_deg);
    prefs_cfg.putFloat("home_lon", home_lon_deg);
  } else {
    prefs_cfg.remove("home_lat");
    prefs_cfg.remove("home_lon");
  }
}

static void set_home(float lat_deg, float lon_deg, uint8_t source) {
  home_set = true;
  home_lat_deg = lat_deg;
  home_lon_deg = lon_deg;
  home_source = source;
  save_home();
}

static void clear_home() {
  home_set = false;
  home_source = 0;
  save_home();
}

static String html_page() {
  return String(
      "<!doctype html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Dog Collar</title>"
      "<style>body{font-family:Arial,sans-serif;margin:20px;color:#111}"
      ".card{border:1px solid #ddd;border-radius:8px;padding:12px;margin:10px 0}"
      "button{padding:10px 14px;border:0;border-radius:6px;background:#111;color:#fff}"
      ".muted{color:#666;font-size:12px}</style></head><body>"
      "<h1>Dog Collar</h1>"
      "<div id='status' class='muted'>Estado: --</div>"
      "<button onclick='loadData()'>Actualizar</button>"
      "<div class='card'><div>Distancia (km)</div><div id='dist'>--</div></div>"
      "<div class='card'><div>Velocidad promedio (km/h)</div><div id='avg'>--</div></div>"
      "<div class='card'><div>Velocidad maxima (km/h)</div><div id='max'>--</div></div>"
      "<div class='muted' id='updated'>Ultima lectura: --</div>"
      "<p><a href='/wifi'>Configurar Wi-Fi</a> | <a href='/config'>Config</a></p>"
      "<script>"
      "function minToTime(m){var h=Math.floor(m/60);var mm=m%60;return String(h).padStart(2,'0')+':'+String(mm).padStart(2,'0');}"
      "function cmpsToKph(v){return (v*0.036).toFixed(1);}"
      "function loadData(){fetch('/api/summary').then(r=>r.json()).then(d=>{"
      "if(!d.has_data){document.getElementById('status').innerText='Estado: Sin datos';return;}"
      "document.getElementById('dist').innerText=(d.distance_m/1000).toFixed(2);"
      "document.getElementById('avg').innerText=cmpsToKph(d.avg_speed_cmps);"
      "document.getElementById('max').innerText=cmpsToKph(d.max_speed_cmps);"
      "document.getElementById('updated').innerText='Ultima lectura: '+minToTime(d.last_update_min);"
      "document.getElementById('status').innerText='Estado: '+(d.gps_fix?'GPS OK':'Sin GPS');"
      "}).catch(()=>{document.getElementById('status').innerText='Estado: Error';});}"
      "loadData();"
      "</script></body></html>");
}

static String html_wifi_page() {
  String page = "<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>Wi-Fi</title></head><body><h1>Configurar Wi-Fi</h1>"
                "<form method='post' action='/api/wifi'>"
                "<label>SSID</label><br><input name='ssid' value='" + wifi_ssid + "'><br>"
                "<label>Password</label><br><input name='pass' type='password'><br><br>"
                "<button type='submit'>Guardar y conectar</button>"
                "</form><p><a href='/'>Volver</a></p></body></html>";
  return page;
}

static void led_begin() {
  strip_a.begin();
  strip_a.setBrightness(g_cfg.brightness);
  strip_a.clear();
  strip_a.show();
  if (LED_STRIP_MODE == 2) {
    strip_b.begin();
    strip_b.setBrightness(g_cfg.brightness);
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

static uint8_t speed_range(float kph) {
  if (kph <= g_cfg.ranges[0]) return 1;
  if (kph <= g_cfg.ranges[1]) return 2;
  if (kph <= g_cfg.ranges[2]) return 3;
  if (kph <= g_cfg.ranges[3]) return 4;
  if (kph <= g_cfg.ranges[4]) return 5;
  if (kph <= g_cfg.ranges[5]) return 6;
  if (kph <= g_cfg.ranges[6]) return 7;
  if (kph <= g_cfg.ranges[7]) return 8;
  if (kph <= g_cfg.ranges[8]) return 9;
  return 10;
}

static void get_range_config(uint8_t range,
                             int &effect_a,
                             int &effect_b,
                             uint8_t &speed,
                             uint8_t &intensity) {
  const uint8_t idx = (range > 0 && range <= 10) ? static_cast<uint8_t>(range - 1) : 0;
  effect_a = g_cfg.effects[idx].effect_a;
  effect_b = g_cfg.effects[idx].effect_b;
  speed = g_cfg.effects[idx].speed;
  intensity = g_cfg.effects[idx].intensity;
}

static Rgb base_color_for_range(uint8_t range) {
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

static const char *effect_name(uint8_t effect_id) {
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

static void start_welcome() {
  welcome.active = true;
  welcome.color_index = 0;
  welcome.laps_done = 0;
  welcome_state_a = {};
  welcome_state_b = {};
  strip_a.setBrightness(255);
  if (LED_STRIP_MODE == 2) {
    strip_b.setBrightness(255);
  }
  fill_range(leds_a, 0, LED_STRIP_COUNT, make_rgb(0, 0, 0));
  if (LED_STRIP_MODE == 2) {
    fill_range(leds_b, 0, LED_STRIP_COUNT, make_rgb(0, 0, 0));
  }
  show_leds();
}

static void update_welcome(unsigned long now_ms) {
  if (now_ms - last_led_update_ms < LED_UPDATE_MS) {
    return;
  }
  last_led_update_ms = now_ms;

  const Rgb base = WELCOME_COLORS[welcome.color_index];
  const uint16_t prev_pos = welcome_state_a.pos;

  apply_effect(3, leds_a, heat_a, 0, LED_STRIP_COUNT, base,
               WELCOME_SPEED, WELCOME_INTENSITY, welcome_state_a);
  if (LED_STRIP_MODE == 2) {
    apply_effect(3, leds_b, heat_b, 0, LED_STRIP_COUNT, base,
                 WELCOME_SPEED, WELCOME_INTENSITY, welcome_state_b);
  }
  show_leds();

  if (welcome_state_a.pos < prev_pos) {
    welcome.laps_done++;
    if (welcome.laps_done >= WELCOME_LAPS) {
      welcome.active = false;
      strip_a.setBrightness(g_cfg.brightness);
      if (LED_STRIP_MODE == 2) {
        strip_b.setBrightness(g_cfg.brightness);
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

static void update_fix_stability(unsigned long now_ms) {
  if (last_fix_check_ms == 0) {
    last_fix_check_ms = now_ms;
  }
  const unsigned long dt = now_ms - last_fix_check_ms;
  last_fix_check_ms = now_ms;

  if (has_gps_fix) {
    if (last_fix_state) {
      fix_stable_ms = (fix_stable_ms + dt > HOME_AUTO_FIX_MS) ? HOME_AUTO_FIX_MS : (fix_stable_ms + dt);
    } else {
      fix_stable_ms = 0;
    }
  } else {
    fix_stable_ms = 0;
  }
  last_fix_state = has_gps_fix;
}

static void maybe_auto_set_home() {
  if (home_set) {
    return;
  }
  if (has_gps_fix && has_current_fix && fix_stable_ms >= HOME_AUTO_FIX_MS) {
    set_home(current_lat_deg, current_lon_deg, 1);
  }
}

static float distance_to_home_m() {
  if (!home_set || !has_current_fix) {
    return -1.0f;
  }
  return haversine_m(current_lat_deg, current_lon_deg, home_lat_deg, home_lon_deg);
}

static uint8_t geofence_range(float dist_m) {
  if (dist_m <= 0.0f) {
    return 1;
  }
  const float step = static_cast<float>(g_cfg.fence_max_m) / 10.0f;
  if (step <= 0.0f) {
    return 1;
  }
  for (int i = 1; i <= 9; ++i) {
    if (dist_m <= step * i) {
      return static_cast<uint8_t>(i);
    }
  }
  return 10;
}

static uint8_t apply_geofence_hysteresis(uint8_t next_range, float dist_m) {
  if (next_range == last_geofence_range) {
    last_geofence_distance_m = dist_m;
    return next_range;
  }
  const float step = static_cast<float>(g_cfg.fence_max_m) / 10.0f;
  const float margin = max(GEOFENCE_HYSTERESIS_MIN_M, step * GEOFENCE_HYSTERESIS_PCT);
  const float current_edge = static_cast<float>(last_geofence_range) * step;

  if (next_range > last_geofence_range) {
    if (dist_m < current_edge + margin) {
      return last_geofence_range;
    }
  } else if (next_range < last_geofence_range) {
    const float lower_edge = static_cast<float>(last_geofence_range - 1) * step;
    if (dist_m > lower_edge - margin) {
      return last_geofence_range;
    }
  }

  last_geofence_range = next_range;
  last_geofence_distance_m = dist_m;
  return next_range;
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
    if (wifi_off) {
      const float pulse = double_pulse_scale(AP_OFF_PULSE_PERIOD_MS, AP_OFF_PULSE_MS);
      wifi_color = scale_rgb(ap_base, pulse);
    } else if (!sta_ok && wifi_ssid.length() > 0 && ap_enabled && WiFi.getMode() == WIFI_AP) {
      wifi_color = err_base;
    } else if (sta_ok) {
      wifi_color = wifi_base;
    } else if (sta_try) {
      wifi_color = scale_rgb(wifi_base, pulse_scale(1500));
    } else if (ap_enabled) {
      if (ap_station_count > 0) {
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

static Rgb random_show_color() {
  const uint8_t hue = random8(0, 255);
  const uint8_t sat = random8(200, 255);
  const uint8_t val = random8(180, 255);
  return hsv_to_rgb(hue, sat, val);
}

static void maybe_reset_show_state() {
  if (show_effect_id == 10) { // FIRE
    for (int i = 0; i < LED_STRIP_COUNT; ++i) {
      heat_a[i] = 0;
      heat_b[i] = 0;
    }
  }
}

static void update_show_mode(unsigned long now_ms) {
  if (show_first_tick) {
    show_first_tick = false;
    show_effect_id = 0;
    show_effect_since_ms = now_ms;
    show_base = random_show_color();
    show_state_a = {};
    show_state_b = {};
  }

  if (now_ms - show_effect_since_ms >= SHOW_EFFECT_MS) {
    show_effect_id = static_cast<uint8_t>((show_effect_id + 1) % EFFECT_COUNT);
    show_effect_since_ms = now_ms;
    show_base = random_show_color();
    maybe_reset_show_state();
  }

  if (now_ms - last_led_update_ms < LED_UPDATE_MS) {
    return;
  }
  last_led_update_ms = now_ms;

  const bool gps_ok = has_gps_fix;
  const bool sta_ok = (wifi_sta_connected && WiFi.status() == WL_CONNECTED);
  const bool sta_try = (!sta_ok && wifi_sta_connecting);

  update_gps_fix_timer(now_ms, gps_ok);
  const bool critical_error = compute_critical_error(now_ms, gps_ok, sta_ok);
  const bool homogeneous_mode = (wifi_off && gps_fix_ms >= WIFI_OFF_GPS_FIX_MS);

  if (homogeneous_mode) {
    apply_effect(show_effect_id, leds_a, heat_a, 0, LED_STRIP_COUNT, show_base, SHOW_SPEED, SHOW_INTENSITY,
                 show_state_a);
    if (LED_STRIP_MODE == 2) {
      apply_effect(show_effect_id, leds_b, heat_b, 0, LED_STRIP_COUNT, show_base, SHOW_SPEED, SHOW_INTENSITY,
                   show_state_b);
    }
    show_leds();
    return;
  }

  const int seg_start = LED_STATUS_COUNT;
  const int seg_count = LED_STRIP_COUNT - LED_STATUS_COUNT;
  if (seg_count > 0) {
    apply_effect(show_effect_id, leds_a, heat_a, seg_start, seg_count, show_base, SHOW_SPEED, SHOW_INTENSITY,
                 show_state_a);
    if (LED_STRIP_MODE == 2) {
      apply_effect(show_effect_id, leds_b, heat_b, seg_start, seg_count, show_base, SHOW_SPEED, SHOW_INTENSITY,
                   show_state_b);
    }
  } else {
    apply_effect(show_effect_id, leds_a, heat_a, 0, LED_STRIP_COUNT, show_base, SHOW_SPEED, SHOW_INTENSITY,
                 show_state_a);
    if (LED_STRIP_MODE == 2) {
      apply_effect(show_effect_id, leds_b, heat_b, 0, LED_STRIP_COUNT, show_base, SHOW_SPEED, SHOW_INTENSITY,
                   show_state_b);
    }
  }

  paint_status_leds(now_ms, gps_ok, sta_ok, sta_try, critical_error);
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
  if (g_cfg.mode != last_mode) {
    if (g_cfg.mode == MODE_SHOW) {
      show_first_tick = true;
    }
    last_mode = g_cfg.mode;
  }
  if (g_cfg.mode == MODE_SHOW) {
    update_show_mode(now_ms);
    return;
  }
  if (now_ms - last_led_update_ms < LED_UPDATE_MS) {
    return;
  }
  last_led_update_ms = now_ms;

  const bool gps_ok = has_gps_fix;
  const bool sta_ok = (wifi_sta_connected && WiFi.status() == WL_CONNECTED);
  const bool sta_try = (!sta_ok && wifi_sta_connecting);
  update_gps_fix_timer(now_ms, gps_ok);
  const bool critical_error = compute_critical_error(now_ms, gps_ok, sta_ok);
  const bool homogeneous_mode = (wifi_off && gps_fix_ms >= WIFI_OFF_GPS_FIX_MS);

  const int seg_start = LED_STATUS_COUNT;
  const int seg_count = LED_STRIP_COUNT - LED_STATUS_COUNT;
  bool body_idle = false;
  bool home_missing = false;
  uint8_t range = 1;
  float geofence_dist_m = -1.0f;

  if (g_cfg.mode == MODE_GEOFENCE) {
    if (!gps_ok) {
      body_idle = true;
    } else if (!home_set) {
      home_missing = true;
    } else {
      geofence_dist_m = distance_to_home_m();
      if (geofence_dist_m < 0.0f) {
        body_idle = true;
      } else {
        range = geofence_range(geofence_dist_m);
        range = apply_geofence_hysteresis(range, geofence_dist_m);
      }
    }
  } else {
    if (!gps_ok) {
      body_idle = true;
    } else {
      range = speed_range(last_speed_kph);
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

static String html_config_page() {
  return String(
      "<!doctype html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Config</title>"
      "<style>body{font-family:Arial,sans-serif;margin:20px;color:#111}"
      "input,select{width:100%;padding:8px;margin:4px 0}"
      ".row{display:grid;grid-template-columns:1fr 1fr;gap:10px}"
      "button{padding:10px 14px;border:0;border-radius:6px;background:#111;color:#fff}"
      "</style></head><body>"
      "<h1>Config</h1>"
      "<div><label>Brightness</label><input id='brightness' type='number' min='1' max='255'></div>"
      "<h3>Modo</h3>"
      "<div><label>Modo</label><select id='mode'>"
      "<option value='speed'>Velocidad</option>"
      "<option value='geofence'>Geocerca</option>"
      "<option value='show'>Show</option>"
      "</select></div>"
      "<div id='speed_block'>"
      "<h3>Speed ranges (kph)</h3>"
      "<div class='row'>"
      "<input id='r1' type='number' step='0.1'><input id='r2' type='number' step='0.1'>"
      "<input id='r3' type='number' step='0.1'><input id='r4' type='number' step='0.1'>"
      "<input id='r5' type='number' step='0.1'><input id='r6' type='number' step='0.1'>"
      "<input id='r7' type='number' step='0.1'><input id='r8' type='number' step='0.1'>"
      "<input id='r9' type='number' step='0.1'>"
      "</div></div>"
      "<div id='geofence_block'>"
      "<h3>Geofence</h3>"
      "<div><label>Distancia maxima (m)</label><input id='fence_max' type='number' min='50' max='5000'></div>"
      "<div id='fence_ranges' style='font-size:12px;color:#555'></div>"
      "<div style='margin:8px 0'>"
      "<button type='button' onclick='setHome()'>Nuevo Home (GPS actual)</button> "
      "<button type='button' onclick='clearHome()'>Clear Home</button>"
      "</div>"
      "<div id='home_status' style='font-size:12px;color:#555'></div>"
      "</div>"
      "<h3>Effects (range 1-10)</h3>"
      "<div id='effects'></div>"
      "<h3>Wi-Fi AP</h3>"
      "<div><label>SSID</label><input id='ap_ssid' type='text'></div>"
      "<div><label>Password</label><input id='ap_pass' type='password' placeholder='(sin cambio)'></div>"
      "<div><label><input id='ap_open' type='checkbox'> AP abierto (sin password)</label></div>"
      "<div id='ap_hint' style='font-size:12px;color:#666'></div>"
      "<div id='ap_warn' style='font-size:12px;color:#b00'></div>"
      "<div><label>mDNS</label><input id='mdns' type='text'></div>"
      "<button onclick='saveCfg()'>Guardar</button> "
      "<button onclick='resetCfg()'>Restaurar defaults</button>"
      "<p id='status'></p>"
      "<p><a href='/'>Volver</a></p>"
      "<script>"
      "const effectsDiv=document.getElementById('effects');"
      "const modeEl=document.getElementById('mode');"
      "const speedBlock=document.getElementById('speed_block');"
      "const geofenceBlock=document.getElementById('geofence_block');"
      "const fenceMax=document.getElementById('fence_max');"
      "const fenceRanges=document.getElementById('fence_ranges');"
      "const homeStatus=document.getElementById('home_status');"
      "for(let i=1;i<=10;i++){"
      "effectsDiv.innerHTML+=`<div class='row'>"
      "<input id='e${i}a' type='number' min='0' max='11' placeholder='R${i} A'>"
      "<input id='e${i}b' type='number' min='0' max='11' placeholder='R${i} B'>"
      "<input id='e${i}s' type='number' min='0' max='255' placeholder='R${i} Speed'>"
      "<input id='e${i}i' type='number' min='0' max='255' placeholder='R${i} Intensity'>"
      "</div>`;}"
      "function updateFenceRanges(){"
      "const max=parseFloat(fenceMax.value||'0');"
      "if(!max||max<=0){fenceRanges.innerText='';return;}"
      "const step=max/10;"
      "let html='';"
      "for(let i=1;i<=10;i++){"
      "const a=((i-1)*step).toFixed(1);"
      "const b=(i*step).toFixed(1);"
      "html+=`R${i}: ${a} - ${b} m<br>`;"
      "}"
      "fenceRanges.innerHTML=html;"
      "}"
      "function updateModeVisibility(){"
      "if(modeEl.value==='geofence'){geofenceBlock.style.display='block';speedBlock.style.display='none';}"
      "else{geofenceBlock.style.display='none';speedBlock.style.display='block';}"
      "}"
      "function loadHome(){"
      "fetch('/api/home').then(r=>r.json()).then(h=>{"
      "if(!h.home_set){homeStatus.innerText='Home: no definido (auto 10s con fix)';return;}"
      "let src=h.home_source||'auto';"
      "let dist=h.distance_m>=0?` | dist ${h.distance_m.toFixed(1)} m`:'';"
      "homeStatus.innerText=`Home (${src}): ${h.home_lat.toFixed(6)}, ${h.home_lon.toFixed(6)}${dist}`;"
      "}).catch(()=>{homeStatus.innerText='Home: error';});"
      "}"
      "modeEl.onchange=updateModeVisibility;"
      "fenceMax.oninput=updateFenceRanges;"
      "fetch('/api/config').then(r=>r.json()).then(c=>{"
      "document.getElementById('brightness').value=c.led.brightness;"
      "modeEl.value=c.mode||'speed';"
      "fenceMax.value=c.fence_max_m||300;"
      "document.getElementById('r1').value=c.speed_ranges_kph[0];"
      "document.getElementById('r2').value=c.speed_ranges_kph[1];"
      "document.getElementById('r3').value=c.speed_ranges_kph[2];"
      "document.getElementById('r4').value=c.speed_ranges_kph[3];"
      "document.getElementById('r5').value=c.speed_ranges_kph[4];"
      "document.getElementById('r6').value=c.speed_ranges_kph[5];"
      "document.getElementById('r7').value=c.speed_ranges_kph[6];"
      "document.getElementById('r8').value=c.speed_ranges_kph[7];"
      "document.getElementById('r9').value=c.speed_ranges_kph[8];"
      "for(let i=1;i<=10;i++){"
      "const e=c.effects['range'+i];"
      "document.getElementById('e'+i+'a').value=e.a;"
      "document.getElementById('e'+i+'b').value=e.b;"
      "document.getElementById('e'+i+'s').value=e.speed;"
      "document.getElementById('e'+i+'i').value=e.intensity;"
      "}"
      "document.getElementById('ap_ssid').value=c.wifi.ap_ssid;"
      "document.getElementById('mdns').value=c.wifi.mdns;"
      "document.getElementById('ap_open').checked=!c.wifi.has_ap_pass;"
      "document.getElementById('ap_hint').innerText=c.wifi.has_ap_pass?'Password configurada':'AP abierto';"
      "updateModeVisibility();"
      "updateFenceRanges();"
      "loadHome();"
      "});"
      "function saveCfg(){"
      "if(ap_ssid.value!==''||ap_pass.value!==''||mdns.value!==''||ap_open.checked){"
      "ap_warn.innerText='Nota: cambiar AP puede desconectar la sesion.';"
      "if(!confirm('Guardar cambios? El AP puede reiniciarse.')){return;}"
      "}"
      "const cfg={version:3,mode:modeEl.value,fence_max_m:parseInt(fenceMax.value||'300'),"
      "led:{brightness:parseInt(brightness.value)},"
      "speed_ranges_kph:[parseFloat(r1.value),parseFloat(r2.value),parseFloat(r3.value),parseFloat(r4.value),parseFloat(r5.value),"
      "parseFloat(r6.value),parseFloat(r7.value),parseFloat(r8.value),parseFloat(r9.value)],"
      "effects:{}};"
      "for(let i=1;i<=10;i++){cfg.effects['range'+i]={"
      "a:parseInt(document.getElementById('e'+i+'a').value),"
      "b:parseInt(document.getElementById('e'+i+'b').value),"
      "speed:parseInt(document.getElementById('e'+i+'s').value),"
      "intensity:parseInt(document.getElementById('e'+i+'i').value)};}"
      "cfg.wifi={ap_ssid:ap_ssid.value,ap_pass:ap_pass.value,ap_open:ap_open.checked,mdns:mdns.value};"
      "fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(cfg)})"
      ".then(r=>r.json()).then(r=>{"
      "status.innerText=r.status+(r.wifi_restart?' (reiniciando AP)':'');"
      "}).catch(()=>{status.innerText='error'});"
      "}"
      "function setHome(){"
      "fetch('/api/home/set',{method:'POST'}).then(r=>r.json()).then(r=>{"
      "homeStatus.innerText=r.status==='ok'?'Home actualizado':'Home error';"
      "loadHome();"
      "}).catch(()=>{homeStatus.innerText='Home error';});"
      "}"
      "function clearHome(){"
      "fetch('/api/home/clear',{method:'POST'}).then(r=>r.json()).then(r=>{"
      "homeStatus.innerText=r.status==='ok'?'Home borrado':'';"
      "loadHome();"
      "}).catch(()=>{homeStatus.innerText='Home error';});"
      "}"
      "function resetCfg(){"
      "if(!confirm('Restaurar defaults y reiniciar AP si aplica?')){return;}"
      "fetch('/api/config/reset',{method:'POST'})"
      ".then(r=>r.json()).then(r=>{status.innerText=r.status;}).catch(()=>{status.innerText='error'});"
      "}"
      "</script></body></html>");
}

static void handle_root() {
  server.send(200, "text/html", html_page());
}

static void handle_wifi_page() {
  server.send(200, "text/html", html_wifi_page());
}

static void handle_summary() {
  server.send(200, "application/json", build_summary_json());
}

static void handle_config_get() {
  StaticJsonDocument<3072> doc;
  doc["version"] = CONFIG_VERSION;
  doc["mode"] = mode_name(g_cfg.mode);
  doc["fence_max_m"] = g_cfg.fence_max_m;
  doc["led"]["brightness"] = g_cfg.brightness;
  JsonArray ranges = doc["speed_ranges_kph"].to<JsonArray>();
  for (int i = 0; i < 9; ++i) {
    ranges.add(g_cfg.ranges[i]);
  }
  JsonObject effects = doc["effects"].to<JsonObject>();
  for (int i = 0; i < 10; ++i) {
    JsonObject r = effects[String("range") + String(i + 1)].to<JsonObject>();
    r["a"] = g_cfg.effects[i].effect_a;
    r["b"] = g_cfg.effects[i].effect_b;
    r["speed"] = g_cfg.effects[i].speed;
    r["intensity"] = g_cfg.effects[i].intensity;
  }
  doc["wifi"]["ap_ssid"] = g_cfg.ap_ssid;
  doc["wifi"]["has_ap_pass"] = (g_cfg.ap_pass.length() >= 8);
  doc["wifi"]["mdns"] = g_cfg.mdns;

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static bool valid_mdns(const String &value) {
  if (value.length() < 1 || value.length() > 32) {
    return false;
  }
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    const bool ok = (c >= 'a' && c <= 'z') ||
                    (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') ||
                    (c == '-');
    if (!ok) {
      return false;
    }
  }
  return true;
}

static void handle_config_post() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"no body\"}");
    return;
  }
  StaticJsonDocument<4096> doc;
  const DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"bad json\"}");
    return;
  }

  RuntimeConfig next = g_cfg;
  const int brightness = doc["led"]["brightness"] | g_cfg.brightness;
  if (brightness < 1 || brightness > 255) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"brightness\"}");
    return;
  }
  next.brightness = static_cast<uint8_t>(brightness);

  if (doc.containsKey("mode")) {
    const char *mode_str = doc["mode"];
    uint8_t parsed_mode = next.mode;
    if (!parse_mode(mode_str, parsed_mode)) {
      server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"mode\"}");
      return;
    }
    next.mode = parsed_mode;
  }

  if (doc.containsKey("fence_max_m")) {
    const int fence_max = doc["fence_max_m"] | static_cast<int>(next.fence_max_m);
    if (fence_max < GEOFENCE_MAX_M_MIN || fence_max > GEOFENCE_MAX_M_MAX) {
      server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"fence_max\"}");
      return;
    }
    next.fence_max_m = static_cast<uint16_t>(fence_max);
  }

  JsonArray ranges = doc["speed_ranges_kph"].as<JsonArray>();
  if (ranges.size() != 9) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"ranges\"}");
    return;
  }
  for (int i = 0; i < 9; ++i) {
    next.ranges[i] = ranges[i].as<float>();
    if (next.ranges[i] <= 0.0f) {
      server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"ranges value\"}");
      return;
    }
  }
  if (!validate_ranges(next.ranges)) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"ranges order\"}");
    return;
  }

  JsonObject effects = doc["effects"].as<JsonObject>();
  for (int i = 0; i < 10; ++i) {
    JsonObject r = effects[String("range") + String(i + 1)];
    if (r.isNull()) {
      server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"effects\"}");
      return;
    }
    const int eff_a = r["a"] | next.effects[i].effect_a;
    const int eff_b = r["b"] | next.effects[i].effect_b;
    const int eff_speed = r["speed"] | next.effects[i].speed;
    const int eff_intensity = r["intensity"] | next.effects[i].intensity;
    if (eff_a < 0 || eff_a > 11 || eff_b < 0 || eff_b > 11 ||
        eff_speed < 0 || eff_speed > 255 || eff_intensity < 0 || eff_intensity > 255) {
      server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"effect values\"}");
      return;
    }
    next.effects[i].effect_a = static_cast<uint8_t>(eff_a);
    next.effects[i].effect_b = static_cast<uint8_t>(eff_b);
    next.effects[i].speed = static_cast<uint8_t>(eff_speed);
    next.effects[i].intensity = static_cast<uint8_t>(eff_intensity);
  }
  if (!validate_effects(next.effects)) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"effect id\"}");
    return;
  }

  const String ap_ssid = doc["wifi"]["ap_ssid"] | next.ap_ssid;
  const String ap_pass = doc["wifi"]["ap_pass"] | String("");
  const bool ap_open = doc["wifi"]["ap_open"] | false;
  const String mdns = doc["wifi"]["mdns"] | next.mdns;
  if (ap_ssid.length() < 1 || ap_ssid.length() > 32) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"ssid\"}");
    return;
  }
  if (!ap_open && ap_pass.length() > 0 && ap_pass.length() < 8) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"pass\"}");
    return;
  }
  if (!valid_mdns(mdns)) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"mdns\"}");
    return;
  }
  next.ap_ssid = ap_ssid;
  if (ap_open) {
    next.ap_pass = "";
  } else if (ap_pass.length() > 0) {
    next.ap_pass = ap_pass;
  }
  next.mdns = mdns;

  RuntimeConfig previous = g_cfg;
  g_cfg = next;
  save_config();
  apply_config(previous);
  const bool wifi_restart = (g_cfg.ap_ssid != previous.ap_ssid || g_cfg.ap_pass != previous.ap_pass);
  if (wifi_restart) {
    pending_ap_restart = true;
    pending_ap_at_ms = millis();
  }
  server.send(200, "application/json", wifi_restart ? "{\"status\":\"ok\",\"wifi_restart\":true}"
                                                     : "{\"status\":\"ok\",\"wifi_restart\":false}");
}

static void handle_config_reset() {
  prefs_cfg.clear();
  RuntimeConfig previous = g_cfg;
  set_default_config();
  save_config();
  apply_config(previous);
  if (g_cfg.ap_ssid != previous.ap_ssid || g_cfg.ap_pass != previous.ap_pass) {
    pending_ap_restart = true;
    pending_ap_at_ms = millis();
  }
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handle_config_page() {
  server.send(200, "text/html", html_config_page());
}

static const char *home_source_name(uint8_t source) {
  switch (source) {
    case 2:
      return "manual";
    case 1:
      return "auto";
    default:
      return "none";
  }
}

static void handle_home_get() {
  StaticJsonDocument<512> doc;
  doc["home_set"] = home_set;
  doc["home_source"] = home_source_name(home_source);
  doc["home_lat"] = home_set ? home_lat_deg : 0.0f;
  doc["home_lon"] = home_set ? home_lon_deg : 0.0f;
  doc["gps_fix"] = has_gps_fix;
  doc["current_lat"] = has_current_fix ? current_lat_deg : 0.0f;
  doc["current_lon"] = has_current_fix ? current_lon_deg : 0.0f;
  const float dist = distance_to_home_m();
  doc["distance_m"] = (dist >= 0.0f) ? dist : -1.0f;

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handle_home_set() {
  if (!has_gps_fix || !has_current_fix) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"no_gps\"}");
    return;
  }
  set_home(current_lat_deg, current_lon_deg, 2);
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handle_home_clear() {
  clear_home();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handle_wifi_save() {
  if (!server.hasArg("ssid")) {
    server.send(400, "text/plain", "missing ssid");
    return;
  }
  const String ssid = server.arg("ssid");
  const String pass = server.arg("pass");
  save_wifi_creds(ssid, pass);
  start_sta_mode();
  server.send(200, "text/plain", "saved, connecting");
}

static void start_ap_mode() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(g_cfg.ap_ssid.c_str(), g_cfg.ap_pass.c_str());
  ap_enabled = true;
  wifi_off = false;
  ap_station_count = 0;
  last_ap_client_ms = millis();
  wifi_sta_connected = false;
  wifi_sta_connecting = false;
}

static void start_sta_mode() {
  if (ap_enabled) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(g_cfg.ap_ssid.c_str(), g_cfg.ap_pass.c_str());
    ap_station_count = 0;
    last_ap_client_ms = millis();
  } else {
    WiFi.mode(WIFI_STA);
  }
  wifi_off = false;
  WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
  wifi_sta_connected = false;
  wifi_sta_connecting = true;
  wifi_sta_start_ms = millis();
}

static void enable_ap() {
  if (ap_enabled) {
    return;
  }
  if (wifi_ssid.length() > 0) {
    WiFi.mode(WIFI_AP_STA);
  } else {
    WiFi.mode(WIFI_AP);
  }
  WiFi.softAP(g_cfg.ap_ssid.c_str(), g_cfg.ap_pass.c_str());
  ap_enabled = true;
  wifi_off = false;
  ap_station_count = 0;
  last_ap_client_ms = millis();
}

static void disable_ap() {
  if (!ap_enabled) {
    return;
  }
  WiFi.softAPdisconnect(true);
  ap_enabled = false;
  ap_station_count = 0;
  last_ap_client_ms = 0;
  WiFi.mode(WIFI_STA);
}

static void set_wifi_off(bool off) {
  if (off) {
    if (wifi_off) {
      return;
    }
    WiFi.mode(WIFI_OFF);
    wifi_off = true;
    ap_enabled = false;
    ap_station_count = 0;
    wifi_sta_connected = false;
    wifi_sta_connecting = false;
    last_ap_client_ms = 0;
    last_ap_poll_ms = 0;
    return;
  }
  if (!wifi_off) {
    return;
  }
  wifi_off = false;
  ap_enabled = true;
  if (wifi_ssid.length() > 0) {
    start_sta_mode();
  } else {
    start_ap_mode();
  }
}

static void setup_wifi() {
  load_wifi_creds();
  ap_enabled = true;
  wifi_off = false;
  if (wifi_ssid.length() > 0) {
    start_sta_mode();
  } else {
    start_ap_mode();
  }
}

static void update_ap_policy(unsigned long now_ms) {
  if (last_stationary_ms == 0) {
    last_stationary_ms = now_ms;
  }
  const unsigned long dt_ms = now_ms - last_stationary_ms;
  last_stationary_ms = now_ms;

  if (has_gps_fix) {
    if (last_speed_kph <= AP_STATIONARY_ON_KPH) {
      stationary_ms = (stationary_ms + dt_ms > AP_STATIONARY_MS) ? AP_STATIONARY_MS : (stationary_ms + dt_ms);
    } else if (last_speed_kph >= AP_STATIONARY_OFF_KPH) {
      stationary_ms = 0;
    }
  } else {
    stationary_ms = 0;
  }

  const bool ap_force_on = !has_gps_fix;
  const bool ap_request_on = (stationary_ms >= AP_STATIONARY_MS);

  if (wifi_off) {
    if (ap_force_on || ap_request_on) {
      set_wifi_off(false);
    } else {
      return;
    }
  }

  if (ap_enabled && (now_ms - last_ap_poll_ms) >= AP_CLIENT_POLL_MS) {
    last_ap_poll_ms = now_ms;
    const int stations = WiFi.softAPgetStationNum();
    ap_station_count = (stations > 0) ? static_cast<uint8_t>(stations) : 0;
    if (stations > 0) {
      last_ap_client_ms = now_ms;
    }
  } else if (!ap_enabled) {
    ap_station_count = 0;
  }

  if (ap_force_on) {
    if (!ap_enabled) {
      enable_ap();
    }
    last_ap_client_ms = now_ms;
    return;
  }

  if (ap_request_on && !ap_enabled) {
    enable_ap();
  }

  if (ap_enabled) {
    if (last_ap_client_ms == 0) {
      last_ap_client_ms = now_ms;
    }
    if ((now_ms - last_ap_client_ms) >= AP_IDLE_TIMEOUT_MS) {
      disable_ap();
      stationary_ms = 0;
      if (!wifi_sta_connected || wifi_ssid.length() == 0) {
        set_wifi_off(true);
      }
    }
  }
}

static void setup_http() {
  server.on("/", HTTP_GET, handle_root);
  server.on("/api/summary", HTTP_GET, handle_summary);
  server.on("/api/config", HTTP_GET, handle_config_get);
  server.on("/api/config", HTTP_POST, handle_config_post);
  server.on("/api/config/reset", HTTP_POST, handle_config_reset);
  server.on("/config", HTTP_GET, handle_config_page);
  server.on("/api/home", HTTP_GET, handle_home_get);
  server.on("/api/home/set", HTTP_POST, handle_home_set);
  server.on("/api/home/clear", HTTP_POST, handle_home_clear);
  server.on("/wifi", HTTP_GET, handle_wifi_page);
  server.on("/api/wifi", HTTP_POST, handle_wifi_save);
  server.begin();
}

// Expose the daily summary via BLE (read-only).
static void setup_ble() {
  BLEDevice::init(BLE_DEVICE_NAME);
  BLEServer *server = BLEDevice::createServer();
  BLEService *service = server->createService(BLE_SERVICE_UUID);
  summary_char = service->createCharacteristic(
      BLE_CHAR_UUID, BLECharacteristic::PROPERTY_READ);
  summary_char->setValue("init");
  service->start();
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(BLE_SERVICE_UUID);
  adv->setScanResponse(true);
  adv->start();
}

// Handle a single NMEA line and update rolling metrics.
static void handle_nmea_line(const char *line) {
  float speed_kph = 0.0f;
  float lat_deg = 0.0f;
  float lon_deg = 0.0f;
  bool valid_fix = false;
  uint32_t date_yyyymmdd = 0;
  uint16_t time_min = 0;

  const bool is_rmc = (strncmp(line, "$GPRMC,", 7) == 0 || strncmp(line, "$GNRMC,", 7) == 0);
  if (is_rmc) {
    gps_rmc_seen++;
    gps_last_rmc_ms = millis();
  }
  const bool is_gga = (strncmp(line, "$GPGGA,", 7) == 0 || strncmp(line, "$GNGGA,", 7) == 0);
  if (is_gga) {
    gps_gga_seen++;
    gps_last_gga_ms = millis();
    uint8_t fix_quality = gps_fix_quality;
    uint8_t sats = gps_sats;
    if (parse_gga(line, &fix_quality, &sats)) {
      gps_fix_quality = fix_quality;
      gps_sats = sats;
    }
  }
  if (parse_rmc(line, &lat_deg, &lon_deg, &speed_kph, &valid_fix, &date_yyyymmdd, &time_min)) {
    has_gps_fix = valid_fix;
    last_speed_kph = speed_kph;
    last_gps_ms = millis();
    last_update_min = time_min;
    if (valid_fix) {
      current_lat_deg = lat_deg;
      current_lon_deg = lon_deg;
      has_current_fix = true;
      gps_rmc_valid++;
      gps_last_fix_ms = last_gps_ms;
    } else {
      has_current_fix = false;
    }

    if (date_yyyymmdd != 0 && date_yyyymmdd != current_date_yyyymmdd) {
      current_date_yyyymmdd = date_yyyymmdd;
      total_distance_m = 0.0f;
      active_time_ms = 0;
      max_speed_kph = 0.0f;
      has_last_point = false;
      save_metrics();
    }

    if (has_gps_fix && speed_kph <= SPEED_MAX_VALID_KPH) {
      const unsigned long now_ms = millis();
      if (now_ms - last_sample_ms >= GPS_SAMPLE_MS) {
        last_sample_ms = now_ms;

        if (has_last_point) {
          const float segment_m = haversine_m(last_lat_deg, last_lon_deg, lat_deg, lon_deg);
          if (segment_m < 50.0f) {
            total_distance_m += segment_m;
          }
        }

        last_lat_deg = lat_deg;
        last_lon_deg = lon_deg;
        has_last_point = true;

        if (speed_kph > SPEED_ACTIVE_KPH) {
          active_time_ms += GPS_SAMPLE_MS;
        }
        if (speed_kph > max_speed_kph) {
          max_speed_kph = speed_kph;
        }
      }
    }
  }
}

// Read bytes from GPS UART and assemble NMEA lines.
static void read_gps() {
  while (GPS.available() > 0) {
    const char c = static_cast<char>(GPS.read());
    gps_bytes_rx++;
    gps_last_byte_ms = millis();
    if (c == '\n') {
      nmea_line[nmea_len] = '\0';
      if (nmea_len > 6) {
        gps_sentences_rx++;
        gps_last_sentence_ms = millis();
        handle_nmea_line(nmea_line);
      }
      nmea_len = 0;
    } else if (c != '\r') {
      if (nmea_len + 1 < sizeof(nmea_line)) {
        nmea_line[nmea_len++] = c;
      } else {
        gps_overflow++;
        nmea_len = 0;
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  // GPS on UART1 with selected RX/TX pins.
  GPS.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);
  // Open NVS namespace and restore last known metrics.
  prefs.begin("dogrgb", false);
  prefs_cfg.begin("dogrgb_cfg", false);
  load_metrics();
  load_config();
  load_home();
  if (LED_UI_ENABLED) {
    led_begin();
    start_welcome();
  }
  setup_wifi();
  setup_http();
  setup_ble();
  Serial.println("Dog-RGB ESP32-S3 GPS-first base firmware");
  Serial.print("GPS UART1: baud=");
  Serial.print(GPS_BAUD);
  Serial.print(" rx_pin=");
  Serial.print(PIN_GPS_RX);
  Serial.print(" tx_pin=");
  Serial.println(PIN_GPS_TX);
  Serial.println("GPS status: waiting for NMEA data...");
}

void loop() {
  const unsigned long now_ms = millis();
  read_gps();
  update_fix_stability(now_ms);
  maybe_auto_set_home();

  // Periodic persistence to avoid flash wear.
  if (now_ms - last_save_ms >= SAVE_INTERVAL_MS) {
    last_save_ms = now_ms;
    save_metrics();
  }

  if (now_ms - last_heartbeat_ms >= HEARTBEAT_MS) {
    last_heartbeat_ms = now_ms;
    led_state = !led_state;
    digitalWrite(PIN_STATUS_LED, led_state ? HIGH : LOW);
  }

  if (now_ms - last_log_ms >= LOG_MS) {
    last_log_ms = now_ms;
    const float avg_speed_kph = (active_time_ms > 0)
                                    ? (total_distance_m / (active_time_ms / 1000.0f)) * 3.6f
                                    : 0.0f;
    // Serial log for quick field diagnostics.

    const unsigned long bytes_delta = gps_bytes_rx - gps_last_bytes_log;
    const unsigned long sentences_delta = gps_sentences_rx - gps_last_sentences_log;
    const unsigned long rmc_delta = gps_rmc_seen - gps_last_rmc_log;
    gps_last_bytes_log = gps_bytes_rx;
    gps_last_sentences_log = gps_sentences_rx;
    gps_last_rmc_log = gps_rmc_seen;

    const unsigned long age_byte_ms = (gps_last_byte_ms > 0) ? (now_ms - gps_last_byte_ms) : 0;
    const unsigned long age_rmc_ms = (gps_last_rmc_ms > 0) ? (now_ms - gps_last_rmc_ms) : 0;
    const unsigned long age_fix_ms = (gps_last_fix_ms > 0) ? (now_ms - gps_last_fix_ms) : 0;
    const unsigned long age_gga_ms = (gps_last_gga_ms > 0) ? (now_ms - gps_last_gga_ms) : 0;
    const bool uart_active = (gps_last_byte_ms > 0 && age_byte_ms <= GPS_NO_DATA_MS);
    const bool rmc_active = (gps_last_rmc_ms > 0 && age_rmc_ms <= GPS_NO_DATA_MS);
    const char *gps_link = "NO_DATA";
    if (uart_active) {
      gps_link = rmc_active ? (has_gps_fix ? "FIX" : "NO_FIX") : "NO_RMC";
    }

    Serial.print("[GPS] fix=");
    Serial.print(has_gps_fix ? "1" : "0");
    Serial.print(" link=");
    Serial.print(gps_link);
    Serial.print(" uart_bps=");
    Serial.print(bytes_delta);
    Serial.print(" nmea=");
    Serial.print(gps_sentences_rx);
    Serial.print(" (");
    Serial.print(sentences_delta);
    Serial.print("/s)");
    Serial.print(" rmc=");
    Serial.print(gps_rmc_seen);
    Serial.print(" (");
    Serial.print(rmc_delta);
    Serial.print("/s)");
    Serial.print(" fix_ok=");
    Serial.print(gps_rmc_valid);
    Serial.print(" fixq=");
    Serial.print(gps_fix_quality);
    Serial.print(" sats=");
    Serial.print(gps_sats);
    Serial.print(" age_ms=");
    Serial.print(age_byte_ms);
    Serial.print(" fix_age_ms=");
    if (gps_last_fix_ms > 0) {
      Serial.print(age_fix_ms);
    } else {
      Serial.print("na");
    }
    Serial.print(" overflow=");
    Serial.println(gps_overflow);

    Serial.print("[POS] lat=");
    if (has_last_point) {
      Serial.print(last_lat_deg, 6);
      Serial.print(" lon=");
      Serial.print(last_lon_deg, 6);
    } else {
      Serial.print("na lon=na");
    }
    Serial.print(" rmc_age_ms=");
    Serial.print(age_rmc_ms);
    Serial.print(" gga_age_ms=");
    Serial.println(age_gga_ms);

    Serial.print("[METRICS] dist_m=");
    Serial.print(total_distance_m, 1);
    Serial.print(" avg_kph=");
    Serial.print(avg_speed_kph, 2);
    Serial.print(" max_kph=");
    Serial.println(max_speed_kph, 2);

    const bool gps_ok = has_gps_fix;
    const bool sta_ok = (wifi_sta_connected && WiFi.status() == WL_CONNECTED);
    const bool sta_try = (!sta_ok && wifi_ssid.length() > 0 && WiFi.getMode() == WIFI_STA);
    const bool ap_mode = (WiFi.getMode() == WIFI_AP);
    const bool critical_error = (!gps_ok && !sta_ok && (now_ms - last_ok_ms) > CRITICAL_NO_OK_MS);

    const char *status_text = "GPS_SEARCH";
    if (critical_error) {
      status_text = "CRITICAL";
    } else if (!sta_ok && wifi_ssid.length() > 0 && ap_mode) {
      status_text = "AP_FALLBACK";
    } else if (sta_ok) {
      status_text = "STA_OK";
    } else if (sta_try) {
      status_text = "STA_CONNECTING";
    } else if (ap_mode) {
      status_text = "AP_MODE";
    } else if (gps_ok) {
      status_text = "GPS_OK";
    }

    const int seg_start = LED_STATUS_COUNT;
    const int seg_count = LED_STRIP_COUNT - LED_STATUS_COUNT;
    bool body_idle = false;
    bool home_missing = false;
    uint8_t range = 1;
    float geofence_dist_m = -1.0f;

    if (g_cfg.mode == MODE_GEOFENCE) {
      if (!gps_ok) {
        body_idle = true;
      } else if (!home_set) {
        home_missing = true;
      } else {
        geofence_dist_m = distance_to_home_m();
        if (geofence_dist_m < 0.0f) {
          body_idle = true;
        } else {
          range = geofence_range(geofence_dist_m);
        }
      }
    } else {
      if (!gps_ok) {
        body_idle = true;
      } else {
        range = speed_range(last_speed_kph);
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

    uint8_t sr = 0;
    uint8_t sg = 0;
    uint8_t sb = 0;
    float sscale = 1.0f;
    if (critical_error) {
      sscale = (now_ms / 200) % 2 ? 1.0f : 0.0f;
      sr = clamp_u8(static_cast<int>(60 * sscale));
    } else if (!sta_ok && wifi_ssid.length() > 0 && ap_mode) {
      sr = 60;
      sg = 0;
      sb = 0;
    } else if (sta_ok) {
      sr = 0;
      sg = 60;
      sb = 0;
    } else if (sta_try) {
      sscale = pulse_scale(1500);
      sr = 0;
      sg = clamp_u8(static_cast<int>(60 * sscale));
      sb = 0;
    } else if (ap_mode) {
      sr = 60;
      sg = 45;
      sb = 0;
    } else if (gps_ok) {
      sr = 0;
      sg = 0;
      sb = 60;
    } else {
      sscale = pulse_scale(1500);
      sr = 0;
      sg = 0;
      sb = clamp_u8(static_cast<int>(60 * sscale));
    }

    Serial.print("[LED] mode=");
    Serial.print(mode_name(g_cfg.mode));
    if (g_cfg.mode == MODE_GEOFENCE) {
      Serial.print(" dist_m=");
      Serial.print(geofence_dist_m, 1);
      Serial.print(" fence_max=");
      Serial.print(g_cfg.fence_max_m);
    }
    Serial.print(" status_leds=0..");
    Serial.print(LED_STATUS_COUNT - 1);
    Serial.print(" status=");
    Serial.print(status_text);
    Serial.print(" status_rgb=");
    Serial.print(sr);
    Serial.print(",");
    Serial.print(sg);
    Serial.print(",");
    Serial.print(sb);
    Serial.print(" body_on=");
    Serial.print(has_range ? "1" : "0");
    Serial.print(" home_missing=");
    Serial.print(home_missing ? "1" : "0");
    Serial.print(" range=");
    Serial.print(range);
    Serial.print(" effect_a=");
    Serial.print(effect_name(static_cast<uint8_t>(effect_a)));
    Serial.print(" effect_b=");
    Serial.print(effect_name(static_cast<uint8_t>(effect_b)));
    Serial.print(" base_rgb=");
    Serial.print(base.r);
    Serial.print(",");
    Serial.print(base.g);
    Serial.print(",");
    Serial.print(base.b);
    Serial.print(" speed=");
    Serial.print(eff_speed);
    Serial.print(" intensity=");
    Serial.print(eff_intensity);
    Serial.print(" seg=");
    Serial.print(seg_start);
    Serial.print("..");
    Serial.println(seg_start + seg_count - 1);
  }

  if (summary_char != nullptr) {
    uint8_t payload[16];
    build_summary_payload(payload, sizeof(payload));
    summary_char->setValue(payload, sizeof(payload));
  }

  if (!wifi_off && (now_ms - last_wifi_check_ms >= WIFI_RETRY_INTERVAL_MS)) {
    last_wifi_check_ms = now_ms;
    if (wifi_sta_connected && WiFi.status() != WL_CONNECTED) {
      wifi_sta_connected = false;
      if (wifi_ssid.length() > 0) {
        start_sta_mode();
      } else {
        start_ap_mode();
      }
    } else if (wifi_sta_connecting) {
      if (WiFi.status() == WL_CONNECTED) {
        wifi_sta_connected = true;
        wifi_sta_connecting = false;
        MDNS.begin(g_cfg.mdns.c_str());
      } else if ((now_ms - wifi_sta_start_ms) >= STA_CONNECT_TIMEOUT_MS) {
        wifi_sta_connecting = false;
        start_ap_mode();
      }
    } else if (!wifi_sta_connected && wifi_ssid.length() > 0) {
      start_sta_mode();
    }
  }

  if (pending_ap_restart && (now_ms - pending_ap_at_ms) >= AP_RESTART_DELAY_MS) {
    pending_ap_restart = false;
    if (ap_enabled) {
      if (wifi_sta_connected || wifi_ssid.length() > 0) {
        WiFi.mode(WIFI_AP_STA);
      } else {
        WiFi.mode(WIFI_AP);
      }
      WiFi.softAP(g_cfg.ap_ssid.c_str(), g_cfg.ap_pass.c_str());
      last_ap_client_ms = now_ms;
    }
  }

  update_ap_policy(now_ms);
  update_led_ui();
  server.handleClient();

  // Placeholder for GPS-based LED mapping and patterns.
}
