#pragma once

#include <stdint.h>

namespace geofence {
void begin();
void tick(unsigned long now_ms);

bool set_home(float lat_deg, float lon_deg, uint8_t source);
bool clear_home();

bool is_set();
uint8_t source();
float home_lat();
float home_lon();
int8_t storage_slot();
uint32_t storage_generation();
uint32_t storage_save_failures();

float distance_to_home_m();
uint8_t geofence_range(float dist_m);
uint8_t apply_hysteresis(uint8_t next_range, float dist_m);

const char *source_name(uint8_t source);
}
