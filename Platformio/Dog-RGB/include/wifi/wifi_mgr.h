#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace wifi_mgr {
struct WifiDiagnostics {
  bool last_ap_start_ok;
  uint32_t ap_start_count;
  uint32_t ap_start_fail_count;
  uint32_t ap_retry_schedule_count;
  uint32_t ap_stop_count;
  uint32_t ap_restart_count;
  uint32_t sta_retry_count;
  uint32_t sta_connect_fail_count;
  uint32_t ap_station_connect_count;
  uint32_t ap_station_disconnect_count;
  uint32_t sta_got_ip_count;
  uint32_t sta_disconnect_count;
  uint32_t event_queue_overflow_count;
  uint8_t event_queue_high_water;
  unsigned long last_ap_start_ms;
  unsigned long last_ap_stop_ms;
  unsigned long next_ap_retry_ms;
  unsigned long ap_retry_delay_ms;
  bool ap_retry_scheduled;
  unsigned long last_sta_retry_ms;
  unsigned long next_sta_retry_ms;
  bool sta_retry_scheduled;
  unsigned long ap_hold_until_ms;
  bool ap_hold_scheduled;
  unsigned long last_wifi_event_ms;
  uint32_t last_wifi_event;
  uint8_t current_ap_channel;
  unsigned long ap_station_poll_max_us;
  unsigned long channel_query_max_us;
  char last_ap_reason[32];
  char last_ap_failure_stage[16];
  char last_sta_reason[32];
};

void begin();
void tick(unsigned long now_ms);

void start_sta_mode();
void start_ap_mode();
bool save_creds(const String &ssid, const String &pass);

const String &ssid();
const String &pass();
int8_t storage_slot();
uint32_t storage_generation();
uint32_t storage_save_failures();

bool sta_connected();
bool sta_connecting();
bool ap_enabled();
IPAddress ap_ip();
uint8_t mode();
bool wifi_off();
uint8_t ap_station_count();
bool is_ap_mode();
const WifiDiagnostics &diagnostics();

void note_portal_activity();
void schedule_ap_restart();
void apply_mdns(const String &previous, const String &current);

// Async scan of nearby networks, so the portal can offer a list instead of
// asking the user to type an SSID by hand.
//
// A scan hops channels, which briefly disturbs the AP the user is sitting on.
// That is why this is request-driven: scan_begin() only ever runs because
// somebody pressed a button. Never call it on a timer.
enum class ScanState : uint8_t {
  Idle,     // nothing requested yet, or results were released
  Running,  // radio is scanning; poll again
  Ready,    // results available via scan_count()/scan_entry()
  Failed,   // the driver refused or the scan timed out
};

// Starts a scan if one is not already in flight. Returns false when the radio
// could not be put into a state where scanning is possible.
bool scan_begin();
ScanState scan_state();
int16_t scan_count();
// Reads one result. Returns false when index is out of range. `open_out` is
// true for networks with no password, which is what the portal shows as a
// warning rather than a lock.
bool scan_entry(int16_t index, String &ssid_out, int32_t &rssi_out, bool &open_out);
// Frees the driver's result buffer. Results are a few KB of heap on a device
// that has little, so the portal releases them as soon as it has serialised.
void scan_release();
}
