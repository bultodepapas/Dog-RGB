#include "display/identity_view.h"
#include <string.h>

namespace display {
namespace {
lv_obj_t *label(lv_obj_t *parent, int y, const lv_font_t *font, uint32_t color) {
  auto *obj = lv_label_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_set_pos(obj, 0, y); lv_obj_set_width(obj, 192);
  lv_obj_set_style_text_font(obj, font, 0);
  lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
  lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
  return obj;
}
void text(lv_obj_t *label, const char *value) {
  if (strcmp(lv_label_get_text(label), value)) lv_label_set_text(label, value);
}
}
bool IdentityView::begin(lv_obj_t *parent) {
  if (screen_) return true;
  screen_ = lv_obj_create(parent);
  if (!screen_) return false;
  lv_obj_remove_style_all(screen_);
  lv_obj_set_pos(screen_, 24, 20); lv_obj_set_size(screen_, 192, 244);
  lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(screen_, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);
  title_ = label(screen_, 0, &lv_font_montserrat_12, 0xb8b8b8);
  name_label_ = label(screen_, 20, &dog_name_28, 0xffffff);
  phone_label_ = label(screen_, 206, &dog_phone_18, 0xffffff);
  hint_ = label(screen_, 229, &lv_font_montserrat_12, 0xb8b8b8);
  if (!qr_.begin(screen_)) { end(); return false; }
  update(nullptr, nullptr, QrContactKind::Disabled);
  return true;
}
void IdentityView::update(const char *name, const char *phone, QrContactKind kind) {
  if (!screen_) return;
  const auto next_name = format_pet_name(name);
  const auto next_contact = format_contact(phone, kind);
  if (initialized_ && next_name.result == name_.result && !strcmp(next_name.text, name_.text) &&
      next_contact.result == contact_.result && !strcmp(next_contact.phone, contact_.phone) && kind == kind_) return;
  initialized_ = true; name_ = next_name; contact_ = next_contact; kind_ = kind;
  const bool named = name_.result == NameResult::Ready;
  const bool has_phone = contact_.phone[0] != '\0';
  const char *display_name = named ? name_.text : "Sin nombre";
  lv_point_t size;
  lv_txt_get_size(&size, display_name, &dog_name_28, 0, 0, 192, LV_TEXT_FLAG_NONE);
  const bool one_line = size.y <= dog_name_28.line_height;
  bool show_qr = named && one_line && contact_.result == ContactResult::Ready;
  if (show_qr) show_qr = qr_.update(contact_.phone, kind) == ContactResult::Ready;
  layout_ = show_qr ? IdentityLayout::Qr : IdentityLayout::Text;
  text(title_, "MI PLACA");
  text(name_label_, display_name);
  // A: fixed scan layout. B: center up to five name lines above the phone.
  lv_obj_set_y(name_label_, show_qr ? 16 : 36 + (170 - size.y) / 2);
  if (show_qr) {
    const int side = (qr_.modules() + 8) * ContactQr::kScale;
    lv_obj_set_pos(qr_.object(), (192 - side) / 2, 56);
    lv_obj_clear_flag(qr_.object(), LV_OBJ_FLAG_HIDDEN);
  } else lv_obj_add_flag(qr_.object(), LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_text_font(phone_label_, has_phone ? &dog_phone_18 : &lv_font_montserrat_14, 0);
  text(phone_label_, has_phone ? contact_.phone : "Sin telefono");
  text(hint_, name_.result == NameResult::Empty ? "Configura tu placa" : !named ? "Revisa el nombre" : !has_phone ? "Configura el contacto" :
       show_qr ? (kind == QrContactKind::Call ? "Llamar / Mi familia" : "WhatsApp / Mi familia") : "Llama a mi familia");
}
void IdentityView::end() {
  qr_.end();
  if (screen_) lv_obj_del(screen_);
  screen_ = title_ = name_label_ = phone_label_ = hint_ = nullptr;
  initialized_ = false;
}
}
