#pragma once
#include "display/text_view.h"
#include "display/connection.h"
class Arduino_ST7789;
namespace display::lvgl_port {
enum class Page : uint8_t { Activity, Connection };
struct Stats {
  uint32_t flushes = 0, pixels = 0, flush_max_us = 0;
  uint32_t free_bytes = 0, largest_free = 0;
};
bool begin(Arduino_ST7789 &panel, const TextView &view, bool demo, const ConnectionText &connection);
void update(const TextView &view, bool demo, const ConnectionText &connection);
void select_page(Page value);
Page page();
void external_draw();
bool restore_margins();
bool tick(uint32_t now_ms, bool full_redraw = false);
void reset_stats();
Stats stats();
} // namespace display::lvgl_port
