#include "display/contact_qr.h"
#include <string.h>

namespace display {
bool ContactQr::begin(lv_obj_t *parent) {
  if (canvas_) return true;
  canvas_ = lv_canvas_create(parent);
  if (!canvas_) return false;
  lv_obj_remove_style_all(canvas_);
  lv_obj_clear_flag(canvas_, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(canvas_, LV_OBJ_FLAG_HIDDEN);
  return true;
}
ContactResult ContactQr::update(const char *phone, QrContactKind kind) {
  const auto next = format_contact(phone, kind);
  if (!canvas_) return ContactResult::NotInitialized;
  if (next.result == contact_.result && !strcmp(next.phone, contact_.phone) &&
      !strcmp(next.payload, contact_.payload)) return contact_.result;
  contact_ = next;
  modules_ = 0;
  if (next.result != ContactResult::Ready) {
    lv_obj_add_flag(canvas_, LV_OBJ_FLAG_HIDDEN);
    return next.result;
  }
  const auto length = strlen(next.payload);
  memcpy(scratch_, next.payload, length);
  ++generations_;
  if (!qrcodegen_encodeBinary(scratch_, length, matrix_, qrcodegen_Ecc_MEDIUM,
                             1, 3, qrcodegen_Mask_AUTO, true)) {
    contact_.result = ContactResult::EncodeFailed;
    lv_obj_add_flag(canvas_, LV_OBJ_FLAG_HIDDEN);
    return contact_.result;
  }
  modules_ = qrcodegen_getSize(matrix_);
  const int side = (modules_ + 2 * kQuietModules) * kScale;
  // Palette 0 = white, 1 = black. Fill padding and quiet zone white once.
  memset(pixels_, 0, sizeof(pixels_));
  lv_canvas_set_buffer(canvas_, pixels_, side, side, LV_IMG_CF_INDEXED_1BIT);
  lv_canvas_set_palette(canvas_, 0, lv_color_white());
  lv_canvas_set_palette(canvas_, 1, lv_color_black());
  auto *image = lv_canvas_get_img(canvas_);
  lv_color_t index{}; index.full = 1;
  for (int y = 0; y < modules_; ++y)
    for (int x = 0; x < modules_; ++x)
      if (qrcodegen_getModule(matrix_, x, y))
        for (int dy = 0; dy < kScale; ++dy)
          for (int dx = 0; dx < kScale; ++dx)
            lv_img_buf_set_px_color(image, (x + kQuietModules) * kScale + dx,
                                    (y + kQuietModules) * kScale + dy, index);
  lv_img_cache_invalidate_src(image);
  lv_obj_clear_flag(canvas_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_invalidate(canvas_);
  return contact_.result;
}
void ContactQr::end() {
  if (canvas_) lv_obj_del(canvas_);
  canvas_ = nullptr;
  contact_ = ContactText{};
  modules_ = 0;
}
}
