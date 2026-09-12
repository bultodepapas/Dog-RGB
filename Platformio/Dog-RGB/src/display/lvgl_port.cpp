#if DOG_RGB_DISPLAY_LVGL == 1
#include "display/lvgl_port.h"
#include "display/walk_view.h"
#include "display/connection_view.h"
#include "display/status_view.h"
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
ConnectionView connection_view;
StatusView status_view;
Page selected = Page::Activity;
Page visible = Page::Activity;
bool margins_dirty = false;
lv_obj_t *root = nullptr;
uint32_t previous_ms = 0, handler_ms = 0;
Stats measured;

lv_obj_t *screen(Page page) {
  return page == Page::Activity ? walk.screen() : page == Page::Connection ? connection_view.screen() : status_view.screen();
}

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

bool begin(Arduino_ST7789 &target, const TextView &view, bool demo, const ConnectionText &connection, const LedStatusSnapshot &led) {
  if (display) return true;
  panel = &target;
  lv_init();
  lv_disp_draw_buf_init(&draw_buffer, pixels, nullptr, 240 * 20);
  lv_disp_drv_init(&driver);
  driver.hor_res = 240; driver.ver_res = 280;
  driver.draw_buf = &draw_buffer;
  driver.flush_cb = flush;
  display = lv_disp_drv_register(&driver);
  if (!display) return false;
  root = lv_obj_create(nullptr);
  if (!root) return false;
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, 240, 280);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
  if (!walk.begin(view, demo, connection.summary, root) || !connection_view.begin(connection, demo, root) ||
      !status_view.begin(format_status(view.gps_state, led), demo, root)) return false;
  lv_obj_add_flag(connection_view.screen(), LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(status_view.screen(), LV_OBJ_FLAG_HIDDEN);
  lv_disp_load_scr(root); // First frame initializes the entire panel before backlight.
  previous_ms = handler_ms = millis();
  tick(previous_ms, true);
  return true;
}

void select_page(Page value) { selected = value; }
Page page() { return selected; }
void external_draw() { margins_dirty = true; }
bool restore_margins() {
  if (!margins_dirty) return false;
  // Separate cooperative service step when returning from full-panel diagnostics.
  panel->fillRect(0, 0, 240, 20, 0x0000);
  panel->fillRect(0, 264, 240, 16, 0x0000);
  panel->fillRect(0, 20, 24, 244, 0x0000);
  panel->fillRect(216, 20, 24, 244, 0x0000);
  margins_dirty = false;
  return true;
}
void update(const TextView &view, bool demo, const ConnectionText &connection, const LedStatusSnapshot &led) {
  if (selected == Page::Activity) walk.update(view, demo, connection.summary);
  else if (selected == Page::Connection) connection_view.update(connection, demo);
  else status_view.update(format_status(view.gps_state, led), demo);
  if (visible != selected) {
    lv_obj_add_flag(screen(visible), LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(screen(selected), LV_OBJ_FLAG_HIDDEN);
    visible = selected;
  }
}
bool tick(uint32_t now_ms, bool full_redraw) {
  if (!display) return false;
  lv_tick_inc(now_ms - previous_ms); // Unsigned rollover; no second tick task.
  previous_ms = now_ms;
  const uint32_t before = measured.flushes;
  if (full_redraw) {
    lv_obj_invalidate(screen(selected));
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
