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
#include <esp_system.h>
#include <math.h>

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
static const unsigned long SYS_LOG_MS = 30000;
static const unsigned long GPS_NO_DATA_MS = 3000;

static unsigned long last_heartbeat_ms = 0;
static unsigned long last_log_ms = 0;
static unsigned long last_sys_log_ms = 0;
static bool led_state = false;

static unsigned long gps_last_bytes_log = 0;
static unsigned long gps_last_sentences_log = 0;
static unsigned long gps_last_rmc_log = 0;
static unsigned long gps_last_gga_log = 0;
static unsigned long gps_last_checksum_fail_log = 0;
static unsigned long gps_last_parse_fail_log = 0;
static unsigned long gps_last_rmc_parse_fail_log = 0;
static unsigned long gps_last_gga_parse_fail_log = 0;
static unsigned long gps_last_speed_spike_log = 0;
static unsigned long gps_last_stale_log = 0;
static unsigned long loop_sum_us = 0;
static unsigned long loop_max_us = 0;
static unsigned long loop_count = 0;

static const char *wifi_mode_name(wifi_mode_t mode) {
  switch (mode) {
    case WIFI_OFF:
      return "OFF";
    case WIFI_STA:
      return "STA";
    case WIFI_AP:
      return "AP";
    case WIFI_AP_STA:
      return "AP_STA";
    default:
      return "UNKNOWN";
  }
}

static long age_ms_or_neg1(unsigned long now_ms, unsigned long last_ms) {
  if (last_ms == 0 || now_ms < last_ms) {
    return -1;
  }
  return static_cast<long>(now_ms - last_ms);
}

static const char *gps_fix_reason(unsigned long now_ms) {
  const RuntimeConfig &cfg = config::get();
  if (gps::trusted_fix()) {
    return "ok";
  }
  if (gps::last_rmc_ms() == 0) {
    return "no_rmc";
  }
  if (now_ms >= gps::last_rmc_ms() && now_ms - gps::last_rmc_ms() > GPS_NO_DATA_MS) {
    return "rmc_stale";
  }
  if (!gps::raw_fix()) {
    return "rmc_v";
  }
  if (gps::last_gga_ms() == 0) {
    return "no_gga";
  }
  if (now_ms >= gps::last_gga_ms() && now_ms - gps::last_gga_ms() > cfg.gps_max_gga_age_ms) {
    return "gga_stale";
  }
  if (gps::fix_quality() < cfg.gps_min_fix_quality) {
    return "fix_quality";
  }
  if (gps::sats() < cfg.gps_min_sats) {
    return "sats";
  }
  const float hdop = gps::hdop();
  if (isnan(hdop) || hdop <= 0.0f || hdop > cfg.gps_max_hdop) {
    return "hdop";
  }
  return "quality";
}

static void emit_periodic_logs(unsigned long now_ms) {
  const unsigned long bytes_delta = gps::bytes_rx() - gps_last_bytes_log;
  const unsigned long sentences_delta = gps::sentences_rx() - gps_last_sentences_log;
  const unsigned long rmc_delta = gps::rmc_seen() - gps_last_rmc_log;
  const unsigned long gga_delta = gps::gga_seen() - gps_last_gga_log;
  const unsigned long checksum_delta = gps::checksum_fail() - gps_last_checksum_fail_log;
  const unsigned long parse_delta = gps::parse_fail() - gps_last_parse_fail_log;
  const unsigned long rmc_parse_delta = gps::rmc_parse_fail() - gps_last_rmc_parse_fail_log;
  const unsigned long gga_parse_delta = gps::gga_parse_fail() - gps_last_gga_parse_fail_log;
  const unsigned long speed_spike_delta = gps::speed_spike() - gps_last_speed_spike_log;
  const unsigned long stale_delta = gps::stale_count() - gps_last_stale_log;
  gps_last_bytes_log = gps::bytes_rx();
  gps_last_sentences_log = gps::sentences_rx();
  gps_last_rmc_log = gps::rmc_seen();
  gps_last_gga_log = gps::gga_seen();
  gps_last_checksum_fail_log = gps::checksum_fail();
  gps_last_parse_fail_log = gps::parse_fail();
  gps_last_rmc_parse_fail_log = gps::rmc_parse_fail();
  gps_last_gga_parse_fail_log = gps::gga_parse_fail();
  gps_last_speed_spike_log = gps::speed_spike();
  gps_last_stale_log = gps::stale_count();

  const long age_byte_ms = age_ms_or_neg1(now_ms, gps::last_byte_ms());
  const long age_rmc_ms = age_ms_or_neg1(now_ms, gps::last_rmc_ms());
  const long age_gga_ms = age_ms_or_neg1(now_ms, gps::last_gga_ms());
  const long age_fix_ms = age_ms_or_neg1(now_ms, gps::last_fix_ms());
  const bool uart_active = (age_byte_ms >= 0 && age_byte_ms <= static_cast<long>(GPS_NO_DATA_MS));
  const bool gps_ok = gps::has_fix();
  const bool sta_ok = (wifi_mgr::sta_connected() && WiFi.status() == WL_CONNECTED);
  const bool sta_try = (!sta_ok && wifi_mgr::sta_connecting());

  Serial.print("[GPS_LINK] uart=");
  Serial.print(uart_active ? "1" : "0");
  Serial.print(" bytes_delta=");
  Serial.print(bytes_delta);
  Serial.print(" nmea_delta=");
  Serial.print(sentences_delta);
  Serial.print(" rmc_delta=");
  Serial.print(rmc_delta);
  Serial.print(" gga_delta=");
  Serial.print(gga_delta);
  Serial.print(" checksum_fail_delta=");
  Serial.print(checksum_delta);
  Serial.print(" parse_fail_delta=");
  Serial.print(parse_delta);
  Serial.print(" rmc_parse_fail_delta=");
  Serial.print(rmc_parse_delta);
  Serial.print(" gga_parse_fail_delta=");
  Serial.print(gga_parse_delta);
  Serial.print(" overflow=");
  Serial.print(gps::overflow());
  Serial.print(" stale_delta=");
  Serial.print(stale_delta);
  Serial.print(" age_byte_ms=");
  Serial.print(age_byte_ms);
  Serial.print(" age_rmc_ms=");
  Serial.print(age_rmc_ms);
  Serial.print(" age_gga_ms=");
  Serial.println(age_gga_ms);

  Serial.print("[GPS_FIX] raw=");
  Serial.print(gps::raw_fix() ? "1" : "0");
  Serial.print(" trusted=");
  Serial.print(gps::trusted_fix() ? "1" : "0");
  Serial.print(" current=");
  Serial.print(gps::has_current_fix() ? "1" : "0");
  Serial.print(" reason=");
  Serial.print(gps_fix_reason(now_ms));
  Serial.print(" fixq=");
  Serial.print(gps::fix_quality());
  Serial.print(" sats=");
  Serial.print(gps::sats());
  Serial.print(" hdop=");
  const float hdop = gps::hdop();
  Serial.print(isnan(hdop) ? -1.0f : hdop, 2);
  Serial.print(" fix_age_ms=");
  Serial.print(age_fix_ms);
  Serial.print(" lat=");
  Serial.print(gps::has_current_fix() ? gps::current_lat_deg() : 0.0f, 6);
  Serial.print(" lon=");
  Serial.println(gps::has_current_fix() ? gps::current_lon_deg() : 0.0f, 6);

  const unsigned long active_ms = gps::active_time_ms();
  const float avg_speed_kph = (active_ms > 0)
                                  ? (gps::total_distance_m() / (active_ms / 1000.0f)) * 3.6f
                                  : 0.0f;
  const bool active_sample = gps::last_speed_kph() > SPEED_ACTIVE_KPH;
  uint8_t range = 1;
  bool body_idle = false;
  bool home_missing = false;
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
  } else if (!gps_ok) {
    body_idle = true;
  } else {
    range = led_ui::speed_range(gps::last_speed_kph());
  }

  Serial.print("[MOTION] mode=");
  Serial.print(config::mode_name(config::get().mode));
  Serial.print(" speed_kph=");
  Serial.print(gps::last_speed_kph(), 2);
  Serial.print(" usable=");
  Serial.print((gps_ok && gps::last_speed_kph() <= SPEED_MAX_VALID_KPH) ? "1" : "0");
  Serial.print(" active=");
  Serial.print(active_sample ? "1" : "0");
  Serial.print(" range=");
  Serial.print(range);
  Serial.print(" dist_m=");
  Serial.print(gps::total_distance_m(), 1);
  Serial.print(" active_s=");
  Serial.print(active_ms / 1000);
  Serial.print(" avg_kph=");
  Serial.print(avg_speed_kph, 2);
  Serial.print(" max_kph=");
  Serial.print(gps::max_speed_kph(), 2);
  Serial.print(" seg_m=");
  Serial.print(gps::last_segment_m(), 2);
  Serial.print(" seg_ok=");
  Serial.print(gps::last_segment_accepted() ? "1" : "0");
  Serial.print(" seg_reason=");
  Serial.println(gps::last_segment_reject_reason());

  Serial.print("[WIFI] mode=");
  Serial.print(wifi_mode_name(WiFi.getMode()));
  Serial.print(" sta=");
  Serial.print(sta_ok ? "1" : "0");
  Serial.print(" sta_try=");
  Serial.print(sta_try ? "1" : "0");
  Serial.print(" ap=");
  Serial.print(wifi_mgr::ap_enabled() ? "1" : "0");
  Serial.print(" clients=");
  Serial.print(wifi_mgr::ap_station_count());
  Serial.print(" wifi_off=");
  Serial.print(wifi_mgr::wifi_off() ? "1" : "0");
  Serial.print(" ssid_set=");
  Serial.print(wifi_mgr::ssid().length() > 0 ? "1" : "0");
  Serial.print(" rssi=");
  Serial.print(sta_ok ? WiFi.RSSI() : 0);
  Serial.print(" ap_ip=");
  Serial.print(WiFi.softAPIP().toString());
  Serial.print(" sta_ip=");
  Serial.print(sta_ok ? WiFi.localIP().toString() : String("0.0.0.0"));
  Serial.print(" ap_ch=");
  Serial.println(wifi_mgr::diagnostics().current_ap_channel);

  const bool has_range = (!body_idle && !home_missing);
  int effect_a = RANGE_1_EFFECT_A;
  int effect_b = RANGE_1_EFFECT_B;
  uint8_t eff_speed = RANGE_1_SPEED;
  uint8_t eff_intensity = RANGE_1_INTENSITY;
  if (has_range) {
    led_ui::get_range_config(range, effect_a, effect_b, eff_speed, eff_intensity);
  }
  Serial.print("[LED] mode=");
  Serial.print(config::mode_name(config::get().mode));
  Serial.print(" body_on=");
  Serial.print(has_range ? "1" : "0");
  Serial.print(" home_missing=");
  Serial.print(home_missing ? "1" : "0");
  Serial.print(" geofence_dist_m=");
  Serial.print(geofence_dist_m, 1);
  Serial.print(" range=");
  Serial.print(range);
  Serial.print(" effect_a=");
  Serial.print(led_ui::effect_name(static_cast<uint8_t>(effect_a)));
  Serial.print(" effect_b=");
  Serial.print(led_ui::effect_name(static_cast<uint8_t>(effect_b)));
  Serial.print(" speed=");
  Serial.print(eff_speed);
  Serial.print(" intensity=");
  Serial.println(eff_intensity);

  if (now_ms - last_sys_log_ms >= SYS_LOG_MS) {
    last_sys_log_ms = now_ms;
    const unsigned long avg_loop_us = (loop_count > 0) ? (loop_sum_us / loop_count) : 0;
    Serial.print("[SYS] uptime_s=");
    Serial.print(now_ms / 1000);
    Serial.print(" heap=");
    Serial.print(ESP.getFreeHeap());
    Serial.print(" min_heap=");
    Serial.print(ESP.getMinFreeHeap());
    Serial.print(" loop_avg_us=");
    Serial.print(avg_loop_us);
    Serial.print(" loop_max_us=");
    Serial.print(loop_max_us);
    Serial.print(" mode=");
    Serial.print(config::mode_name(config::get().mode));
    Serial.print(" brightness=");
    Serial.print(config::get().brightness);
    Serial.print(" date=");
    Serial.println(gps::current_date());
    loop_sum_us = 0;
    loop_max_us = 0;
    loop_count = 0;
    const wifi_mgr::WifiDiagnostics &wd = wifi_mgr::diagnostics();
    const long ap_hold_s = (wifi_mgr::ap_enabled() && wd.ap_hold_until_ms > now_ms)
                               ? static_cast<long>((wd.ap_hold_until_ms - now_ms) / 1000) : -1;
    const long retry_s = (!sta_ok && wd.next_sta_retry_ms > now_ms)
                             ? static_cast<long>((wd.next_sta_retry_ms - now_ms) / 1000) : -1;
    Serial.print("[WIFI_DIAG] ap_start=");
    Serial.print(wd.ap_start_count);
    Serial.print(" ap_fail=");
    Serial.print(wd.ap_start_fail_count);
    Serial.print(" ap_stop=");
    Serial.print(wd.ap_stop_count);
    Serial.print(" ap_restart=");
    Serial.print(wd.ap_restart_count);
    Serial.print(" sta_retry=");
    Serial.print(wd.sta_retry_count);
    Serial.print(" sta_fail=");
    Serial.print(wd.sta_connect_fail_count);
    Serial.print(" sta_got_ip=");
    Serial.print(wd.sta_got_ip_count);
    Serial.print(" sta_disc=");
    Serial.print(wd.sta_disconnect_count);
    Serial.print(" ap_conn=");
    Serial.print(wd.ap_station_connect_count);
    Serial.print(" ap_disc=");
    Serial.print(wd.ap_station_disconnect_count);
    Serial.print(" ap_hold_s=");
    Serial.print(ap_hold_s);
    Serial.print(" retry_s=");
    Serial.print(retry_s);
    Serial.print(" last_ap_rsn=");
    Serial.print(wd.last_ap_reason);
    Serial.print(" last_sta_rsn=");
    Serial.println(wd.last_sta_reason);
  }
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

  // BLE must be initialized BEFORE WiFi when enabled so the coexistence module
  // is configured before the WiFi stack starts. If BLE initializes after the AP
  // is already running, BLEDevice::init() resets the RF scheduler and causes
  // beacon starvation, making the SSID invisible on phones.
  if (BLE_ENABLED) {
    summary_ble::begin();
  }
  wifi_mgr::begin();
  portal_http::begin();

  Serial.println("Dog-RGB ESP32-S3 GPS-first base firmware");

  const esp_reset_reason_t rr = esp_reset_reason();
  const char *rr_names[] = {
    "UNKNOWN", "POWERON", "EXT_PIN", "SW", "PANIC",
    "INT_WDT", "TASK_WDT", "WDT", "DEEPSLEEP", "BROWNOUT", "SDIO"
  };
  const char *rr_str = (rr <= 10) ? rr_names[rr] : "?";
  Serial.print("[BOOT] reset_reason=");
  Serial.print(rr_str);
  Serial.print(" (");
  Serial.print(rr);
  Serial.println(")");
  if (rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT) {
    Serial.println("[BOOT] WARNING: last reset was a CRASH or WATCHDOG!");
  }
  if (rr == ESP_RST_BROWNOUT) {
    Serial.println("[BOOT] WARNING: last reset was a BROWNOUT (power supply issue)!");
  }

  Serial.print("[BOOT] ble_enabled=");
  Serial.println(BLE_ENABLED ? "1" : "0");
  Serial.print("GPS UART1: baud=");
  Serial.print(GPS_BAUD);
  Serial.print(" rx_pin=");
  Serial.print(PIN_GPS_RX);
  Serial.print(" tx_pin=");
  Serial.println(PIN_GPS_TX);
  Serial.println("GPS status: waiting for NMEA data...");
}

void loop() {
  const unsigned long loop_start_us = micros();
  const unsigned long now_ms = millis();
  gps::tick();
  geofence::tick(now_ms);
  gps::save_if_due(now_ms);
  gps::track_tick(now_ms);

  if (now_ms - last_heartbeat_ms >= HEARTBEAT_MS) {
    last_heartbeat_ms = now_ms;
    led_state = !led_state;
    digitalWrite(PIN_STATUS_LED, led_state ? HIGH : LOW);
  }

  if (now_ms - last_log_ms >= LOG_MS) {
    last_log_ms = now_ms;
    emit_periodic_logs(now_ms);
  }

  if (BLE_ENABLED) {
    summary_ble::tick();
  }
  wifi_mgr::tick(now_ms);
  led_ui::tick();
  portal_http::handle_client();

  const unsigned long loop_elapsed_us = micros() - loop_start_us;
  loop_sum_us += loop_elapsed_us;
  loop_count++;
  if (loop_elapsed_us > loop_max_us) {
    loop_max_us = loop_elapsed_us;
  }
}
