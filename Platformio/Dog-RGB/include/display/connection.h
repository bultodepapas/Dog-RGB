#pragma once
#include <stdint.h>

namespace display {
// Copied in the cooperative loop. No credentials, Arduino objects or radio actions.
struct ConnectionSnapshot {
  bool radio_off = false, ap_enabled = false;
  bool sta_connected = false, sta_connecting = false;
  uint8_t clients = 0;
  char ap_ssid[33] = {}, sta_ssid[33] = {};
  char ap_ip[16] = {}, sta_ip[16] = {};
};
struct ConnectionText {
  char ap_status[32] = {}, ap_name[40] = {}, ap_address[32] = {};
  char sta_status[32] = {}, sta_name[40] = {}, sta_address[32] = {};
  char summary[32] = {};
};
ConnectionSnapshot capture_connection();
ConnectionText format_connection(const ConnectionSnapshot &sample);
} // namespace display
