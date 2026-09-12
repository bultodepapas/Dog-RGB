#pragma once
#include "display/snapshot.h"

namespace display {
constexpr uint8_t kRowCount = 5;
struct TextView {
  char rows[kRowCount][24] = {};
  uint16_t gps_color = 0;
};
TextView format_view(const DisplaySnapshot &sample);
} // namespace display
