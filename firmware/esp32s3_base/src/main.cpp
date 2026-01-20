#include <Arduino.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "pins.h"

// Heartbeat for status LED and periodic serial logs.
static const unsigned long HEARTBEAT_MS = 1000;
static unsigned long last_heartbeat_ms = 0;
static bool led_state = false;

// GPS UART settings.
static const uint32_t GPS_BAUD = 9600;
static HardwareSerial GPS(1);
static Preferences prefs;
static BLECharacteristic *summary_char = nullptr;
static WebServer server(80);

// NMEA line buffer for incoming GPS sentences.
static char nmea_line[128];
static size_t nmea_len = 0;

// Latest GPS state.
static bool has_gps_fix = false;
static float last_speed_kph = 0.0f;
static unsigned long last_gps_ms = 0;

// Behavior thresholds and sampling.
static const float SPEED_ACTIVE_KPH = 0.7f;
static const float SPEED_MAX_VALID_KPH = 40.0f;
static const unsigned long GPS_SAMPLE_MS = 1000;

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
static const unsigned long SAVE_INTERVAL_MS = 60000;

// BLE identifiers for the daily summary.
static const char *BLE_DEVICE_NAME = "Dog-Collar";
static const char *BLE_SERVICE_UUID = "8b4c0001-6c1d-4f3c-a5b0-1e0c5a00a101";
static const char *BLE_CHAR_UUID = "8b4c0002-6c1d-4f3c-a5b0-1e0c5a00a101";

// Wi-Fi settings (AP + STA). Change here to update default credentials.
static const char *AP_SSID = "dog";
static const char *AP_PASS = "dog";
static const char *MDNS_NAME = "dog-collar";
static const unsigned long STA_CONNECT_TIMEOUT_MS = 10000;

static String wifi_ssid;
static String wifi_pass;
static bool wifi_sta_connected = false;

static float knots_to_kph(float knots) {
  return knots * 1.852f;
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
  char ns = 'N';
  char ew = 'E';
  char date_buf[8] = {0};
  int time_len = 0;
  int lat_len = 0;
  int lon_len = 0;
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
    if (field == 7) {
      knots = strtof(p, nullptr);
    }
    if (field == 9 && date_len < 6) {
      date_buf[date_len++] = *p;
    }
  }

  *valid_fix = (status == 'A');
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
      "<p><a href='/wifi'>Configurar Wi-Fi</a></p>"
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

static void handle_root() {
  server.send(200, "text/html", html_page());
}

static void handle_wifi_page() {
  server.send(200, "text/html", html_wifi_page());
}

static void handle_summary() {
  server.send(200, "application/json", build_summary_json());
}

static void handle_wifi_save() {
  if (!server.hasArg("ssid")) {
    server.send(400, "text/plain", "missing ssid");
    return;
  }
  const String ssid = server.arg("ssid");
  const String pass = server.arg("pass");
  save_wifi_creds(ssid, pass);
  server.send(200, "text/plain", "saved");
}

static void start_ap_mode() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  wifi_sta_connected = false;
}

static void start_sta_mode() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
  const unsigned long start_ms = millis();
  while (WiFi.status() != WL_CONNECTED &&
         (millis() - start_ms) < STA_CONNECT_TIMEOUT_MS) {
    delay(200);
  }
  wifi_sta_connected = (WiFi.status() == WL_CONNECTED);
  if (wifi_sta_connected) {
    MDNS.begin(MDNS_NAME);
  }
}

static void setup_wifi() {
  load_wifi_creds();
  if (wifi_ssid.length() > 0) {
    start_sta_mode();
  }
  if (!wifi_sta_connected) {
    start_ap_mode();
  }
}

static void setup_http() {
  server.on("/", HTTP_GET, handle_root);
  server.on("/api/summary", HTTP_GET, handle_summary);
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
    if (time_min > 0) {
      last_update_min = time_min;
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
  load_metrics();
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

  server.handleClient();

  // Placeholder for GPS-based LED mapping and patterns.
}
