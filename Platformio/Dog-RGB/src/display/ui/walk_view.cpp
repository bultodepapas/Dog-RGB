#include "display/walk_view.h"
#include <string.h>
#include <stdio.h>

namespace display {
using namespace ui;
bool WalkView::begin(const TextView &view, bool demo, const char *connection, lv_obj_t *parent) {
  if (screen_) return true;
  screen_ = page(parent);
  if (!screen_) return false;
  header_.begin(screen_, "1/3");
  label(screen_, 24, 41, 192, &lv_font_montserrat_20, kWhite, "Actividad");
  status_ = label(screen_, 24, 70, 192, &lv_font_montserrat_14, kMuted, "");
  distance_ = label(screen_, 24, 90, 192, &lv_font_montserrat_48, kWhite, "");
  unit_ = label(screen_, 24, 149, 192, &lv_font_montserrat_12, kMuted, "");
  date_ = label(screen_, 24, 167, 192, &lv_font_montserrat_12, kMuted, "");
  rule(screen_, 190);
  speed_ = label(screen_, 24, 201, 192, &lv_font_montserrat_20, kWhite, "");
  mode_ = label(screen_, 24, 230, 192, &lv_font_montserrat_12, kMuted, "");
  connection_ = label(screen_, 24, 248, 192, &lv_font_montserrat_12, kMuted, "");
  update(view, demo, connection);
  return true;
}

void WalkView::set_page_indicator(const char *text) {
  header_.set_index(text);
}
void WalkView::update(const TextView &view, bool demo, const char *connection) {
  if (!screen_) return;
  header_.update(demo);
  const char *status = "GPS: sin datos";
  switch (view.gps_state) {
    case gps::ReceptionState::NoData: break;
    case gps::ReceptionState::Receiving: status = "GPS: recibiendo"; break;
    case gps::ReceptionState::Searching: status = "Buscando GPS"; break;
    case gps::ReceptionState::Untrusted: status = "GPS: baja calidad"; break;
    case gps::ReceptionState::Fix: status = "GPS listo"; break;
    case gps::ReceptionState::Stale: status = "GPS: dato vencido"; break;
  }
  ui::text(status_, status);
  char value[40];
  snprintf(value, sizeof(value), "%s km/h", view.rows[1]);
  ui::text(speed_, value);
  ui::text(distance_, view.distance_value);
  snprintf(value, sizeof(value), "%s registrados", view.distance_unit);
  ui::text(unit_, value);
  ui::text(date_, view.rows[3]);
  const char *mode = "Luces: Velocidad";
  switch (view.led_mode) {
    case led::LedMode::Speed: break;
    case led::LedMode::Geofence: mode = "Luces: Zona"; break;
    case led::LedMode::Show: mode = "Luces: Show"; break;
    case led::LedMode::Simple: mode = "Luces: Simple"; break;
  }
  ui::text(mode_, mode);
  ui::text(connection_, connection);
  if (color_ != view.gps_color) {
    color_ = view.gps_color;
    lv_color_t color; color.full = color_;
    lv_obj_set_style_text_color(status_, color, 0);
  }
}
} // namespace display
