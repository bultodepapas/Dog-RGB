#include "display/status_view.h"
#include <string.h>

namespace display {
using namespace ui;
bool StatusView::begin(const StatusText &text, bool demo, lv_obj_t *parent) {
  if (screen_) return true;
  screen_ = page(parent);
  if (!screen_) return false;
  header_.begin(screen_, "3/3");
  label(screen_, 24, 41, 192, &lv_font_montserrat_20, kWhite, "Estado");
  label(screen_, 24, 76, 192, &lv_font_montserrat_12, kMuted, "GPS");
  gps_status_ = label(screen_, 24, 96, 192, &lv_font_montserrat_14, kWhite);
  gps_detail_ = label(screen_, 24, 120, 192, &lv_font_montserrat_12, kMuted);
  rule(screen_, 158);
  label(screen_, 24, 170, 192, &lv_font_montserrat_12, kMuted, "Luces / control");
  led_status_ = label(screen_, 24, 190, 192, &lv_font_montserrat_14, kWhite);
  led_detail_ = label(screen_, 24, 214, 192, &lv_font_montserrat_12, kMuted);
  led_notice_ = label(screen_, 24, 240, 192, &lv_font_montserrat_12, kMuted);
  update(text, demo);
  return true;
}
void StatusView::set_page_indicator(const char *text) {
  header_.set_index(text);
}
void StatusView::update(const StatusText &text, bool demo) {
  if (!screen_) return;
  header_.update(demo);
  ui::text(gps_status_, text.gps_status); ui::text(gps_detail_, text.gps_detail);
  ui::text(led_status_, text.led_status); ui::text(led_detail_, text.led_detail); ui::text(led_notice_, text.led_notice);
  color(gps_status_, text.gps_valid ? kValid : kAttention);
  color(led_notice_, text.led_alert ? kAttention : kMuted);
}
} // namespace display
