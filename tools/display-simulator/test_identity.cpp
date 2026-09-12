#include "display/identity_view.h"
#include "display/walk_view.h"
#include "display/connection_view.h"
#include "display/status_view.h"
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
  ++flushes; lv_disp_flush_ready(drv);
}
void save(const std::filesystem::path &path) {
  std::ofstream f(path, std::ios::binary); f << "P6\n240 280\n255\n";
  for (auto p : frame) {
    const int r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
    const char rgb[] = {char((r << 3) | (r >> 2)), char((g << 2) | (g >> 4)), char((b << 3) | (b >> 2))};
    f.write(rgb, 3);
  }
  assert(f.good());
}
void layout(lv_obj_t *root) {
  lv_obj_update_layout(root);
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i) {
    auto *a = lv_obj_get_child(root, i);
    if (lv_obj_has_flag(a, LV_OBJ_FLAG_HIDDEN)) continue;
    lv_area_t box; lv_obj_get_coords(a, &box);
    if (!(box.x1 >= 24 && box.x2 < 216 && box.y1 >= 20 && box.y2 < 264))
      std::cerr << "outside safe area child " << i << " y=" << box.y1 << ".." << box.y2 << '\n';
    assert(box.x1 >= 24 && box.x2 < 216 && box.y1 >= 20 && box.y2 < 264);
    for (uint32_t j = 0; j < i; ++j) {
      auto *b = lv_obj_get_child(root, j);
      if (lv_obj_has_flag(b, LV_OBJ_FLAG_HIDDEN)) continue;
      lv_area_t other; lv_obj_get_coords(b, &other);
      assert(box.x2 < other.x1 || box.x1 > other.x2 || box.y2 < other.y1 || box.y1 > other.y2);
    }
    if (lv_obj_check_type(a, &lv_label_class)) {
      lv_point_t size;
      lv_txt_get_size(&size, lv_label_get_text(a), lv_obj_get_style_text_font(a, 0), 0, 0, 192, LV_TEXT_FLAG_NONE);
      assert(size.x <= 192 && size.y <= lv_obj_get_height(a));
      assert(lv_label_get_long_mode(a) == LV_LABEL_LONG_WRAP);
    }
  }
}
bool contains(lv_obj_t *root, const char *value) {
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i) {
    auto *obj = lv_obj_get_child(root, i);
    if (lv_obj_check_type(obj, &lv_label_class) && !strcmp(lv_label_get_text(obj), value)) return true;
  }
  return false;
}
std::string utf8(unsigned cp) {
  if (cp < 128) return std::string(1, char(cp));
  return std::string{char(0xc0 | (cp >> 6)), char(0x80 | (cp & 63))};
}
}
int main(int argc, char **argv) {
  assert(argc == 2 || argc == 4);
  using namespace display;
  using Kind = QrContactKind;
  using Result = NameResult;
  assert(format_pet_name(nullptr).result == Result::Empty);
  for (const char *s : {"Freya", "Ni\xc3\xb1o", "Ren\xc3\xa9", "D'Artagnan", "Ana-Maria"})
    assert(format_pet_name(s).result == Result::Ready);
  assert(format_pet_name("Rene\xcc\x81").result == Result::NeedsNormalization);
  assert(format_pet_name("Nin\xcc\x83o").result == Result::NeedsNormalization);
  for (const char *s : {"\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xc3", "\x80", "A\xc3x"})
    assert(format_pet_name(s).result == Result::InvalidUtf8);
  for (const char *s : {"A\nB", "Dog\xf0\x9f\x90\xb6", "\xe4\xb8\xad", "A/B"})
    assert(format_pet_name(s).result == Result::Unsupported);
  for (const char *s : {" Freya", "Freya ", "Ana  Maria"})
    assert(format_pet_name(s).result == Result::InvalidSpacing);
  assert(format_pet_name(std::string(25, 'W').c_str()).result == Result::TooLong);
  assert(format_pet_name(std::string(49, 'W').c_str()).result == Result::TooLong);
  const std::filesystem::path output(argv[1]); std::filesystem::create_directories(output);
  lv_init();
  lv_disp_draw_buf_t draw; lv_disp_draw_buf_init(&draw, buffer, nullptr, W * 20);
  lv_disp_drv_t driver; lv_disp_drv_init(&driver);
  driver.hor_res = W; driver.ver_res = H; driver.draw_buf = &draw; driver.flush_cb = flush;
  auto *disp = lv_disp_drv_register(&driver); assert(disp);
  auto *root = lv_obj_create(nullptr); lv_obj_remove_style_all(root);
  lv_obj_set_size(root, W, H); lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(root, lv_color_black(), 0); lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
  // Actual I6d views stay allocated: measure the fourth view against that pool.
  WalkView walk; ConnectionView connection; StatusView status;
  assert(walk.begin({}, false, "", root)); assert(connection.begin({}, false, root)); assert(status.begin({}, false, root));
  for (auto *s : {walk.screen(), connection.screen(), status.screen()}) lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
  lv_disp_load_scr(root); lv_refr_now(disp);
  lv_mem_monitor_t baseline; lv_mem_monitor(&baseline);
  IdentityView view; assert(view.begin(root));
  lv_mem_monitor_t created; lv_mem_monitor(&created);
  std::ofstream expected(output / "identity-expected.tsv");
  const auto render = [&](const char *fixture, const char *name, const char *number, Kind kind) {
    view.update(name, number, kind); lv_refr_now(disp); layout(view.screen());
    if (view.name().result == Result::Ready) assert(contains(view.screen(), name));
    if (view.contact().phone[0]) assert(contains(view.screen(), view.contact().phone));
    if (fixture) {
      save(output / (std::string(fixture) + ".ppm"));
      expected << fixture << '\t' << (view.layout() == IdentityLayout::Qr ? view.contact().payload : "-") << '\n';
    }
  };
  render("identity-short", "FREYA", "+100000000000", Kind::WhatsApp);
  assert(view.layout() == IdentityLayout::Qr);
  lv_mem_monitor_t first; lv_mem_monitor(&first);
  const auto generations = view.qr_generations(), old_flushes = flushes;
  for (int i = 0; i < 100; ++i) render(nullptr, "FREYA", "+1 (000) 000-00000", Kind::WhatsApp);
  assert(view.qr_generations() == generations && flushes == old_flushes);
  render("identity-accent", "Ren\xc3\xa9", "+100000000000", Kind::WhatsApp);
  assert(view.qr_generations() == generations); // Name change does not regenerate contact.
  render("identity-enye", "Ni\xc3\xb1o", "+100000000000", Kind::Call);
  render("identity-max-phone", "FREYA", "+100000000000000", Kind::WhatsApp);
  render("identity-long", "Maximiliano de la Sierra", "+100000000000000", Kind::WhatsApp);
  assert(view.name().result == Result::Ready);
  assert(view.layout() == IdentityLayout::Text);
  render("identity-wide", "WWWWWWWWWWWWWWWWWWWWWWWW", "+100000000000000", Kind::WhatsApp);
  render("identity-wordwrap", "WWWW WWWW WWWW WWWW WWWW", "+100000000000000", Kind::WhatsApp);
  assert(view.name().result == Result::Ready);
  render("identity-disabled", "FREYA", "+100000000000", Kind::Disabled);
  render("identity-invalid-phone", "FREYA", "no-prefix", Kind::WhatsApp);
  render("identity-empty", "", "", Kind::Disabled);
  render("identity-nfd", "Rene\xcc\x81", "+100000000000", Kind::WhatsApp);
  render("identity-unsupported", "Dog\xf0\x9f\x90\xb6", "+100000000000", Kind::WhatsApp);
  render("identity-recovered", "FREYA", "+100000000000", Kind::WhatsApp);
  // Every supported letter exists in the font and its worst-case 24-char name fits.
  for (unsigned cp = 33; cp <= 255; ++cp) {
    const auto character = utf8(cp);
    if (format_pet_name(character.c_str()).result != Result::Ready) continue;
    lv_font_glyph_dsc_t glyph{};
    assert(lv_font_get_glyph_dsc(&dog_name_28, &glyph, cp, 0));
    assert(!glyph.is_placeholder && lv_font_get_glyph_bitmap(&dog_name_28, cp));
    std::string longest;
    for (int i = 0; i < 24; ++i) longest += character;
    assert(format_pet_name(longest.c_str()).result == Result::Ready);
    render(nullptr, longest.c_str(), "+100000000000000", Kind::WhatsApp);
  }
  for (int i = 0; i < 30; ++i) {
    render(nullptr, "Maximiliano de la Sierra", "+100000000000000", Kind::WhatsApp);
    render(nullptr, "Rene\xcc\x81", "bad", Kind::Call);
    render(nullptr, "FREYA", "+100000000000", Kind::WhatsApp);
  }
  lv_mem_monitor_t stable; lv_mem_monitor(&stable);
  assert(stable.free_size == first.free_size);
  assert(baseline.free_size - stable.free_size <= 8192);
  if (argc == 4) {
    render("identity-local-preview", argv[2], argv[3], Kind::WhatsApp);
    assert(view.name().result == Result::Ready && view.contact().result == ContactResult::Ready);
  }
  std::ofstream memory(output / "identity-memory.txt");
  memory << "instance_bytes=" << sizeof(IdentityView) << "\npool_I6d=" << baseline.free_size
         << "\npool_created=" << created.free_size << "\npool_first=" << first.free_size
         << "\npool_stable=" << stable.free_size << '\n';
  view.end(); assert(view.begin(root)); render(nullptr, "FREYA", "+100000000000", Kind::WhatsApp); view.end();
  std::cout << "Identity UTF-8, glyphs, A/B bounds, QR caching and pool contracts passed\n";
}
