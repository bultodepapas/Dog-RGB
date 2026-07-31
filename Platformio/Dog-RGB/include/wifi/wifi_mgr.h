#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace wifi_mgr {
struct WifiDiagnostics {
  bool last_ap_start_ok;
  uint32_t ap_start_count;
  uint32_t ap_start_fail_count;
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
  unsigned long last_sta_retry_ms;
  unsigned long next_sta_retry_ms;
  unsigned long ap_hold_until_ms;
  unsigned long last_wifi_event_ms;
  uint32_t last_wifi_event;
  uint8_t current_ap_channel;
  unsigned long ap_station_poll_max_us;
  unsigned long channel_query_max_us;
  char last_ap_reason[32];
  char last_sta_reason[32];
};

void begin();
void tick(unsigned long now_ms);

void start_sta_mode();
void start_ap_mode();
void save_creds(const String &ssid, const String &pass);

const String &ssid();
const String &pass();

bool sta_connected();
bool sta_connecting();
bool ap_enabled();
bool wifi_off();
uint8_t ap_station_count();
bool is_ap_mode();
const WifiDiagnostics &diagnostics();

void note_portal_activity();
void schedule_ap_restart();
void apply_mdns(const String &previous, const String &current);
}
