#include "display/connection_view.h"
#include <string.h>

namespace display {
using namespace ui;
bool ConnectionView::begin(const ConnectionText &text, bool demo, lv_obj_t *parent) {
  if (screen_) return true;
  screen_ = page(parent);
  if (!screen_) return false;
  header_.begin(screen_, "2/3");
  label(screen_, 24, 41, 192, &lv_font_montserrat_20, kWhite, "Wi-Fi");
  ap_status_ = label(screen_, 24, 76, 192, &lv_font_montserrat_14, kWhite);
  ap_name_ = label(screen_, 24, 101, 192, &lv_font_montserrat_12, kMuted);
  ap_address_ = label(screen_, 24, 148, 192, &lv_font_montserrat_14, kWhite);
  rule(screen_, 173);
  sta_status_ = label(screen_, 24, 184, 192, &lv_font_montserrat_14, kWhite);
  sta_name_ = label(screen_, 24, 203, 192, &lv_font_montserrat_12, kMuted);
  sta_address_ = label(screen_, 24, 249, 192, &lv_font_montserrat_12, kWhite);
  update(text, demo);
  return true;
}
void ConnectionView::set_page_indicator(const char *text) {
  header_.set_index(text);
}
void ConnectionView::update(const ConnectionText &text, bool demo) {
  if (!screen_) return;
  // GPS fixture only. Wi-Fi always comes from real radio state on the board.
  header_.update(demo);
  ui::text(ap_status_, text.ap_status); ui::text(ap_name_, text.ap_name); ui::text(ap_address_, text.ap_address);
  ui::text(sta_status_, text.sta_status); ui::text(sta_name_, text.sta_name); ui::text(sta_address_, text.sta_address);
}
} // namespace display
