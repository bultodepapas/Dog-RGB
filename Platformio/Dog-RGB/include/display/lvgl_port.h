#pragma once
#include "display/text_view.h"
class Arduino_ST7789;
namespace display::lvgl_port {
struct Stats {
  uint32_t flushes = 0, pixels = 0, flush_max_us = 0;
  uint32_t free_bytes = 0, largest_free = 0;
};
bool begin(Arduino_ST7789 &panel, const TextView &view, bool demo);
void update(const TextView &view, bool demo);
bool tick(uint32_t now_ms, bool full_redraw = false);
void reset_stats();
Stats stats();
} // namespace display::lvgl_port
