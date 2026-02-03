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
#include <WiFi.h>

#include "ble/summary_ble.h"
#include "config/runtime_config.h"
#include "config.h"
#include "geofence/home.h"
#include "gps/gps.h"
#include "led/led_ui.h"
#include "pins.h"
#include "storage/nvs_store.h"
#include "web/portal_http.h"
#include "wifi/wifi_mgr.h"

// Heartbeat for status LED and periodic serial logs.
static const unsigned long HEARTBEAT_MS = 1000;
static const unsigned long LOG_MS = 3000;
static const unsigned long GPS_NO_DATA_MS = 3000;

static unsigned long last_heartbeat_ms = 0;
static unsigned long last_log_ms = 0;
static bool led_state = false;

static unsigned long gps_last_bytes_log = 0;
static unsigned long gps_last_sentences_log = 0;
static unsigned long gps_last_rmc_log = 0;
static unsigned long last_ok_ms = 0;

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

void setup() {
  Serial.begin(115200);
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);

  storage::begin();
  config::load();
  gps::begin();
  geofence::begin();

  if (LED_UI_ENABLED) {
    led_ui::begin();
    led_ui::start_welcome();
  }

  wifi_mgr::begin();
  portal_http::begin();
  summary_ble::begin();

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
  gps::tick();
  geofence::tick(now_ms);
  gps::save_if_due(now_ms);

  if (now_ms - last_heartbeat_ms >= HEARTBEAT_MS) {
    last_heartbeat_ms = now_ms;
    led_state = !led_state;
    digitalWrite(PIN_STATUS_LED, led_state ? HIGH : LOW);
  }

  if (now_ms - last_log_ms >= LOG_MS) {
    last_log_ms = now_ms;
    const unsigned long active_ms = gps::active_time_ms();
    const float avg_speed_kph = (active_ms > 0)
                                    ? (gps::total_distance_m() / (active_ms / 1000.0f)) * 3.6f
                                    : 0.0f;

    const unsigned long bytes_delta = gps::bytes_rx() - gps_last_bytes_log;
    const unsigned long sentences_delta = gps::sentences_rx() - gps_last_sentences_log;
    const unsigned long rmc_delta = gps::rmc_seen() - gps_last_rmc_log;
    gps_last_bytes_log = gps::bytes_rx();
    gps_last_sentences_log = gps::sentences_rx();
    gps_last_rmc_log = gps::rmc_seen();

    const unsigned long age_byte_ms = (gps::last_byte_ms() > 0) ? (now_ms - gps::last_byte_ms()) : 0;
    const unsigned long age_rmc_ms = (gps::last_rmc_ms() > 0) ? (now_ms - gps::last_rmc_ms()) : 0;
    const unsigned long age_fix_ms = (gps::last_fix_ms() > 0) ? (now_ms - gps::last_fix_ms()) : 0;
    const unsigned long age_gga_ms = (gps::last_gga_ms() > 0) ? (now_ms - gps::last_gga_ms()) : 0;
    const bool uart_active = (gps::last_byte_ms() > 0 && age_byte_ms <= GPS_NO_DATA_MS);
    const bool rmc_active = (gps::last_rmc_ms() > 0 && age_rmc_ms <= GPS_NO_DATA_MS);
    const char *gps_link = "NO_DATA";
    if (uart_active) {
      gps_link = rmc_active ? (gps::has_fix() ? "FIX" : "NO_FIX") : "NO_RMC";
    }

    Serial.print("[GPS] fix=");
    Serial.print(gps::has_fix() ? "1" : "0");
    Serial.print(" link=");
    Serial.print(gps_link);
    Serial.print(" uart_bps=");
    Serial.print(bytes_delta);
    Serial.print(" nmea=");
    Serial.print(gps::sentences_rx());
    Serial.print(" (");
    Serial.print(sentences_delta);
    Serial.print("/s)");
    Serial.print(" rmc=");
    Serial.print(gps::rmc_seen());
    Serial.print(" (");
    Serial.print(rmc_delta);
    Serial.print("/s)");
    Serial.print(" fix_ok=");
    Serial.print(gps::rmc_valid());
    Serial.print(" fixq=");
    Serial.print(gps::fix_quality());
    Serial.print(" sats=");
    Serial.print(gps::sats());
    Serial.print(" age_ms=");
    Serial.print(age_byte_ms);
    Serial.print(" fix_age_ms=");
    if (gps::last_fix_ms() > 0) {
      Serial.print(age_fix_ms);
    } else {
      Serial.print("na");
    }
    Serial.print(" overflow=");
    Serial.println(gps::overflow());

    Serial.print("[POS] lat=");
    if (gps::has_last_point()) {
      Serial.print(gps::last_lat_deg(), 6);
      Serial.print(" lon=");
      Serial.print(gps::last_lon_deg(), 6);
    } else {
      Serial.print("na lon=na");
    }
    Serial.print(" rmc_age_ms=");
    Serial.print(age_rmc_ms);
    Serial.print(" gga_age_ms=");
    Serial.println(age_gga_ms);

    Serial.print("[METRICS] dist_m=");
    Serial.print(gps::total_distance_m(), 1);
    Serial.print(" avg_kph=");
    Serial.print(avg_speed_kph, 2);
    Serial.print(" max_kph=");
    Serial.println(gps::max_speed_kph(), 2);

    const bool gps_ok = gps::has_fix();
    const bool sta_ok = (wifi_mgr::sta_connected() && WiFi.status() == WL_CONNECTED);
    const bool sta_try = (!sta_ok && wifi_mgr::ssid().length() > 0 && WiFi.getMode() == WIFI_STA);
    const bool ap_mode = (WiFi.getMode() == WIFI_AP);
    if (gps_ok || sta_ok) {
      last_ok_ms = now_ms;
    }
    const bool critical_error = (!gps_ok && !sta_ok && (now_ms - last_ok_ms) > CRITICAL_NO_OK_MS);

    const char *status_text = "GPS_SEARCH";
    if (critical_error) {
      status_text = "CRITICAL";
    } else if (!sta_ok && wifi_mgr::ssid().length() > 0 && ap_mode) {
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
        }
      }
    } else {
      if (!gps_ok) {
        body_idle = true;
      } else {
        range = led_ui::speed_range(gps::last_speed_kph());
      }
    }

    const bool has_range = (!body_idle && !home_missing);
    int effect_a = RANGE_1_EFFECT_A;
    int effect_b = RANGE_1_EFFECT_B;
    uint8_t eff_speed = RANGE_1_SPEED;
    uint8_t eff_intensity = RANGE_1_INTENSITY;
    if (has_range) {
      led_ui::get_range_config(range, effect_a, effect_b, eff_speed, eff_intensity);
    }
    const led_ui::Rgb base = led_ui::base_color_for_range(range);

    uint8_t sr = 0;
    uint8_t sg = 0;
    uint8_t sb = 0;
    float sscale = 1.0f;
    if (critical_error) {
      sscale = (now_ms / 200) % 2 ? 1.0f : 0.0f;
      sr = clamp_u8(static_cast<int>(60 * sscale));
    } else if (!sta_ok && wifi_mgr::ssid().length() > 0 && ap_mode) {
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
    Serial.print(config::mode_name(config::get().mode));
    if (config::get().mode == MODE_GEOFENCE) {
      Serial.print(" dist_m=");
      Serial.print(geofence_dist_m, 1);
      Serial.print(" fence_max=");
      Serial.print(config::get().fence_max_m);
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
    Serial.print(led_ui::effect_name(static_cast<uint8_t>(effect_a)));
    Serial.print(" effect_b=");
    Serial.print(led_ui::effect_name(static_cast<uint8_t>(effect_b)));
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

  summary_ble::tick();
  wifi_mgr::tick(now_ms);
  led_ui::tick();
  portal_http::handle_client();
}
