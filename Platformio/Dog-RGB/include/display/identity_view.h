#pragma once
#include "display/contact_qr.h"
#include "display/identity.h"

extern "C" {
LV_FONT_DECLARE(dog_name_28);
LV_FONT_DECLARE(dog_phone_18);
}
namespace display {
enum class IdentityLayout { Qr, Text };
// Persistent owner; call end() before deleting the parent, as with ContactQr.
class IdentityView {
 public:
  bool begin(lv_obj_t *parent);
  void update(const char *name, const char *phone, QrContactKind kind);
  void end();
  lv_obj_t *screen() const { return screen_; }
  IdentityLayout layout() const { return layout_; }
  const PetName &name() const { return name_; }
  const ContactText &contact() const { return contact_; }
  unsigned qr_generations() const { return qr_.generations(); }
 private:
  lv_obj_t *screen_ = nullptr, *title_ = nullptr, *name_label_ = nullptr;
  lv_obj_t *phone_label_ = nullptr, *hint_ = nullptr;
  ContactQr qr_;
  PetName name_{};
  ContactText contact_{};
  QrContactKind kind_ = QrContactKind::Disabled;
  IdentityLayout layout_ = IdentityLayout::Text;
  bool initialized_ = false;
};
}
