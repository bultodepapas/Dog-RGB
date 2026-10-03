#pragma once
#include <lvgl.h>
#include "display/identity.h"

extern "C" { LV_FONT_DECLARE(dog_name_14); }
namespace display::ui {
constexpr uint32_t kBlack = 0x000000, kWhite = 0xffffff, kMuted = 0xb8b8b8;
constexpr uint32_t kRule = 0x303030, kAttention = 0xffb547, kValid = 0x50efab;
constexpr uint32_t kIdentity = 0xd8e5ec;
lv_obj_t *page(lv_obj_t *parent);
lv_obj_t *label(lv_obj_t *parent, int x, int y, int width, const lv_font_t *font,
                uint32_t color, const char *text = "");
void text(lv_obj_t *label, const char *value);
void color(lv_obj_t *label, uint32_t value);
void rule(lv_obj_t *parent, int y);

// Direct children, fixed geometry; no extra LVGL containers or animation.
class PageHeader {
 public:
  void begin(lv_obj_t *parent, const char *index);
  void set_name(const char *name);
  void update(bool demo);
  void set_index(const char *index);
 private:
  void refresh();
  lv_obj_t *name_ = nullptr, *demo_label_ = nullptr, *index_ = nullptr;
  PetName pet_{};
  bool demo_ = false;
};
}
