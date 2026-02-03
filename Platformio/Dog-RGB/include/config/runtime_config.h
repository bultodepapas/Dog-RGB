#pragma once

#include <Arduino.h>

struct RangeEffect {
  uint8_t effect_a;
  uint8_t effect_b;
  uint8_t speed;
  uint8_t intensity;
};

struct SingleEffectConfig {
  uint8_t effect_id;
  uint8_t speed;
  uint8_t intensity;
  uint8_t base_r;
  uint8_t base_g;
  uint8_t base_b;
};

struct RuntimeConfig {
  uint8_t brightness;
  float ranges[9];
  RangeEffect effects[10];
  SingleEffectConfig single;
  String ap_ssid;
  String ap_pass;
  String mdns;
  uint8_t mode;
  uint16_t fence_max_m;
};

namespace config {
const RuntimeConfig &get();
RuntimeConfig &get_mut();

uint8_t version();

void set_defaults();
void load();
void save();
void apply(const RuntimeConfig &previous);

const char *mode_name(uint8_t mode);
bool parse_mode(const char *value, uint8_t &mode_out);
bool validate_mode(uint8_t mode);
uint16_t clamp_fence_max(int value);

bool validate_ranges(const float *ranges);
bool validate_effects(const RangeEffect *effects);

bool valid_mdns(const String &value);
}
