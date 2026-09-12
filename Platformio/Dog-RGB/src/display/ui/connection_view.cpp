#include "display/connection_view.h"
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
}
bool ConnectionView::begin(const ConnectionText &text, bool demo, lv_obj_t *parent) {
  if (screen_) return true;
  screen_ = lv_obj_create(parent);
  if (!screen_) return false;
  lv_obj_remove_style_all(screen_);
  lv_obj_set_size(screen_, parent ? 192 : 240, parent ? 244 : 280);
  if (parent) lv_obj_set_pos(screen_, 24, 20);
  lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(screen_, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);
  title_ = label(screen_, 24, 20, 164, &lv_font_montserrat_12, 0xB8B8B8);
  label(screen_, 192, 20, 24, &lv_font_montserrat_12, 0xB8B8B8, "2/2");
  label(screen_, 24, 41, 192, &lv_font_montserrat_20, 0xFFFFFF, "Wi-Fi");
  ap_status_ = label(screen_, 24, 76, 192, &lv_font_montserrat_14, 0xFFFFFF);
  ap_name_ = label(screen_, 24, 101, 192, &lv_font_montserrat_12, 0xB8B8B8);
  ap_address_ = label(screen_, 24, 148, 192, &lv_font_montserrat_14, 0xFFFFFF);
  auto *rule = lv_obj_create(screen_);
  lv_obj_remove_style_all(rule); lv_obj_set_pos(rule, parent ? 0 : 24, parent ? 153 : 173); lv_obj_set_size(rule, 192, 1);
  lv_obj_set_style_bg_color(rule, lv_color_hex(0x303030), 0);
  lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
  lv_obj_clear_flag(rule, LV_OBJ_FLAG_SCROLLABLE);
  sta_status_ = label(screen_, 24, 184, 192, &lv_font_montserrat_14, 0xFFFFFF);
  sta_name_ = label(screen_, 24, 203, 192, &lv_font_montserrat_12, 0xB8B8B8);
  sta_address_ = label(screen_, 24, 249, 192, &lv_font_montserrat_12, 0xFFFFFF);
  update(text, demo);
  return true;
}
void ConnectionView::update(const ConnectionText &text, bool demo) {
  if (!screen_) return;
  // GPS fixture only. Wi-Fi always comes from real radio state on the board.
  set(title_, demo ? "RGB DOG / GPS DEMO" : "RGB DOG");
  set(ap_status_, text.ap_status); set(ap_name_, text.ap_name); set(ap_address_, text.ap_address);
  set(sta_status_, text.sta_status); set(sta_name_, text.sta_name); set(sta_address_, text.sta_address);
}
} // namespace display
