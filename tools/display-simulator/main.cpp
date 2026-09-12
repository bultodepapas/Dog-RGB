#include "display/walk_view.h"
#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

namespace {
constexpr int kWidth = 240, kHeight = 280;
std::array<uint16_t, kWidth * kHeight> frame{};
lv_color_t buffer[kWidth * 20];
lv_disp_draw_buf_t draw_buffer;
lv_disp_drv_t driver;
unsigned flushes = 0;
void flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *colors) {
  assert(area->x1 >= 0 && area->y1 >= 0 && area->x2 < kWidth && area->y2 < kHeight);
  assert(lv_area_get_size(area) <= kWidth * 20);
  for (int y = area->y1; y <= area->y2; ++y)
    for (int x = area->x1; x <= area->x2; ++x) frame[y * kWidth + x] = (colors++)->full;
  ++flushes;
  lv_disp_flush_ready(drv);
}
void save(const std::filesystem::path &path) {
  std::ofstream out(path, std::ios::binary);
  out << "P6\n240 280\n255\n";
  for (uint16_t pixel : frame) {
    const unsigned r = (pixel >> 11) & 31, g = (pixel >> 5) & 63, b = pixel & 31;
    const char rgb[] = {static_cast<char>((r << 3) | (r >> 2)),
                        static_cast<char>((g << 2) | (g >> 4)),
                        static_cast<char>((b << 3) | (b >> 2))};
    out.write(rgb, 3);
  }
  assert(out.good());
}
void verify_layout(lv_obj_t *screen) {
  lv_obj_update_layout(screen);
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(screen); ++i) {
    auto *obj = lv_obj_get_child(screen, i);
    lv_area_t area; lv_obj_get_coords(obj, &area);
    assert(area.x1 >= 20 && area.x2 < 220 && area.y1 >= 20 && area.y2 < 264);
    if (lv_obj_check_type(obj, &lv_label_class)) {
      lv_point_t size;
      lv_txt_get_size(&size, lv_label_get_text(obj), lv_obj_get_style_text_font(obj, 0),
                     0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
      assert(size.x <= lv_obj_get_width(obj)); // No hidden horizontal truncation.
      assert(size.y <= lv_obj_get_height(obj));
    }
  }
}
bool contains(lv_obj_t *screen, const char *value) {
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(screen); ++i) {
    auto *obj = lv_obj_get_child(screen, i);
    if (lv_obj_check_type(obj, &lv_label_class) && strcmp(lv_label_get_text(obj), value) == 0) return true;
  }
  return false;
}
}

int main(int argc, char **argv) {
  assert(argc == 2);
  const std::filesystem::path output = argv[1];
  std::filesystem::create_directories(output);
  lv_init();
  lv_disp_draw_buf_init(&draw_buffer, buffer, nullptr, kWidth * 20);
  lv_disp_drv_init(&driver);
  driver.hor_res = kWidth; driver.ver_res = kHeight;
  driver.draw_buf = &draw_buffer; driver.flush_cb = flush;
  auto *disp = lv_disp_drv_register(&driver);
  assert(disp);
  display::WalkView walk;
  display::DisplaySnapshot sample;
  sample.gps_state = gps::ReceptionState::Searching;
  sample.daily_distance_m = 1842; sample.distance_date = 20260912;
  assert(walk.begin(display::format_view(sample), true));
  lv_disp_load_scr(walk.screen());
  const auto render = [&](const char *name) {
    walk.update(display::format_view(sample), true);
    lv_refr_now(disp);
    verify_layout(walk.screen());
    if (name) save(output / (std::string(name) + ".ppm"));
  };
  render("searching");
  assert(contains(walk.screen(), "--") && contains(walk.screen(), "RGB DOG / DEMO"));
  sample.gps_state = gps::ReceptionState::Fix; sample.speed_valid = true; sample.speed_kph = 7.2f;
  render("fix"); assert(contains(walk.screen(), "7.2"));
  const auto before = flushes;
  render(nullptr); assert(flushes == before); // Identical values do not invalidate.
  lv_mem_monitor_t baseline; lv_mem_monitor(&baseline);
  sample.gps_state = gps::ReceptionState::Stale;
  render("stale");
  assert(contains(walk.screen(), "--") && contains(walk.screen(), "1842 m"));
  sample.gps_state = gps::ReceptionState::Fix; sample.speed_kph = 0;
  render(nullptr); assert(contains(walk.screen(), "0.0"));
  for (unsigned i = 0; i < 120; ++i) {
    sample.gps_state = static_cast<gps::ReceptionState>(i % 6);
    sample.speed_kph = i % 2 ? 0 : std::numeric_limits<float>::quiet_NaN();
    render(nullptr);
    assert(contains(walk.screen(), sample.gps_state == gps::ReceptionState::Fix && i % 2 ? "0.0" : "--"));
  }
  sample.gps_state = gps::ReceptionState::Fix; sample.speed_kph = 10000;
  sample.daily_distance_m = 1e20f; sample.distance_date = 0;
  render(nullptr);
  assert(contains(walk.screen(), "999+") && contains(walk.screen(), ">999 km"));
  for (auto mode : {led::LedMode::Speed, led::LedMode::Geofence, led::LedMode::Show, led::LedMode::Simple}) {
    sample.led_mode = mode; render(nullptr);
  }
  sample.led_mode = led::LedMode::Speed; sample.speed_kph = 7.2f;
  sample.daily_distance_m = 1842; sample.distance_date = 20260912;
  render(nullptr);
  lv_mem_monitor_t after; lv_mem_monitor(&after);
  assert(after.free_size == baseline.free_size && after.free_biggest_size >= 16000);
  walk.update(display::format_view(sample), false);
  lv_refr_now(disp);
  assert(contains(walk.screen(), "RGB DOG") && !contains(walk.screen(), "RGB DOG / DEMO"));
  std::cout << "LVGL " << LVGL_VERSION_MAJOR << '.' << LVGL_VERSION_MINOR << '.' << LVGL_VERSION_PATCH
            << ": three captures, bounds, unchanged updates, validity, retained distance, modes, demo isolation passed; "
            << "pool_free=" << after.free_size << " largest=" << after.free_biggest_size << '\n';
}
