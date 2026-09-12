#if DOG_RGB_DISPLAY_LVGL == 1
#include "display/connection.h"
#include <WiFi.h>
#include <stdio.h>
#include "config/runtime_config.h"
#include "wifi/wifi_mgr.h"

namespace display {
ConnectionSnapshot capture_connection() {
  ConnectionSnapshot value;
  value.radio_off = wifi_mgr::wifi_off();
  value.ap_enabled = wifi_mgr::ap_enabled();
  value.sta_connected = wifi_mgr::sta_connected();
  value.sta_connecting = wifi_mgr::sta_connecting();
  value.clients = value.ap_enabled ? wifi_mgr::ap_station_count() : 0;
  snprintf(value.ap_ssid, sizeof(value.ap_ssid), "%s", config::get().ap_ssid.c_str());
  snprintf(value.sta_ssid, sizeof(value.sta_ssid), "%s",
      value.sta_connected ? WiFi.SSID().c_str() : wifi_mgr::ssid().c_str());
  if (value.ap_enabled) {
    const auto ip = wifi_mgr::ap_ip();
    snprintf(value.ap_ip, sizeof(value.ap_ip), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  }
  if (value.sta_connected) {
    const auto ip = WiFi.localIP();
    snprintf(value.sta_ip, sizeof(value.sta_ip), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  }
  return value;
}
} // namespace display
#endif
