#include "display/status_view.h"
#include <string.h>

namespace display {
namespace {
lv_obj_t *label(lv_obj_t *parent, int x, int y, int width,
                const lv_font_t *font, uint32_t color, const char *text = "") {
  auto *obj = lv_label_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_set_pos(obj, x - (lv_obj_get_parent(parent) ? 24 : 0),
                     y - (lv_obj_get_parent(parent) ? 20 : 0));
  lv_obj_set_width(obj, width);
  lv_obj_set_style_text_font(obj, font, 0);
  lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
  lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
  lv_label_set_text(obj, text);
  return obj;
}
void set(lv_obj_t *obj, const char *text) {
  if (strcmp(lv_label_get_text(obj), text)) lv_label_set_text(obj, text);
}
void color(lv_obj_t *obj, uint32_t hex) {
  const auto value = lv_color_hex(hex);
  if (lv_obj_get_style_text_color(obj, LV_PART_MAIN).full != value.full)
    lv_obj_set_style_text_color(obj, value, 0);
}
}
bool StatusView::begin(const StatusText &text, bool demo, lv_obj_t *parent) {
  if (screen_) return true;
  screen_ = lv_obj_create(parent);
  if (!screen_) return false;
  lv_obj_remove_style_all(screen_);
  lv_obj_set_size(screen_, parent ? 192 : 240, parent ? 244 : 280);
  if (parent) lv_obj_set_pos(screen_, 24, 20);
  lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(screen_, lv_color_hex(0), 0);
  lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);
  title_ = label(screen_, 24, 20, 164, &lv_font_montserrat_12, 0xB8B8B8);
  label(screen_, 192, 20, 24, &lv_font_montserrat_12, 0xB8B8B8, "3/3");
  label(screen_, 24, 41, 192, &lv_font_montserrat_20, 0xFFFFFF, "Estado");
  label(screen_, 24, 76, 192, &lv_font_montserrat_12, 0xB8B8B8, "GPS");
  gps_status_ = label(screen_, 24, 96, 192, &lv_font_montserrat_14, 0xFFFFFF);
  gps_detail_ = label(screen_, 24, 120, 192, &lv_font_montserrat_12, 0xB8B8B8);
  auto *rule = lv_obj_create(screen_);
  lv_obj_remove_style_all(rule); lv_obj_set_pos(rule, parent ? 0 : 24, parent ? 138 : 158);
  lv_obj_set_size(rule, 192, 1); lv_obj_set_style_bg_color(rule, lv_color_hex(0x303030), 0);
  lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0); lv_obj_clear_flag(rule, LV_OBJ_FLAG_SCROLLABLE);
  label(screen_, 24, 170, 192, &lv_font_montserrat_12, 0xB8B8B8, "Luces / control");
  led_status_ = label(screen_, 24, 190, 192, &lv_font_montserrat_14, 0xFFFFFF);
  led_detail_ = label(screen_, 24, 214, 192, &lv_font_montserrat_12, 0xB8B8B8);
  led_notice_ = label(screen_, 24, 240, 192, &lv_font_montserrat_12, 0xB8B8B8);
  update(text, demo);
  return true;
}
void StatusView::update(const StatusText &text, bool demo) {
  if (!screen_) return;
  set(title_, demo ? "RGB DOG / GPS DEMO" : "RGB DOG");
  set(gps_status_, text.gps_status); set(gps_detail_, text.gps_detail);
  set(led_status_, text.led_status); set(led_detail_, text.led_detail); set(led_notice_, text.led_notice);
  color(gps_status_, text.gps_valid ? 0x50EFAB : 0xFFB547);
  color(led_notice_, text.led_alert ? 0xFFB547 : 0xB8B8B8);
}
} // namespace display
