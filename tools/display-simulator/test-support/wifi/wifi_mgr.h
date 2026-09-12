#pragma once
#include <WiFi.h>
namespace wifi_mgr {
inline bool off = false, ap = true, connected = false, connecting = false;
inline uint8_t clients = 0;
inline String configured_ssid = "ConfiguredRouter";
inline bool wifi_off() { return off; }
inline bool ap_enabled() { return ap; }
inline bool sta_connected() { return connected; }
inline bool sta_connecting() { return connecting; }
inline uint8_t ap_station_count() { return clients; }
inline IPAddress ap_ip() { return {{192, 168, 4, 1}}; }
inline const String &ssid() { return configured_ssid; }
} // namespace wifi_mgr
