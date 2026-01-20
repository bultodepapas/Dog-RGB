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
  - LEDs: SK6812 (single-wire)

  Pin table (XIAO ESP32-S3):
  - GNSS RX: D6 / GPIO7
  - GNSS TX: D7 / GPIO8
  - Status LED: D2 / GPIO3
  - LED A data: GPIO11
  - LED B data: GPIO12

  Dependencies:
  - FastLED
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
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <FastLED.h>
#include <ArduinoJson.h>
#include "pins.h"
#include "config.h"

// Heartbeat for status LED and periodic serial logs.
static const unsigned long HEARTBEAT_MS = 1000;
static unsigned long last_heartbeat_ms = 0;
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

// LED strip configuration is defined in config.h.
static unsigned long last_led_update_ms = 0;

static unsigned long last_ok_ms = 0;

// Speed-to-color ranges are defined in config.h.

static CRGB leds_a[LED_STRIP_COUNT];
static CRGB leds_b[LED_STRIP_COUNT];
static uint8_t heat_a[LED_STRIP_COUNT];
static uint8_t heat_b[LED_STRIP_COUNT];

struct EffectState {
  uint8_t hue = 0;
  uint16_t pos = 0;
};

static EffectState state_a;
static EffectState state_b;

struct RangeEffect {
  uint8_t effect_a;
  uint8_t effect_b;
  uint8_t speed;
  uint8_t intensity;
};

struct RuntimeConfig {
  uint8_t brightness;
  float ranges[5];
  RangeEffect effects[6];
  String ap_ssid;
  String ap_pass;
  String mdns;
};

static RuntimeConfig g_cfg;
static const uint8_t CONFIG_VERSION = 1;
static bool pending_ap_restart = false;
static unsigned long pending_ap_at_ms = 0;
static const unsigned long AP_RESTART_DELAY_MS = 500;

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

static float pulse_scale(unsigned long period_ms) {
  const unsigned long now_ms = millis();
  const float phase = static_cast<float>(now_ms % period_ms) / static_cast<float>(period_ms);
  if (phase < 0.5f) {
    return phase * 2.0f;
  }
  return (1.0f - phase) * 2.0f;
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
  for (int i = 1; i < 5; ++i) {
    if (!(ranges[i] > ranges[i - 1])) {
      return false;
    }
  }
  return true;
}

static bool validate_effects(const RangeEffect *effects) {
  for (int i = 0; i < 6; ++i) {
    if (effects[i].effect_a > 11 || effects[i].effect_b > 11) {
      return false;
    }
  }
  return true;
}

static void set_default_config() {
  g_cfg.brightness = LED_BRIGHTNESS;
  g_cfg.ranges[0] = SPEED_RANGE_1_KPH;
  g_cfg.ranges[1] = SPEED_RANGE_2_KPH;
  g_cfg.ranges[2] = SPEED_RANGE_3_KPH;
  g_cfg.ranges[3] = SPEED_RANGE_4_KPH;
  g_cfg.ranges[4] = SPEED_RANGE_5_KPH;

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

  g_cfg.ap_ssid = AP_SSID;
  g_cfg.ap_pass = AP_PASS;
  g_cfg.mdns = MDNS_NAME;
}

static void save_config() {
  prefs_cfg.putUChar("ver", CONFIG_VERSION);
  prefs_cfg.putUChar("brightness", g_cfg.brightness);
  prefs_cfg.putBytes("ranges", g_cfg.ranges, sizeof(g_cfg.ranges));
  prefs_cfg.putBytes("effects", g_cfg.effects, sizeof(g_cfg.effects));
  prefs_cfg.putString("ap_ssid", g_cfg.ap_ssid);
  prefs_cfg.putString("ap_pass", g_cfg.ap_pass);
  prefs_cfg.putString("mdns", g_cfg.mdns);
}

static void load_config() {
  if (prefs_cfg.getUChar("ver", 0) != CONFIG_VERSION) {
    set_default_config();
    save_config();
    return;
  }

  g_cfg.brightness = prefs_cfg.getUChar("brightness", LED_BRIGHTNESS);
  if (g_cfg.brightness < 1) {
    g_cfg.brightness = LED_BRIGHTNESS;
  }
  if (prefs_cfg.getBytes("ranges", g_cfg.ranges, sizeof(g_cfg.ranges)) != sizeof(g_cfg.ranges)) {
    set_default_config();
    save_config();
    return;
  }
  if (prefs_cfg.getBytes("effects", g_cfg.effects, sizeof(g_cfg.effects)) != sizeof(g_cfg.effects)) {
    set_default_config();
    save_config();
    return;
  }
  g_cfg.ap_ssid = prefs_cfg.getString("ap_ssid", AP_SSID);
  g_cfg.ap_pass = prefs_cfg.getString("ap_pass", AP_PASS);
  g_cfg.mdns = prefs_cfg.getString("mdns", MDNS_NAME);

  if (!validate_ranges(g_cfg.ranges) || !validate_effects(g_cfg.effects)) {
    set_default_config();
    save_config();
  }
}

static void apply_config(const RuntimeConfig &previous) {
  FastLED.setBrightness(g_cfg.brightness);
  if (g_cfg.mdns != previous.mdns) {
    if (wifi_sta_connected) {
      MDNS.end();
      MDNS.begin(g_cfg.mdns.c_str());
    }
  }
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
  FastLED.addLeds<SK6812, PIN_LED_A_DATA, GRB>(leds_a, LED_STRIP_COUNT);
  if (LED_STRIP_MODE == 2) {
    FastLED.addLeds<SK6812, PIN_LED_B_DATA, GRB>(leds_b, LED_STRIP_COUNT);
  }
  FastLED.setBrightness(g_cfg.brightness);
  FastLED.clear(true);
}

static void fill_range(CRGB *leds, int start, int count, const CRGB &color) {
  for (int i = start; i < start + count; ++i) {
    leds[i] = color;
  }
}

static void fade_range(CRGB *leds, int start, int count, uint8_t amount) {
  for (int i = start; i < start + count; ++i) {
    leds[i].fadeToBlackBy(amount);
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
  return 6;
}

static void get_range_config(uint8_t range,
                             int &effect_a,
                             int &effect_b,
                             uint8_t &speed,
                             uint8_t &intensity) {
  const uint8_t idx = (range > 0 && range <= 6) ? static_cast<uint8_t>(range - 1) : 0;
  effect_a = g_cfg.effects[idx].effect_a;
  effect_b = g_cfg.effects[idx].effect_b;
  speed = g_cfg.effects[idx].speed;
  intensity = g_cfg.effects[idx].intensity;
}

static CRGB base_color_for_range(uint8_t range) {
  switch (range) {
    case 1:
      return CRGB(0, 0, 60);
    case 2:
      return CRGB(20, 0, 60);
    case 3:
      return CRGB(40, 0, 60);
    case 4:
      return CRGB(60, 0, 40);
    case 5:
      return CRGB(60, 0, 20);
    default:
      return CRGB(60, 0, 0);
  }
}

static void apply_fire(CRGB *leds,
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
    heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2]) / 3;
  }
  if (random8() < sparking) {
    const int y = start + random8(min(count, 7));
    heat[y] = qadd8(heat[y], random8(160, 255));
  }
  for (int j = start; j < start + count; ++j) {
    leds[j] = HeatColor(heat[j]);
  }
  (void)speed;
}

static void apply_effect(int effect_id,
                         CRGB *leds,
                         uint8_t *heat,
                         int start,
                         int count,
                         const CRGB &base,
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
      const uint8_t beat = beatsin8(bpm, 10, 255);
      CRGB c = base;
      c.nscale8(beat);
      fill_range(leds, start, count, c);
      break;
    }
    case 2: { // BREATH
      const uint8_t beat = beatsin8(bpm, 20, 200);
      CRGB c = base;
      c.nscale8(beat);
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
      const int pos = start + beatsin16(bpm, 0, count - 1);
      leds[pos] = base;
      break;
    }
    case 6: { // CONFETTI
      fade_range(leds, start, count, fade_amt);
      const int pos = start + random16(count);
      leds[pos] += base;
      break;
    }
    case 7: { // JUGGLE
      fade_range(leds, start, count, fade_amt);
      for (uint8_t i = 0; i < 4; ++i) {
        leds[start + beatsin16(bpm + i * 2, 0, count - 1)] |= base;
      }
      break;
    }
    case 8: { // BPM
      const uint8_t beat = beatsin8(bpm, 64, 255);
      for (int i = start; i < start + count; ++i) {
        leds[i] = base;
        leds[i].nscale8(beat);
      }
      break;
    }
    case 9: { // RAINBOW
      state.hue += step_from_speed(speed, 16);
      fill_rainbow(&leds[start], count, state.hue, 7);
      break;
    }
    case 10: // FIRE
      apply_fire(leds, heat, start, count, intensity, speed);
      break;
    case 11: { // GRADIENT_WAVE
      state.hue += step_from_speed(speed, 24);
      for (int i = start; i < start + count; ++i) {
        const uint8_t offset = (state.hue + (i * 8));
        leds[i] = CHSV(offset, 200, 255);
      }
      break;
    }
    default:
      fill_range(leds, start, count, base);
      break;
  }
}

static void update_led_ui() {
  if (!LED_UI_ENABLED) {
    return;
  }
  const unsigned long now_ms = millis();
  if (now_ms - last_led_update_ms < LED_UPDATE_MS) {
    return;
  }
  last_led_update_ms = now_ms;

  const bool gps_ok = has_gps_fix;
  const bool sta_ok = (wifi_sta_connected && WiFi.status() == WL_CONNECTED);
  const bool sta_try = (!sta_ok && wifi_ssid.length() > 0 && WiFi.getMode() == WIFI_STA);
  const bool ap_mode = (WiFi.getMode() == WIFI_AP);

  if (gps_ok || sta_ok) {
    last_ok_ms = now_ms;
  }

  const bool critical_error = (!gps_ok && !sta_ok && (now_ms - last_ok_ms) > CRITICAL_NO_OK_MS);

  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  float scale = 1.0f;

  bool full_override = false;
  uint8_t full_r = 0;
  uint8_t full_g = 0;
  uint8_t full_b = 0;

  if (critical_error) {
    scale = (now_ms / 200) % 2 ? 1.0f : 0.0f;
    full_override = true;
    full_r = clamp_u8(static_cast<int>(60 * scale));
  } else if (!sta_ok && wifi_ssid.length() > 0 && ap_mode) {
    full_override = true;
    full_r = 60;
    full_g = 0;
    full_b = 0;
  }

  if (full_override) {
    fill_solid(leds_a, LED_STRIP_COUNT, CRGB(full_r, full_g, full_b));
    if (LED_STRIP_MODE == 2) {
      fill_solid(leds_b, LED_STRIP_COUNT, CRGB(full_r, full_g, full_b));
    }
    FastLED.show();
    return;
  }

  if (sta_ok) {
    r = 0;
    g = 60;
    b = 0;
  } else if (sta_try) {
    scale = pulse_scale(1500);
    r = 0;
    g = clamp_u8(static_cast<int>(60 * scale));
    b = 0;
  } else if (ap_mode) {
    r = 60;
    g = 45;
    b = 0;
  } else if (gps_ok) {
    r = 0;
    g = 0;
    b = 60;
  } else {
    scale = pulse_scale(1500);
    r = 0;
    g = 0;
    b = clamp_u8(static_cast<int>(60 * scale));
  }

  const bool body_on = gps_ok;
  const int seg_start = LED_STATUS_COUNT;
  const int seg_count = LED_STRIP_COUNT - LED_STATUS_COUNT;
  const uint8_t range = speed_range(last_speed_kph);
  int effect_a = RANGE_1_EFFECT_A;
  int effect_b = RANGE_1_EFFECT_B;
  uint8_t eff_speed = RANGE_1_SPEED;
  uint8_t eff_intensity = RANGE_1_INTENSITY;
  get_range_config(range, effect_a, effect_b, eff_speed, eff_intensity);
  const CRGB base = base_color_for_range(range);

  if (body_on && seg_count > 0) {
    apply_effect(effect_a, leds_a, heat_a, seg_start, seg_count, base, eff_speed, eff_intensity, state_a);
    if (LED_STRIP_MODE == 2) {
      apply_effect(effect_b, leds_b, heat_b, seg_start, seg_count, base, eff_speed, eff_intensity, state_b);
    }
  } else if (seg_count > 0) {
    fill_range(leds_a, seg_start, seg_count, CRGB(0, 0, 0));
    if (LED_STRIP_MODE == 2) {
      fill_range(leds_b, seg_start, seg_count, CRGB(0, 0, 0));
    }
  }

  fill_range(leds_a, 0, LED_STATUS_COUNT, CRGB(r, g, b));
  if (LED_STRIP_MODE == 2) {
    fill_range(leds_b, 0, LED_STATUS_COUNT, CRGB(r, g, b));
  }
  FastLED.show();
}

static String html_config_page() {
  return String(
      "<!doctype html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Config</title>"
      "<style>body{font-family:Arial,sans-serif;margin:20px;color:#111}"
      "input{width:100%;padding:8px;margin:4px 0}"
      ".row{display:grid;grid-template-columns:1fr 1fr;gap:10px}"
      "button{padding:10px 14px;border:0;border-radius:6px;background:#111;color:#fff}"
      "</style></head><body>"
      "<h1>Config</h1>"
      "<div><label>Brightness</label><input id='brightness' type='number' min='1' max='255'></div>"
      "<h3>Speed ranges (kph)</h3>"
      "<div class='row'>"
      "<input id='r1' type='number' step='0.1'><input id='r2' type='number' step='0.1'>"
      "<input id='r3' type='number' step='0.1'><input id='r4' type='number' step='0.1'>"
      "<input id='r5' type='number' step='0.1'>"
      "</div>"
      "<h3>Effects (range 1-6)</h3>"
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
      "for(let i=1;i<=6;i++){"
      "effectsDiv.innerHTML+=`<div class='row'>"
      "<input id='e${i}a' type='number' min='0' max='11' placeholder='R${i} A'>"
      "<input id='e${i}b' type='number' min='0' max='11' placeholder='R${i} B'>"
      "<input id='e${i}s' type='number' min='0' max='255' placeholder='R${i} Speed'>"
      "<input id='e${i}i' type='number' min='0' max='255' placeholder='R${i} Intensity'>"
      "</div>`;}"
      "fetch('/api/config').then(r=>r.json()).then(c=>{"
      "document.getElementById('brightness').value=c.led.brightness;"
      "document.getElementById('r1').value=c.speed_ranges_kph[0];"
      "document.getElementById('r2').value=c.speed_ranges_kph[1];"
      "document.getElementById('r3').value=c.speed_ranges_kph[2];"
      "document.getElementById('r4').value=c.speed_ranges_kph[3];"
      "document.getElementById('r5').value=c.speed_ranges_kph[4];"
      "for(let i=1;i<=6;i++){"
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
      "});"
      "function saveCfg(){"
      "if(ap_ssid.value!==''||ap_pass.value!==''||mdns.value!==''||ap_open.checked){"
      "ap_warn.innerText='Nota: cambiar AP puede desconectar la sesion.';"
      "if(!confirm('Guardar cambios? El AP puede reiniciarse.')){return;}"
      "}"
      "const cfg={version:1,led:{brightness:parseInt(brightness.value)},"
      "speed_ranges_kph:[parseFloat(r1.value),parseFloat(r2.value),parseFloat(r3.value),parseFloat(r4.value),parseFloat(r5.value)],"
      "effects:{}};"
      "for(let i=1;i<=6;i++){cfg.effects['range'+i]={"
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
  StaticJsonDocument<1536> doc;
  doc["version"] = CONFIG_VERSION;
  doc["led"]["brightness"] = g_cfg.brightness;
  JsonArray ranges = doc.createNestedArray("speed_ranges_kph");
  for (int i = 0; i < 5; ++i) {
    ranges.add(g_cfg.ranges[i]);
  }
  JsonObject effects = doc.createNestedObject("effects");
  for (int i = 0; i < 6; ++i) {
    JsonObject r = effects.createNestedObject(String("range") + String(i + 1));
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
  StaticJsonDocument<2048> doc;
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

  JsonArray ranges = doc["speed_ranges_kph"].as<JsonArray>();
  if (ranges.size() != 5) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"ranges\"}");
    return;
  }
  for (int i = 0; i < 5; ++i) {
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
  for (int i = 0; i < 6; ++i) {
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
  wifi_sta_connected = false;
  wifi_sta_connecting = false;
}

static void start_sta_mode() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(g_cfg.ap_ssid.c_str(), g_cfg.ap_pass.c_str());
  WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
  wifi_sta_connected = false;
  wifi_sta_connecting = true;
  wifi_sta_start_ms = millis();
}

static void setup_wifi() {
  load_wifi_creds();
  if (wifi_ssid.length() > 0) {
    start_sta_mode();
  } else {
    start_ap_mode();
  }
}

static void setup_http() {
  server.on("/", HTTP_GET, handle_root);
  server.on("/api/summary", HTTP_GET, handle_summary);
  server.on("/api/config", HTTP_GET, handle_config_get);
  server.on("/api/config", HTTP_POST, handle_config_post);
  server.on("/api/config/reset", HTTP_POST, handle_config_reset);
  server.on("/config", HTTP_GET, handle_config_page);
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

  if (parse_rmc(line, &lat_deg, &lon_deg, &speed_kph, &valid_fix, &date_yyyymmdd, &time_min)) {
    has_gps_fix = valid_fix;
    last_speed_kph = speed_kph;
    last_gps_ms = millis();
    last_update_min = time_min;

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
    if (c == '\n') {
      nmea_line[nmea_len] = '\0';
      if (nmea_len > 6) {
        handle_nmea_line(nmea_line);
      }
      nmea_len = 0;
    } else if (c != '\r') {
      if (nmea_len + 1 < sizeof(nmea_line)) {
        nmea_line[nmea_len++] = c;
      } else {
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
  if (LED_UI_ENABLED) {
    led_begin();
  }
  setup_wifi();
  setup_http();
  setup_ble();
  Serial.println("Dog-RGB ESP32-S3 GPS-first base firmware");
}

void loop() {
  const unsigned long now_ms = millis();
  read_gps();

  // Periodic persistence to avoid flash wear.
  if (now_ms - last_save_ms >= SAVE_INTERVAL_MS) {
    last_save_ms = now_ms;
    save_metrics();
  }

  if (now_ms - last_heartbeat_ms >= HEARTBEAT_MS) {
    last_heartbeat_ms = now_ms;
    led_state = !led_state;
    digitalWrite(PIN_STATUS_LED, led_state ? HIGH : LOW);

    const float avg_speed_kph = (active_time_ms > 0)
                                    ? (total_distance_m / (active_time_ms / 1000.0f)) * 3.6f
                                    : 0.0f;
    // Serial log for quick field diagnostics.
    Serial.print("heartbeat | gps_fix=");
    Serial.print(has_gps_fix ? "1" : "0");
    Serial.print(" | speed_kph=");
    Serial.println(last_speed_kph, 2);

    Serial.print("distance_m=");
    Serial.print(total_distance_m, 1);
    Serial.print(" avg_kph=");
    Serial.print(avg_speed_kph, 2);
    Serial.print(" max_kph=");
    Serial.println(max_speed_kph, 2);
  }

  if (summary_char != nullptr) {
    uint8_t payload[16];
    build_summary_payload(payload, sizeof(payload));
    summary_char->setValue(payload, sizeof(payload));
  }

  if (now_ms - last_wifi_check_ms >= WIFI_RETRY_INTERVAL_MS) {
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
        WiFi.softAPdisconnect(true);
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
    if (wifi_sta_connected) {
      WiFi.mode(WIFI_AP_STA);
    } else {
      WiFi.mode(WIFI_AP);
    }
    WiFi.softAP(g_cfg.ap_ssid.c_str(), g_cfg.ap_pass.c_str());
  }

  update_led_ui();
  server.handleClient();

  // Placeholder for GPS-based LED mapping and patterns.
}
