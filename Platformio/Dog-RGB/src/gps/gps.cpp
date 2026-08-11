#include "gps/gps.h"

#include <Arduino.h>
#include <Preferences.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "config.h"
#include "config/runtime_config.h"
#include "pins.h"
#include "storage/nvs_store.h"
#include "util/crc32.h"
#include "util/geo.h"
#include "util/time_utils.h"

namespace gps {
namespace {
// GPS UART settings are defined in config.h.
HardwareSerial GPS(1);

// NMEA line buffer for incoming GPS sentences.
char nmea_line[128];
size_t nmea_len = 0;

// Latest GPS state.
bool has_gps_fix = false;
bool has_gps_fix_raw = false;
bool gps_quality_ok = false;
bool gps_trusted_fix = false;
bool last_speed_usable_val = false;
float last_speed_kph_val = 0.0f;
unsigned long last_gps_ms = 0;
unsigned long gps_bytes_rx = 0;
unsigned long gps_sentences_rx = 0;
unsigned long gps_rmc_seen = 0;
unsigned long gps_rmc_valid = 0;
unsigned long gps_gga_seen = 0;
unsigned long gps_overflow = 0;
unsigned long gps_checksum_fail = 0;
unsigned long gps_parse_fail = 0;
unsigned long gps_rmc_parse_fail = 0;
unsigned long gps_gga_parse_fail = 0;
unsigned long gps_speed_spike = 0;
unsigned long gps_stale_count = 0;
unsigned long gps_small_segment_rejects = 0;
unsigned long gps_large_segment_rejects = 0;
unsigned long gps_last_byte_ms = 0;
unsigned long gps_last_sentence_ms = 0;
unsigned long gps_last_rmc_ms = 0;
unsigned long gps_last_gga_ms = 0;
unsigned long gps_last_fix_ms = 0;
bool gps_byte_observed = false;
bool gps_rmc_observed = false;
bool gps_gga_observed = false;
bool gps_fix_observed = false;
bool gps_time_observed = false;
uint8_t gps_sats = 0;
uint8_t gps_fix_quality = 0;
float gps_hdop = NAN;
unsigned long gps_last_time_ms = 0;

static const unsigned long GPS_RMC_STALE_MS = 3000;
static const unsigned long GPS_UART_STALE_MS = 5000;

// Rolling metrics for the current day.
unsigned long last_sample_ms = 0;
unsigned long active_time_ms_val = 0;
float total_distance_m_val = 0.0f;
float max_speed_kph_val = 0.0f;
uint16_t last_update_min_val = 0;
bool has_activity_observation = false;
bool last_activity_observation_active = false;
uint32_t last_activity_observation_date = 0;
uint32_t last_activity_observation_ms = 0;
uint32_t gps_activity_observation_intervals = 0;
uint32_t gps_activity_gap_rejects = 0;
uint32_t gps_last_activity_delta_ms = 0;

// Trusted GNSS date-transition guard. Daily metrics may only roll after an
// immediate timestamp-contiguous midnight or repeated forward-date evidence.
bool has_accepted_date_observation = false;
uint32_t last_accepted_date_observation_ms = 0;
uint32_t pending_date_candidate = 0;
uint32_t pending_date_last_time_ms = 0;
uint8_t pending_date_observations = 0;
uint32_t gps_date_transition_count = 0;
uint32_t gps_date_rejected_count = 0;

static const uint32_t DAILY_JOURNAL_MAGIC = 0x31594144UL; // "DAY1"
static const uint8_t DAILY_JOURNAL_VERSION = 1;
static const uint32_t METRICS_RECORD_MAGIC = 0x3154454DUL; // "MET1"
static const uint8_t METRICS_RECORD_VERSION = 1;
static const uint8_t METRICS_MIGRATION_VERSION = 1;

struct MetricsRecord {
  uint32_t magic;
  uint8_t version;
  uint8_t flags;
  uint16_t size;
  uint32_t generation;
  uint32_t date;
  float distance_m;
  uint32_t active_ms;
  float max_speed_kph;
  uint16_t last_update_min;
  uint16_t reserved;
  uint32_t crc32;
} __attribute__((packed));

static_assert(sizeof(MetricsRecord) == 36, "MetricsRecord size");

struct DailyJournalRecord {
  uint32_t magic;
  uint8_t version;
  uint8_t flags;
  uint16_t size;
  uint32_t generation;
  uint32_t date;
  float distance_m;
  uint32_t active_ms;
  float max_speed_kph;
  uint16_t last_update_min;
  uint16_t reserved;
  uint32_t crc32;
} __attribute__((packed));

static_assert(sizeof(DailyJournalRecord) == 36, "DailyJournalRecord size");

DailyJournalRecord last_completed_day = {};
bool last_completed_day_valid = false;
int8_t daily_journal_active_slot = -1;
uint32_t daily_journal_active_generation = 0;
uint32_t daily_journal_failures = 0;
int8_t metrics_active_slot = -1;
uint32_t metrics_generation = 0;
uint32_t metrics_failures = 0;
uint32_t metrics_recoveries = 0;
MetricsRecord metrics_persisted = {};
bool metrics_persisted_valid = false;
bool metrics_mirror_degraded = true;
bool metrics_migration_marked = false;

// Rolling metrics for the current session (boot -> shutdown).
unsigned long session_active_time_ms = 0;
float session_total_distance_m = 0.0f;
float session_max_speed_kph = 0.0f;
bool session_fix_seen = false;
bool session_start_set = false;
uint32_t session_start_date = 0;
uint16_t session_start_min = 0;
uint32_t session_end_date = 0;
uint16_t session_end_min = 0;
bool session_open = false;

static const uint8_t SESSION_VER = 1;
static const uint8_t SESSION_FLAG_GPS_FIX = 0x01;
static const uint8_t SESSION_FLAG_HAS_DATA = 0x02;
static const uint8_t SESSION_FLAG_IN_PROGRESS = 0x04;
static const uint8_t SESSION_FLAG_NO_FIX = 0x08;
static const uint8_t HISTORY_MAX = 3;
static const uint32_t SESSION_STORE_MAGIC = 0x31534553UL; // "SES1"
static const uint8_t SESSION_STORE_VERSION = 1;
static const uint8_t SESSION_STORE_FLAG_OPEN = 0x01;
static const uint8_t SESSION_STORE_MIGRATION_VERSION = 1;

struct SessionSummary {
  uint8_t ver;
  uint8_t flags;
  uint32_t start_date;
  uint16_t start_min;
  uint32_t end_date;
  uint16_t end_min;
  uint32_t distance_m;
  uint32_t active_s;
  uint16_t avg_speed_cmps;
  uint16_t max_speed_cmps;
  uint8_t crc;
  uint8_t pad;
} __attribute__((packed));

static_assert(sizeof(SessionSummary) == 28, "SessionSummary size");

struct SessionStoreRecord {
  uint32_t magic;
  uint8_t version;
  uint8_t flags;
  uint16_t size;
  uint32_t generation;
  uint8_t history_count;
  uint8_t history_idx;
  uint16_t reserved;
  SessionSummary history[HISTORY_MAX];
  SessionSummary current;
  uint32_t crc32;
} __attribute__((packed));

static_assert(sizeof(SessionStoreRecord) == 132, "SessionStoreRecord size");

SessionSummary history[HISTORY_MAX];
uint8_t history_count = 0;
uint8_t history_idx = 0;
SessionSummary session_snapshot_last;
bool session_snapshot_valid = false;
SessionSummary boot_session_snapshot;
bool boot_session_open = false;
int8_t session_store_active_slot = -1;
uint32_t session_store_generation = 0;
uint32_t session_store_failures = 0;
uint32_t session_store_recoveries = 0;
SessionStoreRecord session_store_persisted = {};
bool session_store_persisted_valid = false;
bool session_store_mirror_degraded = true;
bool session_store_migration_marked = false;

// Track storage (3-session window, 2h max).
static const uint8_t TRACK_VER = 2;
static const uint8_t TRACK_SLOTS = 4;
static const uint8_t TRACK_FLAG_OPEN = 0x01;
static const uint8_t TRACK_FLAG_BBOX_DIRTY = 0x02;
static const uint32_t TRACK_SAMPLE_MS = 5000; // 5s sampling
static const uint32_t TRACK_WINDOW_MS = 2UL * 60UL * 60UL * 1000UL; // 2h
static const uint16_t TRACK_MAX_POINTS = static_cast<uint16_t>(TRACK_WINDOW_MS / TRACK_SAMPLE_MS);
static const uint8_t TRACK_CHUNK_POINTS = 48;
static const uint32_t TRACK_FLUSH_MS = 15000;
static const uint8_t TRACK_DATA_CHUNKS =
    static_cast<uint8_t>((TRACK_MAX_POINTS + TRACK_CHUNK_POINTS - 1) / TRACK_CHUNK_POINTS);
// One extra chunk keeps the current partial chunk durable without evicting
// any of the two-hour data window. Iteration trims the oldest excess points.
static const uint8_t TRACK_MAX_CHUNKS = static_cast<uint8_t>(TRACK_DATA_CHUNKS + 1);
static const uint16_t TRACK_STORAGE_POINTS =
    static_cast<uint16_t>(TRACK_MAX_CHUNKS * TRACK_CHUNK_POINTS);

struct TrackChunkHeader {
  uint8_t ver;
  uint8_t count;
  uint16_t first_t_min;
  uint8_t flags;
  uint32_t crc32;
} __attribute__((packed));

struct TrackMeta {
  uint8_t ver;
  uint8_t flags;
  uint16_t sample_ms;
  uint16_t max_points;
  uint16_t total_points;
  uint8_t chunk_head;
  uint8_t chunk_count;
  uint32_t start_date;
  uint16_t start_min;
  uint32_t end_date;
  uint16_t end_min;
  int32_t min_lat_e7;
  int32_t max_lat_e7;
  int32_t min_lon_e7;
  int32_t max_lon_e7;
  uint32_t crc32;
} __attribute__((packed));

static_assert(sizeof(TrackChunkHeader) == 9, "TrackChunkHeader size");
static_assert(sizeof(TrackMeta) == 42, "TrackMeta size");

struct TrackSession {
  TrackPoint flush_buf[TRACK_CHUNK_POINTS];
  uint8_t flush_count;
  uint8_t persisted_flush_count;
  unsigned long last_flush_ms;
  unsigned long last_sample_ms;
  uint16_t persisted_points;
  uint8_t chunk_head;
  uint8_t chunk_count;
  bool bbox_dirty;
  bool meta_dirty;
  uint32_t start_date;
  uint16_t start_min;
  uint32_t end_date;
  uint16_t end_min;
  int32_t min_lat_e7;
  int32_t max_lat_e7;
  int32_t min_lon_e7;
  int32_t max_lon_e7;
  bool has_bbox;
  TrackPoint last_point;
  bool has_last_point;
};

TrackSession track_current;
uint8_t track_slot = 0;
// Set while an export walks the track. Exports service GNSS between socket
// writes, so without this the very callback that keeps the link alive could
// rewrite the buffer being read. Point capture pauses for the duration.
bool track_export_active = false;

uint32_t track_meta_crc(const TrackMeta &m);
TrackMeta track_load_meta(Preferences &prefs, uint8_t slot);
bool track_save_meta(Preferences &prefs, uint8_t slot, TrackMeta &meta);
void track_clear_slot(Preferences &prefs, uint8_t slot);
void track_clear_all(Preferences &prefs);
void track_reset_ram();
void track_open_new(Preferences &prefs, uint8_t slot);
void track_begin();
void track_flush_if_due(unsigned long now_ms);
void track_try_add_point(float lat_deg, float lon_deg, uint16_t t_min, uint32_t date_yyyymmdd, unsigned long now_ms);
bool track_iter_points_internal(uint8_t slot, uint16_t max_points, TrackPointCb cb, void *ctx);
void clear_pending_date(bool rejected);

// Last position for distance calculation.
bool has_last_point_val = false;
float last_lat_deg_val = 0.0f;
float last_lon_deg_val = 0.0f;
float current_lat_deg_val = 0.0f;
float current_lon_deg_val = 0.0f;
bool has_current_fix_val = false;

bool session_has_last_point = false;
float session_last_lat_deg = 0.0f;
float session_last_lon_deg = 0.0f;
float gps_last_segment_m = 0.0f;
bool gps_last_segment_accepted = false;
const char *gps_last_segment_reject_reason = "none";

// Daily reset date (YYYYMMDD from GPS).
uint32_t current_date_yyyymmdd = 0;
unsigned long last_save_ms = 0;

float knots_to_kph(float knots) {
  return knots * 1.852f;
}

bool is_hex_digit(char c) {
  return (c >= '0' && c <= '9') ||
         (c >= 'A' && c <= 'F') ||
         (c >= 'a' && c <= 'f');
}

uint8_t hex_value(char c) {
  if (c >= '0' && c <= '9') {
    return static_cast<uint8_t>(c - '0');
  }
  if (c >= 'A' && c <= 'F') {
    return static_cast<uint8_t>(10 + c - 'A');
  }
  return static_cast<uint8_t>(10 + c - 'a');
}

bool is_digit_string(const char *value, size_t len) {
  if (value == nullptr || strlen(value) != len) {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    if (value[i] < '0' || value[i] > '9') {
      return false;
    }
  }
  return true;
}

bool is_sentence_type(const char *line, const char *type) {
  return line != nullptr &&
         strlen(line) >= 7 &&
         line[0] == '$' &&
         line[3] == type[0] &&
         line[4] == type[1] &&
         line[5] == type[2] &&
         line[6] == ',';
}

bool nmea_checksum_ok(const char *line) {
  if (line == nullptr || line[0] != '$') {
    return false;
  }
  uint8_t checksum = 0;
  const char *star = nullptr;
  for (const char *p = line + 1; *p != '\0'; ++p) {
    if (*p == '*') {
      star = p;
      break;
    }
    if (static_cast<uint8_t>(*p) < 32 || static_cast<uint8_t>(*p) > 126) {
      return false;
    }
    checksum ^= static_cast<uint8_t>(*p);
  }
  if (star == nullptr || strlen(star) != 3 || !is_hex_digit(star[1]) || !is_hex_digit(star[2])) {
    return false;
  }
  const uint8_t expected = static_cast<uint8_t>((hex_value(star[1]) << 4) | hex_value(star[2]));
  return checksum == expected;
}

bool copy_nmea_field(const char *line, int target_field, char *out, size_t out_len) {
  if (line == nullptr || out == nullptr || out_len == 0 || target_field < 0) {
    return false;
  }
  out[0] = '\0';
  int field = 0;
  size_t len = 0;
  for (const char *p = line; *p != '\0' && *p != '*'; ++p) {
    if (*p == ',') {
      if (field == target_field) {
        break;
      }
      field++;
      continue;
    }
    if (field == target_field) {
      if (len + 1 >= out_len) {
        return false;
      }
      out[len++] = *p;
    }
  }
  out[len] = '\0';
  return field >= target_field;
}

bool parse_float_strict(const char *value, float *out) {
  if (value == nullptr || out == nullptr || value[0] == '\0') {
    return false;
  }
  char *end = nullptr;
  const float parsed = strtof(value, &end);
  if (end == value || *end != '\0' || !isfinite(parsed)) {
    return false;
  }
  *out = parsed;
  return true;
}

bool parse_uint8_strict(const char *value, uint8_t *out) {
  if (value == nullptr || out == nullptr || value[0] == '\0') {
    return false;
  }
  uint16_t parsed = 0;
  for (const char *p = value; *p != '\0'; ++p) {
    if (*p < '0' || *p > '9') {
      return false;
    }
    parsed = static_cast<uint16_t>((parsed * 10) + (*p - '0'));
    if (parsed > 255) {
      return false;
    }
  }
  *out = static_cast<uint8_t>(parsed);
  return true;
}

bool parse_nmea_coord(const char *value, char hemi, bool latitude, float *out) {
  if (value == nullptr || out == nullptr || value[0] == '\0') {
    return false;
  }
  if (latitude) {
    if (hemi != 'N' && hemi != 'S') {
      return false;
    }
  } else if (hemi != 'E' && hemi != 'W') {
    return false;
  }

  float raw = 0.0f;
  if (!parse_float_strict(value, &raw) || raw < 0.0f) {
    return false;
  }
  const int deg = static_cast<int>(raw / 100.0f);
  const float minutes = raw - (deg * 100.0f);
  const int max_deg = latitude ? 90 : 180;
  if (deg < 0 || deg > max_deg || minutes < 0.0f || minutes >= 60.0f) {
    return false;
  }
  if (deg == max_deg && minutes > 0.0f) {
    return false;
  }

  float dec = static_cast<float>(deg) + (minutes / 60.0f);
  if (hemi == 'S' || hemi == 'W') {
    dec = -dec;
  }
  *out = dec;
  return true;
}

bool parse_time_of_day_ms(const char *value, uint32_t *out) {
  if (value == nullptr || out == nullptr || strlen(value) < 6) {
    return false;
  }
  for (size_t i = 0; i < 6; ++i) {
    if (value[i] < '0' || value[i] > '9') {
      return false;
    }
  }
  if (value[6] == '\0') {
    // Plain hhmmss is valid.
  } else if (value[6] == '.') {
    if (value[7] == '\0') {
      return false;
    }
    for (size_t i = 7; value[i] != '\0'; ++i) {
      if (value[i] < '0' || value[i] > '9') {
        return false;
      }
    }
  } else {
    return false;
  }
  const int hour = (value[0] - '0') * 10 + (value[1] - '0');
  const int min = (value[2] - '0') * 10 + (value[3] - '0');
  const int sec = (value[4] - '0') * 10 + (value[5] - '0');
  if (hour > 23 || min > 59 || sec > 59) {
    return false;
  }
  uint16_t fractional_ms = 0;
  if (value[6] == '.') {
    uint16_t scale = 100;
    for (size_t i = 7; value[i] != '\0' && scale > 0; ++i) {
      fractional_ms = static_cast<uint16_t>(fractional_ms + ((value[i] - '0') * scale));
      scale = static_cast<uint16_t>(scale / 10);
    }
  }
  *out = static_cast<uint32_t>(hour) * 3600000UL +
         static_cast<uint32_t>(min) * 60000UL +
         static_cast<uint32_t>(sec) * 1000UL +
         fractional_ms;
  return true;
}

bool is_leap_year(int year) {
  return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

uint8_t days_in_month(int year, int month) {
  static const uint8_t days_per_month[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
  };
  if (month < 1 || month > 12) {
    return 0;
  }
  if (month == 2 && is_leap_year(year)) {
    return 29;
  }
  return days_per_month[month - 1];
}

bool calendar_date_valid(uint32_t date_yyyymmdd) {
  const int year = static_cast<int>(date_yyyymmdd / 10000UL);
  const int month = static_cast<int>((date_yyyymmdd / 100UL) % 100UL);
  const int day = static_cast<int>(date_yyyymmdd % 100UL);
  const uint8_t month_days = days_in_month(year, month);
  return year >= 2020 && year <= 2099 && month_days != 0 &&
         day >= 1 && day <= month_days;
}

bool parse_date_yyyymmdd(const char *value, uint32_t *out) {
  if (!is_digit_string(value, 6) || out == nullptr) {
    return false;
  }
  const int day = (value[0] - '0') * 10 + (value[1] - '0');
  const int mon = (value[2] - '0') * 10 + (value[3] - '0');
  const int year = 2000 + ((value[4] - '0') * 10 + (value[5] - '0'));
  if (year < 2020 || mon < 1 || mon > 12) {
    return false;
  }
  const int max_day = days_in_month(year, mon);
  if (day < 1 || day > max_day) {
    return false;
  }
  *out = static_cast<uint32_t>(year) * 10000 +
         static_cast<uint32_t>(mon) * 100 +
         static_cast<uint32_t>(day);
  return true;
}

bool date_is_next_day(uint32_t previous, uint32_t current) {
  int year = static_cast<int>(previous / 10000UL);
  int month = static_cast<int>((previous / 100UL) % 100UL);
  int day = static_cast<int>(previous % 100UL);
  const uint8_t month_days = days_in_month(year, month);
  if (month_days == 0 || day < 1 || day > month_days) {
    return false;
  }
  day++;
  if (day > month_days) {
    day = 1;
    month++;
    if (month > 12) {
      month = 1;
      year++;
    }
  }
  const uint32_t expected = static_cast<uint32_t>(year) * 10000UL +
                            static_cast<uint32_t>(month) * 100UL +
                            static_cast<uint32_t>(day);
  return current == expected;
}

void saturating_add_active_time(unsigned long &value, uint32_t delta_ms) {
  value = (delta_ms > ULONG_MAX - value) ? ULONG_MAX : value + delta_ms;
}

void update_active_time_observation(uint32_t date_yyyymmdd,
                                    uint32_t time_ms_of_day,
                                    bool active) {
  if (has_activity_observation) {
    uint32_t delta_ms = 0;
    bool ordered = false;
    bool duplicate = false;
    if (date_yyyymmdd == last_activity_observation_date) {
      ordered = time_ms_of_day > last_activity_observation_ms;
      duplicate = time_ms_of_day == last_activity_observation_ms;
      if (ordered) {
        delta_ms = time_ms_of_day - last_activity_observation_ms;
      }
    } else if (date_is_next_day(last_activity_observation_date, date_yyyymmdd)) {
      ordered = true;
      delta_ms = (86400000UL - last_activity_observation_ms) + time_ms_of_day;
    }

    if (ordered && delta_ms <= GPS_ACTIVE_MAX_GAP_MS) {
      gps_activity_observation_intervals++;
      gps_last_activity_delta_ms = delta_ms;
      if (last_activity_observation_active && active) {
        const uint32_t daily_delta_ms =
            (date_yyyymmdd == last_activity_observation_date) ? delta_ms : time_ms_of_day;
        saturating_add_active_time(active_time_ms_val, daily_delta_ms);
        saturating_add_active_time(session_active_time_ms, delta_ms);
      }
    } else if (!duplicate) {
      gps_activity_gap_rejects++;
      gps_last_activity_delta_ms = 0;
    }
  }

  has_activity_observation = true;
  last_activity_observation_active = active;
  last_activity_observation_date = date_yyyymmdd;
  last_activity_observation_ms = time_ms_of_day;
}

void reset_distance_baseline(bool reset_activity = true) {
  has_last_point_val = false;
  session_has_last_point = false;
  track_current.has_last_point = false;
  last_sample_ms = 0;
  if (reset_activity) {
    has_activity_observation = false;
    last_activity_observation_active = false;
    last_activity_observation_date = 0;
    last_activity_observation_ms = 0;
  }
  gps_last_segment_m = 0.0f;
  gps_last_segment_accepted = false;
  gps_last_segment_reject_reason = "baseline";
}

void expire_gps_if_stale(unsigned long now_ms) {
  const bool uart_stale = gps_byte_observed &&
                          time_utils::elapsed_more_than(now_ms, gps_last_byte_ms,
                                                       GPS_UART_STALE_MS);
  const bool rmc_stale = gps_rmc_observed &&
                         time_utils::elapsed_more_than(now_ms, gps_last_rmc_ms,
                                                      GPS_RMC_STALE_MS);
  if (!uart_stale && !rmc_stale) {
    return;
  }

  const bool had_live_state = has_gps_fix ||
                              has_gps_fix_raw ||
                              gps_quality_ok ||
                              gps_trusted_fix ||
                              has_current_fix_val ||
                              last_speed_kph_val > 0.0f;
  if (had_live_state) {
    gps_stale_count++;
  }
  has_gps_fix = false;
  has_gps_fix_raw = false;
  gps_quality_ok = false;
  gps_trusted_fix = false;
  has_current_fix_val = false;
  last_speed_usable_val = false;
  last_speed_kph_val = 0.0f;
  reset_distance_baseline();
  clear_pending_date(true);
  gps_last_segment_reject_reason = uart_stale ? "uart_stale" : "rmc_stale";
}

uint16_t kph_to_cmps_u16_clamped(float kph) {
  if (!isfinite(kph) || kph <= 0.0f) {
    return 0;
  }
  const float cmps = kph * 27.7778f;
  if (cmps >= 65535.0f) {
    return 65535;
  }
  return static_cast<uint16_t>(cmps + 0.5f);
}

// Parse RMC sentence for position, speed, fix status, and date/time.
bool parse_rmc(const char *line,
               float *lat_deg,
               float *lon_deg,
               float *speed_kph,
               bool *valid_fix,
               uint32_t *date_yyyymmdd,
               uint16_t *time_min,
               uint32_t *time_ms_of_day) {
  if (!is_sentence_type(line, "RMC")) {
    return false;
  }

  // RMC fields:
  // 1 time, 2 status (A/V), 3 lat, 4 N/S, 5 lon, 6 E/W, 7 speed (knots), 9 date (ddmmyy)
  char time_buf[16] = {0};
  char status_buf[2] = {0};
  char lat_buf[16] = {0};
  char ns_buf[2] = {0};
  char lon_buf[16] = {0};
  char ew_buf[2] = {0};
  char speed_buf[12] = {0};
  char date_buf[8] = {0};

  if (!copy_nmea_field(line, 1, time_buf, sizeof(time_buf)) ||
      !copy_nmea_field(line, 2, status_buf, sizeof(status_buf)) ||
      !copy_nmea_field(line, 3, lat_buf, sizeof(lat_buf)) ||
      !copy_nmea_field(line, 4, ns_buf, sizeof(ns_buf)) ||
      !copy_nmea_field(line, 5, lon_buf, sizeof(lon_buf)) ||
      !copy_nmea_field(line, 6, ew_buf, sizeof(ew_buf)) ||
      !copy_nmea_field(line, 7, speed_buf, sizeof(speed_buf)) ||
      !copy_nmea_field(line, 9, date_buf, sizeof(date_buf))) {
    return false;
  }

  if (status_buf[0] != 'A' && status_buf[0] != 'V') {
    return false;
  }

  *valid_fix = (status_buf[0] == 'A');
  *speed_kph = 0.0f;
  *date_yyyymmdd = 0;
  *time_min = 0;
  *time_ms_of_day = 0;

  if (!*valid_fix) {
    return true;
  }

  float knots = 0.0f;
  if (!parse_time_of_day_ms(time_buf, time_ms_of_day) ||
      !parse_date_yyyymmdd(date_buf, date_yyyymmdd) ||
      !parse_nmea_coord(lat_buf, ns_buf[0], true, lat_deg) ||
      !parse_nmea_coord(lon_buf, ew_buf[0], false, lon_deg) ||
      !parse_float_strict(speed_buf, &knots) ||
      knots < 0.0f) {
    return false;
  }
  *time_min = static_cast<uint16_t>(*time_ms_of_day / 60000UL);
  *speed_kph = knots_to_kph(knots);
  return true;
}

// Parse GGA sentence for fix quality and satellites.
bool parse_gga(const char *line, uint8_t *fix_quality, uint8_t *sats, float *hdop) {
  if (!is_sentence_type(line, "GGA")) {
    return false;
  }

  // GGA fields: 1 time, 2 lat, 3 N/S, 4 lon, 5 E/W, 6 fix quality, 7 satellites
  char fix_buf[4] = {0};
  char sat_buf[4] = {0};
  char hdop_buf[8] = {0};

  if (!copy_nmea_field(line, 6, fix_buf, sizeof(fix_buf)) ||
      !copy_nmea_field(line, 7, sat_buf, sizeof(sat_buf)) ||
      !copy_nmea_field(line, 8, hdop_buf, sizeof(hdop_buf))) {
    return false;
  }

  uint8_t parsed_fix = 0;
  uint8_t parsed_sats = 0;
  if (!parse_uint8_strict(fix_buf, &parsed_fix) ||
      !parse_uint8_strict(sat_buf, &parsed_sats)) {
    return false;
  }
  if (parsed_fix > GPS_MIN_FIX_QUALITY_MAX) {
    return false;
  }

  float parsed_hdop = NAN;
  if (hdop_buf[0] != '\0') {
    if (!parse_float_strict(hdop_buf, &parsed_hdop) || parsed_hdop <= 0.0f) {
      return false;
    }
  } else if (parsed_fix > 0) {
    return false;
  }
  *fix_quality = parsed_fix;
  *sats = parsed_sats;
  *hdop = parsed_hdop;
  return true;
}

void reset_metrics_values() {
  current_date_yyyymmdd = 0;
  total_distance_m_val = 0.0f;
  active_time_ms_val = 0;
  max_speed_kph_val = 0.0f;
  last_update_min_val = 0;
}

bool metrics_values_valid(uint32_t date, float distance_m, uint32_t active_ms,
                          float max_speed_kph, uint16_t update_min) {
  if (!isfinite(distance_m) || distance_m < 0.0f ||
      !isfinite(max_speed_kph) || max_speed_kph < 0.0f ||
      update_min >= 1440) {
    return false;
  }
  if (date == 0) {
    return distance_m == 0.0f && active_ms == 0 &&
           max_speed_kph == 0.0f && update_min == 0;
  }
  return calendar_date_valid(date);
}

uint32_t metrics_record_crc(const MetricsRecord &record) {
  return util::crc32_ieee(&record, offsetof(MetricsRecord, crc32));
}

bool metrics_record_valid(const MetricsRecord &record) {
  return record.magic == METRICS_RECORD_MAGIC &&
         record.version == METRICS_RECORD_VERSION && record.flags == 0 &&
         record.size == sizeof(MetricsRecord) && record.reserved == 0 &&
         metrics_values_valid(record.date, record.distance_m, record.active_ms,
                              record.max_speed_kph, record.last_update_min) &&
         record.crc32 == metrics_record_crc(record);
}

bool metrics_generation_is_newer(uint32_t candidate, uint32_t reference) {
  return candidate != reference &&
         static_cast<int32_t>(candidate - reference) > 0;
}

const char *metrics_record_key(uint8_t slot) {
  return slot == 0 ? "met_a" : "met_b";
}

bool load_metrics_record(Preferences &prefs, uint8_t slot,
                         MetricsRecord &record) {
  record = MetricsRecord{};
  const size_t len = prefs.getBytes(metrics_record_key(slot), &record,
                                    sizeof(record));
  return len == sizeof(record) && metrics_record_valid(record);
}

MetricsRecord build_metrics_record(uint32_t generation) {
  MetricsRecord record = {};
  record.magic = METRICS_RECORD_MAGIC;
  record.version = METRICS_RECORD_VERSION;
  record.flags = 0;
  record.size = sizeof(MetricsRecord);
  record.generation = generation;
  record.date = current_date_yyyymmdd;
  record.distance_m = total_distance_m_val;
  record.active_ms = active_time_ms_val;
  record.max_speed_kph = max_speed_kph_val;
  record.last_update_min = last_update_min_val;
  record.reserved = 0;
  record.crc32 = metrics_record_crc(record);
  return record;
}

bool metrics_payload_matches_ram(const MetricsRecord &record) {
  return record.date == current_date_yyyymmdd &&
         record.distance_m == total_distance_m_val &&
         record.active_ms == active_time_ms_val &&
         record.max_speed_kph == max_speed_kph_val &&
         record.last_update_min == last_update_min_val;
}

void apply_metrics_record(const MetricsRecord &record, uint8_t slot) {
  current_date_yyyymmdd = record.date;
  total_distance_m_val = record.distance_m;
  active_time_ms_val = record.active_ms;
  max_speed_kph_val = record.max_speed_kph;
  last_update_min_val = record.last_update_min;
  metrics_active_slot = static_cast<int8_t>(slot);
  metrics_generation = record.generation;
  metrics_persisted = record;
  metrics_persisted_valid = true;
}

bool write_metrics_record(Preferences &prefs, uint8_t slot,
                          const MetricsRecord &record) {
  if (prefs.putBytes(metrics_record_key(slot), &record, sizeof(record)) !=
      sizeof(record)) {
    metrics_failures++;
    return false;
  }
  MetricsRecord readback = {};
  if (!load_metrics_record(prefs, slot, readback) ||
      memcmp(&record, &readback, sizeof(record)) != 0) {
    metrics_failures++;
    return false;
  }
  return true;
}

bool mark_metrics_migrated(Preferences &prefs) {
  if (prefs.getUChar("met_mig", 0) == METRICS_MIGRATION_VERSION) {
    metrics_migration_marked = true;
    return true;
  }
  if (prefs.putUChar("met_mig", METRICS_MIGRATION_VERSION) != 1) {
    metrics_migration_marked = false;
    metrics_failures++;
    return false;
  }
  metrics_migration_marked = true;
  return true;
}

// Persist one coherent metrics snapshot. The inactive slot is not selected
// until its complete record has passed CRC validation and byte-for-byte readback.
bool save_metrics() {
  if (!metrics_values_valid(current_date_yyyymmdd, total_distance_m_val,
                            active_time_ms_val, max_speed_kph_val,
                            last_update_min_val)) {
    metrics_failures++;
    return false;
  }
  if (metrics_persisted_valid && !metrics_mirror_degraded &&
      metrics_migration_marked &&
      metrics_payload_matches_ram(metrics_persisted)) {
    return true;
  }

  Preferences &prefs = storage::prefs();
  const int8_t previous_slot = metrics_active_slot;
  const uint8_t target_slot = previous_slot == 0 ? 1 : 0;
  const MetricsRecord record = build_metrics_record(metrics_generation + 1UL);
  if (!write_metrics_record(prefs, target_slot, record)) {
    return false;
  }
  apply_metrics_record(record, target_slot);
  // With no previous valid slot this first write establishes only one copy;
  // the next save repairs the mirror even if the payload has not changed.
  metrics_mirror_degraded = previous_slot < 0;
  mark_metrics_migrated(prefs);
  return true;
}

// Restore the newest complete snapshot. Legacy independent keys are consumed
// once and immediately converted, but never resurrected after migration.
void load_metrics() {
  Preferences &prefs = storage::prefs();
  MetricsRecord records[2] = {};
  const bool valid_a = load_metrics_record(prefs, 0, records[0]);
  const bool valid_b = load_metrics_record(prefs, 1, records[1]);
  if (valid_a || valid_b) {
    const uint8_t selected = (!valid_a ||
                              (valid_b && metrics_generation_is_newer(
                                              records[1].generation,
                                              records[0].generation)))
                                 ? 1
                                 : 0;
    apply_metrics_record(records[selected], selected);
    metrics_mirror_degraded = valid_a != valid_b;
    mark_metrics_migrated(prefs);
    return;
  }

  metrics_active_slot = -1;
  metrics_generation = 0;
  metrics_persisted = MetricsRecord{};
  metrics_persisted_valid = false;
  metrics_mirror_degraded = true;
  if (prefs.getUChar("met_mig", 0) == METRICS_MIGRATION_VERSION) {
    metrics_migration_marked = true;
    reset_metrics_values();
    return;
  }
  metrics_migration_marked = false;

  const uint32_t legacy_date = prefs.getUInt("date", 0);
  const float legacy_distance = prefs.getFloat("dist_m", 0.0f);
  const uint32_t legacy_active = prefs.getULong("active_ms", 0);
  const float legacy_max_speed = prefs.getFloat("max_kph", 0.0f);
  const uint16_t legacy_update = prefs.getUShort("upd_min", 0);
  if (metrics_values_valid(legacy_date, legacy_distance, legacy_active,
                           legacy_max_speed, legacy_update)) {
    current_date_yyyymmdd = legacy_date;
    total_distance_m_val = legacy_distance;
    active_time_ms_val = legacy_active;
    max_speed_kph_val = legacy_max_speed;
    last_update_min_val = legacy_update;
  } else {
    reset_metrics_values();
  }

  const MetricsRecord first = build_metrics_record(0);
  const bool wrote_a = write_metrics_record(prefs, 0, first);
  const MetricsRecord second = build_metrics_record(1);
  const bool wrote_b = write_metrics_record(prefs, 1, second);
  if (wrote_b) {
    apply_metrics_record(second, 1);
  } else if (wrote_a) {
    apply_metrics_record(first, 0);
  }
  metrics_mirror_degraded = !(wrote_a && wrote_b);
  if (wrote_a || wrote_b) {
    mark_metrics_migrated(prefs);
  }
}

uint32_t daily_journal_crc(const DailyJournalRecord &record) {
  return util::crc32_ieee(&record, offsetof(DailyJournalRecord, crc32));
}

bool daily_journal_record_valid(const DailyJournalRecord &record) {
  return record.magic == DAILY_JOURNAL_MAGIC &&
         record.version == DAILY_JOURNAL_VERSION &&
         record.flags == 0 && record.size == sizeof(DailyJournalRecord) &&
         calendar_date_valid(record.date) &&
         isfinite(record.distance_m) && record.distance_m >= 0.0f &&
         isfinite(record.max_speed_kph) && record.max_speed_kph >= 0.0f &&
         record.last_update_min < 1440 && record.reserved == 0 &&
         record.crc32 == daily_journal_crc(record);
}

const char *daily_journal_key(uint8_t slot) {
  return slot == 0 ? "day_a" : "day_b";
}

bool load_daily_journal_record(Preferences &prefs,
                               uint8_t slot,
                               DailyJournalRecord &record) {
  record = DailyJournalRecord{};
  const size_t len = prefs.getBytes(daily_journal_key(slot), &record, sizeof(record));
  return len == sizeof(record) && daily_journal_record_valid(record);
}

bool daily_generation_is_newer(uint32_t candidate, uint32_t reference) {
  return candidate != reference &&
         static_cast<int32_t>(candidate - reference) > 0;
}

void daily_journal_begin() {
  Preferences &prefs = storage::prefs();
  DailyJournalRecord records[2] = {};
  const bool valid_a = load_daily_journal_record(prefs, 0, records[0]);
  const bool valid_b = load_daily_journal_record(prefs, 1, records[1]);
  if (!valid_a && !valid_b) {
    last_completed_day = DailyJournalRecord{};
    last_completed_day_valid = false;
    daily_journal_active_slot = -1;
    daily_journal_active_generation = 0;
    return;
  }
  const uint8_t selected = (!valid_a ||
                            (valid_b && daily_generation_is_newer(records[1].generation,
                                                                  records[0].generation)))
                               ? 1
                               : 0;
  last_completed_day = records[selected];
  last_completed_day_valid = true;
  daily_journal_active_slot = static_cast<int8_t>(selected);
  daily_journal_active_generation = records[selected].generation;
}

void reconcile_metrics_with_daily_journal() {
  if (!last_completed_day_valid || current_date_yyyymmdd == 0 ||
      current_date_yyyymmdd > last_completed_day.date) {
    return;
  }
  // The completed-day journal is committed before the live counters reset.
  // If power disappears in that narrow window, an older live snapshot may
  // still describe the already completed day. Never expose it a second time.
  reset_metrics_values();
  metrics_recoveries++;
  save_metrics();
}

bool journal_current_day() {
  if (!calendar_date_valid(current_date_yyyymmdd)) {
    return false;
  }
  DailyJournalRecord record = {};
  record.magic = DAILY_JOURNAL_MAGIC;
  record.version = DAILY_JOURNAL_VERSION;
  record.flags = 0;
  record.size = sizeof(DailyJournalRecord);
  record.generation = daily_journal_active_generation + 1UL;
  record.date = current_date_yyyymmdd;
  record.distance_m = total_distance_m_val;
  record.active_ms = active_time_ms_val;
  record.max_speed_kph = max_speed_kph_val;
  record.last_update_min = last_update_min_val;
  record.reserved = 0;
  record.crc32 = daily_journal_crc(record);

  const uint8_t target_slot = daily_journal_active_slot == 0 ? 1 : 0;
  Preferences &prefs = storage::prefs();
  if (prefs.putBytes(daily_journal_key(target_slot), &record, sizeof(record)) != sizeof(record)) {
    daily_journal_failures++;
    return false;
  }
  DailyJournalRecord readback = {};
  if (!load_daily_journal_record(prefs, target_slot, readback) ||
      memcmp(&record, &readback, sizeof(record)) != 0) {
    daily_journal_failures++;
    return false;
  }
  last_completed_day = readback;
  last_completed_day_valid = true;
  daily_journal_active_slot = static_cast<int8_t>(target_slot);
  daily_journal_active_generation = readback.generation;
  return true;
}

void clear_pending_date(bool rejected) {
  if (rejected && pending_date_candidate != 0) {
    gps_date_rejected_count++;
  }
  pending_date_candidate = 0;
  pending_date_last_time_ms = 0;
  pending_date_observations = 0;
}

bool activate_daily_date(uint32_t date_yyyymmdd, uint32_t time_ms_of_day) {
  const bool had_current_day = calendar_date_valid(current_date_yyyymmdd);
  if (had_current_day && !journal_current_day()) {
    return false;
  }
  current_date_yyyymmdd = date_yyyymmdd;
  total_distance_m_val = 0.0f;
  active_time_ms_val = 0;
  max_speed_kph_val = 0.0f;
  last_update_min_val = static_cast<uint16_t>(time_ms_of_day / 60000UL);
  reset_distance_baseline(false);
  save_metrics();
  if (had_current_day) {
    gps_date_transition_count++;
  }
  return true;
}

bool accept_date_observation(uint32_t date_yyyymmdd, uint32_t time_ms_of_day) {
  if (!calendar_date_valid(date_yyyymmdd) || time_ms_of_day >= 86400000UL) {
    clear_pending_date(true);
    gps_date_rejected_count++;
    return false;
  }
  if (!calendar_date_valid(current_date_yyyymmdd)) {
    if (!activate_daily_date(date_yyyymmdd, time_ms_of_day)) {
      return false;
    }
    has_accepted_date_observation = true;
    last_accepted_date_observation_ms = time_ms_of_day;
    clear_pending_date(false);
    return true;
  }
  if (date_yyyymmdd == current_date_yyyymmdd) {
    clear_pending_date(true);
    if (!has_accepted_date_observation ||
        time_ms_of_day >= last_accepted_date_observation_ms) {
      has_accepted_date_observation = true;
      last_accepted_date_observation_ms = time_ms_of_day;
    }
    return true;
  }
  if (date_yyyymmdd < current_date_yyyymmdd) {
    clear_pending_date(true);
    gps_date_rejected_count++;
    return false;
  }

  const bool next_day = date_is_next_day(current_date_yyyymmdd, date_yyyymmdd);
  const uint32_t midnight_delta_ms =
      (has_accepted_date_observation && next_day)
          ? (86400000UL - last_accepted_date_observation_ms) + time_ms_of_day
          : UINT32_MAX;
  if (midnight_delta_ms <= GPS_DATE_CONFIRM_MAX_GAP_MS) {
    if (!activate_daily_date(date_yyyymmdd, time_ms_of_day)) {
      pending_date_candidate = date_yyyymmdd;
      pending_date_last_time_ms = time_ms_of_day;
      pending_date_observations = GPS_DATE_CONFIRM_OBSERVATIONS;
      return false;
    }
    has_accepted_date_observation = true;
    last_accepted_date_observation_ms = time_ms_of_day;
    clear_pending_date(false);
    return true;
  }

  if (pending_date_candidate != date_yyyymmdd) {
    clear_pending_date(true);
    pending_date_candidate = date_yyyymmdd;
    pending_date_last_time_ms = time_ms_of_day;
    pending_date_observations = 1;
  } else if (time_ms_of_day > pending_date_last_time_ms &&
             time_ms_of_day - pending_date_last_time_ms <= GPS_DATE_CONFIRM_MAX_GAP_MS) {
    pending_date_last_time_ms = time_ms_of_day;
    if (pending_date_observations < GPS_DATE_CONFIRM_OBSERVATIONS) {
      pending_date_observations++;
    }
  } else if (time_ms_of_day != pending_date_last_time_ms) {
    gps_date_rejected_count++;
    pending_date_last_time_ms = time_ms_of_day;
    pending_date_observations = 1;
  }

  if (pending_date_observations < GPS_DATE_CONFIRM_OBSERVATIONS) {
    return false;
  }
  if (!activate_daily_date(date_yyyymmdd, time_ms_of_day)) {
    return false;
  }
  has_accepted_date_observation = true;
  last_accepted_date_observation_ms = time_ms_of_day;
  clear_pending_date(false);
  return true;
}

uint8_t session_checksum(const SessionSummary &s) {
  const uint8_t *p = reinterpret_cast<const uint8_t *>(&s);
  const size_t len = offsetof(SessionSummary, crc);
  uint8_t out = 0;
  for (size_t i = 0; i < len; ++i) {
    out ^= p[i];
  }
  return out;
}

void session_write_crc(SessionSummary &s) {
  s.crc = session_checksum(s);
}

void session_zero(SessionSummary &s) {
  memset(&s, 0, sizeof(s));
  s.ver = SESSION_VER;
  s.pad = 0;
  session_write_crc(s);
}

bool session_payload_is_zero(const SessionSummary &s) {
  return s.start_date == 0 && s.start_min == 0 && s.end_date == 0 &&
         s.end_min == 0 && s.distance_m == 0 && s.active_s == 0 &&
         s.avg_speed_cmps == 0 && s.max_speed_cmps == 0;
}

bool session_is_valid(const SessionSummary &s) {
  const uint8_t known_flags = SESSION_FLAG_GPS_FIX | SESSION_FLAG_HAS_DATA |
                              SESSION_FLAG_IN_PROGRESS | SESSION_FLAG_NO_FIX;
  if (s.ver != SESSION_VER || (s.flags & ~known_flags) != 0 || s.pad != 0 ||
      s.crc != session_checksum(s)) {
    return false;
  }

  const bool has_fix = (s.flags & SESSION_FLAG_GPS_FIX) != 0;
  const bool has_data = (s.flags & SESSION_FLAG_HAS_DATA) != 0;
  const bool in_progress = (s.flags & SESSION_FLAG_IN_PROGRESS) != 0;
  const bool no_fix = (s.flags & SESSION_FLAG_NO_FIX) != 0;
  if (!has_fix) {
    const bool blank = !has_data && !in_progress && !no_fix;
    const bool open_empty = !has_data && in_progress && !no_fix;
    const bool closed_no_fix = !has_data && !in_progress && no_fix;
    return (blank || open_empty || closed_no_fix) && session_payload_is_zero(s);
  }
  if (!has_data || no_fix || !calendar_date_valid(s.start_date) ||
      !calendar_date_valid(s.end_date) || s.start_min >= 1440 ||
      s.end_min >= 1440 || s.start_date > s.end_date ||
      (s.start_date == s.end_date && s.start_min > s.end_min)) {
    return false;
  }
  const uint64_t calculated_avg =
      s.active_s == 0
          ? 0
          : (static_cast<uint64_t>(s.distance_m) * 100ULL) / s.active_s;
  const uint64_t expected_avg = calculated_avg > 65535ULL
                                    ? 65535ULL
                                    : calculated_avg;
  return s.avg_speed_cmps == static_cast<uint16_t>(expected_avg);
}

bool session_is_blank(const SessionSummary &s) {
  return session_is_valid(s) && s.flags == 0 && session_payload_is_zero(s);
}

bool session_is_history_entry(const SessionSummary &s) {
  return session_is_valid(s) &&
         (s.flags & SESSION_FLAG_IN_PROGRESS) == 0 &&
         (s.flags & (SESSION_FLAG_GPS_FIX | SESSION_FLAG_NO_FIX)) != 0;
}

SessionSummary build_session_snapshot(bool finalize) {
  SessionSummary s = {};
  s.ver = SESSION_VER;
  s.flags = 0;
  const bool has_data = session_fix_seen && session_start_set &&
                        calendar_date_valid(session_start_date) &&
                        calendar_date_valid(session_end_date);
  if (has_data) {
    s.flags |= SESSION_FLAG_GPS_FIX;
    s.flags |= SESSION_FLAG_HAS_DATA;
  }
  if (session_open) {
    s.flags |= SESSION_FLAG_IN_PROGRESS;
  }
  if (finalize && !has_data) {
    s.flags |= SESSION_FLAG_NO_FIX;
  }
  if (has_data) {
    s.start_date = session_start_date;
    s.start_min = session_start_min;
    s.end_date = session_end_date;
    s.end_min = session_end_min;
    const uint32_t distance_m = !isfinite(session_total_distance_m) ||
                                        session_total_distance_m <= 0.0f
                                    ? 0
                                    : session_total_distance_m >=
                                              static_cast<float>(UINT32_MAX)
                                          ? UINT32_MAX
                                          : static_cast<uint32_t>(
                                                session_total_distance_m + 0.5f);
    s.distance_m = distance_m;
    s.active_s = static_cast<uint32_t>(session_active_time_ms / 1000);
    uint32_t avg_cmps = 0;
    if (s.active_s > 0) {
      const uint64_t calculated =
          (static_cast<uint64_t>(distance_m) * 100ULL) / s.active_s;
      avg_cmps = static_cast<uint32_t>(calculated > 65535ULL
                                           ? 65535ULL
                                           : calculated);
    }
    s.avg_speed_cmps = static_cast<uint16_t>(avg_cmps);
    s.max_speed_cmps = kph_to_cmps_u16_clamped(session_max_speed_kph);
  }
  s.pad = 0;
  session_write_crc(s);
  return s;
}

void finalize_snapshot(SessionSummary &s) {
  s.flags &= static_cast<uint8_t>(~SESSION_FLAG_IN_PROGRESS);
  if ((s.flags & SESSION_FLAG_GPS_FIX) == 0) {
    s.flags |= SESSION_FLAG_NO_FIX;
    s.flags &= static_cast<uint8_t>(~SESSION_FLAG_HAS_DATA);
    s.start_date = 0;
    s.start_min = 0;
    s.end_date = 0;
    s.end_min = 0;
    s.distance_m = 0;
    s.active_s = 0;
    s.avg_speed_cmps = 0;
    s.max_speed_cmps = 0;
  }
  session_write_crc(s);
}

void history_clear_ram() {
  history_count = 0;
  history_idx = 0;
  for (uint8_t i = 0; i < HISTORY_MAX; ++i) {
    session_zero(history[i]);
  }
}

bool history_shape_valid(uint8_t count, uint8_t idx) {
  return count <= HISTORY_MAX && idx < HISTORY_MAX &&
         (count == HISTORY_MAX || idx == count);
}

uint32_t session_store_crc(const SessionStoreRecord &record) {
  return util::crc32_ieee(&record, offsetof(SessionStoreRecord, crc32));
}

bool session_store_record_valid(const SessionStoreRecord &record) {
  if (record.magic != SESSION_STORE_MAGIC ||
      record.version != SESSION_STORE_VERSION ||
      (record.flags & ~SESSION_STORE_FLAG_OPEN) != 0 ||
      record.size != sizeof(SessionStoreRecord) || record.reserved != 0 ||
      !history_shape_valid(record.history_count, record.history_idx)) {
    return false;
  }
  for (uint8_t i = 0; i < HISTORY_MAX; ++i) {
    const bool used = record.history_count == HISTORY_MAX ||
                      i < record.history_count;
    if ((used && !session_is_history_entry(record.history[i])) ||
        (!used && !session_is_blank(record.history[i]))) {
      return false;
    }
  }
  const bool open = (record.flags & SESSION_STORE_FLAG_OPEN) != 0;
  if ((open && (!session_is_valid(record.current) ||
                (record.current.flags & SESSION_FLAG_IN_PROGRESS) == 0)) ||
      (!open && !session_is_blank(record.current))) {
    return false;
  }
  return record.crc32 == session_store_crc(record);
}

bool session_store_generation_is_newer(uint32_t candidate,
                                       uint32_t reference) {
  return candidate != reference &&
         static_cast<int32_t>(candidate - reference) > 0;
}

const char *session_store_key(uint8_t slot) {
  return slot == 0 ? "ses_a" : "ses_b";
}

bool load_session_store_record(Preferences &prefs, uint8_t slot,
                               SessionStoreRecord &record) {
  record = SessionStoreRecord{};
  const size_t len = prefs.getBytes(session_store_key(slot), &record,
                                    sizeof(record));
  return len == sizeof(record) && session_store_record_valid(record);
}

SessionStoreRecord build_session_store_record(const SessionSummary &current,
                                               bool open,
                                               uint32_t generation) {
  SessionStoreRecord record = {};
  record.magic = SESSION_STORE_MAGIC;
  record.version = SESSION_STORE_VERSION;
  record.flags = open ? SESSION_STORE_FLAG_OPEN : 0;
  record.size = sizeof(SessionStoreRecord);
  record.generation = generation;
  record.history_count = history_count;
  record.history_idx = history_idx;
  record.reserved = 0;
  memcpy(record.history, history, sizeof(history));
  record.current = current;
  record.crc32 = session_store_crc(record);
  return record;
}

bool session_store_payload_matches(const SessionStoreRecord &record,
                                   const SessionSummary &current, bool open) {
  return record.flags == (open ? SESSION_STORE_FLAG_OPEN : 0) &&
         record.history_count == history_count &&
         record.history_idx == history_idx &&
         memcmp(record.history, history, sizeof(history)) == 0 &&
         memcmp(&record.current, &current, sizeof(current)) == 0;
}

void apply_session_store_record(const SessionStoreRecord &record,
                                uint8_t slot) {
  history_count = record.history_count;
  history_idx = record.history_idx;
  memcpy(history, record.history, sizeof(history));
  boot_session_snapshot = record.current;
  boot_session_open = (record.flags & SESSION_STORE_FLAG_OPEN) != 0;
  session_store_active_slot = static_cast<int8_t>(slot);
  session_store_generation = record.generation;
  session_store_persisted = record;
  session_store_persisted_valid = true;
}

bool write_session_store_record(Preferences &prefs, uint8_t slot,
                                const SessionStoreRecord &record) {
  if (prefs.putBytes(session_store_key(slot), &record, sizeof(record)) !=
      sizeof(record)) {
    session_store_failures++;
    return false;
  }
  SessionStoreRecord readback = {};
  if (!load_session_store_record(prefs, slot, readback) ||
      memcmp(&record, &readback, sizeof(record)) != 0) {
    session_store_failures++;
    return false;
  }
  return true;
}

bool mark_session_store_migrated(Preferences &prefs) {
  if (prefs.getUChar("ses_mig", 0) == SESSION_STORE_MIGRATION_VERSION) {
    session_store_migration_marked = true;
    return true;
  }
  if (prefs.putUChar("ses_mig", SESSION_STORE_MIGRATION_VERSION) != 1) {
    session_store_migration_marked = false;
    session_store_failures++;
    return false;
  }
  session_store_migration_marked = true;
  return true;
}

bool save_session_store(const SessionSummary &current, bool open) {
  const SessionStoreRecord candidate = build_session_store_record(
      current, open, session_store_generation + 1UL);
  if (!session_store_record_valid(candidate)) {
    session_store_failures++;
    return false;
  }
  if (session_store_persisted_valid && !session_store_mirror_degraded &&
      session_store_migration_marked &&
      session_store_payload_matches(session_store_persisted, current, open)) {
    return true;
  }

  Preferences &prefs = storage::prefs();
  const int8_t previous_slot = session_store_active_slot;
  const uint8_t target_slot = previous_slot == 0 ? 1 : 0;
  if (!write_session_store_record(prefs, target_slot, candidate)) {
    return false;
  }
  session_store_active_slot = static_cast<int8_t>(target_slot);
  session_store_generation = candidate.generation;
  session_store_persisted = candidate;
  session_store_persisted_valid = true;
  session_store_mirror_degraded = previous_slot < 0;
  mark_session_store_migrated(prefs);
  return true;
}

bool load_legacy_history(Preferences &prefs) {
  history_clear_ram();
  if (prefs.getUChar("h_ver", 0) != SESSION_VER) {
    return false;
  }
  const uint8_t count = prefs.getUChar("h_cnt", 0);
  const uint8_t idx = prefs.getUChar("h_idx", 0);
  if (!history_shape_valid(count, idx)) {
    return false;
  }
  SessionSummary loaded[HISTORY_MAX] = {};
  for (uint8_t i = 0; i < HISTORY_MAX; ++i) {
    session_zero(loaded[i]);
    if (count != HISTORY_MAX && i >= count) {
      continue;
    }
    const char key[3] = {'h', static_cast<char>('0' + i), '\0'};
    if (prefs.getBytes(key, &loaded[i], sizeof(SessionSummary)) !=
            sizeof(SessionSummary) ||
        !session_is_history_entry(loaded[i])) {
      history_clear_ram();
      return false;
    }
  }
  history_count = count;
  history_idx = idx;
  memcpy(history, loaded, sizeof(history));
  return true;
}

void history_load() {
  Preferences &prefs = storage::prefs();
  SessionStoreRecord records[2] = {};
  const bool valid_a = load_session_store_record(prefs, 0, records[0]);
  const bool valid_b = load_session_store_record(prefs, 1, records[1]);
  if (valid_a || valid_b) {
    const uint8_t selected = (!valid_a ||
                              (valid_b && session_store_generation_is_newer(
                                              records[1].generation,
                                              records[0].generation)))
                                 ? 1
                                 : 0;
    apply_session_store_record(records[selected], selected);
    session_store_mirror_degraded = valid_a != valid_b;
    mark_session_store_migrated(prefs);
    return;
  }

  history_clear_ram();
  session_zero(boot_session_snapshot);
  boot_session_open = false;
  session_store_active_slot = -1;
  session_store_generation = 0;
  session_store_persisted = SessionStoreRecord{};
  session_store_persisted_valid = false;
  session_store_mirror_degraded = true;
  if (prefs.getUChar("ses_mig", 0) == SESSION_STORE_MIGRATION_VERSION) {
    session_store_migration_marked = true;
    return;
  }
  session_store_migration_marked = false;

  load_legacy_history(prefs);
  if (prefs.getUChar("s_open", 0) == 1) {
    SessionSummary legacy_current = {};
    const size_t len = prefs.getBytes("s_cur", &legacy_current,
                                      sizeof(legacy_current));
    if (len == sizeof(legacy_current) && session_is_valid(legacy_current) &&
        (legacy_current.flags & SESSION_FLAG_IN_PROGRESS) != 0) {
      boot_session_snapshot = legacy_current;
    } else {
      session_zero(boot_session_snapshot);
      boot_session_snapshot.flags |= SESSION_FLAG_IN_PROGRESS;
      session_write_crc(boot_session_snapshot);
    }
    boot_session_open = true;
  }

  const SessionStoreRecord first = build_session_store_record(
      boot_session_snapshot, boot_session_open, 0);
  const bool wrote_a = write_session_store_record(prefs, 0, first);
  const SessionStoreRecord second = build_session_store_record(
      boot_session_snapshot, boot_session_open, 1);
  const bool wrote_b = write_session_store_record(prefs, 1, second);
  if (wrote_b) {
    apply_session_store_record(second, 1);
  } else if (wrote_a) {
    apply_session_store_record(first, 0);
  }
  session_store_mirror_degraded = !(wrote_a && wrote_b);
  if (wrote_a || wrote_b) {
    mark_session_store_migrated(prefs);
  }
}

void history_push(SessionSummary s) {
  if (!session_is_history_entry(s)) {
    return;
  }
  history[history_idx] = s;
  history_idx = static_cast<uint8_t>((history_idx + 1) % HISTORY_MAX);
  if (history_count < HISTORY_MAX) {
    history_count++;
  }
}

void save_session_snapshot_if_needed() {
  if (!session_open) {
    return;
  }
  SessionSummary snap = build_session_snapshot(false);
  if ((!session_snapshot_valid ||
       memcmp(&snap, &session_snapshot_last, sizeof(SessionSummary)) != 0 ||
       session_store_mirror_degraded || !session_store_migration_marked) &&
      save_session_store(snap, true)) {
    session_snapshot_last = snap;
    session_snapshot_valid = true;
  }
}

void session_close_previous_on_boot() {
  if (!boot_session_open) {
    return;
  }
  SessionSummary previous = boot_session_snapshot;
  finalize_snapshot(previous);
  history_push(previous);
  session_store_recoveries++;
  session_zero(boot_session_snapshot);
  boot_session_open = false;
}

void session_begin() {
  session_open = true;
  session_fix_seen = false;
  session_start_set = false;
  session_start_date = 0;
  session_start_min = 0;
  session_end_date = 0;
  session_end_min = 0;
  session_active_time_ms = 0;
  session_total_distance_m = 0.0f;
  session_max_speed_kph = 0.0f;
  session_has_last_point = false;
  session_last_lat_deg = 0.0f;
  session_last_lon_deg = 0.0f;
  SessionSummary snap = build_session_snapshot(false);
  session_snapshot_valid = false;
  if (save_session_store(snap, true)) {
    session_snapshot_last = snap;
    session_snapshot_valid = true;
  }
}

uint32_t track_meta_crc(const TrackMeta &m) {
  const uint8_t *p = reinterpret_cast<const uint8_t *>(&m);
  return util::crc32_ieee(p, offsetof(TrackMeta, crc32));
}

void track_key_meta(char *out, uint8_t slot) {
  snprintf(out, 5, "t%um", slot);
}

void track_key_chunk(char *out, uint8_t slot, uint8_t idx) {
  snprintf(out, 6, "t%uc%02u", slot, idx);
}

bool track_meta_fields_valid(const TrackMeta &meta) {
  if (meta.sample_ms != TRACK_SAMPLE_MS || meta.max_points != TRACK_MAX_POINTS ||
      meta.chunk_head >= TRACK_MAX_CHUNKS || meta.chunk_count > TRACK_MAX_CHUNKS ||
      meta.total_points > TRACK_STORAGE_POINTS ||
      (meta.flags & ~(TRACK_FLAG_OPEN | TRACK_FLAG_BBOX_DIRTY)) != 0) {
    return false;
  }
  if ((meta.chunk_count == 0) != (meta.total_points == 0)) {
    return false;
  }
  if (meta.chunk_count > 0) {
    const uint16_t min_points = static_cast<uint16_t>(
        (static_cast<uint16_t>(meta.chunk_count - 1) * TRACK_CHUNK_POINTS) + 1);
    const uint16_t max_points = static_cast<uint16_t>(
        static_cast<uint16_t>(meta.chunk_count) * TRACK_CHUNK_POINTS);
    if (meta.total_points < min_points || meta.total_points > max_points) {
      return false;
    }
  }
  if ((meta.start_date != 0 && meta.start_min >= 1440) ||
      (meta.end_date != 0 && meta.end_min >= 1440)) {
    return false;
  }
  return true;
}

TrackMeta track_load_meta(Preferences &prefs, uint8_t slot) {
  TrackMeta meta = {};
  char key[5];
  track_key_meta(key, slot);
  size_t len = prefs.getBytes(key, &meta, sizeof(meta));
  if (len != sizeof(meta) || meta.ver != TRACK_VER ||
      meta.crc32 != track_meta_crc(meta) || !track_meta_fields_valid(meta)) {
    memset(&meta, 0, sizeof(meta));
    meta.ver = TRACK_VER;
  }
  return meta;
}

bool track_save_meta(Preferences &prefs, uint8_t slot, TrackMeta &meta) {
  meta.ver = TRACK_VER;
  meta.crc32 = track_meta_crc(meta);
  char key[5];
  track_key_meta(key, slot);
  return prefs.putBytes(key, &meta, sizeof(meta)) == sizeof(meta);
}

bool track_point_valid(const TrackPoint &point) {
  return point.lat_e7 >= -900000000 && point.lat_e7 <= 900000000 &&
         point.lon_e7 >= -1800000000 && point.lon_e7 <= 1800000000 &&
         point.t_min < 1440;
}

bool track_load_chunk(Preferences &prefs,
                      uint8_t slot,
                      uint8_t idx,
                      TrackPoint *points,
                      uint8_t &count) {
  count = 0;
  uint8_t buffer[sizeof(TrackChunkHeader) + (TRACK_CHUNK_POINTS * sizeof(TrackPoint))];
  char key[6];
  track_key_chunk(key, slot, idx);
  const size_t stored_len = prefs.getBytesLength(key);
  if (stored_len < sizeof(TrackChunkHeader) + sizeof(TrackPoint) ||
      stored_len > sizeof(buffer)) {
    return false;
  }
  const size_t len = prefs.getBytes(key, buffer, sizeof(buffer));
  if (len != stored_len) {
    return false;
  }

  TrackChunkHeader hdr = {};
  memcpy(&hdr, buffer, sizeof(hdr));
  if (hdr.ver != TRACK_VER || hdr.count == 0 || hdr.count > TRACK_CHUNK_POINTS ||
      hdr.flags != 0 || hdr.first_t_min >= 1440) {
    return false;
  }
  const size_t expected_len = sizeof(TrackChunkHeader) +
                              (static_cast<size_t>(hdr.count) * sizeof(TrackPoint));
  if (len != expected_len) {
    return false;
  }

  const uint32_t stored_crc = hdr.crc32;
  hdr.crc32 = 0;
  memcpy(buffer, &hdr, sizeof(hdr));
  if (stored_crc != util::crc32_ieee(buffer, len)) {
    return false;
  }

  memcpy(points, buffer + sizeof(TrackChunkHeader), hdr.count * sizeof(TrackPoint));
  for (uint8_t i = 0; i < hdr.count; ++i) {
    if (!track_point_valid(points[i])) {
      return false;
    }
  }
  if (points[0].t_min != hdr.first_t_min) {
    return false;
  }
  count = hdr.count;
  return true;
}

void track_clear_slot(Preferences &prefs, uint8_t slot) {
  char key[6];
  track_key_meta(key, slot);
  prefs.remove(key);
  for (uint8_t i = 0; i < TRACK_MAX_CHUNKS; ++i) {
    track_key_chunk(key, slot, i);
    prefs.remove(key);
  }
}

void track_clear_all(Preferences &prefs) {
  for (uint8_t i = 0; i < TRACK_SLOTS; ++i) {
    track_clear_slot(prefs, i);
  }
  prefs.remove("t_idx");
  prefs.remove("t_open");
  prefs.putUChar("t_ver", TRACK_VER);
}

void track_reset_ram() {
  memset(&track_current, 0, sizeof(track_current));
  track_current.last_flush_ms = 0;
  track_current.last_sample_ms = 0;
  track_current.persisted_flush_count = 0;
  track_current.persisted_points = 0;
  track_current.chunk_head = 0;
  track_current.chunk_count = 0;
  track_current.bbox_dirty = false;
  track_current.meta_dirty = false;
  track_current.has_bbox = false;
  track_current.has_last_point = false;
}

void track_open_new(Preferences &prefs, uint8_t slot) {
  track_reset_ram();
  TrackMeta meta = {};
  meta.ver = TRACK_VER;
  meta.flags = TRACK_FLAG_OPEN;
  meta.sample_ms = static_cast<uint16_t>(TRACK_SAMPLE_MS);
  meta.max_points = TRACK_MAX_POINTS;
  meta.total_points = 0;
  meta.chunk_head = 0;
  meta.chunk_count = 0;
  meta.start_date = 0;
  meta.start_min = 0;
  meta.end_date = 0;
  meta.end_min = 0;
  meta.min_lat_e7 = 0;
  meta.max_lat_e7 = 0;
  meta.min_lon_e7 = 0;
  meta.max_lon_e7 = 0;
  track_save_meta(prefs, slot, meta);
}

bool track_save_current_meta(Preferences &prefs) {
  TrackMeta meta = track_load_meta(prefs, track_slot);
  meta.flags = static_cast<uint8_t>(meta.flags | TRACK_FLAG_OPEN);
  if (track_current.bbox_dirty) {
    meta.flags = static_cast<uint8_t>(meta.flags | TRACK_FLAG_BBOX_DIRTY);
  } else {
    meta.flags = static_cast<uint8_t>(meta.flags & ~TRACK_FLAG_BBOX_DIRTY);
    if (track_current.has_bbox) {
      meta.min_lat_e7 = track_current.min_lat_e7;
      meta.max_lat_e7 = track_current.max_lat_e7;
      meta.min_lon_e7 = track_current.min_lon_e7;
      meta.max_lon_e7 = track_current.max_lon_e7;
    }
  }
  meta.sample_ms = static_cast<uint16_t>(TRACK_SAMPLE_MS);
  meta.max_points = TRACK_MAX_POINTS;
  meta.total_points = track_current.persisted_points;
  meta.chunk_head = track_current.chunk_head;
  meta.chunk_count = track_current.chunk_count;
  meta.start_date = track_current.start_date;
  meta.start_min = track_current.start_min;
  meta.end_date = track_current.end_date;
  meta.end_min = track_current.end_min;
  return track_save_meta(prefs, track_slot, meta);
}

void track_begin() {
  Preferences &prefs = storage::prefs_trk();
  uint8_t ver = prefs.getUChar("t_ver", 0);
  if (ver != TRACK_VER) {
    track_clear_all(prefs);
  }
  uint8_t prev_slot = prefs.getUChar("t_idx", 0);
  if (prev_slot >= TRACK_SLOTS) {
    prev_slot = 0;
  }
  const uint8_t open = prefs.getUChar("t_open", 0);
  if (open == 1) {
    TrackMeta prev = track_load_meta(prefs, prev_slot);
    prev.flags = static_cast<uint8_t>(prev.flags & ~TRACK_FLAG_OPEN);
    track_save_meta(prefs, prev_slot, prev);
    prefs.putUChar("t_open", 0);
    const uint8_t next_slot = static_cast<uint8_t>((prev_slot + 1) % TRACK_SLOTS);
    track_clear_slot(prefs, next_slot);
    track_slot = next_slot;
  } else {
    track_slot = prev_slot;
    track_clear_slot(prefs, track_slot);
  }
  prefs.putUChar("t_idx", track_slot);
  track_open_new(prefs, track_slot);
  prefs.putUChar("t_open", 1);
}

void track_flush_if_due(unsigned long now_ms) {
  // Rewriting or rotating a chunk mid-export would make the reader see a
  // different ring than the one it measured.
  if (track_export_active) {
    return;
  }
  Preferences &prefs = storage::prefs_trk();
  if (track_current.meta_dirty) {
    if (!track_save_current_meta(prefs)) {
      return;
    }
    track_current.meta_dirty = false;
  }
  if (track_current.flush_count == 0) {
    return;
  }
  if (track_current.flush_count <= track_current.persisted_flush_count) {
    return;
  }
  const bool due_time = (now_ms - track_current.last_flush_ms) >= TRACK_FLUSH_MS;
  const bool full = (track_current.flush_count >= TRACK_CHUNK_POINTS);
  if (!due_time && !full) {
    return;
  }

  uint8_t write_idx = 0;
  uint8_t next_chunk_head = track_current.chunk_head;
  uint8_t next_chunk_count = track_current.chunk_count;
  bool overwrote_oldest = false;
  const bool rewrite_active = (track_current.persisted_flush_count > 0 && track_current.chunk_count > 0);
  if (rewrite_active) {
    write_idx = static_cast<uint8_t>(
        (track_current.chunk_head + track_current.chunk_count - 1) % TRACK_MAX_CHUNKS);
  } else if (track_current.chunk_count < TRACK_MAX_CHUNKS) {
    write_idx = static_cast<uint8_t>(
        (track_current.chunk_head + track_current.chunk_count) % TRACK_MAX_CHUNKS);
    next_chunk_count++;
  } else {
    write_idx = track_current.chunk_head;
    next_chunk_head = static_cast<uint8_t>((track_current.chunk_head + 1) % TRACK_MAX_CHUNKS);
    overwrote_oldest = true;
  }

  TrackChunkHeader hdr = {};
  hdr.ver = TRACK_VER;
  hdr.count = track_current.flush_count;
  hdr.first_t_min = track_current.flush_buf[0].t_min;
  hdr.flags = 0;
  hdr.crc32 = 0;

  const size_t blob_len = sizeof(hdr) + (static_cast<size_t>(track_current.flush_count) * sizeof(TrackPoint));
  uint8_t blob[sizeof(TrackChunkHeader) + (TRACK_CHUNK_POINTS * sizeof(TrackPoint))];
  memcpy(blob, &hdr, sizeof(hdr));
  memcpy(blob + sizeof(hdr), track_current.flush_buf, track_current.flush_count * sizeof(TrackPoint));
  hdr.crc32 = util::crc32_ieee(blob, blob_len);
  memcpy(blob, &hdr, sizeof(hdr));

  char key[6];
  track_key_chunk(key, track_slot, write_idx);
  if (prefs.putBytes(key, blob, blob_len) != blob_len) {
    return;
  }

  uint16_t next_persisted_points = track_current.persisted_points;
  if (overwrote_oldest) {
    next_persisted_points = (next_persisted_points >= TRACK_CHUNK_POINTS)
                                ? static_cast<uint16_t>(next_persisted_points - TRACK_CHUNK_POINTS)
                                : 0;
  }
  const uint8_t added_points = static_cast<uint8_t>(
      track_current.flush_count - track_current.persisted_flush_count);
  const uint32_t next_total = static_cast<uint32_t>(next_persisted_points) + added_points;
  track_current.persisted_points = static_cast<uint16_t>(
      (next_total > TRACK_STORAGE_POINTS) ? TRACK_STORAGE_POINTS : next_total);
  track_current.chunk_head = next_chunk_head;
  track_current.chunk_count = next_chunk_count;
  track_current.persisted_flush_count = track_current.flush_count;
  if (overwrote_oldest) {
    track_current.bbox_dirty = true;
  }

  track_current.meta_dirty = !track_save_current_meta(prefs);

  if (full) {
    track_current.flush_count = 0;
    track_current.persisted_flush_count = 0;
  }
  track_current.last_flush_ms = now_ms;
}

void track_try_add_point(float lat_deg, float lon_deg, uint16_t t_min, uint32_t date_yyyymmdd, unsigned long now_ms) {
  // Appending here would overwrite the RAM tail the export is streaming, and
  // with flushing paused flush_count could run past the end of flush_buf.
  // A handful of samples are dropped while the user downloads their track.
  if (track_export_active || date_yyyymmdd == 0) {
    return;
  }
  if (track_current.last_sample_ms == 0 ||
      (now_ms - track_current.last_sample_ms) >= TRACK_SAMPLE_MS) {
    track_current.last_sample_ms = now_ms;
  } else {
    return;
  }

  const int32_t lat_e7 = static_cast<int32_t>(lroundf(lat_deg * 1e7f));
  const int32_t lon_e7 = static_cast<int32_t>(lroundf(lon_deg * 1e7f));

  if (!track_current.has_last_point) {
    track_current.last_point = {lat_e7, lon_e7, t_min};
    track_current.has_last_point = true;
  } else {
    const float last_lat = track_current.last_point.lat_e7 * 1e-7f;
    const float last_lon = track_current.last_point.lon_e7 * 1e-7f;
    const float segment_m = haversine_m(last_lat, last_lon, lat_deg, lon_deg);
    const RuntimeConfig &cfg = config::get();
    float min_segment_m = cfg.gps_min_segment_m;
    if (!isnan(gps_hdop) && gps_hdop > 0.0f) {
      const float hdop_segment = cfg.gps_hdop_factor * gps_hdop;
      if (hdop_segment > min_segment_m) {
        min_segment_m = hdop_segment;
      }
    }
    if (min_segment_m > cfg.gps_max_min_segment_m) {
      min_segment_m = cfg.gps_max_min_segment_m;
    }
    if (segment_m < min_segment_m) {
      return;
    }
  }

  if (track_current.start_date == 0) {
    track_current.start_date = date_yyyymmdd;
    track_current.start_min = t_min;
  }
  track_current.end_date = date_yyyymmdd;
  track_current.end_min = t_min;

  if (!track_current.has_bbox) {
    track_current.min_lat_e7 = lat_e7;
    track_current.max_lat_e7 = lat_e7;
    track_current.min_lon_e7 = lon_e7;
    track_current.max_lon_e7 = lon_e7;
    track_current.has_bbox = true;
  } else {
    if (lat_e7 < track_current.min_lat_e7) {
      track_current.min_lat_e7 = lat_e7;
    }
    if (lat_e7 > track_current.max_lat_e7) {
      track_current.max_lat_e7 = lat_e7;
    }
    if (lon_e7 < track_current.min_lon_e7) {
      track_current.min_lon_e7 = lon_e7;
    }
    if (lon_e7 > track_current.max_lon_e7) {
      track_current.max_lon_e7 = lon_e7;
    }
  }

  if (track_current.flush_count < TRACK_CHUNK_POINTS) {
    track_current.flush_buf[track_current.flush_count++] = {lat_e7, lon_e7, t_min};
  }
  track_current.last_point = {lat_e7, lon_e7, t_min};
  track_current.has_last_point = true;
}

bool track_iter_points_internal(uint8_t slot, uint16_t max_points, TrackPointCb cb, void *ctx) {
  Preferences &prefs = storage::prefs_trk();
  TrackMeta meta = track_load_meta(prefs, slot);
  if (meta.ver != TRACK_VER) {
    return false;
  }
  const bool current = (slot == track_slot);
  const uint8_t chunk_count = current
                                  ? ((track_current.chunk_count <= TRACK_MAX_CHUNKS)
                                         ? track_current.chunk_count
                                         : TRACK_MAX_CHUNKS)
                                  : ((meta.chunk_count <= TRACK_MAX_CHUNKS)
                                         ? meta.chunk_count
                                         : TRACK_MAX_CHUNKS);
  const uint8_t chunk_head = current
                                 ? ((track_current.chunk_head < TRACK_MAX_CHUNKS)
                                        ? track_current.chunk_head
                                        : 0)
                                 : ((meta.chunk_head < TRACK_MAX_CHUNKS)
                                        ? meta.chunk_head
                                        : 0);
  uint16_t persisted = 0;
  uint8_t newest_chunk_count = 0;
  TrackPoint chunk_points[TRACK_CHUNK_POINTS];
  for (uint8_t i = 0; i < chunk_count; ++i) {
    const uint8_t chunk_idx = static_cast<uint8_t>((chunk_head + i) % TRACK_MAX_CHUNKS);
    uint8_t valid_count = 0;
    if (track_load_chunk(prefs, slot, chunk_idx, chunk_points, valid_count)) {
      persisted = static_cast<uint16_t>(persisted + valid_count);
      if (i == chunk_count - 1) {
        newest_chunk_count = valid_count;
      }
    }
  }
  // Bound the RAM tail for this iteration. These indices alone are not enough
  // to keep the export consistent -- a callback that services GNSS could still
  // reset flush_count and refill the buffer underneath them -- so capture and
  // flushing are both paused for the duration via track_export_active.
  const uint8_t unpersisted_end = current ? track_current.flush_count : 0;
  const uint8_t unpersisted_start = current
                                        ? ((newest_chunk_count <= track_current.flush_count)
                                               ? newest_chunk_count
                                               : 0)
                                        : 0;
  const uint16_t unpersisted_count = current
                                         ? static_cast<uint16_t>(unpersisted_end - unpersisted_start)
                                         : 0;
  uint32_t total = static_cast<uint32_t>(persisted) + static_cast<uint32_t>(unpersisted_count);
  if (total == 0) {
    return false;
  }
  if (total > TRACK_MAX_POINTS) {
    total = TRACK_MAX_POINTS;
  }
  uint16_t skip_oldest = 0;
  if (persisted + unpersisted_count > TRACK_MAX_POINTS) {
    skip_oldest = static_cast<uint16_t>((persisted + unpersisted_count) - TRACK_MAX_POINTS);
  }
  uint16_t stride = 1;
  if (max_points > 0 && total > max_points) {
    stride = static_cast<uint16_t>((total + max_points - 1) / max_points);
    if (stride == 0) {
      stride = 1;
    }
  }

  uint16_t idx = 0;
  for (uint8_t i = 0; i < chunk_count; ++i) {
    const uint8_t chunk_idx = static_cast<uint8_t>((chunk_head + i) % TRACK_MAX_CHUNKS);
    uint8_t valid_count = 0;
    if (!track_load_chunk(prefs, slot, chunk_idx, chunk_points, valid_count)) {
      continue;
    }
    for (uint8_t p = 0; p < valid_count; ++p) {
      if (skip_oldest > 0) {
        skip_oldest--;
        continue;
      }
      if ((idx++ % stride) == 0) {
        if (!cb(chunk_points[p], ctx)) {
          return true;
        }
      }
    }
  }

  if (current) {
    for (uint8_t p = unpersisted_start; p < unpersisted_end; ++p) {
      if (skip_oldest > 0) {
        skip_oldest--;
        continue;
      }
      if ((idx++ % stride) == 0) {
        if (!cb(track_current.flush_buf[p], ctx)) {
          return true;
        }
      }
    }
  }

  return true;
}

// Build the 16-byte payload for BLE read.
void build_summary_payload_internal(uint8_t *out, size_t len) {
  if (len < 16) {
    return;
  }

  const float avg_speed_kph = (active_time_ms_val > 0)
                                  ? (total_distance_m_val / (active_time_ms_val / 1000.0f)) * 3.6f
                                  : 0.0f;
  const uint32_t distance_m = static_cast<uint32_t>(total_distance_m_val + 0.5f);
  const uint16_t avg_speed_cmps = kph_to_cmps_u16_clamped(avg_speed_kph);
  const uint16_t max_speed_cmps = kph_to_cmps_u16_clamped(max_speed_kph_val);

  memset(out, 0, len);
  out[0] = static_cast<uint8_t>(current_date_yyyymmdd & 0xFF);
  out[1] = static_cast<uint8_t>((current_date_yyyymmdd >> 8) & 0xFF);
  out[2] = static_cast<uint8_t>((current_date_yyyymmdd >> 16) & 0xFF);
  out[3] = static_cast<uint8_t>((current_date_yyyymmdd >> 24) & 0xFF);

  out[4] = static_cast<uint8_t>(distance_m & 0xFF);
  out[5] = static_cast<uint8_t>((distance_m >> 8) & 0xFF);
  out[6] = static_cast<uint8_t>((distance_m >> 16) & 0xFF);
  out[7] = static_cast<uint8_t>((distance_m >> 24) & 0xFF);

  out[8] = static_cast<uint8_t>(avg_speed_cmps & 0xFF);
  out[9] = static_cast<uint8_t>((avg_speed_cmps >> 8) & 0xFF);
  out[10] = static_cast<uint8_t>(max_speed_cmps & 0xFF);
  out[11] = static_cast<uint8_t>((max_speed_cmps >> 8) & 0xFF);

  out[12] = static_cast<uint8_t>(last_update_min_val & 0xFF);
  out[13] = static_cast<uint8_t>((last_update_min_val >> 8) & 0xFF);

  uint8_t flags = 0;
  if (has_gps_fix) {
    flags |= 0x01;
  }
  if (current_date_yyyymmdd != 0) {
    flags |= 0x02;
  }
  out[14] = flags;

  uint8_t checksum = 0;
  for (size_t i = 0; i < 15; ++i) {
    checksum ^= out[i];
  }
  out[15] = checksum;
}

void append_session_json(String &json, const SessionSummary &s) {
  json += "{";
  json += "\"start_date\":" + String(s.start_date);
  json += ",\"start_min\":" + String(s.start_min);
  json += ",\"end_date\":" + String(s.end_date);
  json += ",\"end_min\":" + String(s.end_min);
  json += ",\"distance_m\":" + String(s.distance_m);
  json += ",\"active_s\":" + String(s.active_s);
  json += ",\"avg_speed_cmps\":" + String(s.avg_speed_cmps);
  json += ",\"max_speed_cmps\":" + String(s.max_speed_cmps);
  json += ",\"flags\":" + String(s.flags);
  json += "}";
}

void append_history_json(String &json) {
  json += ",\"history\":[";
  for (uint8_t i = 0; i < history_count; ++i) {
    const uint8_t idx = static_cast<uint8_t>((history_idx + HISTORY_MAX - 1 - i) % HISTORY_MAX);
    if (i > 0) {
      json += ",";
    }
    append_session_json(json, history[idx]);
  }
  json += "]";
}

void append_session_current_json(String &json) {
  json += ",\"session_current\":";
  if (!session_open) {
    json += "null";
    return;
  }
  const SessionSummary snap = build_session_snapshot(false);
  append_session_json(json, snap);
}

void append_last_completed_day_json(String &json) {
  json += ",\"last_completed_day\":";
  if (!last_completed_day_valid) {
    json += "null";
    return;
  }
  json += "{";
  json += "\"date\":" + String(last_completed_day.date);
  json += ",\"distance_m\":" + String(last_completed_day.distance_m, 1);
  json += ",\"active_ms\":" + String(last_completed_day.active_ms);
  json += ",\"max_speed_kph\":" + String(last_completed_day.max_speed_kph, 1);
  json += ",\"last_update_min\":" + String(last_completed_day.last_update_min);
  json += "}";
}

String build_summary_json_internal() {
  const float avg_speed_kph = (active_time_ms_val > 0)
                                  ? (total_distance_m_val / (active_time_ms_val / 1000.0f)) * 3.6f
                                  : 0.0f;
  const uint32_t distance_m = static_cast<uint32_t>(total_distance_m_val + 0.5f);
  const uint16_t avg_speed_cmps = kph_to_cmps_u16_clamped(avg_speed_kph);
  const uint16_t max_speed_cmps = kph_to_cmps_u16_clamped(max_speed_kph_val);
  const bool has_data = (current_date_yyyymmdd != 0);

  String json = "{";
  json += "\"date\":" + String(current_date_yyyymmdd);
  json += ",\"distance_m\":" + String(distance_m);
  json += ",\"avg_speed_cmps\":" + String(avg_speed_cmps);
  json += ",\"max_speed_cmps\":" + String(max_speed_cmps);
  json += ",\"last_update_min\":" + String(last_update_min_val);
  json += ",\"gps_fix\":" + String(has_gps_fix ? "true" : "false");
  json += ",\"gps_raw_fix\":" + String(has_gps_fix_raw ? "true" : "false");
  json += ",\"gps_quality_ok\":" + String(gps_quality_ok ? "true" : "false");
  json += ",\"has_data\":" + String(has_data ? "true" : "false");
  append_last_completed_day_json(json);
  append_history_json(json);
  append_session_current_json(json);
  json += "}";
  return json;
}

// Handle a single NMEA line and update rolling metrics.
void handle_nmea_line(const char *line) {
  float speed_kph = 0.0f;
  float lat_deg = 0.0f;
  float lon_deg = 0.0f;
  bool valid_fix = false;
  uint32_t date_yyyymmdd = 0;
  uint16_t time_min = 0;
  uint32_t time_ms_of_day = 0;
  const unsigned long now_ms = millis();

  const bool is_rmc = is_sentence_type(line, "RMC");
  if (is_rmc) {
    gps_rmc_seen++;
  }
  const bool is_gga = is_sentence_type(line, "GGA");
  if (is_gga) {
    uint8_t fix_quality = gps_fix_quality;
    uint8_t sats = gps_sats;
    float hdop = NAN;
    if (parse_gga(line, &fix_quality, &sats, &hdop)) {
      gps_gga_seen++;
      gps_gga_observed = true;
      gps_last_gga_ms = now_ms;
      gps_fix_quality = fix_quality;
      gps_sats = sats;
      gps_hdop = hdop;
    } else {
      gps_parse_fail++;
      gps_gga_parse_fail++;
    }
  }
  const bool parsed_rmc = parse_rmc(line, &lat_deg, &lon_deg, &speed_kph, &valid_fix,
                                    &date_yyyymmdd, &time_min, &time_ms_of_day);
  if (is_rmc && !parsed_rmc) {
    gps_parse_fail++;
    gps_rmc_parse_fail++;
    clear_pending_date(true);
  }
  if (parsed_rmc) {
    gps_rmc_observed = true;
    gps_last_rmc_ms = now_ms;
    has_gps_fix_raw = valid_fix;
    last_gps_ms = now_ms;

    const RuntimeConfig &cfg = config::get();
    const bool gga_fresh = gps_gga_observed &&
                           time_utils::elapsed_at_most(now_ms, gps_last_gga_ms,
                                                      cfg.gps_max_gga_age_ms);
    const bool hdop_ok = (!isnan(gps_hdop) && gps_hdop > 0.0f && gps_hdop <= cfg.gps_max_hdop);
    gps_quality_ok = (gps_fix_quality >= cfg.gps_min_fix_quality) &&
                     (gps_sats >= cfg.gps_min_sats) &&
                     hdop_ok &&
                     gga_fresh;
    gps_trusted_fix = (has_gps_fix_raw && gps_quality_ok);
    has_gps_fix = gps_trusted_fix;
    const bool speed_usable = gps_trusted_fix &&
                              isfinite(speed_kph) &&
                              speed_kph >= 0.0f &&
                              speed_kph <= SPEED_MAX_VALID_KPH;
    const bool date_accepted = gps_trusted_fix && date_yyyymmdd != 0 &&
                               accept_date_observation(date_yyyymmdd, time_ms_of_day);
    last_speed_usable_val = speed_usable;
    last_speed_kph_val = speed_usable ? speed_kph : 0.0f;
    if (gps_trusted_fix && isfinite(speed_kph) && speed_kph > SPEED_MAX_VALID_KPH) {
      gps_speed_spike++;
      gps_last_segment_reject_reason = "speed_spike";
    }
    if (!gps_trusted_fix) {
      clear_pending_date(true);
    }
    if (!gps_trusted_fix || !speed_usable || !date_accepted) {
      reset_distance_baseline();
      if (!gps_trusted_fix) {
        gps_last_segment_reject_reason = "bad_fix";
      } else if (!speed_usable) {
        gps_last_segment_reject_reason = "speed_spike";
      } else if (!date_accepted) {
        gps_last_segment_reject_reason = "date_pending";
      }
    }

    if (valid_fix) {
      if (gps_trusted_fix) {
        if (date_yyyymmdd != 0) {
          gps_time_observed = true;
          gps_last_time_ms = now_ms;
        }
      }
      current_lat_deg_val = lat_deg;
      current_lon_deg_val = lon_deg;
      has_current_fix_val = true;
      gps_rmc_valid++;
      gps_fix_observed = true;
      gps_last_fix_ms = last_gps_ms;
      if (gps_trusted_fix) {
        session_fix_seen = true;
        if (!session_start_set && date_accepted) {
          session_start_set = true;
          session_start_date = date_yyyymmdd;
          session_start_min = time_min;
        }
        if (date_accepted) {
          session_end_date = date_yyyymmdd;
          session_end_min = time_min;
        }
      }
    } else {
      has_current_fix_val = false;
    }

    if (date_accepted) {
      last_update_min_val = time_min;
    }

    const bool metrics_usable = speed_usable && date_accepted;
    const bool active_sample = metrics_usable && (speed_kph > SPEED_ACTIVE_KPH);
    if (metrics_usable) {
      update_active_time_observation(date_yyyymmdd, time_ms_of_day, active_sample);
    }

    if (metrics_usable && now_ms - last_sample_ms >= GPS_SAMPLE_MS) {
      last_sample_ms = now_ms;

      float min_segment_m = cfg.gps_min_segment_m;
      if (!isnan(gps_hdop) && gps_hdop > 0.0f) {
        const float hdop_segment = cfg.gps_hdop_factor * gps_hdop;
        if (hdop_segment > min_segment_m) {
          min_segment_m = hdop_segment;
        }
      }
      if (min_segment_m > cfg.gps_max_min_segment_m) {
        min_segment_m = cfg.gps_max_min_segment_m;
      }

      if (active_sample) {
        track_try_add_point(lat_deg, lon_deg, time_min, date_yyyymmdd, now_ms);
        if (has_last_point_val) {
          const float segment_m = haversine_m(last_lat_deg_val, last_lon_deg_val, lat_deg, lon_deg);
          gps_last_segment_m = segment_m;
          if (segment_m >= min_segment_m && segment_m < 50.0f) {
            total_distance_m_val += segment_m;
            gps_last_segment_accepted = true;
            gps_last_segment_reject_reason = "ok";
          } else {
            gps_last_segment_accepted = false;
            if (segment_m < min_segment_m) {
              gps_small_segment_rejects++;
              gps_last_segment_reject_reason = "small_segment";
            } else {
              gps_large_segment_rejects++;
              gps_last_segment_reject_reason = "large_segment";
            }
          }
        } else {
          gps_last_segment_m = 0.0f;
          gps_last_segment_accepted = false;
          gps_last_segment_reject_reason = "baseline";
        }
        if (session_has_last_point) {
          const float segment_m = haversine_m(session_last_lat_deg, session_last_lon_deg, lat_deg, lon_deg);
          if (segment_m >= min_segment_m && segment_m < 50.0f) {
            session_total_distance_m += segment_m;
          }
        }
      } else {
        gps_last_segment_m = 0.0f;
        gps_last_segment_accepted = false;
        gps_last_segment_reject_reason = "inactive_speed";
      }

      last_lat_deg_val = lat_deg;
      last_lon_deg_val = lon_deg;
      has_last_point_val = true;
      session_last_lat_deg = lat_deg;
      session_last_lon_deg = lon_deg;
      session_has_last_point = true;

      if (speed_kph > max_speed_kph_val) {
        max_speed_kph_val = speed_kph;
      }
      if (speed_kph > session_max_speed_kph) {
        session_max_speed_kph = speed_kph;
      }
    }
  }
}

// Read bytes from GPS UART and assemble NMEA lines.
void read_gps() {
  while (GPS.available() > 0) {
    const char c = static_cast<char>(GPS.read());
    gps_bytes_rx++;
    gps_byte_observed = true;
    gps_last_byte_ms = millis();
    if (c == '\n') {
      nmea_line[nmea_len] = '\0';
      if (nmea_len > 6) {
        gps_sentences_rx++;
        if (nmea_checksum_ok(nmea_line)) {
          gps_last_sentence_ms = millis();
          handle_nmea_line(nmea_line);
        } else {
          gps_checksum_fail++;
          if (is_sentence_type(nmea_line, "RMC")) {
            clear_pending_date(true);
          }
        }
      }
      nmea_len = 0;
    } else if (c != '\r') {
      if (nmea_len + 1 < sizeof(nmea_line)) {
        nmea_line[nmea_len++] = c;
      } else {
        gps_overflow++;
        nmea_len = 0;
      }
    }
  }
}
} // namespace

void build_summary_payload(uint8_t *out, size_t len) {
  build_summary_payload_internal(out, len);
}

String build_summary_json() {
  return build_summary_json_internal();
}

void begin() {
  GPS.setRxBufferSize(GPS_RX_BUFFER_SIZE);
  GPS.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  load_metrics();
  daily_journal_begin();
  reconcile_metrics_with_daily_journal();
  history_load();
  session_close_previous_on_boot();
  track_begin();
  session_begin();
}

void tick() {
  read_gps();
  expire_gps_if_stale(millis());
}

void track_tick(unsigned long now_ms) {
  track_flush_if_due(now_ms);
}

void save_if_due(unsigned long now_ms) {
  if (now_ms - last_save_ms >= SAVE_INTERVAL_MS) {
    last_save_ms = now_ms;
    save_metrics();
    save_session_snapshot_if_needed();
  }
}

bool track_iter_points(uint8_t slot, uint16_t max_points, TrackPointCb cb, void *ctx) {
  track_export_active = true;
  const bool ok = track_iter_points_internal(slot, max_points, cb, ctx);
  track_export_active = false;
  return ok;
}

bool track_get_view(int session_id, TrackView &out) {
  memset(&out, 0, sizeof(out));
  uint8_t slot = track_slot;
  bool current = false;
  if (session_id < 0) {
    current = true;
    slot = track_slot;
  } else if (session_id >= 0 && session_id <= 2) {
    slot = static_cast<uint8_t>((track_slot + TRACK_SLOTS - 1 - session_id) % TRACK_SLOTS);
  } else {
    return false;
  }

  Preferences &prefs = storage::prefs_trk();
  TrackMeta meta = track_load_meta(prefs, slot);
  if (meta.ver != TRACK_VER) {
    return false;
  }
  const uint16_t persisted = meta.total_points;
  const uint8_t unpersisted_start = current
                                        ? ((track_current.persisted_flush_count <= track_current.flush_count)
                                               ? track_current.persisted_flush_count
                                               : 0)
                                        : 0;
  const uint16_t unpersisted_count = current
                                         ? static_cast<uint16_t>(track_current.flush_count - unpersisted_start)
                                         : 0;
  uint32_t total = static_cast<uint32_t>(persisted) + static_cast<uint32_t>(unpersisted_count);
  if (total == 0) {
    return false;
  }
  if (total > TRACK_MAX_POINTS) {
    total = TRACK_MAX_POINTS;
  }

  out.ok = true;
  out.open = current;
  out.slot = slot;
  out.count = static_cast<uint16_t>(total);
  out.sample_ms = meta.sample_ms;
  if (out.sample_ms == 0) {
    out.sample_ms = static_cast<uint16_t>(TRACK_SAMPLE_MS);
  }
  out.start_date = meta.start_date;
  out.start_min = meta.start_min;
  out.end_date = meta.end_date;
  out.end_min = meta.end_min;
  out.min_lat_e7 = meta.min_lat_e7;
  out.max_lat_e7 = meta.max_lat_e7;
  out.min_lon_e7 = meta.min_lon_e7;
  out.max_lon_e7 = meta.max_lon_e7;

  if (current) {
    if (track_current.start_date != 0) {
      out.start_date = track_current.start_date;
      out.start_min = track_current.start_min;
    }
    if (track_current.end_date != 0) {
      out.end_date = track_current.end_date;
      out.end_min = track_current.end_min;
    }
    if (track_current.has_bbox) {
      out.min_lat_e7 = track_current.min_lat_e7;
      out.max_lat_e7 = track_current.max_lat_e7;
      out.min_lon_e7 = track_current.min_lon_e7;
      out.max_lon_e7 = track_current.max_lon_e7;
    }
  }

  struct BboxCtx {
    bool has;
    uint16_t count;
    int32_t min_lat;
    int32_t max_lat;
    int32_t min_lon;
    int32_t max_lon;
  } bbox = {false, 0, 0, 0, 0, 0};

  auto bbox_cb = [](const TrackPoint &p, void *ctx) -> bool {
    BboxCtx *b = reinterpret_cast<BboxCtx *>(ctx);
    if (!b->has) {
      b->min_lat = p.lat_e7;
      b->max_lat = p.lat_e7;
      b->min_lon = p.lon_e7;
      b->max_lon = p.lon_e7;
      b->has = true;
    } else {
      if (p.lat_e7 < b->min_lat) b->min_lat = p.lat_e7;
      if (p.lat_e7 > b->max_lat) b->max_lat = p.lat_e7;
      if (p.lon_e7 < b->min_lon) b->min_lon = p.lon_e7;
      if (p.lon_e7 > b->max_lon) b->max_lon = p.lon_e7;
    }
    b->count++;
    return true;
  };

  if (!track_iter_points_internal(slot, 0, bbox_cb, &bbox) || !bbox.has) {
    return false;
  }
  out.min_lat_e7 = bbox.min_lat;
  out.max_lat_e7 = bbox.max_lat;
  out.min_lon_e7 = bbox.min_lon;
  out.max_lon_e7 = bbox.max_lon;
  out.count = bbox.count;

  return true;
}

bool has_fix() {
  return has_gps_fix;
}

bool raw_fix() {
  return has_gps_fix_raw;
}

bool trusted_fix() {
  return gps_trusted_fix;
}

bool has_current_fix() {
  return has_current_fix_val;
}

bool speed_usable() {
  return last_speed_usable_val;
}

float last_speed_kph() {
  return last_speed_kph_val;
}

float total_distance_m() {
  return total_distance_m_val;
}

float max_speed_kph() {
  return max_speed_kph_val;
}

unsigned long active_time_ms() {
  return active_time_ms_val;
}

uint32_t activity_observation_intervals() {
  return gps_activity_observation_intervals;
}

uint32_t activity_gap_rejects() {
  return gps_activity_gap_rejects;
}

uint32_t last_activity_delta_ms() {
  return gps_last_activity_delta_ms;
}

uint32_t date_transition_count() {
  return gps_date_transition_count;
}

uint32_t date_rejected_count() {
  return gps_date_rejected_count;
}

uint32_t date_pending_candidate() {
  return pending_date_candidate;
}

uint8_t date_pending_observations() {
  return pending_date_observations;
}

int8_t metrics_storage_slot() {
  return metrics_active_slot;
}

uint32_t metrics_storage_generation() {
  return metrics_generation;
}

uint32_t metrics_storage_save_failures() {
  return metrics_failures;
}

uint32_t metrics_storage_recoveries() {
  return metrics_recoveries;
}

int8_t session_storage_slot() {
  return session_store_active_slot;
}

uint32_t session_storage_generation() {
  return session_store_generation;
}

uint32_t session_storage_save_failures() {
  return session_store_failures;
}

uint32_t session_storage_recoveries() {
  return session_store_recoveries;
}

uint8_t session_history_count() {
  return history_count;
}

int8_t daily_journal_slot() {
  return daily_journal_active_slot;
}

uint32_t daily_journal_generation() {
  return daily_journal_active_generation;
}

uint32_t daily_journal_save_failures() {
  return daily_journal_failures;
}

uint32_t last_completed_date() {
  return last_completed_day_valid ? last_completed_day.date : 0;
}

uint32_t current_date() {
  return current_date_yyyymmdd;
}

uint16_t last_update_min() {
  return last_update_min_val;
}

bool has_time() {
  const unsigned long now_ms = millis();
  return current_date_yyyymmdd != 0 &&
         gps_time_observed &&
         time_utils::elapsed_at_most(now_ms, gps_last_time_ms,
                                    DAY_MODE_TIME_STALE_MS);
}

uint16_t local_time_min(int16_t offset_min) {
  int local = static_cast<int>(last_update_min_val) + static_cast<int>(offset_min);
  local %= 1440;
  if (local < 0) {
    local += 1440;
  }
  return static_cast<uint16_t>(local);
}

unsigned long last_time_ms() {
  return gps_last_time_ms;
}

bool has_last_point() {
  return has_last_point_val;
}

float last_lat_deg() {
  return last_lat_deg_val;
}

float last_lon_deg() {
  return last_lon_deg_val;
}

float current_lat_deg() {
  return current_lat_deg_val;
}

float current_lon_deg() {
  return current_lon_deg_val;
}

uint8_t sats() {
  return gps_sats;
}

uint8_t fix_quality() {
  return gps_fix_quality;
}

float hdop() {
  return gps_hdop;
}

bool quality_ok() {
  return gps_quality_ok;
}

unsigned long bytes_rx() {
  return gps_bytes_rx;
}

unsigned long sentences_rx() {
  return gps_sentences_rx;
}

unsigned long rmc_seen() {
  return gps_rmc_seen;
}

unsigned long rmc_valid() {
  return gps_rmc_valid;
}

unsigned long gga_seen() {
  return gps_gga_seen;
}

unsigned long overflow() {
  return gps_overflow;
}

unsigned long checksum_fail() {
  return gps_checksum_fail;
}

unsigned long parse_fail() {
  return gps_parse_fail;
}

unsigned long rmc_parse_fail() {
  return gps_rmc_parse_fail;
}

unsigned long gga_parse_fail() {
  return gps_gga_parse_fail;
}

unsigned long speed_spike() {
  return gps_speed_spike;
}

unsigned long small_segment_rejects() {
  return gps_small_segment_rejects;
}

unsigned long large_segment_rejects() {
  return gps_large_segment_rejects;
}

unsigned long stale_count() {
  return gps_stale_count;
}

bool has_byte_observation() {
  return gps_byte_observed;
}

bool has_rmc_observation() {
  return gps_rmc_observed;
}

bool has_gga_observation() {
  return gps_gga_observed;
}

bool has_fix_observation() {
  return gps_fix_observed;
}

unsigned long last_byte_ms() {
  return gps_last_byte_ms;
}

unsigned long last_rmc_ms() {
  return gps_last_rmc_ms;
}

unsigned long last_gga_ms() {
  return gps_last_gga_ms;
}

unsigned long last_fix_ms() {
  return gps_last_fix_ms;
}

float last_segment_m() {
  return gps_last_segment_m;
}

bool last_segment_accepted() {
  return gps_last_segment_accepted;
}

const char *last_segment_reject_reason() {
  return gps_last_segment_reject_reason;
}
} // namespace gps
