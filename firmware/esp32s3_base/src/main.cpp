#include <Arduino.h>
#include "pins.h"

static const unsigned long HEARTBEAT_MS = 1000;
static unsigned long last_heartbeat_ms = 0;
static bool led_state = false;

static const uint32_t GPS_BAUD = 9600;
static HardwareSerial GPS(1);

static char nmea_line[128];
static size_t nmea_len = 0;

static bool has_gps_fix = false;
static float last_speed_kph = 0.0f;
static unsigned long last_gps_ms = 0;

static float knots_to_kph(float knots) {
  return knots * 1.852f;
}

static bool parse_rmc_speed_kph(const char *line, float *speed_kph, bool *valid_fix) {
  if (strncmp(line, "$GPRMC,", 7) != 0 && strncmp(line, "$GNRMC,", 7) != 0) {
    return false;
  }

  // RMC fields:
  // 1 time, 2 status (A/V), 3 lat, 4 N/S, 5 lon, 6 E/W, 7 speed (knots)
  int field = 0;
  float knots = 0.0f;
  char status = 'V';

  for (const char *p = line; *p != '\0' && *p != '*'; ++p) {
    if (*p == ',') {
      field++;
      continue;
    }
    if (field == 2 && status == 'V') {
      status = *p;
    }
    if (field == 7) {
      knots = strtof(p, nullptr);
      break;
    }
  }

  *valid_fix = (status == 'A');
  *speed_kph = knots_to_kph(knots);
  return true;
}

static void handle_nmea_line(const char *line) {
  float speed_kph = 0.0f;
  bool valid_fix = false;

  if (parse_rmc_speed_kph(line, &speed_kph, &valid_fix)) {
    has_gps_fix = valid_fix;
    last_speed_kph = speed_kph;
    last_gps_ms = millis();
    Serial.print("GPS speed kph: ");
    Serial.print(last_speed_kph, 2);
    Serial.print(" fix: ");
    Serial.println(has_gps_fix ? "A" : "V");
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
  Serial.println("Dog-RGB ESP32-S3 GPS-first base firmware");
}

void loop() {
  const unsigned long now_ms = millis();
  read_gps();

  if (now_ms - last_heartbeat_ms >= HEARTBEAT_MS) {
    last_heartbeat_ms = now_ms;
    led_state = !led_state;
    digitalWrite(PIN_STATUS_LED, led_state ? HIGH : LOW);

    Serial.print("heartbeat | gps_fix=");
    Serial.print(has_gps_fix ? "1" : "0");
    Serial.print(" | speed_kph=");
    Serial.println(last_speed_kph, 2);
  }

  // Placeholder for GPS-based LED mapping and patterns.
}
