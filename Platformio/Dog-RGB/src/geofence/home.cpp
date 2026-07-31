#include "geofence/home.h"

#include <Arduino.h>

#include "config/runtime_config.h"
#include "config.h"
#include "gps/gps.h"
#include "storage/nvs_store.h"
#include "util/crc32.h"
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
int8_t home_active_record = -1;
uint32_t home_record_generation = 0;
uint32_t home_save_failure_count = 0;

static const uint32_t HOME_RECORD_MAGIC = 0x454D4F48UL; // "HOME" on little-endian storage.
static const uint16_t HOME_RECORD_VERSION = 1;
static const char *HOME_RECORD_KEYS[2] = {"home_a", "home_b"};
static const char *HOME_BLOB_MIGRATED_KEY = "home_blob";

struct __attribute__((packed)) HomeRecord {
  uint32_t magic;
  uint16_t record_version;
  uint16_t record_size;
  uint32_t generation;
  uint8_t is_set;
  uint8_t source;
  uint16_t reserved;
  float lat_deg;
  float lon_deg;
  uint32_t crc32;
};

static_assert(sizeof(HomeRecord) == 28, "Home record layout changed; bump its format version");

bool valid_home_state(bool is_set, float lat_deg, float lon_deg, uint8_t source) {
  if (!is_set) {
    return source == 0 && lat_deg == 0.0f && lon_deg == 0.0f;
  }
  return (source == 1 || source == 2) &&
         isfinite(lat_deg) && isfinite(lon_deg) &&
         lat_deg >= -90.0f && lat_deg <= 90.0f &&
         lon_deg >= -180.0f && lon_deg <= 180.0f;
}

uint32_t home_record_crc(const HomeRecord &record) {
  return util::crc32_ieee(&record, offsetof(HomeRecord, crc32));
}

HomeRecord make_home_record(uint32_t generation) {
  HomeRecord record = {};
  record.magic = HOME_RECORD_MAGIC;
  record.record_version = HOME_RECORD_VERSION;
  record.record_size = sizeof(record);
  record.generation = generation;
  record.is_set = home_set_state ? 1 : 0;
  record.source = home_source_state;
  record.lat_deg = home_set_state ? home_lat_deg : 0.0f;
  record.lon_deg = home_set_state ? home_lon_deg : 0.0f;
  record.crc32 = home_record_crc(record);
  return record;
}

bool decode_home_record(const HomeRecord &record) {
  if (record.magic != HOME_RECORD_MAGIC ||
      record.record_version != HOME_RECORD_VERSION ||
      record.record_size != sizeof(record) ||
      record.is_set > 1 ||
      record.reserved != 0 ||
      record.crc32 != home_record_crc(record) ||
      !valid_home_state(record.is_set != 0, record.lat_deg, record.lon_deg, record.source)) {
    return false;
  }
  return true;
}

bool load_home_record(Preferences &prefs, uint8_t slot, HomeRecord &record) {
  const char *key = HOME_RECORD_KEYS[slot];
  return prefs.getBytesLength(key) == sizeof(record) &&
         prefs.getBytes(key, &record, sizeof(record)) == sizeof(record) &&
         decode_home_record(record);
}

bool generation_is_newer(uint32_t candidate, uint32_t reference) {
  const uint32_t delta = candidate - reference;
  return delta != 0 && delta < 0x80000000UL;
}

void apply_home_record(const HomeRecord &record) {
  home_set_state = record.is_set != 0;
  home_source_state = record.source;
  home_lat_deg = record.lat_deg;
  home_lon_deg = record.lon_deg;
}

bool save_home() {
  if (!valid_home_state(home_set_state, home_lat_deg, home_lon_deg, home_source_state)) {
    home_save_failure_count++;
    return false;
  }
  Preferences &prefs_cfg = storage::prefs_cfg();
  const uint8_t target_slot = (home_active_record == 0) ? 1 : 0;
  const uint32_t next_generation = (home_record_generation == UINT32_MAX)
                                       ? 1U
                                       : home_record_generation + 1U;
  const HomeRecord record = make_home_record(next_generation);
  const char *key = HOME_RECORD_KEYS[target_slot];
  if (prefs_cfg.putBytes(key, &record, sizeof(record)) != sizeof(record)) {
    home_save_failure_count++;
    return false;
  }

  HomeRecord readback = {};
  if (!load_home_record(prefs_cfg, target_slot, readback) ||
      memcmp(&record, &readback, sizeof(record)) != 0) {
    home_save_failure_count++;
    return false;
  }
  home_active_record = target_slot;
  home_record_generation = next_generation;
  return true;
}

void load_home() {
  Preferences &prefs_cfg = storage::prefs_cfg();
  home_set_state = false;
  home_lat_deg = 0.0f;
  home_lon_deg = 0.0f;
  home_source_state = 0;
  home_active_record = -1;
  home_record_generation = 0;

  HomeRecord records[2] = {};
  const bool valid_a = load_home_record(prefs_cfg, 0, records[0]);
  const bool valid_b = load_home_record(prefs_cfg, 1, records[1]);
  if (valid_a || valid_b) {
    uint8_t selected = 0;
    if (!valid_a || (valid_b && generation_is_newer(records[1].generation, records[0].generation))) {
      selected = 1;
    }
    apply_home_record(records[selected]);
    home_active_record = selected;
    home_record_generation = records[selected].generation;
    if (!prefs_cfg.getBool(HOME_BLOB_MIGRATED_KEY, false)) {
      prefs_cfg.putBool(HOME_BLOB_MIGRATED_KEY, true);
    }
    return;
  }

  // Do not resurrect legacy coordinates after blob migration if both records
  // are later damaged. An unset home is the safe, visible recovery state.
  if (!prefs_cfg.getBool(HOME_BLOB_MIGRATED_KEY, false)) {
    const bool legacy_set = prefs_cfg.getUChar("home_set", 0) == 1;
    const float legacy_lat = legacy_set ? prefs_cfg.getFloat("home_lat", 0.0f) : 0.0f;
    const float legacy_lon = legacy_set ? prefs_cfg.getFloat("home_lon", 0.0f) : 0.0f;
    const uint8_t legacy_source = legacy_set ? prefs_cfg.getUChar("home_src", 0) : 0;
    if (valid_home_state(legacy_set, legacy_lat, legacy_lon, legacy_source)) {
      home_set_state = legacy_set;
      home_lat_deg = legacy_lat;
      home_lon_deg = legacy_lon;
      home_source_state = legacy_source;
    }
  }
  if (save_home() && save_home()) {
    prefs_cfg.putBool(HOME_BLOB_MIGRATED_KEY, true);
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

bool set_home(float lat_deg, float lon_deg, uint8_t source) {
  if (!valid_home_state(true, lat_deg, lon_deg, source)) {
    return false;
  }
  const bool previous_set = home_set_state;
  const float previous_lat = home_lat_deg;
  const float previous_lon = home_lon_deg;
  const uint8_t previous_source = home_source_state;
  home_set_state = true;
  home_lat_deg = lat_deg;
  home_lon_deg = lon_deg;
  home_source_state = source;
  if (save_home()) {
    return true;
  }
  home_set_state = previous_set;
  home_lat_deg = previous_lat;
  home_lon_deg = previous_lon;
  home_source_state = previous_source;
  return false;
}

bool clear_home() {
  const bool previous_set = home_set_state;
  const float previous_lat = home_lat_deg;
  const float previous_lon = home_lon_deg;
  const uint8_t previous_source = home_source_state;
  home_set_state = false;
  home_lat_deg = 0.0f;
  home_lon_deg = 0.0f;
  home_source_state = 0;
  if (save_home()) {
    return true;
  }
  home_set_state = previous_set;
  home_lat_deg = previous_lat;
  home_lon_deg = previous_lon;
  home_source_state = previous_source;
  return false;
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

int8_t storage_slot() {
  return home_active_record;
}

uint32_t storage_generation() {
  return home_record_generation;
}

uint32_t storage_save_failures() {
  return home_save_failure_count;
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
