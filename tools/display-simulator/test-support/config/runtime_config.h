#pragma once
#include <WiFi.h>
namespace config {
struct Config { String ap_ssid = "DogRGB"; };
inline Config value;
inline const Config &get() { return value; }
} // namespace config
