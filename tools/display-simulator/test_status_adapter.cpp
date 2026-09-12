#include <cassert>
#include "display/status.h"
#include "led/led_ui.h"

int main() {
  led_ui::state.mode = led::LedMode::Show;
  led_ui::state.intent = led::LedIntent::DayStatus;
  led_ui::state.body_enabled = false;
  led_ui::state.alert = led::LedAlert::System;
  const auto copied = display::capture_led_status();
  assert(copied.transport_enabled && !copied.body_enabled);
  assert(copied.mode == led::LedMode::Show && copied.intent == led::LedIntent::DayStatus);
  assert(copied.alert == led::LedAlert::System); // Day mode must not erase an alert.
  led_ui::state.intent = led::LedIntent::SceneManual;
  led_ui::state.body_enabled = true;
  led_ui::state.alert = led::LedAlert::None;
  led_ui::transport = false;
  const auto current = display::capture_led_status();
  assert(!current.transport_enabled && current.body_enabled);
  assert(current.intent == led::LedIntent::SceneManual && current.alert == led::LedAlert::None);
  assert(copied.intent == led::LedIntent::DayStatus); // Value copy, not a mutable alias.
}
