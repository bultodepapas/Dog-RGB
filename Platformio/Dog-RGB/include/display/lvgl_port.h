#pragma once
#include "display/text_view.h"
#include "display/connection.h"
#include "display/status.h"
#include "display/identity_snapshot.h"
class Arduino_ST7789;
namespace display::lvgl_port {
enum class Page : uint8_t { Activity, Connection, Status, Identity };
constexpr Page next_page(Page page, bool identity_available = false) {
  return page == Page::Activity ? Page::Connection : page == Page::Connection ? Page::Status :
      page == Page::Status && identity_available ? Page::Identity : Page::Activity;
}
constexpr const char *page_name(Page page) {
  return page == Page::Activity ? "activity" : page == Page::Connection ? "connection" :
      page == Page::Status ? "status" : "identity";
}
struct Stats {
  uint32_t flushes = 0, pixels = 0, flush_max_us = 0;
  uint32_t free_bytes = 0, largest_free = 0;
  uint32_t qr_generations = 0;
};
bool begin(Arduino_ST7789 &panel, const TextView &view, bool demo, const ConnectionText &connection,
           const LedStatusSnapshot &led, const IdentitySnapshot &identity);
void set_identity(const IdentitySnapshot &identity);
bool identity_available();
void update(const TextView &view, bool demo, const ConnectionText &connection, const LedStatusSnapshot &led);
void select_page(Page value);
Page page();
void external_draw();
bool restore_margins();
bool tick(uint32_t now_ms, bool full_redraw = false);
void reset_stats();
Stats stats();
} // namespace display::lvgl_port
