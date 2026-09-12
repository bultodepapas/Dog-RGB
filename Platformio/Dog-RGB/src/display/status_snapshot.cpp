#if DOG_RGB_DISPLAY_LVGL == 1
#include "display/status.h"
#include "led/led_ui.h"

namespace display {
LedStatusSnapshot capture_led_status() {
  const auto &state = led_ui::current_state();
  LedStatusSnapshot value;
  value.mode = state.mode; value.intent = state.intent; value.alert = state.alert;
  value.body_enabled = state.body_enabled;
  value.transport_enabled = led_ui::transport_enabled();
  return value;
}
} // namespace display
#endif
