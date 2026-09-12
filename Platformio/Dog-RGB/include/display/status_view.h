#pragma once
#include <lvgl.h>
#include "display/status.h"

namespace display {
class StatusView {
 public:
  bool begin(const StatusText &text, bool demo, lv_obj_t *parent = nullptr);
  void update(const StatusText &text, bool demo);
  lv_obj_t *screen() const { return screen_; }
 private:
  lv_obj_t *screen_ = nullptr, *title_ = nullptr;
  lv_obj_t *gps_status_ = nullptr, *gps_detail_ = nullptr;
  lv_obj_t *led_status_ = nullptr, *led_detail_ = nullptr, *led_notice_ = nullptr;
};
} // namespace display
