#pragma once
#include "display/snapshot.h"

namespace display {
constexpr uint8_t kRowCount = 5;
struct TextView {
  char rows[kRowCount][24] = {};
  char distance_value[8] = {}, distance_unit[4] = {};
  uint16_t gps_color = 0;
  gps::ReceptionState gps_state = gps::ReceptionState::NoData;
  led::LedMode led_mode = led::LedMode::Speed;
};
TextView format_view(const DisplaySnapshot &sample);
} // namespace display
