#include <Arduino.h>
#include <Preferences.h>
#include "pins.h"

static const unsigned long HEARTBEAT_MS = 1000;
static unsigned long last_heartbeat_ms = 0;
static bool led_state = false;

static const uint32_t GPS_BAUD = 9600;
static HardwareSerial GPS(1);
static Preferences prefs;

static char nmea_line[128];
static size_t nmea_len = 0;

static bool has_gps_fix = false;
static float last_speed_kph = 0.0f;
static unsigned long last_gps_ms = 0;

static const float SPEED_ACTIVE_KPH = 0.7f;
static const float SPEED_MAX_VALID_KPH = 40.0f;
static const unsigned long GPS_SAMPLE_MS = 1000;

static unsigned long last_sample_ms = 0;
static unsigned long active_time_ms = 0;
static float total_distance_m = 0.0f;
static float max_speed_kph = 0.0f;
static uint16_t last_update_min = 0;

static bool has_last_point = false;
static float last_lat_deg = 0.0f;
static float last_lon_deg = 0.0f;

static uint32_t current_date_yyyymmdd = 0;
static unsigned long last_save_ms = 0;
static const unsigned long SAVE_INTERVAL_MS = 60000;

static float knots_to_kph(float knots) {
  return knots * 1.852f;
}

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

static void save_metrics() {
  prefs.putUInt("date", current_date_yyyymmdd);
  prefs.putFloat("dist_m", total_distance_m);
  prefs.putULong("active_ms", active_time_ms);
  prefs.putFloat("max_kph", max_speed_kph);
  prefs.putUShort("upd_min", last_update_min);
}

static void load_metrics() {
  current_date_yyyymmdd = prefs.getUInt("date", 0);
  total_distance_m = prefs.getFloat("dist_m", 0.0f);
  active_time_ms = prefs.getULong("active_ms", 0);
  max_speed_kph = prefs.getFloat("max_kph", 0.0f);
  last_update_min = prefs.getUShort("upd_min", 0);
}

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
  GPS.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);
  prefs.begin("dogrgb", false);
  load_metrics();
  Serial.println("Dog-RGB ESP32-S3 GPS-first base firmware");
}

void loop() {
  const unsigned long now_ms = millis();
  read_gps();

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

  // Placeholder for GPS-based LED mapping and patterns.
}
