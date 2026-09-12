#pragma once

#include <stdint.h>
#include "gps/reception.h"
#include "led/led_state.h"

namespace display {
// Value-only domain sample. No display/Arduino objects, storage or mutable refs.
struct DisplaySnapshot {
  uint32_t captured_ms = 0;
  gps::ReceptionState gps_state = gps::ReceptionState::NoData;
  float speed_kph = 0;
  bool speed_valid = false;
  float daily_distance_m = 0;
  uint32_t distance_date = 0;
  led::LedMode led_mode = led::LedMode::Speed;
};
DisplaySnapshot capture_snapshot();
} // namespace display
