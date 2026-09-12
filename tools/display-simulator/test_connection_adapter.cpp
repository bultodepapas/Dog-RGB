#include <cassert>
#include <cstring>
#include "display/connection.h"
#include "wifi/wifi_mgr.h"
#include "config/runtime_config.h"

int main() {
  auto s = display::capture_connection();
  assert(s.ap_enabled && !s.sta_connected && s.clients == 0);
  assert(strcmp(s.ap_ssid, "DogRGB") == 0 && strcmp(s.ap_ip, "192.168.4.1") == 0);
  assert(!s.sta_ip[0] && strcmp(s.sta_ssid, "ConfiguredRouter") == 0);
  wifi_mgr::connected = true; wifi_mgr::clients = 2;
  s = display::capture_connection();
  assert(s.clients == 2 && strcmp(s.sta_ip, "192.168.1.42") == 0);
  assert(strcmp(s.sta_ssid, "ConnectedRouter") == 0);
  wifi_mgr::connected = false; wifi_mgr::ap = false;
  s = display::capture_connection();
  assert(!s.sta_ip[0] && !s.ap_ip[0] && s.clients == 0 && !s.radio_off);
  wifi_mgr::off = true;
  s = display::capture_connection();
  assert(s.radio_off);
  config::value.ap_ssid = std::string(32, 'W');
  s = display::capture_connection();
  assert(strlen(s.ap_ssid) == 32 && s.ap_ssid[32] == 0);
}
