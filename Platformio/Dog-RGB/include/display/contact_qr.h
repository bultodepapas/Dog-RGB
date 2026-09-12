#pragma once
#include "display/contact.h"
#include <lvgl.h>
#include <src/extra/libs/qrcode/qrcodegen.h>

namespace display {
// Owns backing storage; use persistent/static storage on the MCU, not a loop
// stack temporary. Must outlive its LVGL object; call end before deleting
// the parent. Noncopyable because the canvas points into this instance.
class ContactQr {
 public:
  static constexpr int kScale = 4, kQuietModules = 4, kMaxModules = 29;
  static constexpr int kMaxSide = (kMaxModules + 2 * kQuietModules) * kScale;
  static constexpr size_t kEncoderBytes = qrcodegen_BUFFER_LEN_FOR_VERSION(3);
  static constexpr size_t kCanvasBytes = LV_CANVAS_BUF_SIZE_INDEXED_1BIT(kMaxSide, kMaxSide);
  ContactQr() = default;
  ContactQr(const ContactQr &) = delete;
  ContactQr &operator=(const ContactQr &) = delete;
  bool begin(lv_obj_t *parent);
  ContactResult update(const char *phone, QrContactKind kind);
  void end();
  lv_obj_t *object() const { return canvas_; }
  const ContactText &contact() const { return contact_; }
  int modules() const { return modules_; }
  unsigned generations() const { return generations_; }
 private:
  lv_obj_t *canvas_ = nullptr;
  alignas(4) uint8_t pixels_[kCanvasBytes]{};
  uint8_t scratch_[kEncoderBytes]{}, matrix_[kEncoderBytes]{};
  ContactText contact_{};
  int modules_ = 0;
  unsigned generations_ = 0;
};
}
