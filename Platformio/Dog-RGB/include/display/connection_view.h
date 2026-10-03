#pragma once
#include <lvgl.h>
#include "display/ui_theme.h"
#include "display/connection.h"

namespace display {
class ConnectionView {
 public:
  bool begin(const ConnectionText &text, bool demo, lv_obj_t *parent = nullptr);
  void update(const ConnectionText &text, bool demo);
  void set_page_indicator(const char *text);
  void set_pet_name(const char *name) { header_.set_name(name); }
  lv_obj_t *screen() const { return screen_; }
 private:
  ui::PageHeader header_;
  lv_obj_t *screen_ = nullptr;
  lv_obj_t *ap_status_ = nullptr, *ap_name_ = nullptr, *ap_address_ = nullptr;
  lv_obj_t *sta_status_ = nullptr, *sta_name_ = nullptr, *sta_address_ = nullptr;
};
} // namespace display
