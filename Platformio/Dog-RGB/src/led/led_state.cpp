#include "led/led_state.h"

namespace led {

const char *led_intent_name(LedIntent intent) {
  switch (intent) {
    case LedIntent::Welcome: return "welcome";
    case LedIntent::DayStatus: return "day_status";
    case LedIntent::Idle: return "idle";
    case LedIntent::HomeMissing: return "home_missing";
    case LedIntent::Range: return "range";
    case LedIntent::Show: return "show";
    case LedIntent::Simple: return "simple";
    case LedIntent::CriticalAlert: return "critical_alert";
  }
  return "unknown";
}

const char *led_mode_name(LedMode mode) {
  switch (mode) {
    case LedMode::Speed: return "speed";
    case LedMode::Geofence: return "geofence";
    case LedMode::Show: return "show";
    case LedMode::Simple: return "simple";
  }
  return "unknown";
}

} // namespace led
