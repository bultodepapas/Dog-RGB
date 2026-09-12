#include "display/snapshot.h"
#include <Arduino.h>
#include <math.h>
#include "gps/gps.h"
#include "led/led_ui.h"

namespace display {
DisplaySnapshot capture_snapshot() {
  DisplaySnapshot sample;
  sample.captured_ms = millis();
  sample.gps_state = gps::reception_state();
  sample.speed_kph = gps::last_speed_kph();
  sample.speed_valid = sample.gps_state == gps::ReceptionState::Fix &&
      gps::speed_usable() && isfinite(sample.speed_kph) && sample.speed_kph >= 0;
  sample.daily_distance_m = gps::total_distance_m();
  sample.distance_date = gps::current_date();
  sample.led_mode = led_ui::current_state().mode;
  return sample;
}
} // namespace display
