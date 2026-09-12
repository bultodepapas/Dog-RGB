#include "display/walk_view.h"
#include <string.h>

namespace display {
namespace {
constexpr uint32_t kBackground = 0x101918, kWhite = 0xF1F5EE;
constexpr uint32_t kMuted = 0xAABBB4, kRule = 0x354540;
lv_obj_t *label(lv_obj_t *parent, int x, int y, int width,
                const lv_font_t *font, uint32_t color, const char *text) {
  lv_obj_t *obj = lv_label_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_set_pos(obj, x, y);
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

bool WalkView::begin(const TextView &view, bool demo) {
  if (screen_) return true;
  screen_ = lv_obj_create(nullptr);
  if (!screen_) return false;
  lv_obj_remove_style_all(screen_);
  lv_obj_set_size(screen_, 240, 280);
  lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(screen_, lv_color_hex(kBackground), 0);
  lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);
  title_ = label(screen_, 24, 20, 192, &lv_font_montserrat_12, kMuted, "");
  label(screen_, 24, 41, 192, &lv_font_montserrat_20, kWhite, "Paseo");
  status_ = label(screen_, 24, 71, 192, &lv_font_montserrat_12, kMuted, "");
  speed_ = label(screen_, 20, 91, 200, &lv_font_montserrat_48, kWhite, "");
  lv_obj_set_style_text_align(speed_, LV_TEXT_ALIGN_CENTER, 0);
  auto *units = label(screen_, 24, 143, 192, &lv_font_montserrat_12, kMuted, "km/h");
  lv_obj_set_style_text_align(units, LV_TEXT_ALIGN_CENTER, 0);
  auto *rule = lv_obj_create(screen_);
  lv_obj_remove_style_all(rule);
  lv_obj_set_pos(rule, 24, 168);
  lv_obj_set_size(rule, 192, 1);
  lv_obj_set_style_bg_color(rule, lv_color_hex(kRule), 0);
  lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
  lv_obj_clear_flag(rule, LV_OBJ_FLAG_SCROLLABLE);
  label(screen_, 24, 180, 192, &lv_font_montserrat_12, kMuted, "Dist. dia registrado");
  distance_ = label(screen_, 24, 197, 192, &lv_font_montserrat_20, kWhite, "");
  date_ = label(screen_, 24, 224, 192, &lv_font_montserrat_12, kMuted, "");
  mode_ = label(screen_, 24, 246, 192, &lv_font_montserrat_12, kMuted, "");
  update(view, demo);
  return true;
}

void WalkView::update(const TextView &view, bool demo) {
  if (!screen_) return;
  set_text(title_, demo ? "RGB DOG / DEMO" : "RGB DOG");
  set_text(status_, view.rows[0]);
  set_text(speed_, view.rows[1]);
  set_text(distance_, view.rows[2]);
  set_text(date_, view.rows[3]);
  set_text(mode_, view.rows[4]);
  if (color_ != view.gps_color) {
    color_ = view.gps_color;
    lv_color_t color; color.full = color_;
    lv_obj_set_style_text_color(status_, color, 0);
  }
}
} // namespace display
