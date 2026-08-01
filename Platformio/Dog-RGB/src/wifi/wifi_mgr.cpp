#include "wifi/wifi_mgr.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "config/runtime_config.h"
#include "config.h"
#include "gps/gps.h"
#include "storage/nvs_store.h"
#include "util/crc32.h"
#include "util/time_utils.h"

namespace wifi_mgr {
namespace {
String wifi_ssid;
String wifi_pass;
bool wifi_sta_connected = false;
bool wifi_sta_connecting = false;
unsigned long wifi_sta_start_ms = 0;
unsigned long last_wifi_check_ms = 0;
bool ap_enabled_state = true;
bool wifi_off_state = false;
wifi_mode_t wifi_mode_state = WIFI_OFF;
unsigned long last_ap_client_ms = 0;
unsigned long last_ap_poll_ms = 0;
uint8_t ap_station_count_state = 0;
unsigned long stationary_ms = 0;
unsigned long last_stationary_ms = 0;
unsigned long sta_retry_backoff_ms = WIFI_RETRY_INTERVAL_MS;

bool pending_ap_restart = false;
unsigned long pending_ap_at_ms = 0;
const unsigned long AP_RESTART_DELAY_MS = 500;
const IPAddress AP_LOCAL_IP(192, 168, 4, 1);
const IPAddress AP_GATEWAY_IP(192, 168, 4, 1);
const IPAddress AP_SUBNET_MASK(255, 255, 255, 0);
IPAddress ap_ip_state(0, 0, 0, 0);
WifiDiagnostics wifi_diag = {};
int8_t wifi_creds_active_record = -1;
uint32_t wifi_creds_generation = 0;
uint32_t wifi_creds_save_failure_count = 0;

static const uint32_t WIFI_CREDS_RECORD_MAGIC = 0x49465744UL; // "DWFI" in storage.
static const uint16_t WIFI_CREDS_RECORD_VERSION = 1;
static const char *WIFI_CREDS_RECORD_KEYS[2] = {"wifi_a", "wifi_b"};
static const char *WIFI_CREDS_MIGRATED_KEY = "wifi_blob";
static const char *LEGACY_WIFI_SSID_KEY = "wifi_ssid";
static const char *LEGACY_WIFI_PASS_KEY = "wifi_pass";

struct __attribute__((packed)) WifiCredentialsRecord {
  uint32_t magic;
  uint16_t record_version;
  uint16_t record_size;
  uint32_t generation;
  uint8_t configured;
  uint8_t ssid_length;
  uint8_t pass_length;
  uint8_t reserved;
  char ssid[33];
  char pass[65];
  uint32_t crc32;
};

static_assert(sizeof(WifiCredentialsRecord) == 118,
              "Wi-Fi credentials record layout changed");

bool set_wifi_mode(wifi_mode_t requested_mode) {
  const bool ok = WiFi.mode(requested_mode);
  if (ok) {
    wifi_mode_state = requested_mode;
  }
  return ok;
}

struct PendingWifiEvent {
  uint32_t id;
  uint32_t captured_ms;
};

static const uint8_t WIFI_EVENT_QUEUE_LENGTH = 16;
StaticQueue_t wifi_event_queue_control;
uint8_t wifi_event_queue_storage[WIFI_EVENT_QUEUE_LENGTH * sizeof(PendingWifiEvent)];
QueueHandle_t wifi_event_queue = nullptr;
std::atomic<uint32_t> wifi_event_dropped_pending{0};

void set_reason(char *dest, size_t size, const char *reason) {
  if (size == 0) {
    return;
  }
  snprintf(dest, size, "%s", reason != nullptr ? reason : "unknown");
}

bool timer_active(unsigned long now_ms, unsigned long until_ms, bool scheduled) {
  return scheduled && time_utils::deadline_pending(now_ms, until_ms);
}

void hold_ap(unsigned long now_ms, unsigned long hold_ms) {
  const unsigned long until = now_ms + hold_ms;
  if (!timer_active(now_ms, wifi_diag.ap_hold_until_ms,
                    wifi_diag.ap_hold_scheduled) ||
      time_utils::deadline_later(until, wifi_diag.ap_hold_until_ms)) {
    wifi_diag.ap_hold_until_ms = until;
    wifi_diag.ap_hold_scheduled = true;
  }
}

uint8_t ap_channel() {
  const uint8_t sta_channel = WiFi.channel();
  if (wifi_sta_connected && sta_channel >= 1 && sta_channel <= 13) {
    return sta_channel;
  }
  return AP_CHANNEL;
}

int read_ap_station_count() {
  const unsigned long started_us = micros();
  const int stations = WiFi.softAPgetStationNum();
  const unsigned long elapsed_us = micros() - started_us;
  if (elapsed_us > wifi_diag.ap_station_poll_max_us) {
    wifi_diag.ap_station_poll_max_us = elapsed_us;
  }
  return stations;
}

uint8_t read_wifi_channel() {
  const unsigned long started_us = micros();
  const uint8_t channel = WiFi.channel();
  const unsigned long elapsed_us = micros() - started_us;
  if (elapsed_us > wifi_diag.channel_query_max_us) {
    wifi_diag.channel_query_max_us = elapsed_us;
  }
  return channel;
}

void update_ap_station_count() {
  if (!ap_enabled_state) {
    ap_station_count_state = 0;
    return;
  }
  const int stations = read_ap_station_count();
  ap_station_count_state = (stations > 0) ? static_cast<uint8_t>(stations) : 0;
}

bool start_ap_radio(const char *reason, bool preserve_sta) {
  const RuntimeConfig &cfg = config::get();
  const unsigned long now_ms = millis();
  const bool use_ap_sta = preserve_sta && wifi_ssid.length() > 0;
  set_wifi_mode(use_ap_sta ? WIFI_AP_STA : WIFI_AP);
  // The mode change is internally asynchronous in the ESP32 driver; without
  // this margin softAPConfig/softAP operate on a half-initialized stack and
  // return false or produce an invisible AP.
  delay(WIFI_MODE_SETTLE_MS);
  WiFi.setSleep(false);
  WiFi.softAPConfig(AP_LOCAL_IP, AP_GATEWAY_IP, AP_SUBNET_MASK);

  const char *pass = (cfg.ap_pass.length() == 0) ? nullptr : cfg.ap_pass.c_str();
  const uint8_t channel = use_ap_sta ? ap_channel() : AP_CHANNEL;
  const bool ok = WiFi.softAP(cfg.ap_ssid.c_str(), pass, channel, false, AP_MAX_CLIENTS);

  wifi_diag.last_ap_start_ok = ok;
  wifi_diag.ap_start_count++;
  wifi_diag.last_ap_start_ms = now_ms;
  wifi_diag.current_ap_channel = channel;
  set_reason(wifi_diag.last_ap_reason, sizeof(wifi_diag.last_ap_reason), reason);

  if (ok) {
    ap_enabled_state = true;
    wifi_off_state = false;
    // Query the driver once at the state transition. Reading softAPIP() from
    // periodic logs or HTTP status paths can synchronously block for ~95 ms.
    ap_ip_state = WiFi.softAPIP();
    update_ap_station_count();
    last_ap_client_ms = now_ms;
    hold_ap(now_ms, AP_SETUP_HOLD_MS);
  } else {
    wifi_diag.ap_start_fail_count++;
    ap_enabled_state = false;
    ap_station_count_state = 0;
  }
  Serial.print("[WIFI_AP] start ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print(" ssid=");
  Serial.print(cfg.ap_ssid);
  Serial.print(" ch=");
  Serial.print(channel);
  Serial.print(" mode=");
  Serial.print(use_ap_sta ? "AP_STA" : "AP");
  Serial.print(" reason=");
  Serial.print(reason);
  Serial.print(" ip=");
  Serial.println(ok ? ap_ip_state.toString() : String("n/a"));
  return ok;
}

void stop_ap_radio(const char *reason) {
  if (!ap_enabled_state) {
    return;
  }
  WiFi.softAPdisconnect(true);
  ap_enabled_state = false;
  ap_station_count_state = 0;
  last_ap_client_ms = 0;
  wifi_diag.ap_stop_count++;
  wifi_diag.last_ap_stop_ms = millis();
  wifi_diag.ap_hold_until_ms = 0;
  wifi_diag.ap_hold_scheduled = false;
  set_reason(wifi_diag.last_ap_reason, sizeof(wifi_diag.last_ap_reason), reason);
  Serial.print("[WIFI_AP] stop reason=");
  Serial.println(reason);
}

void reset_sta_backoff() {
  sta_retry_backoff_ms = WIFI_RETRY_INTERVAL_MS;
  wifi_diag.next_sta_retry_ms = 0;
  wifi_diag.sta_retry_scheduled = false;
}

void schedule_sta_retry(unsigned long now_ms, const char *reason) {
  wifi_diag.sta_connect_fail_count++;
  set_reason(wifi_diag.last_sta_reason, sizeof(wifi_diag.last_sta_reason), reason);
  wifi_diag.next_sta_retry_ms = now_ms + sta_retry_backoff_ms;
  wifi_diag.sta_retry_scheduled = true;
  sta_retry_backoff_ms = (sta_retry_backoff_ms >= STA_RETRY_BACKOFF_MAX_MS / 2)
                             ? STA_RETRY_BACKOFF_MAX_MS
                             : sta_retry_backoff_ms * 2;
  Serial.print("[WIFI_STA] retry_sched delay_ms=");
  Serial.print(wifi_diag.next_sta_retry_ms - now_ms);
  Serial.print(" next_backoff_ms=");
  Serial.print(sta_retry_backoff_ms);
  Serial.print(" fail_count=");
  Serial.print(wifi_diag.sta_connect_fail_count);
  Serial.print(" reason=");
  Serial.println(reason);
}

void begin_mdns() {
  MDNS.end();
  MDNS.begin(config::get().mdns.c_str());
}

void on_wifi_event(WiFiEvent_t event) {
  if (wifi_event_queue == nullptr) {
    wifi_event_dropped_pending.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const PendingWifiEvent pending = {
      static_cast<uint32_t>(event),
      static_cast<uint32_t>(millis()),
  };
  if (xQueueSend(wifi_event_queue, &pending, 0) != pdTRUE) {
    wifi_event_dropped_pending.fetch_add(1, std::memory_order_relaxed);
  }
}

void process_wifi_event(const PendingWifiEvent &pending, unsigned long now_ms) {
  const WiFiEvent_t event = static_cast<WiFiEvent_t>(pending.id);
  wifi_diag.last_wifi_event = pending.id;
  wifi_diag.last_wifi_event_ms = pending.captured_ms;

#if defined(ARDUINO_EVENT_WIFI_AP_STACONNECTED)
  if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
#elif defined(SYSTEM_EVENT_AP_STACONNECTED)
  if (event == SYSTEM_EVENT_AP_STACONNECTED) {
#else
  if (false) {
#endif
    wifi_diag.ap_station_connect_count++;
    update_ap_station_count();
    last_ap_client_ms = now_ms;
    hold_ap(now_ms, AP_PORTAL_ACTIVITY_HOLD_MS);
    Serial.print("[WIFI_EVT] AP_STA_CONN clients=");
    Serial.println(ap_station_count_state);
    return;
  }

#if defined(ARDUINO_EVENT_WIFI_AP_STADISCONNECTED)
  if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
#elif defined(SYSTEM_EVENT_AP_STADISCONNECTED)
  if (event == SYSTEM_EVENT_AP_STADISCONNECTED) {
#else
  if (false) {
#endif
    wifi_diag.ap_station_disconnect_count++;
    update_ap_station_count();
    Serial.print("[WIFI_EVT] AP_STA_DISC clients=");
    Serial.println(ap_station_count_state);
    return;
  }

#if defined(ARDUINO_EVENT_WIFI_STA_GOT_IP)
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
#elif defined(SYSTEM_EVENT_STA_GOT_IP)
  if (event == SYSTEM_EVENT_STA_GOT_IP) {
#else
  if (false) {
#endif
    wifi_sta_connected = true;
    wifi_sta_connecting = false;
    wifi_diag.sta_got_ip_count++;
    wifi_diag.current_ap_channel = read_wifi_channel();
    reset_sta_backoff();
    begin_mdns();
    Serial.print("[WIFI_EVT] STA_GOT_IP ip=");
    Serial.print(WiFi.localIP().toString());
    Serial.print(" rssi=");
    Serial.println(WiFi.RSSI());
    return;
  }

#if defined(ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
#elif defined(SYSTEM_EVENT_STA_DISCONNECTED)
  if (event == SYSTEM_EVENT_STA_DISCONNECTED) {
#else
  if (false) {
#endif
    wifi_sta_connected = false;
    wifi_sta_connecting = false;
    wifi_diag.sta_disconnect_count++;
    if (wifi_ssid.length() > 0 && !wifi_diag.sta_retry_scheduled) {
      schedule_sta_retry(now_ms, "sta_disconnected");
    } else {
      set_reason(wifi_diag.last_sta_reason, sizeof(wifi_diag.last_sta_reason), "sta_disconnected");
    }
    Serial.println("[WIFI_EVT] STA_DISC");
  }
}

bool drain_wifi_events(unsigned long now_ms) {
  const uint32_t dropped = wifi_event_dropped_pending.exchange(0, std::memory_order_relaxed);
  wifi_diag.event_queue_overflow_count += dropped;
  if (dropped > 0) {
    // Force the normal Wi-Fi/AP polling path below to reconcile final driver
    // state in this same tick when a burst exceeded the diagnostic queue.
    last_wifi_check_ms = now_ms - WIFI_RETRY_INTERVAL_MS;
  }
  if (wifi_event_queue == nullptr) {
    return dropped > 0;
  }

  const UBaseType_t waiting = uxQueueMessagesWaiting(wifi_event_queue);
  if (waiting > wifi_diag.event_queue_high_water) {
    wifi_diag.event_queue_high_water = static_cast<uint8_t>(
        (waiting > UINT8_MAX) ? UINT8_MAX : waiting);
  }

  PendingWifiEvent pending = {};
  while (xQueueReceive(wifi_event_queue, &pending, 0) == pdTRUE) {
    process_wifi_event(pending, now_ms);
  }
  return dropped > 0;
}

uint32_t wifi_creds_record_crc(const WifiCredentialsRecord &record) {
  return util::crc32_ieee(&record, offsetof(WifiCredentialsRecord, crc32));
}

bool wifi_creds_generation_is_newer(uint32_t candidate, uint32_t reference) {
  const uint32_t delta = candidate - reference;
  return delta != 0 && delta < 0x80000000UL;
}

bool encode_wifi_creds_record(const String &ssid,
                              const String &pass,
                              uint32_t generation,
                              WifiCredentialsRecord &record) {
  const size_t ssid_length = ssid.length();
  const size_t pass_length = pass.length();
  const bool configured = ssid_length > 0;
  if (ssid_length > 32 || pass_length > 64 || (!configured && pass_length > 0)) {
    return false;
  }

  record = WifiCredentialsRecord{};
  record.magic = WIFI_CREDS_RECORD_MAGIC;
  record.record_version = WIFI_CREDS_RECORD_VERSION;
  record.record_size = sizeof(record);
  record.generation = generation;
  record.configured = configured ? 1 : 0;
  record.ssid_length = static_cast<uint8_t>(ssid_length);
  record.pass_length = static_cast<uint8_t>(pass_length);
  if (ssid_length > 0) {
    memcpy(record.ssid, ssid.c_str(), ssid_length);
  }
  if (pass_length > 0) {
    memcpy(record.pass, pass.c_str(), pass_length);
  }
  record.crc32 = wifi_creds_record_crc(record);
  return true;
}

bool decode_wifi_creds_record(const WifiCredentialsRecord &record,
                              String &ssid,
                              String &pass) {
  if (record.magic != WIFI_CREDS_RECORD_MAGIC ||
      record.record_version != WIFI_CREDS_RECORD_VERSION ||
      record.record_size != sizeof(record) ||
      record.configured > 1 ||
      record.reserved != 0 ||
      record.ssid_length > 32 ||
      record.pass_length > 64 ||
      record.crc32 != wifi_creds_record_crc(record) ||
      record.ssid[record.ssid_length] != '\0' ||
      record.pass[record.pass_length] != '\0' ||
      memchr(record.ssid, '\0', sizeof(record.ssid)) !=
          record.ssid + record.ssid_length ||
      memchr(record.pass, '\0', sizeof(record.pass)) !=
          record.pass + record.pass_length ||
      (record.configured == 0 &&
       (record.ssid_length != 0 || record.pass_length != 0)) ||
      (record.configured != 0 && record.ssid_length == 0)) {
    return false;
  }

  ssid = String(record.ssid);
  pass = String(record.pass);
  return ssid.length() == record.ssid_length && pass.length() == record.pass_length;
}

bool load_wifi_creds_record(Preferences &prefs,
                            uint8_t slot,
                            WifiCredentialsRecord &record,
                            String &ssid,
                            String &pass) {
  const char *key = WIFI_CREDS_RECORD_KEYS[slot];
  if (prefs.getBytesLength(key) != sizeof(record) ||
      prefs.getBytes(key, &record, sizeof(record)) != sizeof(record)) {
    return false;
  }
  return decode_wifi_creds_record(record, ssid, pass);
}

bool write_wifi_creds_record(Preferences &prefs,
                             const String &ssid,
                             const String &pass) {
  const uint8_t target_slot = (wifi_creds_active_record == 0) ? 1 : 0;
  const uint32_t next_generation = (wifi_creds_generation == UINT32_MAX)
                                       ? 1U
                                       : wifi_creds_generation + 1U;
  WifiCredentialsRecord record = {};
  if (!encode_wifi_creds_record(ssid, pass, next_generation, record)) {
    wifi_creds_save_failure_count++;
    return false;
  }

  const char *key = WIFI_CREDS_RECORD_KEYS[target_slot];
  if (prefs.putBytes(key, &record, sizeof(record)) != sizeof(record)) {
    wifi_creds_save_failure_count++;
    return false;
  }

  WifiCredentialsRecord readback = {};
  String verified_ssid;
  String verified_pass;
  if (!load_wifi_creds_record(prefs, target_slot, readback,
                              verified_ssid, verified_pass) ||
      memcmp(&record, &readback, sizeof(record)) != 0 ||
      verified_ssid != ssid || verified_pass != pass) {
    wifi_creds_save_failure_count++;
    return false;
  }

  wifi_creds_active_record = target_slot;
  wifi_creds_generation = next_generation;
  wifi_ssid = verified_ssid;
  wifi_pass = verified_pass;
  return true;
}

bool mark_wifi_creds_migrated(Preferences &prefs) {
  if (prefs.getBool(WIFI_CREDS_MIGRATED_KEY, false)) {
    return true;
  }
  if (prefs.putBool(WIFI_CREDS_MIGRATED_KEY, true) != 1) {
    wifi_creds_save_failure_count++;
    return false;
  }
  return true;
}

void load_wifi_creds() {
  Preferences &prefs = storage::prefs();
  wifi_ssid = "";
  wifi_pass = "";
  wifi_creds_active_record = -1;
  wifi_creds_generation = 0;

  WifiCredentialsRecord records[2] = {};
  String candidate_ssids[2];
  String candidate_passes[2];
  const bool valid_a = load_wifi_creds_record(
      prefs, 0, records[0], candidate_ssids[0], candidate_passes[0]);
  const bool valid_b = load_wifi_creds_record(
      prefs, 1, records[1], candidate_ssids[1], candidate_passes[1]);

  if (valid_a || valid_b) {
    uint8_t selected = 0;
    if (!valid_a ||
        (valid_b && wifi_creds_generation_is_newer(
                        records[1].generation, records[0].generation))) {
      selected = 1;
    }
    wifi_creds_active_record = selected;
    wifi_creds_generation = records[selected].generation;
    wifi_ssid = candidate_ssids[selected];
    wifi_pass = candidate_passes[selected];

    // Restore A/B redundancy after a damaged or interrupted inactive slot.
    if (valid_a != valid_b) {
      write_wifi_creds_record(prefs, wifi_ssid, wifi_pass);
    }
    mark_wifi_creds_migrated(prefs);
    return;
  }

  // A completed migration makes the records authoritative. Never resurrect
  // stale legacy credentials if both records are later damaged.
  if (prefs.getBool(WIFI_CREDS_MIGRATED_KEY, false)) {
    return;
  }

  const String legacy_ssid = prefs.getString(LEGACY_WIFI_SSID_KEY, "");
  const String legacy_pass = prefs.getString(LEGACY_WIFI_PASS_KEY, "");
  String migration_ssid = legacy_ssid;
  String migration_pass = legacy_pass;
  WifiCredentialsRecord migration_check = {};
  if (!encode_wifi_creds_record(migration_ssid, migration_pass, 1,
                                migration_check)) {
    Serial.println("[WIFI_STORE] invalid legacy credentials; migrating as unconfigured");
    migration_ssid = "";
    migration_pass = "";
  }

  // Populate both slots once so the first post-upgrade boot already has a
  // complete fallback generation. RAM changes only after each verified write.
  if (write_wifi_creds_record(prefs, migration_ssid, migration_pass) &&
      write_wifi_creds_record(prefs, migration_ssid, migration_pass)) {
    mark_wifi_creds_migrated(prefs);
    Serial.print("[WIFI_STORE] legacy migration complete generation=");
    Serial.println(wifi_creds_generation);
  } else {
    // Preserve legacy runtime behavior for this boot if migration could not
    // commit even one verified record. A later boot will retry migration.
    if (wifi_creds_active_record < 0) {
      wifi_ssid = migration_ssid;
      wifi_pass = migration_pass;
    }
    Serial.println("[WIFI_STORE] legacy migration incomplete; will retry");
  }
}

void start_ap_mode_internal(const char *reason) {
  wifi_sta_connected = false;
  wifi_sta_connecting = false;
  reset_sta_backoff();
  start_ap_radio(reason, false);
}

void start_sta_mode_internal(const char *reason) {
  if (wifi_ssid.length() == 0) {
    start_ap_mode_internal("no_sta_creds");
    return;
  }

  if (ap_enabled_state && wifi_mode_state != WIFI_AP_STA) {
    set_wifi_mode(WIFI_AP_STA);
  } else if (!ap_enabled_state) {
    set_wifi_mode(WIFI_STA);
  }
  wifi_off_state = false;
  wifi_diag.sta_retry_scheduled = false;
  WiFi.setSleep(false);
  WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
  wifi_sta_connected = false;
  wifi_sta_connecting = true;
  wifi_sta_start_ms = millis();
  wifi_diag.sta_retry_count++;
  wifi_diag.last_sta_retry_ms = wifi_sta_start_ms;
  set_reason(wifi_diag.last_sta_reason, sizeof(wifi_diag.last_sta_reason), reason);
  Serial.print("[WIFI_STA] begin ssid=");
  Serial.print(wifi_ssid);
  Serial.print(" retry_count=");
  Serial.print(wifi_diag.sta_retry_count);
  Serial.print(" reason=");
  Serial.println(reason);
}

void enable_ap(const char *reason) {
  if (ap_enabled_state) {
    return;
  }
  start_ap_radio(reason, wifi_ssid.length() > 0 && (wifi_sta_connected || wifi_sta_connecting));
}

void disable_ap(const char *reason) {
  if (!ap_enabled_state) {
    return;
  }
  stop_ap_radio(reason);
  if (wifi_ssid.length() > 0) {
    set_wifi_mode(WIFI_STA);
  }
}

void set_wifi_off(bool off) {
  if (off) {
    if (wifi_off_state) {
      return;
    }
    set_wifi_mode(WIFI_OFF);
    wifi_off_state = true;
    ap_enabled_state = false;
    ap_station_count_state = 0;
    wifi_sta_connected = false;
    wifi_sta_connecting = false;
    last_ap_client_ms = 0;
    last_ap_poll_ms = 0;
    Serial.println("[WIFI_OFF] radio disabled");
    return;
  }
  if (!wifi_off_state) {
    return;
  }
  Serial.println("[WIFI_OFF] radio re-enabled");
  wifi_off_state = false;
  if (wifi_ssid.length() > 0) {
    // Bug fix: use preserve_sta=true to start in WIFI_AP_STA directly, avoiding
    // a WIFI_AP → WIFI_AP_STA mode switch without a settle delay (same fix as boot).
    start_ap_radio("wifi_on", true);
    start_sta_mode_internal("wifi_on");
  } else {
    start_ap_mode_internal("wifi_on");
  }
}

void update_ap_policy(unsigned long now_ms) {
  if (last_stationary_ms == 0) {
    last_stationary_ms = now_ms;
  }
  const unsigned long dt_ms = now_ms - last_stationary_ms;
  last_stationary_ms = now_ms;

  if (gps::has_fix()) {
    if (gps::last_speed_kph() <= AP_STATIONARY_ON_KPH) {
      stationary_ms = (stationary_ms + dt_ms > AP_STATIONARY_MS) ? AP_STATIONARY_MS : (stationary_ms + dt_ms);
    } else if (gps::last_speed_kph() >= AP_STATIONARY_OFF_KPH) {
      stationary_ms = 0;
    }
  } else {
    stationary_ms = 0;
  }

  const bool ap_force_on = !gps::has_fix();
  const bool ap_request_on = (stationary_ms >= AP_STATIONARY_MS);

  if (wifi_off_state) {
    if (ap_force_on || ap_request_on) {
      set_wifi_off(false);
    } else {
      return;
    }
  }

  if (ap_enabled_state && (now_ms - last_ap_poll_ms) >= AP_CLIENT_POLL_MS) {
    last_ap_poll_ms = now_ms;
    const int stations = read_ap_station_count();
    ap_station_count_state = (stations > 0) ? static_cast<uint8_t>(stations) : 0;
    if (stations > 0) {
      last_ap_client_ms = now_ms;
    }
  } else if (!ap_enabled_state) {
    ap_station_count_state = 0;
  }

  if (ap_force_on) {
    if (!ap_enabled_state) {
      enable_ap("gps_no_fix");
    }
    last_ap_client_ms = now_ms;
    return;
  }

  if (ap_request_on && !ap_enabled_state) {
    enable_ap("stationary");
  }

  if (ap_enabled_state) {
    if (last_ap_client_ms == 0) {
      last_ap_client_ms = now_ms;
    }
    if (wifi_diag.ap_hold_scheduled &&
        time_utils::deadline_reached(now_ms, wifi_diag.ap_hold_until_ms)) {
      wifi_diag.ap_hold_scheduled = false;
    }
    if (timer_active(now_ms, wifi_diag.ap_hold_until_ms,
                     wifi_diag.ap_hold_scheduled)) {
      return;
    }
    if ((now_ms - last_ap_client_ms) >= AP_IDLE_TIMEOUT_MS) {
      disable_ap("idle_timeout");
      // Bug fix: do NOT call set_wifi_off(true) here. Turning WiFi completely off
      // when there is no STA connection means the portal becomes permanently
      // unreachable while the dog has a GPS fix and is moving, because
      // ap_force_on=false and ap_request_on=false in that state.
      // Leaving WiFi on allows STA retry to keep running and the AP policy to
      // re-enable the AP as soon as the dog stops (stationary_ms accumulates).
      // Bug fix: do NOT reset stationary_ms here. If the dog is already
      // stationary, resetting it forced an extra 2-minute wait before the AP
      // came back on.
    }
  }
}

} // namespace

void begin() {
  if (wifi_event_queue == nullptr) {
    wifi_event_queue = xQueueCreateStatic(
        WIFI_EVENT_QUEUE_LENGTH,
        sizeof(PendingWifiEvent),
        wifi_event_queue_storage,
        &wifi_event_queue_control);
  } else {
    xQueueReset(wifi_event_queue);
  }
  wifi_event_dropped_pending.store(0, std::memory_order_relaxed);
  wifi_diag.event_queue_overflow_count = 0;
  wifi_diag.event_queue_high_water = 0;

  // Hard-reset the radio before anything else. This clears residual state from
  // brownouts, WDT resets or power glitches that leave the driver half-initialized,
  // which causes softAP() to fail silently or produce an invisible AP.
  WiFi.persistent(false); // Never auto-write STA credentials to flash.
  WiFi.disconnect(true, true);
  set_wifi_mode(WIFI_OFF);
  delay(WIFI_BOOT_STABILIZE_MS);

  WiFi.onEvent(on_wifi_event);
  load_wifi_creds();
  ap_enabled_state = false;
  wifi_off_state = false;
  if (wifi_ssid.length() > 0) {
    if (DEBUG_AP_ONLY_MINIMAL) {
      start_ap_mode_internal("debug_ap_only");
      return;
    }
    // Boot with retry: attempt softAP up to WIFI_BOOT_AP_MAX_ATTEMPTS times.
    // Each failed attempt resets the radio and waits before retrying.
    bool ap_ok = false;
    for (int attempt = 1; attempt <= WIFI_BOOT_AP_MAX_ATTEMPTS; attempt++) {
      // Bug fix: use preserve_sta=true so WiFi starts directly in WIFI_AP_STA mode.
      // Previously preserve_sta=false caused WIFI_AP → WIFI_AP_STA mode switch
      // immediately after softAP(), forcing the AP stack to reinitialize.
      ap_ok = start_ap_radio("boot_with_sta", true);
      if (ap_ok) {
        break;
      }
      Serial.print("[WIFI_AP] boot attempt ");
      Serial.print(attempt);
      Serial.print("/");
      Serial.print(WIFI_BOOT_AP_MAX_ATTEMPTS);
      Serial.println(" failed, retrying");
      if (attempt < WIFI_BOOT_AP_MAX_ATTEMPTS) {
        set_wifi_mode(WIFI_OFF);
        delay(WIFI_BOOT_AP_RETRY_DELAY_MS);
      }
    }
    if (!ap_ok) {
      Serial.println("[WIFI_AP] all boot attempts failed — AP unavailable, continuing STA-only");
    }
    start_sta_mode_internal("boot");
  } else {
    // No STA creds: boot with retry in AP-only mode.
    bool ap_ok = false;
    for (int attempt = 1; attempt <= WIFI_BOOT_AP_MAX_ATTEMPTS; attempt++) {
      ap_ok = start_ap_radio("boot_no_sta", false);
      if (ap_ok) {
        break;
      }
      Serial.print("[WIFI_AP] boot attempt ");
      Serial.print(attempt);
      Serial.print("/");
      Serial.print(WIFI_BOOT_AP_MAX_ATTEMPTS);
      Serial.println(" failed, retrying");
      if (attempt < WIFI_BOOT_AP_MAX_ATTEMPTS) {
        set_wifi_mode(WIFI_OFF);
        delay(WIFI_BOOT_AP_RETRY_DELAY_MS);
      }
    }
    if (!ap_ok) {
      Serial.println("[WIFI_AP] all boot attempts failed — AP unavailable, portal unreachable");
    }
  }
}

void tick(unsigned long now_ms) {
  const bool reconcile_ap_state = drain_wifi_events(now_ms);

  if (!DEBUG_AP_ONLY_MINIMAL && !wifi_off_state && (now_ms - last_wifi_check_ms >= WIFI_RETRY_INTERVAL_MS)) {
    last_wifi_check_ms = now_ms;
    if (reconcile_ap_state) {
      update_ap_station_count();
    }
    if (wifi_sta_connected && WiFi.status() != WL_CONNECTED) {
      Serial.print("[WIFI_STA] status_lost wl_status=");
      Serial.println(WiFi.status());
      wifi_sta_connected = false;
      if (wifi_ssid.length() > 0) {
        schedule_sta_retry(now_ms, "status_lost");
      } else {
        start_ap_mode_internal("status_lost_no_sta");
      }
    } else if (wifi_sta_connecting) {
      if (WiFi.status() == WL_CONNECTED) {
        wifi_sta_connected = true;
        wifi_sta_connecting = false;
        reset_sta_backoff();
        begin_mdns();
        Serial.print("[WIFI_STA] connected ip=");
        Serial.print(WiFi.localIP().toString());
        Serial.print(" rssi=");
        Serial.println(WiFi.RSSI());
      } else if ((now_ms - wifi_sta_start_ms) >= STA_CONNECT_TIMEOUT_MS) {
        Serial.print("[WIFI_STA] connect_timeout after_ms=");
        Serial.print(now_ms - wifi_sta_start_ms);
        Serial.print(" wl_status=");
        Serial.println(WiFi.status());
        wifi_sta_connecting = false;
        WiFi.disconnect(false, false);
        if (!ap_enabled_state) {
          start_ap_radio("sta_timeout_ap_fallback", false);
        }
        schedule_sta_retry(now_ms, "sta_timeout");
      }
    } else if (!wifi_sta_connected && wifi_ssid.length() > 0 &&
               (!wifi_diag.sta_retry_scheduled ||
                time_utils::deadline_reached(now_ms, wifi_diag.next_sta_retry_ms))) {
      if (ap_station_count_state > 0) {
        wifi_diag.next_sta_retry_ms = now_ms + WIFI_RETRY_INTERVAL_MS;
        wifi_diag.sta_retry_scheduled = true;
      } else {
        wifi_diag.sta_retry_scheduled = false;
        start_sta_mode_internal("retry");
      }
    }
  }

  if (pending_ap_restart && (now_ms - pending_ap_at_ms) >= AP_RESTART_DELAY_MS) {
    pending_ap_restart = false;
    if (ap_enabled_state) {
      wifi_diag.ap_restart_count++;
      start_ap_radio("config_restart", wifi_ssid.length() > 0 && (wifi_sta_connected || wifi_sta_connecting));
    }
  }

  update_ap_policy(now_ms);
}

void start_sta_mode() {
  reset_sta_backoff();
  start_sta_mode_internal("manual");
}

void start_ap_mode() {
  start_ap_mode_internal("manual");
}

bool save_creds(const String &ssid, const String &pass) {
  Preferences &prefs = storage::prefs();
  if (!write_wifi_creds_record(prefs, ssid, pass)) {
    Serial.println("[WIFI_STORE] credential save failed; previous generation remains active");
    return false;
  }
  mark_wifi_creds_migrated(prefs);
  reset_sta_backoff();
  Serial.print("[WIFI_STORE] credentials committed slot=");
  Serial.print(wifi_creds_active_record);
  Serial.print(" generation=");
  Serial.println(wifi_creds_generation);
  return true;
}

const String &ssid() {
  return wifi_ssid;
}

const String &pass() {
  return wifi_pass;
}

int8_t storage_slot() {
  return wifi_creds_active_record;
}

uint32_t storage_generation() {
  return wifi_creds_generation;
}

uint32_t storage_save_failures() {
  return wifi_creds_save_failure_count;
}

bool sta_connected() {
  return wifi_sta_connected;
}

bool sta_connecting() {
  return wifi_sta_connecting;
}

bool ap_enabled() {
  return ap_enabled_state;
}

IPAddress ap_ip() {
  return ap_enabled_state ? ap_ip_state : IPAddress(0, 0, 0, 0);
}

uint8_t mode() {
  return static_cast<uint8_t>(wifi_mode_state);
}

bool wifi_off() {
  return wifi_off_state;
}

uint8_t ap_station_count() {
  return ap_station_count_state;
}

bool is_ap_mode() {
  return wifi_mode_state == WIFI_AP;
}

const WifiDiagnostics &diagnostics() {
  return wifi_diag;
}

void note_portal_activity() {
  if (!ap_enabled_state) {
    return;
  }
  const unsigned long now_ms = millis();
  last_ap_client_ms = now_ms;
  hold_ap(now_ms, AP_PORTAL_ACTIVITY_HOLD_MS);
}

void schedule_ap_restart() {
  pending_ap_restart = true;
  pending_ap_at_ms = millis();
}

void apply_mdns(const String &previous, const String &current) {
  if (previous == current) {
    return;
  }
  if (wifi_sta_connected) {
    MDNS.end();
    MDNS.begin(current.c_str());
  }
}
} // namespace wifi_mgr
