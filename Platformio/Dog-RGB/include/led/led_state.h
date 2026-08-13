#pragma once

#include <stdint.h>

#include "led/led_color.h"

namespace led {

enum class LedMode : uint8_t {
  Speed = 0,
  Geofence = 1,
  Show = 2,
  Simple = 3,
};

enum class LedIntent : uint8_t {
  Welcome = 0,
  DayStatus = 1,
  Idle = 2,
  HomeMissing = 3,
  Range = 4,
  Show = 5,
  Simple = 6,
  CriticalAlert = 7,
};

struct LedState {
  LedMode mode;
  LedIntent intent;
  uint8_t priority;
  uint8_t brightness;
  bool body_enabled;
  bool status_enabled;
  bool homogeneous;
  bool critical_alert;
  int8_t range;
  uint8_t effect_a;
  uint8_t effect_b;
  uint8_t speed;
  uint8_t intensity;
  Rgb base;
};

const char *led_intent_name(LedIntent intent);
const char *led_mode_name(LedMode mode);

} // namespace led
