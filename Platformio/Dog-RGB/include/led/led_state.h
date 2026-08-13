#pragma once

#include <stdint.h>

#include "led/led_color.h"
#include "led/palette_registry.h"

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

enum class LedAlert : uint8_t {
  None = 0,
  System = 1,
  Geofence = 2,
};

struct LedState {
  LedMode mode = LedMode::Speed;
  LedIntent intent = LedIntent::Idle;
  LedAlert alert = LedAlert::None;
  uint8_t priority = 20;
  uint8_t brightness = 1;
  bool body_enabled = true;
  bool status_enabled = true;
  bool homogeneous = false;
  bool mirror = false;
  bool critical_alert = false;
  int8_t range = -1;
  uint8_t effect_a = 9;
  uint8_t effect_b = 9;
  uint8_t palette_a = PALETTE_PRIDE;
  uint8_t palette_b = PALETTE_PRIDE;
  uint8_t speed = 80;
  uint8_t intensity = 140;
  uint16_t transition_ms = 0;
  Rgb base = {0, 60, 60};
  Rgb accent = {0, 60, 60};
};

const char *led_intent_name(LedIntent intent);
const char *led_mode_name(LedMode mode);
const char *led_alert_name(LedAlert alert);
bool led_visual_state_equal(const LedState &left, const LedState &right);

} // namespace led
