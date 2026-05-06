#include "power/day_mode.h"

#include "config.h"
#include "config/runtime_config.h"
#include "gps/gps.h"

namespace day_mode {
bool enabled() {
  return config::get().day_mode_enabled;
}

bool time_available() {
  return gps::has_time();
}

uint16_t local_min() {
  return gps::local_time_min(DAY_MODE_TZ_OFFSET_MIN);
}

bool active_now() {
  if (!config::get().day_mode_enabled) {
    return false;
  }
  if (!gps::has_time()) {
    return false;
  }
  const uint16_t now_min = local_min();
  return now_min >= DAY_MODE_START_MIN && now_min < DAY_MODE_END_MIN;
}

const char *state_name() {
  if (!config::get().day_mode_enabled) {
    return "disabled";
  }
  if (!gps::has_time()) {
    return "waiting_time";
  }
  return active_now() ? "active" : "outside_window";
}
} // namespace day_mode
