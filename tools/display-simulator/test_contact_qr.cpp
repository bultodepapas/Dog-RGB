#include "display/contact_qr.h"
#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
constexpr int W = 240, H = 280;
std::array<uint16_t, W * H> frame{};
lv_color_t buffer[W * 20];
unsigned flushes = 0;
void flush(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *colors) {
  assert(a->x1 >= 0 && a->y1 >= 0 && a->x2 < W && a->y2 < H);
  assert(lv_area_get_size(a) <= W * 20);
  for (int y = a->y1; y <= a->y2; ++y)
    for (int x = a->x1; x <= a->x2; ++x) frame[y * W + x] = (colors++)->full;
  ++flushes;
  lv_disp_flush_ready(drv);
}
void save(const std::filesystem::path &path) {
  std::ofstream f(path, std::ios::binary);
  f << "P6\n240 280\n255\n";
  for (auto p : frame) {
    const int r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
    const char rgb[] = {char((r << 3) | (r >> 2)), char((g << 2) | (g >> 4)), char((b << 3) | (b >> 2))};
    f.write(rgb, 3);
  }
  assert(f.good());
}
lv_obj_t *label(lv_obj_t *root, int y, const lv_font_t *font) {
  auto *obj = lv_label_create(root);
  lv_obj_remove_style_all(obj);
  lv_obj_set_pos(obj, 24, y); lv_obj_set_width(obj, 192);
  lv_obj_set_style_text_color(obj, lv_color_white(), 0);
  lv_obj_set_style_text_font(obj, font, 0);
  lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
  return obj;
}
void check_text(lv_obj_t *obj) {
  lv_obj_update_layout(obj);
  lv_point_t size;
  lv_txt_get_size(&size, lv_label_get_text(obj), lv_obj_get_style_text_font(obj, 0), 0, 0, 192, LV_TEXT_FLAG_NONE);
  assert(size.x <= 192 && size.y <= 24);
}
}

int main(int argc, char **argv) {
  assert(argc == 2 || argc == 4); // Optional local name/phone: never fixture defaults.
  using namespace display;
  using Kind = QrContactKind;
  using Result = ContactResult;
  assert(format_contact(nullptr, Kind::Call).result == Result::InvalidPhone);
  for (const char *bad : {"", "3162421", "+123456", "+0123456789", "+1234567890123456", "+1234567x", "++1234567", "+12\t34567", "+1234567 ext 8"})
    assert(format_contact(bad, Kind::Call).result == Result::InvalidPhone);
  const std::string too_long(64, ' ');
  assert(format_contact(too_long.c_str(), Kind::Call).result == Result::InvalidPhone);
  const auto normalized = format_contact(" +1 (000) 000-00000 ", Kind::WhatsApp);
  assert(!strcmp(normalized.phone, "+100000000000"));
  assert(!strcmp(normalized.payload, "https://wa.me/100000000000"));
  assert(format_contact("+100000000000", static_cast<Kind>(99)).result == Result::InvalidKind);
  assert(format_contact("+100000000000", Kind::Disabled).payload[0] == '\0');

  std::filesystem::path output(argv[1]);
  std::filesystem::create_directories(output);
  lv_init();
  lv_disp_draw_buf_t draw;
  lv_disp_draw_buf_init(&draw, buffer, nullptr, W * 20);
  lv_disp_drv_t driver; lv_disp_drv_init(&driver);
  driver.hor_res = W; driver.ver_res = H; driver.draw_buf = &draw; driver.flush_cb = flush;
  auto *disp = lv_disp_drv_register(&driver); assert(disp);
  auto *root = lv_obj_create(nullptr);
  lv_obj_remove_style_all(root); lv_obj_set_size(root, W, H);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(root, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
  auto *title = label(root, 24, &lv_font_montserrat_20);
  lv_label_set_text(title, "QR / TEST");
  auto *phone = label(root, 224, &lv_font_montserrat_14);
  auto *channel = label(root, 248, &lv_font_montserrat_12);
  // Precreated fallback: does not need new objects when input becomes invalid.
  auto *fallback = label(root, 132, &lv_font_montserrat_14);
  lv_label_set_text(fallback, "Contacto sin QR");
  lv_obj_add_flag(fallback, LV_OBJ_FLAG_HIDDEN);
  lv_disp_load_scr(root); lv_refr_now(disp);
  lv_mem_monitor_t before; lv_mem_monitor(&before);
  ContactQr qr;
  assert(qr.update("+100000000000", Kind::Call) == Result::NotInitialized);
  assert(qr.begin(root));
  lv_mem_monitor_t created; lv_mem_monitor(&created);
  std::ofstream expected(output / "qr-expected.tsv");
  const auto render = [&](const char *name, const char *number, Kind kind) {
    const auto result = qr.update(number, kind);
    const bool ready = result == Result::Ready;
    if (ready) {
      lv_obj_align(qr.object(), LV_ALIGN_TOP_MID, 0, 64);
      lv_obj_add_flag(fallback, LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_clear_flag(fallback, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(phone, qr.contact().phone[0] ? qr.contact().phone : "Sin telefono valido");
    lv_label_set_text(channel, ready ? (kind == Kind::Call ? "Llamar" : "WhatsApp") : "Contacto / TEST");
    lv_refr_now(disp);
    check_text(title); check_text(phone); check_text(channel);
    if (ready) {
      lv_area_t a; lv_obj_get_coords(qr.object(), &a);
      const int side = (qr.modules() + 8) * 4;
      assert(qr.modules() >= 21 && qr.modules() <= 29);
      assert(lv_area_get_width(&a) == side && lv_area_get_height(&a) == side);
      assert(a.x1 >= 24 && a.x2 < 216 && a.y1 >= 64 && a.y2 < 224);
      for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
          const auto p = frame[(a.y1 + y) * W + a.x1 + x];
          assert(p == 0 || p == 0xffff); // No antialiasing or blended modules.
          if (x < 16 || y < 16 || x >= side - 16 || y >= side - 16) assert(p == 0xffff);
          else assert(p == frame[(a.y1 + 16 + (y - 16) / 4 * 4) * W + a.x1 + 16 + (x - 16) / 4 * 4]);
        }
    } else assert(lv_obj_has_flag(qr.object(), LV_OBJ_FLAG_HIDDEN));
    if (name) {
      save(output / (std::string(name) + ".ppm"));
      expected << name << '\t' << (ready ? qr.contact().payload : "-") << '\n';
    }
  };
  render("qr-whatsapp", "+100000000000", Kind::WhatsApp);
  lv_mem_monitor_t first; lv_mem_monitor(&first);
  const auto count = qr.generations(), old_flushes = flushes;
  for (int i = 0; i < 100; ++i) {
    assert(qr.update("+1 (000) 000-00000", Kind::WhatsApp) == Result::Ready);
    lv_refr_now(disp);
  }
  assert(qr.generations() == count && flushes == old_flushes);
  render("qr-whatsapp-max", "+100000000000000", Kind::WhatsApp);
  assert(qr.modules() == 29);
  render("qr-call", "+100000000000", Kind::Call);
  render("qr-call-max", "+100000000000000", Kind::Call);
  render("qr-min", "+1000000", Kind::Call);
  render("qr-disabled", "+100000000000", Kind::Disabled);
  render("qr-invalid", "missing-prefix", Kind::WhatsApp);
  render("qr-recovered", "+100000000000", Kind::WhatsApp);
  for (int i = 0; i < 30; ++i) {
    render(nullptr, "+100000000000000", Kind::WhatsApp);
    render(nullptr, "bad", Kind::Call);
    render(nullptr, "+100000000000", Kind::Disabled);
    render(nullptr, "+100000000000", Kind::WhatsApp);
  }
  lv_mem_monitor_t stable; lv_mem_monitor(&stable);
  assert(stable.free_size == first.free_size);
  assert(before.free_size - stable.free_size <= 8192);
  if (argc == 4) {
    lv_label_set_text(title, argv[2]);
    render("qr-local-preview", argv[3], Kind::WhatsApp);
    assert(qr.contact().result == Result::Ready);
  }
  std::ofstream memory(output / "qr-memory.txt");
  memory << "component_size=" << sizeof(ContactQr) << "\ncanvas_bytes=" << ContactQr::kCanvasBytes
         << "\nencoder_arrays=" << ContactQr::kEncoderBytes * 2
         << "\npool_before=" << before.free_size << "\npool_created=" << created.free_size
         << "\npool_first=" << first.free_size << "\npool_stable=" << stable.free_size << '\n';
  qr.end(); assert(qr.object() == nullptr);
  assert(qr.begin(root)); render(nullptr, "+100000000000", Kind::WhatsApp);
  qr.end();
  std::cout << "QR geometry, fallback, recovery and stable memory passed\n";
}
