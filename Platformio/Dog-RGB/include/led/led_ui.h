#pragma once

#include <stdint.h>

#include "led/led_frame.h"
#include "led/led_state.h"
#include "led/power_limiter.h"

namespace led_ui {
using Rgb = led::Rgb;

void begin();
void start_welcome();
void tick();
void apply_brightness(uint8_t brightness);
void apply_power_config(bool enabled, uint16_t budget_ma,
                        uint16_t base_current_ma, uint8_t rgb_channel_ma,
                        uint8_t white_channel_ma);
const led::PowerDiagnostics &power_diagnostics();
const led::LedState &current_state();
void set_transport_enabled(bool enabled);
bool transport_enabled();

uint8_t speed_range(float kph);
void get_range_config(uint8_t range, int &effect_a, int &effect_b, uint8_t &speed, uint8_t &intensity);
Rgb base_color_for_range(uint8_t range);
const char *effect_name(uint8_t effect_id);
uint8_t current_show_effect();
}
