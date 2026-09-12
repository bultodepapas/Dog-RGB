#include "display/text_view.h"
#include <math.h>
#include <stdio.h>

namespace display {
TextView format_view(const DisplaySnapshot &sample) {
  TextView view;
  const char *state = "SIN DATOS";
  view.gps_color = 0xFDC0; // Amber: lack of trustworthy current data.
  switch (sample.gps_state) {
    case gps::ReceptionState::NoData: break;
    case gps::ReceptionState::Receiving: state = "SIN RMC"; break;
    case gps::ReceptionState::Searching: state = "BUSCANDO"; break;
    case gps::ReceptionState::Untrusted: state = "BAJA CALIDAD"; break;
    case gps::ReceptionState::Fix: state = "FIX VALIDO"; view.gps_color = 0x5EF4; break;
    case gps::ReceptionState::Stale: state = "CADUCADO"; break;
  }
  snprintf(view.rows[0], sizeof(view.rows[0]), "%s", state);
  if (sample.gps_state != gps::ReceptionState::Fix || !sample.speed_valid ||
      !isfinite(sample.speed_kph) || sample.speed_kph < 0) {
    snprintf(view.rows[1], sizeof(view.rows[1]), "--");
  } else if (sample.speed_kph > 999.9f) {
    snprintf(view.rows[1], sizeof(view.rows[1]), "999+");
  } else {
    snprintf(view.rows[1], sizeof(view.rows[1]), "%.1f", static_cast<double>(sample.speed_kph));
  }
  if (!isfinite(sample.daily_distance_m) || sample.daily_distance_m < 0) {
    snprintf(view.rows[2], sizeof(view.rows[2]), "-- m");
  } else if (sample.daily_distance_m > 999999.0f) {
    snprintf(view.rows[2], sizeof(view.rows[2]), ">999 km");
  } else {
    snprintf(view.rows[2], sizeof(view.rows[2]), "%.0f m", static_cast<double>(sample.daily_distance_m));
  }
  if (sample.distance_date >= 20000101 && sample.distance_date <= 20991231) {
    snprintf(view.rows[3], sizeof(view.rows[3]), "%04lu-%02lu-%02lu",
        static_cast<unsigned long>(sample.distance_date / 10000),
        static_cast<unsigned long>((sample.distance_date / 100) % 100),
        static_cast<unsigned long>(sample.distance_date % 100));
  } else {
    snprintf(view.rows[3], sizeof(view.rows[3]), "Fecha sin registrar");
  }
  // Reuse the domain's existing mode names; never infer the active effect.
  const float meters = sample.daily_distance_m;
  if (!isfinite(meters) || meters < 0) {
    snprintf(view.distance_value, sizeof(view.distance_value), "--");
    snprintf(view.distance_unit, sizeof(view.distance_unit), "m");
  } else if (meters < 999.5f) {
    snprintf(view.distance_value, sizeof(view.distance_value), "%.0f", static_cast<double>(meters));
    snprintf(view.distance_unit, sizeof(view.distance_unit), "m");
  } else {
    if (meters > 999999.0f) snprintf(view.distance_value, sizeof(view.distance_value), ">999");
    else snprintf(view.distance_value, sizeof(view.distance_value), meters < 99950.0f ? "%.2f" : "%.0f",
        static_cast<double>(meters / 1000.0f));
    snprintf(view.distance_unit, sizeof(view.distance_unit), "km");
  }
  snprintf(view.rows[4], sizeof(view.rows[4]), "LED %s", led::led_mode_name(sample.led_mode));
  return view;
}
} // namespace display
