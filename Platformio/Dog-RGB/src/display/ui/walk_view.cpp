#include "display/walk_view.h"
#include <string.h>
#include <stdio.h>

namespace display {
namespace {
constexpr uint32_t kBackground = 0x000000, kWhite = 0xFFFFFF;
constexpr uint32_t kMuted = 0xB8B8B8, kRule = 0x303030;
lv_obj_t *label(lv_obj_t *parent, int x, int y, int width,
                const lv_font_t *font, uint32_t color, const char *text) {
  lv_obj_t *obj = lv_label_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_set_pos(obj, x - (lv_obj_get_parent(parent) ? 24 : 0),
                     y - (lv_obj_get_parent(parent) ? 20 : 0));
  lv_obj_set_width(obj, width);
  lv_obj_set_style_text_font(obj, font, 0);
  lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
  lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
  lv_label_set_text(obj, text);
  return obj;
}
void set_text(lv_obj_t *obj, const char *text) {
  if (strcmp(lv_label_get_text(obj), text) != 0) lv_label_set_text(obj, text);
}
}

bool WalkView::begin(const TextView &view, bool demo, const char *connection, lv_obj_t *parent) {
  if (screen_) return true;
  screen_ = lv_obj_create(parent);
  if (!screen_) return false;
  lv_obj_remove_style_all(screen_);
  lv_obj_set_size(screen_, parent ? 192 : 240, parent ? 244 : 280);
  if (parent) lv_obj_set_pos(screen_, 24, 20);
  lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(screen_, lv_color_hex(kBackground), 0);
  lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);
  title_ = label(screen_, 24, 20, 164, &lv_font_montserrat_12, kMuted, "");
  label(screen_, 192, 20, 24, &lv_font_montserrat_12, kMuted, "1/2");
  label(screen_, 24, 41, 192, &lv_font_montserrat_20, kWhite, "Actividad");
  status_ = label(screen_, 24, 70, 192, &lv_font_montserrat_14, kMuted, "");
  distance_ = label(screen_, 24, 90, 192, &lv_font_montserrat_48, kWhite, "");
  unit_ = label(screen_, 24, 149, 192, &lv_font_montserrat_12, kMuted, "");
  date_ = label(screen_, 24, 167, 192, &lv_font_montserrat_12, kMuted, "");
  auto *rule = lv_obj_create(screen_);
  lv_obj_remove_style_all(rule);
  lv_obj_set_pos(rule, parent ? 0 : 24, parent ? 170 : 190);
  lv_obj_set_size(rule, 192, 1);
  lv_obj_set_style_bg_color(rule, lv_color_hex(kRule), 0);
  lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
  lv_obj_clear_flag(rule, LV_OBJ_FLAG_SCROLLABLE);
  speed_ = label(screen_, 24, 201, 192, &lv_font_montserrat_20, kWhite, "");
  mode_ = label(screen_, 24, 230, 192, &lv_font_montserrat_12, kMuted, "");
  connection_ = label(screen_, 24, 248, 192, &lv_font_montserrat_12, kMuted, "");
  update(view, demo, connection);
  return true;
}

void WalkView::update(const TextView &view, bool demo, const char *connection) {
  if (!screen_) return;
  set_text(title_, demo ? "RGB DOG / DEMO" : "RGB DOG");
  const char *status = "GPS: sin datos";
  switch (view.gps_state) {
    case gps::ReceptionState::NoData: break;
    case gps::ReceptionState::Receiving: status = "GPS: recibiendo"; break;
    case gps::ReceptionState::Searching: status = "Buscando GPS"; break;
    case gps::ReceptionState::Untrusted: status = "GPS: baja calidad"; break;
    case gps::ReceptionState::Fix: status = "GPS listo"; break;
    case gps::ReceptionState::Stale: status = "GPS: dato vencido"; break;
  }
  set_text(status_, status);
  char value[40];
  snprintf(value, sizeof(value), "%s km/h", view.rows[1]);
  set_text(speed_, value);
  set_text(distance_, view.distance_value);
  snprintf(value, sizeof(value), "%s registrados", view.distance_unit);
  set_text(unit_, value);
  set_text(date_, view.rows[3]);
  const char *mode = "Luces: Velocidad";
  switch (view.led_mode) {
    case led::LedMode::Speed: break;
    case led::LedMode::Geofence: mode = "Luces: Zona"; break;
    case led::LedMode::Show: mode = "Luces: Show"; break;
    case led::LedMode::Simple: mode = "Luces: Simple"; break;
  }
  set_text(mode_, mode);
  set_text(connection_, connection);
  if (color_ != view.gps_color) {
    color_ = view.gps_color;
    lv_color_t color; color.full = color_;
    lv_obj_set_style_text_color(status_, color, 0);
  }
}
} // namespace display
