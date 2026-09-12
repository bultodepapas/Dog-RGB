#pragma once
#include "gps/reception.h"
#include "led/led_state.h"

namespace display {
struct LedStatusSnapshot {
  led::LedMode mode = led::LedMode::Speed;
  led::LedIntent intent = led::LedIntent::Idle;
  led::LedAlert alert = led::LedAlert::None;
  bool transport_enabled = false;
  bool body_enabled = false;
};
struct StatusText {
  char gps_status[32] = {}, gps_detail[48] = {};
  char led_status[32] = {}, led_detail[40] = {}, led_notice[32] = {};
  bool gps_valid = false, led_alert = false;
};
LedStatusSnapshot capture_led_status();
StatusText format_status(gps::ReceptionState gps, const LedStatusSnapshot &led);
} // namespace display
