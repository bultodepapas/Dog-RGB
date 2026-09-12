#if DOG_RGB_DISPLAY_LVGL == 1
#include "display/lvgl_port.h"
#include "display/walk_view.h"
#include <Arduino_GFX_Library.h>

namespace display::lvgl_port {
namespace {
Arduino_ST7789 *panel = nullptr;
lv_disp_t *display = nullptr;
lv_disp_draw_buf_t draw_buffer;
lv_disp_drv_t driver;
alignas(4) lv_color_t pixels[240 * 20];
static_assert(sizeof(pixels) == 9600, "RGB565 partial buffer required");
WalkView walk;
uint32_t previous_ms = 0, handler_ms = 0;
Stats measured;

void flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *colors) {
  const uint32_t started = micros();
  const int w = area->x2 - area->x1 + 1, h = area->y2 - area->y1 + 1;
  // Single synchronous SPI owner. Native RGB565; Arduino_GFX handles wire order.
  panel->draw16bitRGBBitmap(area->x1, area->y1,
      reinterpret_cast<uint16_t *>(colors), w, h);
  const uint32_t elapsed = micros() - started;
  ++measured.flushes;
  measured.pixels += static_cast<uint32_t>(w * h);
  if (elapsed > measured.flush_max_us) measured.flush_max_us = elapsed;
  lv_disp_flush_ready(drv); // Buffer is reusable only after the transfer returned.
}
}

bool begin(Arduino_ST7789 &target, const TextView &view, bool demo) {
  if (display) return true;
  panel = &target;
  lv_init();
  lv_disp_draw_buf_init(&draw_buffer, pixels, nullptr, 240 * 20);
  lv_disp_drv_init(&driver);
  driver.hor_res = 240; driver.ver_res = 280;
  driver.draw_buf = &draw_buffer;
  driver.flush_cb = flush;
  display = lv_disp_drv_register(&driver);
  if (!display || !walk.begin(view, demo)) return false;
  lv_disp_load_scr(walk.screen());
  previous_ms = handler_ms = millis();
  tick(previous_ms, true);
  return true;
}

void update(const TextView &view, bool demo) { walk.update(view, demo); }
bool tick(uint32_t now_ms, bool full_redraw) {
  if (!display) return false;
  lv_tick_inc(now_ms - previous_ms); // Unsigned rollover; no second tick task.
  previous_ms = now_ms;
  const uint32_t before = measured.flushes;
  if (full_redraw) {
    lv_obj_invalidate(walk.screen());
    lv_refr_now(display);
    handler_ms = now_ms;
  } else if (now_ms - handler_ms >= 5) {
    handler_ms = now_ms;
    lv_timer_handler();
  }
  return before != measured.flushes;
}
void reset_stats() { measured = {}; }
Stats stats() {
  lv_mem_monitor_t memory;
  lv_mem_monitor(&memory);
  Stats result = measured;
  result.free_bytes = memory.free_size;
  result.largest_free = memory.free_biggest_size;
  return result;
}
} // namespace display::lvgl_port
#endif
