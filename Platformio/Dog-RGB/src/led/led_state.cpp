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

const char *led_alert_name(LedAlert alert) {
  switch (alert) {
    case LedAlert::None: return "none";
    case LedAlert::System: return "system";
    case LedAlert::Geofence: return "geofence";
  }
  return "unknown";
}

bool led_visual_state_equal(const LedState &left, const LedState &right) {
  const bool base_equal = left.base.r == right.base.r &&
                          left.base.g == right.base.g &&
                          left.base.b == right.base.b;
  const bool accent_equal = left.accent.r == right.accent.r &&
                            left.accent.g == right.accent.g &&
                            left.accent.b == right.accent.b;
  return left.intent == right.intent &&
         left.body_enabled == right.body_enabled &&
         left.status_enabled == right.status_enabled &&
         left.mirror == right.mirror && left.effect_a == right.effect_a &&
         left.effect_b == right.effect_b &&
         left.palette_a == right.palette_a &&
         left.palette_b == right.palette_b && left.speed == right.speed &&
         left.intensity == right.intensity && base_equal && accent_equal;
}

} // namespace led
