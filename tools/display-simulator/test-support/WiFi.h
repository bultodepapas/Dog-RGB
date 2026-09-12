#pragma once
#include <Arduino.h>
using String = std::string;
struct IPAddress {
  uint8_t bytes[4] = {};
  uint8_t operator[](unsigned i) const { return bytes[i]; }
};
inline IPAddress station_ip{{192, 168, 1, 42}};
inline String station_name = "ConnectedRouter";
struct WifiReadOnly {
  IPAddress localIP() const { return station_ip; }
  String SSID() const { return station_name; }
};
inline WifiReadOnly WiFi;
