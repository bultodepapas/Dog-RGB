#include "display/ui_theme.h"
#include <string.h>

namespace display::ui {
lv_obj_t *page(lv_obj_t *parent) {
  auto *obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_set_size(obj, parent ? 192 : 240, parent ? 244 : 280);
  if (parent) lv_obj_set_pos(obj, 24, 20);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(obj, lv_color_hex(kBlack), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  return obj;
}
lv_obj_t *label(lv_obj_t *parent, int x, int y, int width, const lv_font_t *font,
                uint32_t value, const char *content) {
  auto *obj = lv_label_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_set_pos(obj, x - (lv_obj_get_parent(parent) ? 24 : 0), y - (lv_obj_get_parent(parent) ? 20 : 0));
  lv_obj_set_width(obj, width);
  lv_obj_set_style_text_font(obj, font, 0);
  lv_obj_set_style_text_color(obj, lv_color_hex(value), 0);
  lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
  lv_label_set_text(obj, content);
  return obj;
}
void text(lv_obj_t *obj, const char *value) {
  if (strcmp(lv_label_get_text(obj), value)) lv_label_set_text(obj, value);
}
void color(lv_obj_t *obj, uint32_t value) {
  const auto next = lv_color_hex(value);
  if (lv_obj_get_style_text_color(obj, 0).full != next.full) lv_obj_set_style_text_color(obj, next, 0);
}
void rule(lv_obj_t *parent, int y) {
  auto *obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_set_pos(obj, lv_obj_get_parent(parent) ? 0 : 24, y - (lv_obj_get_parent(parent) ? 20 : 0));
  lv_obj_set_size(obj, 192, 1);
  lv_obj_set_style_bg_color(obj, lv_color_hex(kRule), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}
void PageHeader::begin(lv_obj_t *parent, const char *index) {
  name_ = label(parent, 24, 20, 164, &dog_name_14, kIdentity);
  demo_label_ = label(parent, 120, 22, 68, &lv_font_montserrat_12, kAttention, "GPS DEMO");
  lv_obj_add_flag(demo_label_, LV_OBJ_FLAG_HIDDEN);
  index_ = label(parent, 192, 22, 24, &lv_font_montserrat_12, kMuted, index);
  refresh();
}
void PageHeader::set_name(const char *name) {
  const auto next = format_pet_name(name);
  if (pet_.result == next.result && !strcmp(pet_.text, next.text)) return;
  pet_ = next;
  if (name_) refresh();
}
void PageHeader::set_index(const char *index) { if (index_) text(index_, index); }
void PageHeader::update(bool demo) {
  if (!name_ || demo_ == demo) return;
  demo_ = demo;
  lv_obj_set_width(name_, demo ? 90 : 164);
  if (demo) lv_obj_clear_flag(demo_label_, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(demo_label_, LV_OBJ_FLAG_HIDDEN);
  refresh();
}
void PageHeader::refresh() {
  char visible[53];
  strcpy(visible, pet_.result == NameResult::Ready ? pet_.text : "RGB DOG");
  const int width = demo_ ? 90 : 164;
  const auto fits = [&]() {
    lv_point_t size;
    lv_txt_get_size(&size, visible, &dog_name_14, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x <= width;
  };
  if (!fits()) {
    size_t end = strlen(visible);
    do {
      do { --end; } while (end && (static_cast<unsigned char>(visible[end]) & 0xc0) == 0x80);
      strcpy(visible + end, "...");
    } while (end && !fits());
  }
  text(name_, visible);
}
}
