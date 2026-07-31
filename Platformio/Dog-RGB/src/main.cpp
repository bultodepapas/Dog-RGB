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
#include "power/day_mode.h"
#include "sim/wokwi_control.h"
#include "storage/nvs_store.h"
#include "web/portal_http.h"
#include "wifi/wifi_mgr.h"

// Heartbeat for status LED and periodic serial logs.
static const unsigned long HEARTBEAT_MS = 1000;
#if defined(DOG_RGB_WOKWI_SIM)
// One detail category is formatted per tick. Five rotating slots preserve a
// two-second/category cadence without building the whole report in one loop.
static const unsigned long LOG_MS = 400;
static const unsigned long SYS_LOG_MS = 10000;
#else
// Five rotating slots preserve the original three-second/category cadence.
static const unsigned long LOG_MS = 600;
static const unsigned long SYS_LOG_MS = 30000;
#endif
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
static unsigned long loop_work_sum_us = 0;
static unsigned long loop_work_max_us = 0;
static unsigned long log_emit_max_us = 0;
static unsigned long log_drain_max_us = 0;
static unsigned long log_slot_max_us[7] = {};
static uint8_t last_log_slot = 0xFF;
static unsigned long loop_count = 0;

struct LoopPhaseMax {
  unsigned long gps_us = 0;
  unsigned long control_us = 0;
  unsigned long geofence_us = 0;
  unsigned long storage_us = 0;
  unsigned long radio_us = 0;
  unsigned long led_us = 0;
  unsigned long http_us = 0;
};

static LoopPhaseMax loop_phase_max;

// Periodic diagnostics are useful but must never become part of the real-time
// path. Queue them in fixed storage, then drain only bytes the UART explicitly
// says it can accept. If a future report outgrows the queue, data is discarded
// and counted instead of blocking GPS, LEDs, WiFi, or HTTP.
class SerialLogQueue : public Print {
 public:
  size_t write(uint8_t value) override {
    return write(&value, 1);
  }

  size_t write(const uint8_t *data, size_t length) override {
    if (data == nullptr) {
      return 0;
    }

    size_t accepted = 0;
    while (accepted < length && used_ < sizeof(buffer_)) {
      const size_t tail = (head_ + used_) % sizeof(buffer_);
      buffer_[tail] = data[accepted++];
      ++used_;
    }
    dropped_bytes_ += static_cast<unsigned long>(length - accepted);
    return accepted;
  }

  template <typename SerialSink>
  size_t drain(SerialSink &sink) {
    const int available = sink.availableForWrite();
    if (available <= 0 || used_ == 0) {
      return 0;
    }

    size_t budget = static_cast<size_t>(available);
    if (budget > MAX_DRAIN_BYTES_PER_TICK) {
      budget = MAX_DRAIN_BYTES_PER_TICK;
    }
    if (budget > used_) {
      budget = used_;
    }

    size_t drained = 0;
    while (drained < budget) {
      size_t chunk = sizeof(buffer_) - head_;
      if (chunk > budget - drained) {
        chunk = budget - drained;
      }
      const size_t written = sink.write(buffer_ + head_, chunk);
      if (written == 0) {
        break;
      }
      head_ = (head_ + written) % sizeof(buffer_);
      used_ -= written;
      drained += written;
      if (written < chunk) {
        break;
      }
    }
    return drained;
  }

  size_t pending() const { return used_; }
  unsigned long dropped_bytes() const { return dropped_bytes_; }

 private:
  static constexpr size_t MAX_DRAIN_BYTES_PER_TICK = 64;
  uint8_t buffer_[4096] = {};
  size_t head_ = 0;
  size_t used_ = 0;
  unsigned long dropped_bytes_ = 0;
};

// Coalesce the many small Print calls that build a line before copying it into
// the queue. Oversized lines stream in bounded chunks without truncation.
class PeriodicLogWriter : public Print {
 public:
  explicit PeriodicLogWriter(Print &sink) : sink_(sink) {}
  ~PeriodicLogWriter() { flush_chunk(); }

  size_t write(uint8_t value) override {
    return write(&value, 1);
  }

  size_t write(const uint8_t *data, size_t length) override {
    if (data == nullptr) {
      return 0;
    }

    for (size_t i = 0; i < length; ++i) {
      if (used_ == sizeof(buffer_)) {
        flush_chunk();
      }
      buffer_[used_++] = data[i];
      if (data[i] == '\n') {
        flush_chunk();
      }
    }
    return length;
  }

 private:
  void flush_chunk() {
    if (used_ == 0) {
      return;
    }
    sink_.write(buffer_, used_);
    used_ = 0;
  }

  Print &sink_;
  uint8_t buffer_[512] = {};
  size_t used_ = 0;
};

static SerialLogQueue serial_log_queue;

static void record_phase_max(unsigned long &current_max, unsigned long elapsed_us) {
  if (elapsed_us > current_max) {
    current_max = elapsed_us;
  }
}

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
  if (last_ms == 0) {
    return -1;
  }
  // gps::tick() runs after the loop timestamp is captured, so a byte received
  // in this same iteration can be a few milliseconds "in the future" relative
  // to now_ms. Its real age is zero, not unavailable.
  if (now_ms < last_ms) {
    return 0;
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
  PeriodicLogWriter periodic_log(serial_log_queue);
#define Serial periodic_log
  static uint8_t next_detail_slot = 0;
  static bool wifi_diag_pending = false;
  const bool sys_due = (now_ms - last_sys_log_ms >= SYS_LOG_MS);
  uint8_t detail_slot = 0xFF;
  if (wifi_diag_pending) {
    detail_slot = 6;
    wifi_diag_pending = false;
  } else if (sys_due) {
    detail_slot = 5;
    wifi_diag_pending = true;
  } else {
    detail_slot = next_detail_slot;
    next_detail_slot = static_cast<uint8_t>((next_detail_slot + 1) % 5);
  }
  last_log_slot = detail_slot;

  const bool gps_ok = gps::has_fix();
  const bool sta_ok = (wifi_mgr::sta_connected() && WiFi.status() == WL_CONNECTED);
  const bool sta_try = (!sta_ok && wifi_mgr::sta_connecting());

  if (detail_slot == 0) {
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
  const bool uart_active = (age_byte_ms >= 0 && age_byte_ms <= static_cast<long>(GPS_NO_DATA_MS));
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
  Serial.print(" speed_spike_delta=");
  Serial.print(speed_spike_delta);
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

  if (checksum_delta > 0 || parse_delta > 0 || speed_spike_delta > 0 ||
      stale_delta > 0 || gps::overflow() > 0) {
    Serial.print("[GPS_ANOMALY] checksum_seen=");
    Serial.print(checksum_delta > 0 ? "1" : "0");
    Serial.print(" parse_seen=");
    Serial.print(parse_delta > 0 ? "1" : "0");
    Serial.print(" rmc_parse_seen=");
    Serial.print(rmc_parse_delta > 0 ? "1" : "0");
    Serial.print(" gga_parse_seen=");
    Serial.print(gga_parse_delta > 0 ? "1" : "0");
    Serial.print(" speed_spike_seen=");
    Serial.print(speed_spike_delta > 0 ? "1" : "0");
    Serial.print(" stale_seen=");
    Serial.print(stale_delta > 0 ? "1" : "0");
    Serial.print(" overflow=");
    Serial.println(gps::overflow());
  }
  }

  if (detail_slot == 1) {
  const long age_fix_ms = age_ms_or_neg1(now_ms, gps::last_fix_ms());
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
  }

  const unsigned long active_ms = gps::active_time_ms();
  const float avg_speed_kph = (active_ms > 0)
                                  ? (gps::total_distance_m() / (active_ms / 1000.0f)) * 3.6f
                                  : 0.0f;
  const bool active_sample = gps::last_speed_kph() > SPEED_ACTIVE_KPH;
  const uint8_t mode = config::get().mode;
  uint8_t range = 0;
  bool body_idle = false;
  bool home_missing = false;
  float geofence_dist_m = -1.0f;
  if (mode == MODE_GEOFENCE) {
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
  } else if (mode == MODE_SPEED) {
    if (!gps_ok) {
      body_idle = true;
    } else {
      range = led_ui::speed_range(gps::last_speed_kph());
    }
  }

  if (detail_slot == 2) {
  Serial.print("[MOTION] mode=");
  Serial.print(config::mode_name(config::get().mode));
  Serial.print(" speed_kph=");
  Serial.print(gps::last_speed_kph(), 2);
  Serial.print(" usable=");
  Serial.print(gps::speed_usable() ? "1" : "0");
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
  Serial.print(gps::last_segment_reject_reason());
  Serial.print(" small_seg_total=");
  Serial.print(gps::small_segment_rejects());
  Serial.print(" large_seg_total=");
  Serial.println(gps::large_segment_rejects());
  }

  if (detail_slot == 3) {
  Serial.print("[WIFI] mode=");
  Serial.print(wifi_mode_name(static_cast<wifi_mode_t>(wifi_mgr::mode())));
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
  Serial.print(wifi_mgr::ap_ip().toString());
  Serial.print(" sta_ip=");
  Serial.print(sta_ok ? WiFi.localIP().toString() : String("0.0.0.0"));
  Serial.print(" ap_ch=");
  Serial.println(wifi_mgr::diagnostics().current_ap_channel);
  }

  const bool has_range = (range >= 1 && range <= 10 && !body_idle && !home_missing);
  const bool day_active = day_mode::active_now();
  bool body_on = has_range;
  const char *render = has_range ? "range" : (home_missing ? "home_missing" : "idle");
  int effect_a = RANGE_1_EFFECT_A;
  int effect_b = RANGE_1_EFFECT_B;
  uint8_t eff_speed = RANGE_1_SPEED;
  uint8_t eff_intensity = RANGE_1_INTENSITY;
  if (day_active) {
    body_on = false;
    render = "day_status";
  } else if (mode == MODE_SHOW) {
    body_on = true;
    render = "show";
    effect_a = led_ui::current_show_effect();
    effect_b = effect_a;
    eff_speed = SHOW_SPEED;
    eff_intensity = SHOW_INTENSITY;
  } else if (mode == MODE_SIMPLE) {
    body_on = true;
    render = "simple";
    effect_a = config::get().single.effect_id;
    effect_b = effect_a;
    eff_speed = config::get().single.speed;
    eff_intensity = config::get().single.intensity;
  } else if (has_range) {
    led_ui::get_range_config(range, effect_a, effect_b, eff_speed, eff_intensity);
  }
  if (detail_slot == 4) {
  Serial.print("[LED] mode=");
  Serial.print(config::mode_name(config::get().mode));
  Serial.print(" body_on=");
  Serial.print(body_on ? "1" : "0");
  Serial.print(" render=");
  Serial.print(render);
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
  Serial.print(eff_intensity);
  Serial.print(" day_mode=");
  Serial.print(day_mode::state_name());
  Serial.print(" local_min=");
  Serial.println(day_mode::time_available() ? static_cast<int>(day_mode::local_min()) : -1);
  }

  if (detail_slot == 5) {
    last_sys_log_ms = now_ms;
    const unsigned long avg_loop_us = (loop_count > 0) ? (loop_sum_us / loop_count) : 0;
    const unsigned long avg_work_us = (loop_count > 0) ? (loop_work_sum_us / loop_count) : 0;
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
    Serial.print(" loop_work_avg_us=");
    Serial.print(avg_work_us);
    Serial.print(" loop_work_max_us=");
    Serial.print(loop_work_max_us);
    Serial.print(" log_emit_max_us=");
    Serial.print(log_emit_max_us);
    Serial.print(" log_drain_max_us=");
    Serial.print(log_drain_max_us);
    Serial.print(" log_queue_pending=");
    Serial.print(serial_log_queue.pending());
    Serial.print(" log_drop_bytes=");
    Serial.print(serial_log_queue.dropped_bytes());
    for (uint8_t slot = 0; slot < 7; ++slot) {
      Serial.print(" log_slot");
      Serial.print(slot);
      Serial.print("_max_us=");
      Serial.print(log_slot_max_us[slot]);
    }
    Serial.print(" gps_max_us=");
    Serial.print(loop_phase_max.gps_us);
    Serial.print(" control_max_us=");
    Serial.print(loop_phase_max.control_us);
    Serial.print(" geofence_max_us=");
    Serial.print(loop_phase_max.geofence_us);
    Serial.print(" storage_max_us=");
    Serial.print(loop_phase_max.storage_us);
    Serial.print(" radio_max_us=");
    Serial.print(loop_phase_max.radio_us);
    Serial.print(" led_max_us=");
    Serial.print(loop_phase_max.led_us);
    Serial.print(" http_max_us=");
    Serial.print(loop_phase_max.http_us);
    Serial.print(" mode=");
    Serial.print(config::mode_name(config::get().mode));
    Serial.print(" brightness=");
    Serial.print(config::get().brightness);
    Serial.print(" date=");
    Serial.print(gps::current_date());
    Serial.print(" day_mode=");
    Serial.print(day_mode::state_name());
    Serial.print(" local_min=");
    Serial.println(day_mode::time_available() ? static_cast<int>(day_mode::local_min()) : -1);
    loop_sum_us = 0;
    loop_max_us = 0;
    loop_work_sum_us = 0;
    loop_work_max_us = 0;
    log_emit_max_us = 0;
    log_drain_max_us = 0;
    for (uint8_t slot = 0; slot < 7; ++slot) {
      log_slot_max_us[slot] = 0;
    }
    loop_phase_max = LoopPhaseMax{};
    loop_count = 0;
  }

  if (detail_slot == 6) {
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
    Serial.print(wd.last_sta_reason);
    Serial.print(" ap_poll_max_us=");
    Serial.print(wd.ap_station_poll_max_us);
    Serial.print(" channel_query_max_us=");
    Serial.println(wd.channel_query_max_us);
  }
#undef Serial
}

void setup() {
#if defined(DOG_RGB_WOKWI_SIM)
  Serial.begin(CONSOLE_BAUD, SERIAL_8N1, PIN_WOKWI_SERIAL_RX, PIN_WOKWI_SERIAL_TX);
#else
  Serial.begin(CONSOLE_BAUD);
#endif
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);

  storage::begin();
  config::load();

  if (DEBUG_AP_ONLY_MINIMAL) {
    wifi_mgr::begin();
    portal_http::begin();
    Serial.println("Dog-RGB DEBUG_AP_ONLY_MINIMAL: AP + portal only");
    const esp_reset_reason_t rr = esp_reset_reason();
    Serial.print("[BOOT] reset_reason=");
    Serial.println(static_cast<int>(rr));
    Serial.print("[BOOT] ap_only=1 led=0 gps=0 ble=0 sta=0 brightness_debug=");
    Serial.println(LED_DEBUG_BRIGHTNESS_ENABLED ? LED_DEBUG_BRIGHTNESS : config::get().brightness);
    return;
  }

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
    // Give the BT/WiFi coexistence RF scheduler time to stabilize before the
    // WiFi stack claims the radio. Without this margin, BLEDevice::init() and
    // wifi_mgr::begin() can race, causing beacon starvation on boot.
    delay(WIFI_BLE_COEX_MS);
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
  wokwi_control::begin();
}

void loop() {
  const unsigned long loop_start_us = micros();
  unsigned long log_elapsed_us = 0;
  unsigned long log_drain_elapsed_us = 0;
  const unsigned long now_ms = millis();

  if (DEBUG_AP_ONLY_MINIMAL) {
    if (now_ms - last_heartbeat_ms >= HEARTBEAT_MS) {
      last_heartbeat_ms = now_ms;
      led_state = !led_state;
      digitalWrite(PIN_STATUS_LED, led_state ? HIGH : LOW);
    }
    if (now_ms - last_log_ms >= LOG_MS) {
      last_log_ms = now_ms;
      const wifi_mgr::WifiDiagnostics &wd = wifi_mgr::diagnostics();
      Serial.print("[DEBUG_AP_ONLY] uptime_s=");
      Serial.print(now_ms / 1000);
      Serial.print(" mode=");
      Serial.print(wifi_mode_name(static_cast<wifi_mode_t>(wifi_mgr::mode())));
      Serial.print(" ap=");
      Serial.print(wifi_mgr::ap_enabled() ? "1" : "0");
      Serial.print(" clients=");
      Serial.print(wifi_mgr::ap_station_count());
      Serial.print(" ap_start=");
      Serial.print(wd.ap_start_count);
      Serial.print(" ap_fail=");
      Serial.print(wd.ap_start_fail_count);
      Serial.print(" ap_stop=");
      Serial.print(wd.ap_stop_count);
      Serial.print(" ap_restart=");
      Serial.print(wd.ap_restart_count);
      Serial.print(" ip=");
      Serial.println(wifi_mgr::ap_ip().toString());
    }
    wifi_mgr::tick(now_ms);
    portal_http::handle_client();
    return;
  }

  unsigned long phase_start_us = micros();
  gps::tick();
  record_phase_max(loop_phase_max.gps_us, micros() - phase_start_us);

  phase_start_us = micros();
  wokwi_control::tick();
  record_phase_max(loop_phase_max.control_us, micros() - phase_start_us);

  phase_start_us = micros();
  geofence::tick(now_ms);
  record_phase_max(loop_phase_max.geofence_us, micros() - phase_start_us);

  phase_start_us = micros();
  gps::save_if_due(now_ms);
  gps::track_tick(now_ms);
  record_phase_max(loop_phase_max.storage_us, micros() - phase_start_us);

  if (now_ms - last_heartbeat_ms >= HEARTBEAT_MS) {
    last_heartbeat_ms = now_ms;
    led_state = !led_state;
    digitalWrite(PIN_STATUS_LED, led_state ? HIGH : LOW);
  }

  if (now_ms - last_log_ms >= LOG_MS) {
    last_log_ms = now_ms;
    const unsigned long log_start_us = micros();
    emit_periodic_logs(now_ms);
    log_elapsed_us = micros() - log_start_us;
    if (last_log_slot < 7 && log_elapsed_us > log_slot_max_us[last_log_slot]) {
      log_slot_max_us[last_log_slot] = log_elapsed_us;
    }
  }

  if (BLE_ENABLED) {
    phase_start_us = micros();
    summary_ble::tick();
  } else {
    phase_start_us = micros();
  }
  wifi_mgr::tick(now_ms);
  record_phase_max(loop_phase_max.radio_us, micros() - phase_start_us);

  phase_start_us = micros();
  led_ui::tick();
  record_phase_max(loop_phase_max.led_us, micros() - phase_start_us);

  phase_start_us = micros();
  portal_http::handle_client();
  record_phase_max(loop_phase_max.http_us, micros() - phase_start_us);

  const unsigned long log_drain_start_us = micros();
  serial_log_queue.drain(Serial);
  log_drain_elapsed_us = micros() - log_drain_start_us;

  const unsigned long loop_elapsed_us = micros() - loop_start_us;
  // Keep both scheduler impact (total) and application work. Serial diagnostics
  // can block while their UART buffer drains, so total latency alone cannot
  // identify a slow firmware path.
  const unsigned long loop_work_us = loop_elapsed_us - log_elapsed_us - log_drain_elapsed_us;
  loop_sum_us += loop_elapsed_us;
  loop_work_sum_us += loop_work_us;
  loop_count++;
  if (loop_elapsed_us > loop_max_us) {
    loop_max_us = loop_elapsed_us;
  }
  if (loop_work_us > loop_work_max_us) {
    loop_work_max_us = loop_work_us;
  }
  if (log_elapsed_us > log_emit_max_us) {
    log_emit_max_us = log_elapsed_us;
  }
  if (log_drain_elapsed_us > log_drain_max_us) {
    log_drain_max_us = log_drain_elapsed_us;
  }
}
