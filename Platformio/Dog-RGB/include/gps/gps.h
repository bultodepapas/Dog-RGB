#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace gps {
void begin();
void tick();
void save_if_due(unsigned long now_ms);

void build_summary_payload(uint8_t *out, size_t len);
String build_summary_json();

bool has_fix();
bool raw_fix();
bool trusted_fix();
bool has_current_fix();
float last_speed_kph();
float total_distance_m();
float max_speed_kph();
unsigned long active_time_ms();
uint32_t current_date();
uint16_t last_update_min();

bool has_last_point();
float last_lat_deg();
float last_lon_deg();
float current_lat_deg();
float current_lon_deg();

uint8_t sats();
uint8_t fix_quality();
float hdop();
bool quality_ok();

unsigned long bytes_rx();
unsigned long sentences_rx();
unsigned long rmc_seen();
unsigned long rmc_valid();
unsigned long gga_seen();
unsigned long overflow();
unsigned long last_byte_ms();
unsigned long last_rmc_ms();
unsigned long last_gga_ms();
unsigned long last_fix_ms();

struct TrackPoint {
  int32_t lat_e7;
  int32_t lon_e7;
  uint16_t t_min;
} __attribute__((packed));

struct TrackView {
  bool ok;
  bool open;
  uint8_t slot;
  uint16_t count;
  uint16_t sample_ms;
  uint32_t start_date;
  uint16_t start_min;
  uint32_t end_date;
  uint16_t end_min;
  int32_t min_lat_e7;
  int32_t max_lat_e7;
  int32_t min_lon_e7;
  int32_t max_lon_e7;
};

typedef bool (*TrackPointCb)(const TrackPoint &p, void *ctx);

bool track_get_view(int session_id, TrackView &out);
bool track_iter_points(uint8_t slot, uint16_t max_points, TrackPointCb cb, void *ctx);
void track_tick(unsigned long now_ms);
}
