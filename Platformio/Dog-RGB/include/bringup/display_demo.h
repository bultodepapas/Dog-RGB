#pragma once
#include "display/snapshot.h"

namespace bringup {
// LCD-only fixtures. They never enter the GPS parser, LED policy or storage.
inline display::DisplaySnapshot display_demo(display::DisplaySnapshot sample,
                                              uint32_t elapsed_ms) {
  const uint32_t phase = (elapsed_ms / 5000U) % 6U;
  sample.daily_distance_m = 1842.0f;
  sample.distance_date = 20260912;
  sample.speed_kph = 0;
  sample.speed_valid = false;
  switch (phase) {
    case 0: sample.gps_state = gps::ReceptionState::Searching; break;
    case 1: sample.gps_state = gps::ReceptionState::Fix; sample.speed_valid = true; break;
    case 2:
    case 5:
      sample.gps_state = gps::ReceptionState::Fix;
      sample.speed_valid = true;
      sample.speed_kph = 4.0f + static_cast<float>((elapsed_ms / 1000U) % 5U);
      break;
    case 3: sample.gps_state = gps::ReceptionState::Untrusted; break;
    default: sample.gps_state = gps::ReceptionState::Stale; break;
  }
  return sample;
}
} // namespace bringup
