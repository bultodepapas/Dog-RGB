#include "geofence/home.h"

#include <Arduino.h>

#include "config/runtime_config.h"
#include "config.h"
#include "gps/gps.h"
#include "storage/nvs_store.h"
#include "util/geo.h"

namespace geofence {
namespace {
bool home_set_state = false;
float home_lat_deg = 0.0f;
float home_lon_deg = 0.0f;
uint8_t home_source_state = 0; // 0=none, 1=auto, 2=manual
unsigned long fix_stable_ms = 0;
unsigned long last_fix_check_ms = 0;
bool last_fix_state = false;
uint8_t last_geofence_range = 1;
float last_geofence_distance_m = 0.0f;

void load_home() {
  Preferences &prefs_cfg = storage::prefs_cfg();
  home_set_state = (prefs_cfg.getUChar("home_set", 0) == 1);
  home_lat_deg = prefs_cfg.getFloat("home_lat", 0.0f);
  home_lon_deg = prefs_cfg.getFloat("home_lon", 0.0f);
  home_source_state = prefs_cfg.getUChar("home_src", 0);
  if (!home_set_state) {
    home_source_state = 0;
  }
}

void save_home() {
  Preferences &prefs_cfg = storage::prefs_cfg();
  prefs_cfg.putUChar("home_set", home_set_state ? 1 : 0);
  prefs_cfg.putUChar("home_src", home_source_state);
  if (home_set_state) {
    prefs_cfg.putFloat("home_lat", home_lat_deg);
    prefs_cfg.putFloat("home_lon", home_lon_deg);
  } else {
    prefs_cfg.remove("home_lat");
    prefs_cfg.remove("home_lon");
  }
}

void update_fix_stability(unsigned long now_ms) {
  if (last_fix_check_ms == 0) {
    last_fix_check_ms = now_ms;
  }
  const unsigned long dt = now_ms - last_fix_check_ms;
  last_fix_check_ms = now_ms;

  if (gps::has_fix()) {
    if (last_fix_state) {
      fix_stable_ms = (fix_stable_ms + dt > HOME_AUTO_FIX_MS) ? HOME_AUTO_FIX_MS : (fix_stable_ms + dt);
    } else {
      fix_stable_ms = 0;
    }
  } else {
    fix_stable_ms = 0;
  }
  last_fix_state = gps::has_fix();
}

void maybe_auto_set_home() {
  if (home_set_state) {
    return;
  }
  if (gps::has_fix() && gps::has_current_fix() && fix_stable_ms >= HOME_AUTO_FIX_MS) {
    set_home(gps::current_lat_deg(), gps::current_lon_deg(), 1);
  }
}
} // namespace

void begin() {
  load_home();
}

void tick(unsigned long now_ms) {
  update_fix_stability(now_ms);
  maybe_auto_set_home();
}

void set_home(float lat_deg, float lon_deg, uint8_t source) {
  home_set_state = true;
  home_lat_deg = lat_deg;
  home_lon_deg = lon_deg;
  home_source_state = source;
  save_home();
}

void clear_home() {
  home_set_state = false;
  home_source_state = 0;
  save_home();
}

bool is_set() {
  return home_set_state;
}

uint8_t source() {
  return home_source_state;
}

float home_lat() {
  return home_lat_deg;
}

float home_lon() {
  return home_lon_deg;
}

float distance_to_home_m() {
  if (!home_set_state || !gps::has_current_fix()) {
    return -1.0f;
  }
  return haversine_m(gps::current_lat_deg(), gps::current_lon_deg(), home_lat_deg, home_lon_deg);
}

uint8_t geofence_range(float dist_m) {
  if (dist_m <= 0.0f) {
    return 1;
  }
  const float step = static_cast<float>(config::get().fence_max_m) / 10.0f;
  if (step <= 0.0f) {
    return 1;
  }
  for (int i = 1; i <= 9; ++i) {
    if (dist_m <= step * i) {
      return static_cast<uint8_t>(i);
    }
  }
  return 10;
}

uint8_t apply_hysteresis(uint8_t next_range, float dist_m) {
  if (next_range == last_geofence_range) {
    last_geofence_distance_m = dist_m;
    return next_range;
  }
  const float step = static_cast<float>(config::get().fence_max_m) / 10.0f;
  const float margin = max(GEOFENCE_HYSTERESIS_MIN_M, step * GEOFENCE_HYSTERESIS_PCT);
  const float current_edge = static_cast<float>(last_geofence_range) * step;

  if (next_range > last_geofence_range) {
    if (dist_m < current_edge + margin) {
      return last_geofence_range;
    }
  } else if (next_range < last_geofence_range) {
    const float lower_edge = static_cast<float>(last_geofence_range - 1) * step;
    if (dist_m > lower_edge - margin) {
      return last_geofence_range;
    }
  }

  last_geofence_range = next_range;
  last_geofence_distance_m = dist_m;
  return next_range;
}

const char *source_name(uint8_t source) {
  switch (source) {
    case 2:
      return "manual";
    case 1:
      return "auto";
    default:
      return "none";
  }
}
} // namespace geofence
