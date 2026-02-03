#include "gps/gps.h"

#include <Arduino.h>
#include <Preferences.h>
#include <stddef.h>

#include "config.h"
#include "pins.h"
#include "storage/nvs_store.h"
#include "util/geo.h"

namespace gps {
namespace {
// GPS UART settings are defined in config.h.
HardwareSerial GPS(1);

// NMEA line buffer for incoming GPS sentences.
char nmea_line[128];
size_t nmea_len = 0;

// Latest GPS state.
bool has_gps_fix = false;
float last_speed_kph_val = 0.0f;
unsigned long last_gps_ms = 0;
unsigned long gps_bytes_rx = 0;
unsigned long gps_sentences_rx = 0;
unsigned long gps_rmc_seen = 0;
unsigned long gps_rmc_valid = 0;
unsigned long gps_gga_seen = 0;
unsigned long gps_overflow = 0;
unsigned long gps_last_byte_ms = 0;
unsigned long gps_last_sentence_ms = 0;
unsigned long gps_last_rmc_ms = 0;
unsigned long gps_last_gga_ms = 0;
unsigned long gps_last_fix_ms = 0;
uint8_t gps_sats = 0;
uint8_t gps_fix_quality = 0;

// Rolling metrics for the current day.
unsigned long last_sample_ms = 0;
unsigned long active_time_ms_val = 0;
float total_distance_m_val = 0.0f;
float max_speed_kph_val = 0.0f;
uint16_t last_update_min_val = 0;

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

SessionSummary history[HISTORY_MAX];
uint8_t history_count = 0;
uint8_t history_idx = 0;
SessionSummary session_snapshot_last;
bool session_snapshot_valid = false;

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

// Daily reset date (YYYYMMDD from GPS).
uint32_t current_date_yyyymmdd = 0;
unsigned long last_save_ms = 0;

float knots_to_kph(float knots) {
  return knots * 1.852f;
}

// Convert NMEA degree-minute format to decimal degrees.
float nmea_to_decimal_degrees(const char *value, char hemi) {
  // NMEA format: DDMM.MMMM (lat) or DDDMM.MMMM (lon)
  const float raw = strtof(value, nullptr);
  const int deg = static_cast<int>(raw / 100.0f);
  const float minutes = raw - (deg * 100.0f);
  float dec = static_cast<float>(deg) + (minutes / 60.0f);
  if (hemi == 'S' || hemi == 'W') {
    dec = -dec;
  }
  return dec;
}

// Parse RMC sentence for position, speed, fix status, and date/time.
bool parse_rmc(const char *line,
               float *lat_deg,
               float *lon_deg,
               float *speed_kph,
               bool *valid_fix,
               uint32_t *date_yyyymmdd,
               uint16_t *time_min) {
  if (strncmp(line, "$GPRMC,", 7) != 0 && strncmp(line, "$GNRMC,", 7) != 0) {
    return false;
  }

  // RMC fields:
  // 1 time, 2 status (A/V), 3 lat, 4 N/S, 5 lon, 6 E/W, 7 speed (knots), 9 date (ddmmyy)
  int field = 0;
  float knots = 0.0f;
  char status = 'V';
  char time_buf[8] = {0};
  char lat_buf[16] = {0};
  char lon_buf[16] = {0};
  char speed_buf[12] = {0};
  char ns = 'N';
  char ew = 'E';
  char date_buf[8] = {0};
  int time_len = 0;
  int lat_len = 0;
  int lon_len = 0;
  int speed_len = 0;
  int date_len = 0;

  for (const char *p = line; *p != '\0' && *p != '*'; ++p) {
    if (*p == ',') {
      field++;
      continue;
    }
    if (field == 1 && time_len < 6) {
      time_buf[time_len++] = *p;
    }
    if (field == 2 && status == 'V') {
      status = *p;
    }
    if (field == 3 && lat_len < 15) {
      lat_buf[lat_len++] = *p;
    }
    if (field == 4) {
      ns = *p;
    }
    if (field == 5 && lon_len < 15) {
      lon_buf[lon_len++] = *p;
    }
    if (field == 6) {
      ew = *p;
    }
    if (field == 7 && speed_len < 11) {
      speed_buf[speed_len++] = *p;
    }
    if (field == 9 && date_len < 6) {
      date_buf[date_len++] = *p;
    }
  }

  *valid_fix = (status == 'A');
  if (speed_len > 0) {
    knots = strtof(speed_buf, nullptr);
  }
  *speed_kph = knots_to_kph(knots);
  if (lat_len > 0 && lon_len > 0) {
    *lat_deg = nmea_to_decimal_degrees(lat_buf, ns);
    *lon_deg = nmea_to_decimal_degrees(lon_buf, ew);
  }
  if (date_len == 6) {
    const int day = (date_buf[0] - '0') * 10 + (date_buf[1] - '0');
    const int mon = (date_buf[2] - '0') * 10 + (date_buf[3] - '0');
    const int year = (date_buf[4] - '0') * 10 + (date_buf[5] - '0');
    *date_yyyymmdd = static_cast<uint32_t>(2000 + year) * 10000 +
                     static_cast<uint32_t>(mon) * 100 +
                     static_cast<uint32_t>(day);
  }
  if (time_len >= 4) {
    const int hour = (time_buf[0] - '0') * 10 + (time_buf[1] - '0');
    const int min = (time_buf[2] - '0') * 10 + (time_buf[3] - '0');
    *time_min = static_cast<uint16_t>(hour * 60 + min);
  }
  return true;
}

// Parse GGA sentence for fix quality and satellites.
bool parse_gga(const char *line, uint8_t *fix_quality, uint8_t *sats) {
  if (strncmp(line, "$GPGGA,", 7) != 0 && strncmp(line, "$GNGGA,", 7) != 0) {
    return false;
  }

  // GGA fields: 1 time, 2 lat, 3 N/S, 4 lon, 5 E/W, 6 fix quality, 7 satellites
  int field = 0;
  char fix_buf[4] = {0};
  char sat_buf[4] = {0};
  int fix_len = 0;
  int sat_len = 0;

  for (const char *p = line; *p != '\0' && *p != '*'; ++p) {
    if (*p == ',') {
      field++;
      continue;
    }
    if (field == 6 && fix_len < 3) {
      fix_buf[fix_len++] = *p;
    }
    if (field == 7 && sat_len < 3) {
      sat_buf[sat_len++] = *p;
    }
  }

  if (fix_len > 0) {
    *fix_quality = static_cast<uint8_t>(atoi(fix_buf));
  }
  if (sat_len > 0) {
    *sats = static_cast<uint8_t>(atoi(sat_buf));
  }
  return true;
}

// Persist daily metrics to NVS (throttled by SAVE_INTERVAL_MS).
void save_metrics() {
  Preferences &prefs = storage::prefs();
  prefs.putUInt("date", current_date_yyyymmdd);
  prefs.putFloat("dist_m", total_distance_m_val);
  prefs.putULong("active_ms", active_time_ms_val);
  prefs.putFloat("max_kph", max_speed_kph_val);
  prefs.putUShort("upd_min", last_update_min_val);
}

// Restore persisted metrics from NVS on boot.
void load_metrics() {
  Preferences &prefs = storage::prefs();
  current_date_yyyymmdd = prefs.getUInt("date", 0);
  total_distance_m_val = prefs.getFloat("dist_m", 0.0f);
  active_time_ms_val = prefs.getULong("active_ms", 0);
  max_speed_kph_val = prefs.getFloat("max_kph", 0.0f);
  last_update_min_val = prefs.getUShort("upd_min", 0);
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

bool session_is_valid(const SessionSummary &s) {
  return s.ver == SESSION_VER && s.crc == session_checksum(s);
}

void session_zero(SessionSummary &s) {
  memset(&s, 0, sizeof(s));
  s.ver = SESSION_VER;
  s.pad = 0;
  session_write_crc(s);
}

SessionSummary build_session_snapshot(bool finalize) {
  SessionSummary s = {};
  s.ver = SESSION_VER;
  s.flags = 0;
  if (session_fix_seen) {
    s.flags |= SESSION_FLAG_GPS_FIX;
    s.flags |= SESSION_FLAG_HAS_DATA;
  }
  if (session_open) {
    s.flags |= SESSION_FLAG_IN_PROGRESS;
  }
  if (finalize && !session_fix_seen) {
    s.flags |= SESSION_FLAG_NO_FIX;
  }
  s.start_date = session_start_date;
  s.start_min = session_start_min;
  s.end_date = session_end_date;
  s.end_min = session_end_min;
  const uint32_t distance_m = static_cast<uint32_t>(session_total_distance_m + 0.5f);
  s.distance_m = distance_m;
  s.active_s = static_cast<uint32_t>(session_active_time_ms / 1000);
  uint32_t avg_cmps = 0;
  if (s.active_s > 0) {
    avg_cmps = (distance_m * 100UL) / s.active_s;
    if (avg_cmps > 65535) {
      avg_cmps = 65535;
    }
  }
  s.avg_speed_cmps = static_cast<uint16_t>(avg_cmps);
  uint32_t max_cmps = static_cast<uint32_t>(session_max_speed_kph * 27.7778f);
  if (max_cmps > 65535) {
    max_cmps = 65535;
  }
  s.max_speed_cmps = static_cast<uint16_t>(max_cmps);
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

void history_clear() {
  Preferences &prefs = storage::prefs();
  history_count = 0;
  history_idx = 0;
  for (uint8_t i = 0; i < HISTORY_MAX; ++i) {
    session_zero(history[i]);
  }
  prefs.remove("h0");
  prefs.remove("h1");
  prefs.remove("h2");
  prefs.putUChar("h_cnt", 0);
  prefs.putUChar("h_idx", 0);
  prefs.putUChar("h_ver", SESSION_VER);
}

void history_load() {
  Preferences &prefs = storage::prefs();
  const uint8_t ver = prefs.getUChar("h_ver", 0);
  history_count = prefs.getUChar("h_cnt", 0);
  history_idx = prefs.getUChar("h_idx", 0);
  if (ver != SESSION_VER) {
    history_clear();
    return;
  }
  if (history_count > HISTORY_MAX) {
    history_count = HISTORY_MAX;
  }
  if (history_count < HISTORY_MAX && history_idx != history_count) {
    history_clear();
    return;
  }
  if (history_idx >= HISTORY_MAX) {
    history_clear();
    return;
  }
  bool ok = true;
  for (uint8_t i = 0; i < HISTORY_MAX; ++i) {
    const char key[3] = {'h', static_cast<char>('0' + i), '\0'};
    size_t len = prefs.getBytes(key, &history[i], sizeof(SessionSummary));
    if (i < history_count) {
      if (len != sizeof(SessionSummary) || !session_is_valid(history[i])) {
        ok = false;
        break;
      }
    } else if (len == sizeof(SessionSummary) && !session_is_valid(history[i])) {
      ok = false;
      break;
    }
  }
  if (!ok) {
    history_clear();
  }
}

void history_write_slot(uint8_t idx, const SessionSummary &s) {
  Preferences &prefs = storage::prefs();
  const char key[3] = {'h', static_cast<char>('0' + idx), '\0'};
  prefs.putBytes(key, &s, sizeof(SessionSummary));
}

void history_push(SessionSummary s) {
  Preferences &prefs = storage::prefs();
  if (!session_is_valid(s)) {
    session_write_crc(s);
  }
  history[history_idx] = s;
  history_write_slot(history_idx, s);
  history_idx = static_cast<uint8_t>((history_idx + 1) % HISTORY_MAX);
  if (history_count < HISTORY_MAX) {
    history_count++;
  }
  prefs.putUChar("h_cnt", history_count);
  prefs.putUChar("h_idx", history_idx);
  prefs.putUChar("h_ver", SESSION_VER);
}

bool load_session_snapshot(SessionSummary &out) {
  Preferences &prefs = storage::prefs();
  size_t len = prefs.getBytes("s_cur", &out, sizeof(SessionSummary));
  if (len != sizeof(SessionSummary)) {
    return false;
  }
  return session_is_valid(out);
}

void save_session_snapshot_if_needed() {
  if (!session_open) {
    return;
  }
  Preferences &prefs = storage::prefs();
  SessionSummary snap = build_session_snapshot(false);
  if (!session_snapshot_valid || memcmp(&snap, &session_snapshot_last, sizeof(SessionSummary)) != 0) {
    prefs.putBytes("s_cur", &snap, sizeof(SessionSummary));
    prefs.putUChar("s_open", 1);
    session_snapshot_last = snap;
    session_snapshot_valid = true;
  }
}

void session_close_previous_on_boot() {
  Preferences &prefs = storage::prefs();
  const uint8_t open = prefs.getUChar("s_open", 0);
  if (open != 1) {
    return;
  }
  SessionSummary prev = {};
  if (load_session_snapshot(prev)) {
    finalize_snapshot(prev);
    history_push(prev);
  } else {
    SessionSummary empty = {};
    session_zero(empty);
    empty.flags |= SESSION_FLAG_NO_FIX;
    session_write_crc(empty);
    history_push(empty);
  }
  prefs.putUChar("s_open", 0);
}

void session_begin() {
  Preferences &prefs = storage::prefs();
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
  prefs.putBytes("s_cur", &snap, sizeof(SessionSummary));
  prefs.putUChar("s_open", 1);
  session_snapshot_last = snap;
  session_snapshot_valid = true;
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
  const uint16_t avg_speed_cmps = static_cast<uint16_t>(avg_speed_kph * 27.7778f);
  const uint16_t max_speed_cmps = static_cast<uint16_t>(max_speed_kph_val * 27.7778f);

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

String build_summary_json_internal() {
  const float avg_speed_kph = (active_time_ms_val > 0)
                                  ? (total_distance_m_val / (active_time_ms_val / 1000.0f)) * 3.6f
                                  : 0.0f;
  const uint32_t distance_m = static_cast<uint32_t>(total_distance_m_val + 0.5f);
  const uint16_t avg_speed_cmps = static_cast<uint16_t>(avg_speed_kph * 27.7778f);
  const uint16_t max_speed_cmps = static_cast<uint16_t>(max_speed_kph_val * 27.7778f);
  const bool has_data = (current_date_yyyymmdd != 0);

  String json = "{";
  json += "\"date\":" + String(current_date_yyyymmdd);
  json += ",\"distance_m\":" + String(distance_m);
  json += ",\"avg_speed_cmps\":" + String(avg_speed_cmps);
  json += ",\"max_speed_cmps\":" + String(max_speed_cmps);
  json += ",\"last_update_min\":" + String(last_update_min_val);
  json += ",\"gps_fix\":" + String(has_gps_fix ? "true" : "false");
  json += ",\"has_data\":" + String(has_data ? "true" : "false");
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

  const bool is_rmc = (strncmp(line, "$GPRMC,", 7) == 0 || strncmp(line, "$GNRMC,", 7) == 0);
  if (is_rmc) {
    gps_rmc_seen++;
    gps_last_rmc_ms = millis();
  }
  const bool is_gga = (strncmp(line, "$GPGGA,", 7) == 0 || strncmp(line, "$GNGGA,", 7) == 0);
  if (is_gga) {
    gps_gga_seen++;
    gps_last_gga_ms = millis();
    uint8_t fix_quality = gps_fix_quality;
    uint8_t sats = gps_sats;
    if (parse_gga(line, &fix_quality, &sats)) {
      gps_fix_quality = fix_quality;
      gps_sats = sats;
    }
  }
  if (parse_rmc(line, &lat_deg, &lon_deg, &speed_kph, &valid_fix, &date_yyyymmdd, &time_min)) {
    has_gps_fix = valid_fix;
    last_speed_kph_val = speed_kph;
    last_gps_ms = millis();
    if (valid_fix) {
      last_update_min_val = time_min;
      current_lat_deg_val = lat_deg;
      current_lon_deg_val = lon_deg;
      has_current_fix_val = true;
      gps_rmc_valid++;
      gps_last_fix_ms = last_gps_ms;
      session_fix_seen = true;
      if (!session_start_set && date_yyyymmdd != 0) {
        session_start_set = true;
        session_start_date = date_yyyymmdd;
        session_start_min = time_min;
      }
      if (date_yyyymmdd != 0) {
        session_end_date = date_yyyymmdd;
        session_end_min = time_min;
      }
    } else {
      has_current_fix_val = false;
    }

    if (valid_fix) {
      if (date_yyyymmdd != 0 && date_yyyymmdd != current_date_yyyymmdd) {
        current_date_yyyymmdd = date_yyyymmdd;
        total_distance_m_val = 0.0f;
        active_time_ms_val = 0;
        max_speed_kph_val = 0.0f;
        has_last_point_val = false;
        save_metrics();
      }
    }

    if (has_gps_fix && speed_kph <= SPEED_MAX_VALID_KPH) {
      const unsigned long now_ms = millis();
      if (now_ms - last_sample_ms >= GPS_SAMPLE_MS) {
        last_sample_ms = now_ms;

        if (has_last_point_val) {
          const float segment_m = haversine_m(last_lat_deg_val, last_lon_deg_val, lat_deg, lon_deg);
          if (segment_m < 50.0f) {
            total_distance_m_val += segment_m;
          }
        }
        if (session_has_last_point) {
          const float segment_m = haversine_m(session_last_lat_deg, session_last_lon_deg, lat_deg, lon_deg);
          if (segment_m < 50.0f) {
            session_total_distance_m += segment_m;
          }
        }

        last_lat_deg_val = lat_deg;
        last_lon_deg_val = lon_deg;
        has_last_point_val = true;
        session_last_lat_deg = lat_deg;
        session_last_lon_deg = lon_deg;
        session_has_last_point = true;

        if (speed_kph > SPEED_ACTIVE_KPH) {
          active_time_ms_val += GPS_SAMPLE_MS;
          session_active_time_ms += GPS_SAMPLE_MS;
        }
        if (speed_kph > max_speed_kph_val) {
          max_speed_kph_val = speed_kph;
        }
        if (speed_kph > session_max_speed_kph) {
          session_max_speed_kph = speed_kph;
        }
      }
    }
  }
}

// Read bytes from GPS UART and assemble NMEA lines.
void read_gps() {
  while (GPS.available() > 0) {
    const char c = static_cast<char>(GPS.read());
    gps_bytes_rx++;
    gps_last_byte_ms = millis();
    if (c == '\n') {
      nmea_line[nmea_len] = '\0';
      if (nmea_len > 6) {
        gps_sentences_rx++;
        gps_last_sentence_ms = millis();
        handle_nmea_line(nmea_line);
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
  GPS.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  load_metrics();
  history_load();
  session_close_previous_on_boot();
  session_begin();
}

void tick() {
  read_gps();
}

void save_if_due(unsigned long now_ms) {
  if (now_ms - last_save_ms >= SAVE_INTERVAL_MS) {
    last_save_ms = now_ms;
    save_metrics();
    save_session_snapshot_if_needed();
  }
}

bool has_fix() {
  return has_gps_fix;
}

bool has_current_fix() {
  return has_current_fix_val;
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

uint32_t current_date() {
  return current_date_yyyymmdd;
}

uint16_t last_update_min() {
  return last_update_min_val;
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
} // namespace gps
