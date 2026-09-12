#include "display/walk_view.h"
#include "display/connection_view.h"
#include "display/status_view.h"
#include "display/button.h"
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
    for (uint32_t j = 0; j < i; ++j) {
      lv_area_t other; lv_obj_get_coords(lv_obj_get_child(screen, j), &other);
      const bool separated = area.x2 < other.x1 || other.x2 < area.x1 || area.y2 < other.y1 || other.y2 < area.y1;
      if (!separated) std::cerr << "overlap children " << i << " / " << j << " at y=" << area.y1 << ".." << area.y2 << " / " << other.y1 << ".." << other.y2 << '\n';
      assert(separated);
    }
    if (lv_obj_check_type(obj, &lv_label_class)) {
      lv_point_t size;
      lv_txt_get_size(&size, lv_label_get_text(obj), lv_obj_get_style_text_font(obj, 0),
                     0, 0, lv_obj_get_width(obj), LV_TEXT_FLAG_NONE);
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
  auto *root = lv_obj_create(nullptr);
  lv_obj_remove_style_all(root); lv_obj_set_size(root, 240, 280);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(root, lv_color_hex(0), 0);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
  display::WalkView walk;
  display::DisplaySnapshot sample;
  sample.gps_state = gps::ReceptionState::Searching;
  sample.daily_distance_m = 1842; sample.distance_date = 20260912;
  assert(walk.begin(display::format_view(sample), true, "Portal disponible", root));
  lv_disp_load_scr(root);
  const auto render = [&](const char *name) {
    walk.update(display::format_view(sample), true, "Portal disponible");
    lv_refr_now(disp);
    verify_layout(walk.screen());
    if (name) save(output / (std::string(name) + ".ppm"));
  };
  render("searching");
  assert(contains(walk.screen(), "-- km/h") && contains(walk.screen(), "RGB DOG / DEMO"));
  sample.gps_state = gps::ReceptionState::Fix; sample.speed_valid = true; sample.speed_kph = 7.2f;
  render("fix"); assert(contains(walk.screen(), "7.2 km/h"));
  const auto before = flushes;
  render(nullptr); assert(flushes == before); // Identical values do not invalidate.
  lv_mem_monitor_t baseline; lv_mem_monitor(&baseline);
  sample.gps_state = gps::ReceptionState::Stale;
  render("stale");
  assert(contains(walk.screen(), "-- km/h") && contains(walk.screen(), "1.84"));
  sample.gps_state = gps::ReceptionState::Fix; sample.speed_kph = 0;
  render(nullptr); assert(contains(walk.screen(), "0.0 km/h"));
  for (unsigned i = 0; i < 120; ++i) {
    sample.gps_state = static_cast<gps::ReceptionState>(i % 6);
    sample.speed_kph = i % 2 ? 0 : std::numeric_limits<float>::quiet_NaN();
    render(nullptr);
    assert(contains(walk.screen(), sample.gps_state == gps::ReceptionState::Fix && i % 2 ? "0.0 km/h" : "-- km/h"));
  }
  sample.gps_state = gps::ReceptionState::Fix; sample.speed_kph = 10000;
  sample.daily_distance_m = 1e20f; sample.distance_date = 0;
  render(nullptr);
  assert(contains(walk.screen(), "999+ km/h") && contains(walk.screen(), ">999"));
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
  display::ConnectionSnapshot connection;
  connection.ap_enabled = true;
  snprintf(connection.ap_ssid, sizeof(connection.ap_ssid), "%s", "DogRGB"); snprintf(connection.ap_ip, sizeof(connection.ap_ip), "%s", "192.168.4.1");
  display::ConnectionView networks;
  assert(networks.begin(display::format_connection(connection), false, root));
  lv_obj_add_flag(walk.screen(), LV_OBJ_FLAG_HIDDEN);
  const auto render_connection = [&](const char *name) {
    networks.update(display::format_connection(connection), false);
    lv_refr_now(disp); verify_layout(networks.screen());
    if (name) save(output / (std::string(name) + ".ppm"));
  };
  render_connection("connection-ap");
  assert(contains(networks.screen(), "Portal disponible"));
  assert(contains(networks.screen(), "192.168.4.1"));
  const auto unchanged = flushes; render_connection(nullptr); assert(flushes == unchanged);
  connection.sta_connected = true; connection.clients = 1;
  snprintf(connection.sta_ssid, sizeof(connection.sta_ssid), "%s", "Casa"); snprintf(connection.sta_ip, sizeof(connection.sta_ip), "%s", "192.168.1.42");
  render_connection("connection-both");
  assert(contains(networks.screen(), "Red conectada"));
  assert(contains(networks.screen(), "Portal: 1 conectado"));
  const auto connected_text = display::format_connection(connection);
  assert(strcmp(connected_text.sta_address, "192.168.1.42") == 0);
  connection.sta_connected = false; connection.sta_connecting = true;
  render_connection("connection-trying");
  assert(!contains(networks.screen(), "192.168.1.42"));
  connection.sta_connecting = false;
  render_connection(nullptr); assert(contains(networks.screen(), "Red no conectada"));
  connection.ap_enabled = false;
  render_connection("connection-idle");
  assert(!contains(networks.screen(), "192.168.4.1"));
  assert(!contains(networks.screen(), "Wi-Fi apagado"));
  connection.radio_off = true;
  render_connection("connection-off"); assert(contains(networks.screen(), "Wi-Fi apagado"));
  connection.radio_off = false; connection.ap_enabled = true; connection.sta_connected = true;
  connection.clients = 255;
  memset(connection.ap_ssid, 'W', 32); connection.ap_ssid[32] = 0;
  memset(connection.sta_ssid, 'W', 32); connection.sta_ssid[32] = 0;
  render_connection("connection-long");
  assert(contains(networks.screen(), "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW"));
  snprintf(connection.ap_ssid, sizeof(connection.ap_ssid), "%s", "Red\\nFalsa"); connection.ap_ssid[3] = '\n';
  render_connection(nullptr); assert(contains(networks.screen(), "Nombre no compatible"));
  snprintf(connection.ap_ssid, sizeof(connection.ap_ssid), "%s", "Caf\xc3\xa9");
  render_connection(nullptr); assert(contains(networks.screen(), "Nombre no compatible"));
  // Status uses typed reception and effective LED policy; no storage-health guess.
  display::StatusView status;
  display::LedStatusSnapshot lights;
  lights.transport_enabled = true; lights.body_enabled = true;
  sample.gps_state = gps::ReceptionState::NoData;
  assert(status.begin(display::format_status(sample.gps_state, lights), false, root));
  lv_obj_add_flag(networks.screen(), LV_OBJ_FLAG_HIDDEN);
  const auto render_status = [&](const char *name) {
    status.update(display::format_status(sample.gps_state, lights), false);
    lv_refr_now(disp); verify_layout(status.screen());
    if (name) save(output / (std::string(name) + ".ppm"));
  };
  render_status("status-no-data");
  assert(contains(status.screen(), "Sin datos") && contains(status.screen(), "Esperando posicion"));
  const auto status_unchanged = flushes; render_status(nullptr); assert(flushes == status_unchanged);
  sample.gps_state = gps::ReceptionState::Fix;
  lights.intent = led::LedIntent::Range;
  render_status("status-fix"); assert(contains(status.screen(), "Posicion confiable"));
  sample.gps_state = gps::ReceptionState::Stale;
  lights.intent = led::LedIntent::DayStatus; lights.body_enabled = false;
  render_status("status-day");
  assert(contains(status.screen(), "Datos vencidos") && contains(status.screen(), "Efectos apagados"));
  lights.alert = led::LedAlert::System;
  render_status("status-alert");
  assert(contains(status.screen(), "Modo dia") && contains(status.screen(), "Aviso GPS / Wi-Fi"));
  lights.transport_enabled = false;
  render_status("status-paused"); assert(contains(status.screen(), "Salida pausada"));
  lv_mem_monitor_t status_before; lv_mem_monitor(&status_before);
  for (unsigned i = 0; i < 120; ++i) {
    sample.gps_state = static_cast<gps::ReceptionState>(i % 6);
    lights.intent = static_cast<led::LedIntent>(i % 9);
    lights.mode = static_cast<led::LedMode>(i % 4);
    lights.alert = static_cast<led::LedAlert>(i % 3);
    lights.transport_enabled = i % 2; lights.body_enabled = i % 3;
    render_status(nullptr);
  }
  sample.gps_state = gps::ReceptionState::Stale;
  lights = {}; lights.intent = led::LedIntent::DayStatus; lights.alert = led::LedAlert::System;
  render_status(nullptr);
  lv_mem_monitor_t status_after; lv_mem_monitor(&status_after);
  assert(status_before.free_size == status_after.free_size);
  lights.intent = static_cast<led::LedIntent>(255); lights.mode = static_cast<led::LedMode>(255);
  lights.alert = static_cast<led::LedAlert>(255); lights.transport_enabled = true; lights.body_enabled = true;
  sample.gps_state = static_cast<gps::ReceptionState>(255);
  render_status(nullptr);
  assert(contains(status.screen(), "Estado desconocido") && contains(status.screen(), "Aviso no identificado"));
  status.update(display::format_status(sample.gps_state, lights), true);
  lv_refr_now(disp); verify_layout(status.screen()); assert(contains(status.screen(), "RGB DOG / GPS DEMO"));
  assert(!contains(status.screen(), "Guardado"));
  display::ReleaseButton button;
  button.begin(true, 0); assert(!button.update(false, 10)); assert(!button.update(false, 50));
  button.begin(false, UINT32_MAX - 100);
  assert(!button.update(true, UINT32_MAX - 60)); assert(!button.update(true, UINT32_MAX - 20));
  assert(!button.update(false, 20)); assert(button.update(false, 60)); assert(!button.update(false, 100));
  std::cout << "LVGL " << LVGL_VERSION_MAJOR << '.' << LVGL_VERSION_MINOR << '.' << LVGL_VERSION_PATCH
            << ": fourteen captures, non-overlapping bounds, unchanged updates, validity, retained distance, modes, demo isolation passed; "
            << "pool_free=" << after.free_size << " largest=" << after.free_biggest_size << '\n';
}
