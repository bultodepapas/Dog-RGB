#pragma once
#include <lvgl.h>
#include "display/text_view.h"

namespace display {
// Same composition on PC and device. No clock, hardware, input or domain writes.
class WalkView {
 public:
  bool begin(const TextView &view, bool demo);
  void update(const TextView &view, bool demo);
  lv_obj_t *screen() const { return screen_; }
 private:
  lv_obj_t *screen_ = nullptr;
  lv_obj_t *title_ = nullptr;
  lv_obj_t *status_ = nullptr;
  lv_obj_t *speed_ = nullptr;
  lv_obj_t *distance_ = nullptr;
  lv_obj_t *date_ = nullptr;
  lv_obj_t *mode_ = nullptr;
  uint16_t color_ = 0;
};
} // namespace display
