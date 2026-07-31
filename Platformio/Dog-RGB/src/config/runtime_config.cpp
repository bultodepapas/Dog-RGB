#include "config/runtime_config.h"

#include <Arduino.h>

#include "config.h"
#include "storage/nvs_store.h"
#include "led/led_ui.h"
#include "util/crc32.h"
#include "wifi/wifi_mgr.h"

namespace config {
namespace {
RuntimeConfig g_cfg;
int8_t g_active_record = -1;
uint32_t g_record_generation = 0;
uint32_t g_save_failure_count = 0;

static const uint32_t CONFIG_RECORD_MAGIC = 0x43475244UL; // "DRGC" on little-endian storage.
static const uint16_t CONFIG_RECORD_VERSION = 1;
static const char *CONFIG_RECORD_KEYS[2] = {"cfg_a", "cfg_b"};
static const char *CONFIG_BLOB_MIGRATED_KEY = "cfg_blob";

struct __attribute__((packed)) ConfigRecord {
  uint32_t magic;
  uint16_t record_version;
  uint16_t record_size;
  uint32_t generation;
  uint8_t schema_version;
  uint8_t brightness;
  float ranges[9];
  RangeEffect effects[10];
  SingleEffectConfig single;
  char ap_ssid[33];
  char ap_pass[64];
  char mdns[33];
  uint8_t mode;
  uint8_t day_mode_enabled;
  uint16_t fence_max_m;
  uint8_t gps_min_fix_quality;
  uint8_t gps_min_sats;
  float gps_max_hdop;
  uint16_t gps_max_gga_age_ms;
  float gps_min_segment_m;
  float gps_hdop_factor;
  float gps_max_min_segment_m;
  uint32_t crc32;
};

static_assert(sizeof(ConfigRecord) < 512, "Runtime config record must remain a small NVS blob");

void set_default_single_config(SingleEffectConfig &cfg) {
  cfg.effect_id = SINGLE_EFFECT_DEFAULT;
  cfg.speed = SINGLE_SPEED_DEFAULT;
  cfg.intensity = SINGLE_INTENSITY_DEFAULT;
  cfg.base_r = SINGLE_R_DEFAULT;
  cfg.base_g = SINGLE_G_DEFAULT;
  cfg.base_b = SINGLE_B_DEFAULT;
}

void set_default_gps_config(RuntimeConfig &cfg) {
  cfg.gps_min_fix_quality = GPS_MIN_FIX_QUALITY_DEFAULT;
  cfg.gps_min_sats = GPS_MIN_SATS_DEFAULT;
  cfg.gps_max_hdop = GPS_MAX_HDOP_DEFAULT;
  cfg.gps_max_gga_age_ms = GPS_MAX_GGA_AGE_MS_DEFAULT;
  cfg.gps_min_segment_m = GPS_MIN_SEGMENT_M_DEFAULT;
  cfg.gps_hdop_factor = GPS_HDOP_FACTOR_DEFAULT;
  cfg.gps_max_min_segment_m = GPS_MAX_MIN_SEGMENT_M_DEFAULT;
}

bool validate_single_config(const SingleEffectConfig &cfg) {
  return cfg.effect_id < EFFECT_COUNT;
}

void load_single_config(RuntimeConfig &cfg) {
  Preferences &prefs_cfg = storage::prefs_cfg();
  cfg.single.effect_id = prefs_cfg.getUChar("single_eff", SINGLE_EFFECT_DEFAULT);
  cfg.single.speed = prefs_cfg.getUChar("single_speed", SINGLE_SPEED_DEFAULT);
  cfg.single.intensity = prefs_cfg.getUChar("single_intensity", SINGLE_INTENSITY_DEFAULT);
  cfg.single.base_r = prefs_cfg.getUChar("single_r", SINGLE_R_DEFAULT);
  cfg.single.base_g = prefs_cfg.getUChar("single_g", SINGLE_G_DEFAULT);
  cfg.single.base_b = prefs_cfg.getUChar("single_b", SINGLE_B_DEFAULT);
  if (!validate_single_config(cfg.single)) {
    set_default_single_config(cfg.single);
  }
}

void load_gps_config(RuntimeConfig &cfg) {
  Preferences &prefs_cfg = storage::prefs_cfg();
  cfg.gps_min_fix_quality = prefs_cfg.getUChar("gps_min_fix", GPS_MIN_FIX_QUALITY_DEFAULT);
  cfg.gps_min_sats = prefs_cfg.getUChar("gps_min_sats", GPS_MIN_SATS_DEFAULT);
  cfg.gps_max_hdop = prefs_cfg.getFloat("gps_max_hdop", GPS_MAX_HDOP_DEFAULT);
  cfg.gps_max_gga_age_ms = prefs_cfg.getUShort("gps_gga_age", GPS_MAX_GGA_AGE_MS_DEFAULT);
  cfg.gps_min_segment_m = prefs_cfg.getFloat("gps_min_seg", GPS_MIN_SEGMENT_M_DEFAULT);
  cfg.gps_hdop_factor = prefs_cfg.getFloat("gps_hdop_factor", GPS_HDOP_FACTOR_DEFAULT);
  cfg.gps_max_min_segment_m = prefs_cfg.getFloat("gps_max_min_seg", GPS_MAX_MIN_SEGMENT_M_DEFAULT);
  if (!validate_gps(cfg)) {
    set_default_gps_config(cfg);
  }
}

bool read_common_config(RuntimeConfig &cfg) {
  Preferences &prefs_cfg = storage::prefs_cfg();
  cfg.brightness = prefs_cfg.getUChar("brightness", LED_BRIGHTNESS);
  if (cfg.brightness < 1) {
    return false;
  }
  if (prefs_cfg.getBytes("ranges", cfg.ranges, sizeof(cfg.ranges)) != sizeof(cfg.ranges)) {
    return false;
  }
  if (prefs_cfg.getBytes("effects", cfg.effects, sizeof(cfg.effects)) != sizeof(cfg.effects)) {
    return false;
  }
  cfg.ap_ssid = prefs_cfg.getString("ap_ssid", AP_SSID);
  cfg.ap_pass = prefs_cfg.getString("ap_pass", AP_PASS);
  cfg.mdns = prefs_cfg.getString("mdns", MDNS_NAME);
  if (!valid_ap_ssid(cfg.ap_ssid)) {
    cfg.ap_ssid = AP_SSID;
  }
  if (!valid_ap_pass(cfg.ap_pass)) {
    cfg.ap_pass = AP_PASS;
  }
  if (!valid_mdns(cfg.mdns)) {
    cfg.mdns = MDNS_NAME;
  }
  if (!validate_ranges(cfg.ranges) || !validate_effects(cfg.effects)) {
    return false;
  }
  return true;
}

bool migrate_legacy_ap_defaults(RuntimeConfig &cfg) {
  if (cfg.ap_ssid == "dog") {
    cfg.ap_ssid = AP_SSID;
    if (cfg.ap_pass == "Dog123456789") {
      cfg.ap_pass = AP_PASS;
    }
    return true;
  }
  return false;
}

bool validate_runtime_config(const RuntimeConfig &cfg) {
  return cfg.brightness >= 1 &&
         validate_ranges(cfg.ranges) &&
         validate_effects(cfg.effects) &&
         validate_single_config(cfg.single) &&
         valid_ap_ssid(cfg.ap_ssid) &&
         valid_ap_pass(cfg.ap_pass) &&
         valid_mdns(cfg.mdns) &&
         validate_mode(cfg.mode) &&
         cfg.fence_max_m >= GEOFENCE_MAX_M_MIN &&
         cfg.fence_max_m <= GEOFENCE_MAX_M_MAX &&
         validate_gps(cfg);
}

bool copy_record_string(char *destination, size_t capacity, const String &source) {
  const size_t length = source.length();
  if (length >= capacity) {
    return false;
  }
  memcpy(destination, source.c_str(), length);
  destination[length] = '\0';
  return true;
}

bool record_string_terminated(const char *value, size_t capacity) {
  return memchr(value, '\0', capacity) != nullptr;
}

uint32_t config_record_crc(const ConfigRecord &record) {
  return util::crc32_ieee(&record, offsetof(ConfigRecord, crc32));
}

bool encode_config_record(const RuntimeConfig &cfg, uint32_t generation, ConfigRecord &record) {
  if (!validate_runtime_config(cfg)) {
    return false;
  }
  record = ConfigRecord{};
  record.magic = CONFIG_RECORD_MAGIC;
  record.record_version = CONFIG_RECORD_VERSION;
  record.record_size = sizeof(record);
  record.generation = generation;
  record.schema_version = version();
  record.brightness = cfg.brightness;
  memcpy(record.ranges, cfg.ranges, sizeof(record.ranges));
  memcpy(record.effects, cfg.effects, sizeof(record.effects));
  record.single = cfg.single;
  if (!copy_record_string(record.ap_ssid, sizeof(record.ap_ssid), cfg.ap_ssid) ||
      !copy_record_string(record.ap_pass, sizeof(record.ap_pass), cfg.ap_pass) ||
      !copy_record_string(record.mdns, sizeof(record.mdns), cfg.mdns)) {
    return false;
  }
  record.mode = cfg.mode;
  record.day_mode_enabled = cfg.day_mode_enabled ? 1 : 0;
  record.fence_max_m = cfg.fence_max_m;
  record.gps_min_fix_quality = cfg.gps_min_fix_quality;
  record.gps_min_sats = cfg.gps_min_sats;
  record.gps_max_hdop = cfg.gps_max_hdop;
  record.gps_max_gga_age_ms = cfg.gps_max_gga_age_ms;
  record.gps_min_segment_m = cfg.gps_min_segment_m;
  record.gps_hdop_factor = cfg.gps_hdop_factor;
  record.gps_max_min_segment_m = cfg.gps_max_min_segment_m;
  record.crc32 = config_record_crc(record);
  return true;
}

bool decode_config_record(const ConfigRecord &record, RuntimeConfig &cfg) {
  if (record.magic != CONFIG_RECORD_MAGIC ||
      record.record_version != CONFIG_RECORD_VERSION ||
      record.record_size != sizeof(record) ||
      record.schema_version != version() ||
      record.day_mode_enabled > 1 ||
      record.crc32 != config_record_crc(record) ||
      !record_string_terminated(record.ap_ssid, sizeof(record.ap_ssid)) ||
      !record_string_terminated(record.ap_pass, sizeof(record.ap_pass)) ||
      !record_string_terminated(record.mdns, sizeof(record.mdns))) {
    return false;
  }

  RuntimeConfig next = cfg;
  next.brightness = record.brightness;
  memcpy(next.ranges, record.ranges, sizeof(next.ranges));
  memcpy(next.effects, record.effects, sizeof(next.effects));
  next.single = record.single;
  next.ap_ssid = record.ap_ssid;
  next.ap_pass = record.ap_pass;
  next.mdns = record.mdns;
  next.mode = record.mode;
  next.day_mode_enabled = record.day_mode_enabled != 0;
  next.fence_max_m = record.fence_max_m;
  next.gps_min_fix_quality = record.gps_min_fix_quality;
  next.gps_min_sats = record.gps_min_sats;
  next.gps_max_hdop = record.gps_max_hdop;
  next.gps_max_gga_age_ms = record.gps_max_gga_age_ms;
  next.gps_min_segment_m = record.gps_min_segment_m;
  next.gps_hdop_factor = record.gps_hdop_factor;
  next.gps_max_min_segment_m = record.gps_max_min_segment_m;
  if (!validate_runtime_config(next)) {
    return false;
  }
  cfg = next;
  return true;
}

bool load_config_record(Preferences &prefs, uint8_t slot, ConfigRecord &record, RuntimeConfig &cfg) {
  const char *key = CONFIG_RECORD_KEYS[slot];
  if (prefs.getBytesLength(key) != sizeof(record) ||
      prefs.getBytes(key, &record, sizeof(record)) != sizeof(record)) {
    return false;
  }
  return decode_config_record(record, cfg);
}

bool generation_is_newer(uint32_t candidate, uint32_t reference) {
  const uint32_t delta = candidate - reference;
  return delta != 0 && delta < 0x80000000UL;
}
} // namespace

const RuntimeConfig &get() {
  return g_cfg;
}

RuntimeConfig &get_mut() {
  return g_cfg;
}

uint8_t version() {
  return 5;
}

int8_t storage_slot() {
  return g_active_record;
}

uint32_t storage_generation() {
  return g_record_generation;
}

uint32_t storage_save_failures() {
  return g_save_failure_count;
}

void set_defaults() {
  g_cfg.brightness = LED_BRIGHTNESS;
  g_cfg.ranges[0] = SPEED_RANGE_1_KPH;
  g_cfg.ranges[1] = SPEED_RANGE_2_KPH;
  g_cfg.ranges[2] = SPEED_RANGE_3_KPH;
  g_cfg.ranges[3] = SPEED_RANGE_4_KPH;
  g_cfg.ranges[4] = SPEED_RANGE_5_KPH;
  g_cfg.ranges[5] = SPEED_RANGE_6_KPH;
  g_cfg.ranges[6] = SPEED_RANGE_7_KPH;
  g_cfg.ranges[7] = SPEED_RANGE_8_KPH;
  g_cfg.ranges[8] = SPEED_RANGE_9_KPH;

  g_cfg.effects[0] = {static_cast<uint8_t>(RANGE_1_EFFECT_A), static_cast<uint8_t>(RANGE_1_EFFECT_B),
                      RANGE_1_SPEED, RANGE_1_INTENSITY};
  g_cfg.effects[1] = {static_cast<uint8_t>(RANGE_2_EFFECT_A), static_cast<uint8_t>(RANGE_2_EFFECT_B),
                      RANGE_2_SPEED, RANGE_2_INTENSITY};
  g_cfg.effects[2] = {static_cast<uint8_t>(RANGE_3_EFFECT_A), static_cast<uint8_t>(RANGE_3_EFFECT_B),
                      RANGE_3_SPEED, RANGE_3_INTENSITY};
  g_cfg.effects[3] = {static_cast<uint8_t>(RANGE_4_EFFECT_A), static_cast<uint8_t>(RANGE_4_EFFECT_B),
                      RANGE_4_SPEED, RANGE_4_INTENSITY};
  g_cfg.effects[4] = {static_cast<uint8_t>(RANGE_5_EFFECT_A), static_cast<uint8_t>(RANGE_5_EFFECT_B),
                      RANGE_5_SPEED, RANGE_5_INTENSITY};
  g_cfg.effects[5] = {static_cast<uint8_t>(RANGE_6_EFFECT_A), static_cast<uint8_t>(RANGE_6_EFFECT_B),
                      RANGE_6_SPEED, RANGE_6_INTENSITY};
  g_cfg.effects[6] = {static_cast<uint8_t>(RANGE_7_EFFECT_A), static_cast<uint8_t>(RANGE_7_EFFECT_B),
                      RANGE_7_SPEED, RANGE_7_INTENSITY};
  g_cfg.effects[7] = {static_cast<uint8_t>(RANGE_8_EFFECT_A), static_cast<uint8_t>(RANGE_8_EFFECT_B),
                      RANGE_8_SPEED, RANGE_8_INTENSITY};
  g_cfg.effects[8] = {static_cast<uint8_t>(RANGE_9_EFFECT_A), static_cast<uint8_t>(RANGE_9_EFFECT_B),
                      RANGE_9_SPEED, RANGE_9_INTENSITY};
  g_cfg.effects[9] = {static_cast<uint8_t>(RANGE_10_EFFECT_A), static_cast<uint8_t>(RANGE_10_EFFECT_B),
                      RANGE_10_SPEED, RANGE_10_INTENSITY};

  set_default_single_config(g_cfg.single);

  g_cfg.ap_ssid = AP_SSID;
  g_cfg.ap_pass = AP_PASS;
  g_cfg.mdns = MDNS_NAME;
  g_cfg.mode = MODE_SPEED;
  g_cfg.day_mode_enabled = false;
  g_cfg.fence_max_m = GEOFENCE_MAX_M_DEFAULT;
  set_default_gps_config(g_cfg);
}

bool save() {
  Preferences &prefs_cfg = storage::prefs_cfg();
  const uint8_t target_slot = (g_active_record == 0) ? 1 : 0;
  const uint32_t next_generation = (g_record_generation == UINT32_MAX)
                                       ? 1U
                                       : g_record_generation + 1U;

  ConfigRecord record = {};
  if (!encode_config_record(g_cfg, next_generation, record)) {
    g_save_failure_count++;
    return false;
  }
  const char *key = CONFIG_RECORD_KEYS[target_slot];
  if (prefs_cfg.putBytes(key, &record, sizeof(record)) != sizeof(record)) {
    g_save_failure_count++;
    return false;
  }

  ConfigRecord readback = {};
  RuntimeConfig verified = g_cfg;
  if (!load_config_record(prefs_cfg, target_slot, readback, verified) ||
      memcmp(&record, &readback, sizeof(record)) != 0) {
    g_save_failure_count++;
    return false;
  }

  g_active_record = target_slot;
  g_record_generation = next_generation;
  return true;
}

void load() {
  Preferences &prefs_cfg = storage::prefs_cfg();
  set_defaults();
  g_active_record = -1;
  g_record_generation = 0;

  ConfigRecord records[2] = {};
  RuntimeConfig candidates[2] = {g_cfg, g_cfg};
  const bool valid_a = load_config_record(prefs_cfg, 0, records[0], candidates[0]);
  const bool valid_b = load_config_record(prefs_cfg, 1, records[1], candidates[1]);
  if (valid_a || valid_b) {
    uint8_t selected = 0;
    if (!valid_a || (valid_b && generation_is_newer(records[1].generation, records[0].generation))) {
      selected = 1;
    }
    g_cfg = candidates[selected];
    g_active_record = selected;
    g_record_generation = records[selected].generation;
    if (migrate_legacy_ap_defaults(g_cfg)) {
      save();
    }
    if (!prefs_cfg.getBool(CONFIG_BLOB_MIGRATED_KEY, false)) {
      prefs_cfg.putBool(CONFIG_BLOB_MIGRATED_KEY, true);
    }
    return;
  }

  // Once blob migration completed, never resurrect stale legacy keys if both
  // records are later damaged. Recover defaults and recreate both generations.
  if (prefs_cfg.getBool(CONFIG_BLOB_MIGRATED_KEY, false)) {
    set_defaults();
    if (save()) {
      save();
    }
    return;
  }

  auto finish_legacy_migration = [&prefs_cfg]() {
    g_active_record = -1;
    g_record_generation = 0;
    if (save() && save()) {
      prefs_cfg.putBool(CONFIG_BLOB_MIGRATED_KEY, true);
    }
  };

  const uint8_t ver = prefs_cfg.getUChar("ver", 0);
  if (ver == version()) {
    RuntimeConfig next = g_cfg;
    if (!read_common_config(next)) {
      set_defaults();
      finish_legacy_migration();
      return;
    }
    next.mode = prefs_cfg.getUChar("mode", MODE_SPEED);
    next.day_mode_enabled = prefs_cfg.getBool("day_mode", false);
    next.fence_max_m = prefs_cfg.getUShort("fence_max", GEOFENCE_MAX_M_DEFAULT);
    load_single_config(next);
    load_gps_config(next);
    if (!validate_mode(next.mode)) {
      next.mode = MODE_SPEED;
    }
    next.fence_max_m = clamp_fence_max(next.fence_max_m);
    if (!validate_gps(next)) {
      set_default_gps_config(next);
    }
    migrate_legacy_ap_defaults(next);
    g_cfg = next;
    finish_legacy_migration();
    return;
  }

  if (ver == 4) {
    RuntimeConfig migrated = g_cfg;
    set_defaults();
    migrated = g_cfg;
    if (read_common_config(migrated)) {
      migrated.mode = prefs_cfg.getUChar("mode", MODE_SPEED);
      migrated.day_mode_enabled = false;
      migrated.fence_max_m = prefs_cfg.getUShort("fence_max", GEOFENCE_MAX_M_DEFAULT);
      load_single_config(migrated);
      load_gps_config(migrated);
      if (!validate_mode(migrated.mode)) {
        migrated.mode = MODE_SPEED;
      }
      migrated.fence_max_m = clamp_fence_max(migrated.fence_max_m);
      if (!validate_gps(migrated)) {
        set_default_gps_config(migrated);
      }
      migrate_legacy_ap_defaults(migrated);
      g_cfg = migrated;
      finish_legacy_migration();
      return;
    }
  }

  if (ver == 3) {
    RuntimeConfig migrated = g_cfg;
    set_defaults();
    migrated = g_cfg;
    if (read_common_config(migrated)) {
      migrated.mode = prefs_cfg.getUChar("mode", MODE_SPEED);
      migrated.day_mode_enabled = false;
      migrated.fence_max_m = prefs_cfg.getUShort("fence_max", GEOFENCE_MAX_M_DEFAULT);
      if (!validate_mode(migrated.mode)) {
        migrated.mode = MODE_SPEED;
      }
      migrated.fence_max_m = clamp_fence_max(migrated.fence_max_m);
      set_default_single_config(migrated.single);
      set_default_gps_config(migrated);
      g_cfg = migrated;
      finish_legacy_migration();
      return;
    }
  }

  if (ver == 2) {
    RuntimeConfig migrated = g_cfg;
    set_defaults();
    migrated = g_cfg;
    if (read_common_config(migrated)) {
      migrated.mode = MODE_SPEED;
      migrated.day_mode_enabled = false;
      migrated.fence_max_m = GEOFENCE_MAX_M_DEFAULT;
      set_default_gps_config(migrated);
      g_cfg = migrated;
      finish_legacy_migration();
      return;
    }
  }

  set_defaults();
  finish_legacy_migration();
}

void apply(const RuntimeConfig &previous) {
  led_ui::apply_brightness(g_cfg.brightness);
  wifi_mgr::apply_mdns(previous.mdns, g_cfg.mdns);
}

const char *mode_name(uint8_t mode) {
  switch (mode) {
    case MODE_GEOFENCE:
      return "geofence";
    case MODE_SHOW:
      return "show";
    case MODE_SIMPLE:
      return "simple";
    case MODE_SPEED:
    default:
      return "speed";
  }
}

bool parse_mode(const char *value, uint8_t &mode_out) {
  if (value == nullptr) {
    return false;
  }
  if (strcmp(value, "speed") == 0) {
    mode_out = MODE_SPEED;
    return true;
  }
  if (strcmp(value, "geofence") == 0) {
    mode_out = MODE_GEOFENCE;
    return true;
  }
  if (strcmp(value, "show") == 0) {
    mode_out = MODE_SHOW;
    return true;
  }
  if (strcmp(value, "simple") == 0) {
    mode_out = MODE_SIMPLE;
    return true;
  }
  return false;
}

bool validate_mode(uint8_t mode) {
  return (mode == MODE_SPEED || mode == MODE_GEOFENCE || mode == MODE_SHOW || mode == MODE_SIMPLE);
}

uint16_t clamp_fence_max(int value) {
  if (value < static_cast<int>(GEOFENCE_MAX_M_MIN)) {
    return GEOFENCE_MAX_M_MIN;
  }
  if (value > static_cast<int>(GEOFENCE_MAX_M_MAX)) {
    return GEOFENCE_MAX_M_MAX;
  }
  return static_cast<uint16_t>(value);
}

bool validate_ranges(const float *ranges) {
  if (!isfinite(ranges[0]) || ranges[0] <= 0.0f) {
    return false;
  }
  for (int i = 1; i < 9; ++i) {
    if (!isfinite(ranges[i]) || !(ranges[i] > ranges[i - 1])) {
      return false;
    }
  }
  return true;
}

bool validate_effects(const RangeEffect *effects) {
  for (int i = 0; i < 10; ++i) {
    if (effects[i].effect_a > 11 || effects[i].effect_b > 11) {
      return false;
    }
  }
  return true;
}

bool validate_gps(const RuntimeConfig &cfg) {
  if (cfg.gps_min_fix_quality > GPS_MIN_FIX_QUALITY_MAX) {
    return false;
  }
  if (cfg.gps_min_sats < GPS_MIN_SATS_MIN || cfg.gps_min_sats > GPS_MIN_SATS_MAX) {
    return false;
  }
  if (!(cfg.gps_max_hdop >= GPS_MAX_HDOP_MIN && cfg.gps_max_hdop <= GPS_MAX_HDOP_MAX)) {
    return false;
  }
  if (cfg.gps_max_gga_age_ms < GPS_MAX_GGA_AGE_MS_MIN || cfg.gps_max_gga_age_ms > GPS_MAX_GGA_AGE_MS_MAX) {
    return false;
  }
  if (!(cfg.gps_min_segment_m >= GPS_MIN_SEGMENT_M_MIN && cfg.gps_min_segment_m <= GPS_MIN_SEGMENT_M_MAX)) {
    return false;
  }
  if (!(cfg.gps_hdop_factor >= GPS_HDOP_FACTOR_MIN && cfg.gps_hdop_factor <= GPS_HDOP_FACTOR_MAX)) {
    return false;
  }
  if (!(cfg.gps_max_min_segment_m >= GPS_MAX_MIN_SEGMENT_M_MIN &&
        cfg.gps_max_min_segment_m <= GPS_MAX_MIN_SEGMENT_M_MAX)) {
    return false;
  }
  if (cfg.gps_min_segment_m > cfg.gps_max_min_segment_m) {
    return false;
  }
  return true;
}

bool valid_ap_ssid(const String &value) {
  if (value.length() < 1 || value.length() > 32) {
    return false;
  }
  if (value[0] == ' ' || value[value.length() - 1] == ' ') {
    return false;
  }
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (c < 32 || c == 127) {
      return false;
    }
  }
  return true;
}

bool valid_ap_pass(const String &value) {
  if (value.length() == 0) {
    return true;
  }
  if (value.length() < 8 || value.length() > 63) {
    return false;
  }
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (c < 32 || c == 127) {
      return false;
    }
  }
  return true;
}

bool valid_mdns(const String &value) {
  if (value.length() < 1 || value.length() > 32) {
    return false;
  }
  if (value[0] == '-' || value[value.length() - 1] == '-') {
    return false;
  }
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    const bool ok = (c >= 'a' && c <= 'z') ||
                    (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') ||
                    (c == '-');
    if (!ok) {
      return false;
    }
  }
  return true;
}
} // namespace config
