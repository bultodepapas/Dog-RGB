#pragma once

#include <stdint.h>

namespace geofence {
void begin();
void tick(unsigned long now_ms);

void set_home(float lat_deg, float lon_deg, uint8_t source);
void clear_home();

bool is_set();
uint8_t source();
float home_lat();
float home_lon();

float distance_to_home_m();
uint8_t geofence_range(float dist_m);
uint8_t apply_hysteresis(uint8_t next_range, float dist_m);

const char *source_name(uint8_t source);
}
