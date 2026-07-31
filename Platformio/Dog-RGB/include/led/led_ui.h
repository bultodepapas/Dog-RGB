#pragma once

#include <stdint.h>

namespace led_ui {
struct Rgb {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

void begin();
void start_welcome();
void tick();
void apply_brightness(uint8_t brightness);
void set_transport_enabled(bool enabled);
bool transport_enabled();

uint8_t speed_range(float kph);
void get_range_config(uint8_t range, int &effect_a, int &effect_b, uint8_t &speed, uint8_t &intensity);
Rgb base_color_for_range(uint8_t range);
const char *effect_name(uint8_t effect_id);
uint8_t current_show_effect();
}
