#include "web/portal_http.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config/runtime_config.h"
#include "config.h"
#include "geofence/home.h"
#include "gps/gps.h"
#include "led/led_ui.h"
#include "led/effect_registry.h"
#include "power/day_mode.h"
#include "storage/nvs_store.h"
#include "util/time_utils.h"
#include "web/pages.h"
#include "web/portal_lock.h"
#include "wifi/wifi_mgr.h"

namespace portal_http {
namespace {
WebServer server(80);
DNSServer dns_server;
bool dns_running = false;
uint32_t dns_start_count = 0;
uint32_t dns_stop_count = 0;
static const uint16_t DNS_PORT = 53;
static const size_t TRACK_STREAM_CHUNK_BYTES = 768;

// WebServer writes are synchronous. Coalesce the many small coordinate
// fragments into bounded chunks and drain GNSS on both sides of every socket
// write. The enlarged HardwareSerial RX buffer is the second line of defense
// if one individual Wi-Fi write blocks for several seconds.
class TrackStream {
 public:
  TrackStream() : used_(0), healthy_(true) {}

  bool append(const char *text) {
    return append(text, strlen(text));
  }

  bool append(const char *data, size_t length) {
    while (healthy_ && length > 0) {
      if (used_ == sizeof(buffer_) && !flush()) {
        return false;
      }
      const size_t available = sizeof(buffer_) - used_;
      const size_t copied = (length < available) ? length : available;
      memcpy(buffer_ + used_, data, copied);
      used_ += copied;
      data += copied;
      length -= copied;
    }
    return healthy_;
  }

  bool finish() {
    return flush();
  }

  bool healthy() const {
    return healthy_;
  }

 private:
  bool flush() {
    if (!healthy_ || used_ == 0) {
      return healthy_;
    }

    gps::tick();
    server.sendContent(buffer_, used_);
    used_ = 0;
    gps::tick();
    yield();

    WiFiClient client = server.client();
    healthy_ = client && client.connected();
    return healthy_;
  }

  char buffer_[TRACK_STREAM_CHUNK_BYTES];
  size_t used_;
  bool healthy_;
};

struct CoordinateStreamContext {
  TrackStream *stream;
  bool first;
};

struct CsvStreamContext {
  TrackStream *stream;
  const gps::TrackView *view;
};

// Browsers may not carry a custom header cross-origin without first passing a
// CORS preflight, and this server answers no OPTIONS route. Requiring the
// header is therefore enough to reject a hostile page's form post or no-cors
// fetch, while costing the portal's own same-origin scripts nothing. Origin
// checking is deliberately not used instead: the captive-portal DNS resolves
// every name to this device, so the portal has no stable origin to compare.
static const char *CSRF_HEADER = "X-Dog-Portal";
static const char *PIN_HEADER = "X-Dog-Pin";
static const char *COLLECTED_HEADERS[] = {"X-Dog-Portal", "X-Dog-Pin"};

void note_activity() {
  wifi_mgr::note_portal_activity();
  // Every handler calls this first, so queueing the headers here attaches them
  // to that handler's response without touching each send site.
  server.sendHeader("X-Content-Type-Options", "nosniff");
  server.sendHeader("X-Frame-Options", "DENY");
  server.sendHeader("Referrer-Policy", "no-referrer");
  server.sendHeader("Cache-Control", "no-store");
}

bool csrf_ok() {
  if (server.hasHeader(CSRF_HEADER)) {
    return true;
  }
  server.send(403, "application/json", "{\"status\":\"error\",\"reason\":\"csrf\"}");
  return false;
}

// Guards every state-changing endpoint. The CSRF check is unconditional; the
// PIN check does nothing unless the user opted into the lock.
bool write_allowed() {
  if (!csrf_ok()) {
    return false;
  }
  if (portal_lock::accepts(server.header(PIN_HEADER))) {
    return true;
  }
  server.send(401, "application/json", "{\"status\":\"error\",\"reason\":\"locked\"}");
  return false;
}

bool persist_config_or_restore(const RuntimeConfig &previous) {
  if (config::save()) {
    return true;
  }
  config::get_mut() = previous;
  return false;
}

// Routes that exist but may have been called with the wrong method. Used to
// answer 405 instead of bouncing an API client to the dashboard.
static const char *API_ROUTES[] = {
    "/api/summary", "/api/status",      "/api/dev",       "/api/track",
    "/api/track.csv", "/api/track.geojson", "/api/config", "/api/config/reset",
    "/api/home",    "/api/home/set",    "/api/home/clear", "/api/wifi",
    "/api/wifi/ap", "/api/wifi/scan", "/api/lock",
    "/api/v1/led/state", "/api/v1/led/capabilities"};

bool is_known_api_route(const String &uri) {
  for (size_t i = 0; i < sizeof(API_ROUTES) / sizeof(API_ROUTES[0]); ++i) {
    if (uri == API_ROUTES[i]) {
      return true;
    }
  }
  return false;
}

void redirect_to_portal() {
  note_activity();
  const String uri = server.uri();

  // An API client should get an API answer. Redirecting it to the dashboard,
  // as this used to do for every unmatched request, is never useful.
  if (uri.startsWith("/api/")) {
    if (is_known_api_route(uri)) {
      server.send(405, "application/json", "{\"status\":\"error\",\"reason\":\"method\"}");
    } else {
      server.send(404, "application/json", "{\"status\":\"error\",\"reason\":\"not found\"}");
    }
    return;
  }

  // Relative on purpose: ap_ip() is 0.0.0.0 while the AP is down, so an
  // absolute URL sent a station-mode client to a dead address.
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void handle_captive_probe() {
  note_activity();
  server.send(200, "text/html",
              "<!doctype html><html><head><meta http-equiv='refresh' content='0;url=/'></head>"
              "<body><a href='/'>Dog-RGB</a></body></html>");
}

void sync_dns() {
  if (wifi_mgr::ap_enabled()) {
    if (!dns_running) {
      dns_server.setErrorReplyCode(DNSReplyCode::NoError);
      dns_running = dns_server.start(DNS_PORT, "*", wifi_mgr::ap_ip());
      if (dns_running) {
        dns_start_count++;
      }
    }
    if (dns_running) {
      dns_server.processNextRequest();
    }
    return;
  }

  if (dns_running) {
    dns_server.stop();
    dns_running = false;
    dns_stop_count++;
  }
}

const char *wifi_mode_name(wifi_mode_t mode) {
  switch (mode) {
    case WIFI_MODE_STA:
      return "STA";
    case WIFI_MODE_AP:
      return "AP";
    case WIFI_MODE_APSTA:
      return "AP+STA";
    default:
      return "OFF";
  }
}

bool parse_track_session(const String &value, int &out) {
  if (value.length() == 0 || value == "current") {
    out = -1;
    return true;
  }
  if (value.length() == 1 && isDigit(value[0])) {
    out = value.toInt();
    return (out >= 0 && out <= 2);
  }
  return false;
}

uint16_t parse_max_points(const String &value) {
  // Bounded before parsing: toInt() returns long and silently wrapped when the
  // caller passed something like "99999999999", which then read as negative
  // and quietly meant "no limit". Five digits cannot overflow.
  if (value.length() == 0 || value.length() > 5) {
    return 0;
  }
  for (size_t i = 0; i < value.length(); ++i) {
    if (!isDigit(value[i])) {
      return 0;
    }
  }
  const long v = value.toInt();
  if (v <= 0) {
    return 0;
  }
  return (v > 2000) ? 2000 : static_cast<uint16_t>(v);
}

uint32_t track_point_date(const gps::TrackView &view, const gps::TrackPoint &p) {
  if (view.start_date == 0) {
    return 0;
  }
  if (view.start_date == view.end_date || view.end_date == 0) {
    return view.start_date;
  }
  if (p.t_min < view.start_min) {
    return view.end_date;
  }
  return view.start_date;
}

void handle_root() {
  note_activity();
  server.send(200, "text/html", web_pages::html_page());
}

void handle_wifi_page() {
  note_activity();
  server.send(200, "text/html", web_pages::html_wifi_page());
}

void handle_dev_page() {
  note_activity();
  server.send(200, "text/html", web_pages::html_dev_page());
}

void handle_summary() {
  note_activity();
  server.send(200, "application/json", gps::build_summary_json());
}

void append_effect_descriptor(JsonObject out,
                              const led::EffectDescriptor &descriptor) {
  out["id"] = descriptor.id;
  out["key"] = descriptor.key;
  out["name"] = descriptor.label;
  JsonObject controls = out["controls"].to<JsonObject>();
  controls["speed"] =
      (descriptor.controls & led::EFFECT_CONTROL_SPEED) != 0;
  controls["intensity"] =
      (descriptor.controls & led::EFFECT_CONTROL_INTENSITY) != 0;
  controls["color"] =
      (descriptor.controls & led::EFFECT_CONTROL_COLOR) != 0;
  JsonObject defaults = out["defaults"].to<JsonObject>();
  defaults["speed"] = descriptor.defaults.speed;
  defaults["intensity"] = descriptor.defaults.intensity;
  JsonObject useful = out["useful_range"].to<JsonObject>();
  useful["speed_min"] = descriptor.useful.speed_min;
  useful["speed_max"] = descriptor.useful.speed_max;
  useful["intensity_min"] = descriptor.useful.intensity_min;
  useful["intensity_max"] = descriptor.useful.intensity_max;
  out["color_mode"] = led::effect_color_mode_name(descriptor.color_mode);
  out["palette_mode"] =
      led::effect_palette_mode_name(descriptor.palette_mode);
  out["safety"] = led::effect_safety_name(descriptor.safety);
}

void handle_led_capabilities_get() {
  note_activity();
  JsonDocument doc;
  doc["schema_version"] = 1;
  doc["effect_registry_version"] = led::EFFECT_REGISTRY_VERSION;
  doc["effect_count"] = led::effect_descriptor_count();
  doc["persistent_effect_ids"] = true;
  JsonObject layout = doc["layout"].to<JsonObject>();
  layout["buses"] = LED_STRIP_MODE == 2 ? 2 : 1;
  layout["pixels_per_bus"] = LED_STRIP_COUNT;
  layout["status_pixels_per_bus"] = LED_STATUS_COUNT;
  layout["physical_format"] = "RGBW";
  layout["logical_format"] = "RGB";
  JsonObject limits = doc["limits"].to<JsonObject>();
  limits["brightness_min"] = 1;
  limits["brightness_max"] = 255;
  limits["speed_min"] = 0;
  limits["speed_max"] = 255;
  limits["intensity_min"] = 0;
  limits["intensity_max"] = 255;
  limits["current_budget_min_ma"] = LED_POWER_BUDGET_MA_MIN;
  limits["current_budget_max_ma"] = LED_POWER_BUDGET_MA_MAX;
  JsonObject features = doc["features"].to<JsonObject>();
  features["state_get"] = true;
  features["state_patch"] = false;
  features["transitions"] = false;
  features["palettes"] = false;
  JsonArray effects = doc["effects"].to<JsonArray>();
  for (size_t i = 0; i < led::effect_descriptor_count(); ++i) {
    JsonObject item = effects.add<JsonObject>();
    append_effect_descriptor(item, led::effect_descriptor_at(i));
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void append_state_effect(JsonObject out, uint8_t effect_id, uint8_t speed,
                         uint8_t intensity) {
  out["id"] = effect_id;
  const led::EffectDescriptor *descriptor = led::effect_descriptor(effect_id);
  out["key"] = descriptor == nullptr ? "unknown" : descriptor->key;
  out["name"] = descriptor == nullptr ? "UNKNOWN" : descriptor->label;
  out["speed"] = speed;
  out["intensity"] = intensity;
}

void handle_led_state_get() {
  note_activity();
  const led::LedState &state = led_ui::current_state();
  const RuntimeConfig &cfg = config::get();
  JsonDocument doc;
  doc["schema_version"] = 1;
  doc["mode"] = led::led_mode_name(state.mode);
  doc["intent"] = led::led_intent_name(state.intent);
  doc["priority"] = state.priority;
  doc["body_enabled"] = state.body_enabled;
  doc["status_enabled"] = state.status_enabled;
  doc["homogeneous"] = state.homogeneous;
  doc["critical_alert"] = state.critical_alert;
  doc["range"] = state.range;
  doc["brightness"] = state.brightness;
  JsonObject effect_a = doc["effect_a"].to<JsonObject>();
  append_state_effect(effect_a, state.effect_a, state.speed, state.intensity);
  JsonObject effect_b = doc["effect_b"].to<JsonObject>();
  append_state_effect(effect_b, state.effect_b, state.speed, state.intensity);
  JsonObject base = doc["base_rgb"].to<JsonObject>();
  base["r"] = state.base.r;
  base["g"] = state.base.g;
  base["b"] = state.base.b;
  const led::PowerDiagnostics &power = led_ui::power_diagnostics();
  JsonObject limiting = doc["power"].to<JsonObject>();
  limiting["budget_ma"] = cfg.led_power_budget_ma;
  limiting["requested_ma"] = power.requested_ma;
  limiting["estimated_ma"] = power.estimated_ma;
  limiting["scale"] = power.scale;
  limiting["estimate_only"] = true;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handle_status_get() {
  note_activity();
  JsonDocument doc;
  const RuntimeConfig &cfg = config::get();
  doc["mode"] = config::mode_name(cfg.mode);
  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["ap_enabled"] = wifi_mgr::ap_enabled();
  wifi["ap_ssid"] = cfg.ap_ssid;
  wifi["ap_stations"] = wifi_mgr::ap_station_count();
  wifi["sta_connected"] = wifi_mgr::sta_connected();
  wifi["sta_connecting"] = wifi_mgr::sta_connecting();
  wifi["wifi_off"] = wifi_mgr::wifi_off();
  wifi["mdns"] = cfg.mdns;
  wifi["sta_ip"] = WiFi.localIP().toString();
  wifi["ap_ip"] = wifi_mgr::ap_ip().toString();

  JsonObject gps = doc["gps"].to<JsonObject>();
  gps["fix"] = gps::has_fix();
  gps["raw_fix"] = gps::raw_fix();
  gps["quality_ok"] = gps::quality_ok();
  gps["speed_usable"] = gps::speed_usable();
  gps["speed_kph"] = gps::last_speed_kph();
  gps["sats"] = gps::sats();
  gps["fix_quality"] = gps::fix_quality();
  const float hdop_status = gps::hdop();
  gps["hdop"] = isnan(hdop_status) ? -1.0f : hdop_status;

  JsonObject home = doc["home"].to<JsonObject>();
  home["set"] = geofence::is_set();
  home["source"] = geofence::source_name(geofence::source());
  const float dist = geofence::distance_to_home_m();
  home["distance_m"] = (dist >= 0.0f) ? dist : -1.0f;

  JsonObject day = doc["day_mode"].to<JsonObject>();
  day["enabled"] = day_mode::enabled();
  day["active"] = day_mode::active_now();
  day["state"] = day_mode::state_name();
  day["time_available"] = day_mode::time_available();
  day["local_min"] = day_mode::time_available() ? static_cast<int>(day_mode::local_min()) : -1;

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handle_dev_get() {
  note_activity();
  JsonDocument doc;
  const RuntimeConfig &cfg = config::get();
  const unsigned long now_ms = millis();

  JsonObject time = doc["time"].to<JsonObject>();
  time["uptime_ms"] = now_ms;
  time["build"] = String(__DATE__) + " " + String(__TIME__);

  JsonObject system = doc["system"].to<JsonObject>();
  system["free_heap"] = ESP.getFreeHeap();
  JsonObject configStorage = system["config_storage"].to<JsonObject>();
  configStorage["slot"] = config::storage_slot();
  configStorage["generation"] = config::storage_generation();
  configStorage["save_failures"] = config::storage_save_failures();

  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["mode"] = wifi_mode_name(static_cast<wifi_mode_t>(wifi_mgr::mode()));
  wifi["sta_connected"] = wifi_mgr::sta_connected();
  wifi["sta_connecting"] = wifi_mgr::sta_connecting();
  wifi["ap_enabled"] = wifi_mgr::ap_enabled();
  wifi["ap_stations"] = wifi_mgr::ap_station_count();
  wifi["wifi_off"] = wifi_mgr::wifi_off();
  wifi["ap_ssid"] = cfg.ap_ssid;
  wifi["mdns"] = cfg.mdns;
  wifi["sta_ip"] = WiFi.localIP().toString();
  wifi["ap_ip"] = wifi_mgr::ap_ip().toString();
  wifi["rssi"] = WiFi.RSSI();
  wifi["ap_mac"] = WiFi.softAPmacAddress();
  wifi["sta_mac"] = WiFi.macAddress();
  JsonObject wifiStorage = wifi["storage"].to<JsonObject>();
  wifiStorage["slot"] = wifi_mgr::storage_slot();
  wifiStorage["generation"] = wifi_mgr::storage_generation();
  wifiStorage["save_failures"] = wifi_mgr::storage_save_failures();

  const wifi_mgr::WifiDiagnostics &diag = wifi_mgr::diagnostics();
  JsonObject wifiDiag = wifi["diagnostics"].to<JsonObject>();
  wifiDiag["last_ap_start_ok"] = diag.last_ap_start_ok;
  wifiDiag["ap_start_count"] = diag.ap_start_count;
  wifiDiag["ap_start_fail_count"] = diag.ap_start_fail_count;
  wifiDiag["ap_retry_schedule_count"] = diag.ap_retry_schedule_count;
  wifiDiag["ap_stop_count"] = diag.ap_stop_count;
  wifiDiag["ap_restart_count"] = diag.ap_restart_count;
  wifiDiag["sta_retry_count"] = diag.sta_retry_count;
  wifiDiag["sta_connect_fail_count"] = diag.sta_connect_fail_count;
  wifiDiag["ap_station_connect_count"] = diag.ap_station_connect_count;
  wifiDiag["ap_station_disconnect_count"] = diag.ap_station_disconnect_count;
  wifiDiag["sta_got_ip_count"] = diag.sta_got_ip_count;
  wifiDiag["sta_disconnect_count"] = diag.sta_disconnect_count;
  wifiDiag["event_queue_overflow_count"] = diag.event_queue_overflow_count;
  wifiDiag["event_queue_high_water"] = diag.event_queue_high_water;
  wifiDiag["last_ap_start_ms"] = diag.last_ap_start_ms;
  wifiDiag["last_ap_stop_ms"] = diag.last_ap_stop_ms;
  wifiDiag["next_ap_retry_ms"] = diag.next_ap_retry_ms;
  wifiDiag["ap_retry_delay_ms"] = diag.ap_retry_delay_ms;
  wifiDiag["ap_retry_scheduled"] = diag.ap_retry_scheduled;
  wifiDiag["ap_retry_remaining_ms"] =
      diag.ap_retry_scheduled &&
              time_utils::deadline_pending(now_ms, diag.next_ap_retry_ms)
          ? time_utils::remaining_ms(now_ms, diag.next_ap_retry_ms)
          : 0;
  wifiDiag["last_sta_retry_ms"] = diag.last_sta_retry_ms;
  wifiDiag["next_sta_retry_ms"] = diag.next_sta_retry_ms;
  wifiDiag["sta_retry_scheduled"] = diag.sta_retry_scheduled;
  wifiDiag["sta_retry_remaining_ms"] =
      diag.sta_retry_scheduled &&
              time_utils::deadline_pending(now_ms, diag.next_sta_retry_ms)
          ? time_utils::remaining_ms(now_ms, diag.next_sta_retry_ms)
          : 0;
  wifiDiag["ap_hold_until_ms"] = diag.ap_hold_until_ms;
  wifiDiag["ap_hold_scheduled"] = diag.ap_hold_scheduled;
  wifiDiag["ap_hold_remaining_ms"] =
      diag.ap_hold_scheduled &&
              time_utils::deadline_pending(now_ms, diag.ap_hold_until_ms)
          ? time_utils::remaining_ms(now_ms, diag.ap_hold_until_ms)
          : 0;
  wifiDiag["last_wifi_event_ms"] = diag.last_wifi_event_ms;
  wifiDiag["last_wifi_event"] = diag.last_wifi_event;
  wifiDiag["current_ap_channel"] = diag.current_ap_channel;
  wifiDiag["ap_station_poll_max_us"] = diag.ap_station_poll_max_us;
  wifiDiag["channel_query_max_us"] = diag.channel_query_max_us;
  wifiDiag["last_ap_reason"] = diag.last_ap_reason;
  wifiDiag["last_ap_failure_stage"] = diag.last_ap_failure_stage;
  wifiDiag["last_sta_reason"] = diag.last_sta_reason;
  wifiDiag["dns_running"] = dns_running;
  wifiDiag["dns_start_count"] = dns_start_count;
  wifiDiag["dns_stop_count"] = dns_stop_count;

  JsonObject gps = doc["gps"].to<JsonObject>();
  gps["fix"] = gps::has_fix();
  gps["raw_fix"] = gps::raw_fix();
  gps["trusted_fix"] = gps::trusted_fix();
  gps["quality_ok"] = gps::quality_ok();
  gps["current_fix"] = gps::has_current_fix();
  gps["sats"] = gps::sats();
  gps["fix_quality"] = gps::fix_quality();
  const float hdop_dev = gps::hdop();
  gps["hdop"] = isnan(hdop_dev) ? -1.0f : hdop_dev;
  gps["speed_usable"] = gps::speed_usable();
  gps["speed_kph"] = gps::last_speed_kph();
  gps["lat"] = gps::current_lat_deg();
  gps["lon"] = gps::current_lon_deg();
  gps["date"] = gps::current_date();
  gps["last_update_min"] = gps::last_update_min();
  gps["has_time"] = gps::has_time();
  gps["local_time_min"] = gps::has_time() ? static_cast<int>(gps::local_time_min(DAY_MODE_TZ_OFFSET_MIN)) : -1;
  gps["bytes_rx"] = gps::bytes_rx();
  gps["sentences_rx"] = gps::sentences_rx();
  gps["rmc_seen"] = gps::rmc_seen();
  gps["rmc_valid"] = gps::rmc_valid();
  gps["gga_seen"] = gps::gga_seen();
  gps["overflow"] = gps::overflow();
  gps["activity_observation_intervals"] = gps::activity_observation_intervals();
  gps["activity_gap_rejects"] = gps::activity_gap_rejects();
  gps["last_activity_delta_ms"] = gps::last_activity_delta_ms();
  gps["date_transitions"] = gps::date_transition_count();
  gps["date_rejected"] = gps::date_rejected_count();
  gps["date_pending_candidate"] = gps::date_pending_candidate();
  gps["date_pending_observations"] = gps::date_pending_observations();
  JsonObject metricsStorage = gps["metrics_storage"].to<JsonObject>();
  metricsStorage["slot"] = gps::metrics_storage_slot();
  metricsStorage["generation"] = gps::metrics_storage_generation();
  metricsStorage["save_failures"] = gps::metrics_storage_save_failures();
  metricsStorage["recoveries"] = gps::metrics_storage_recoveries();
  JsonObject sessionStorage = gps["session_storage"].to<JsonObject>();
  sessionStorage["slot"] = gps::session_storage_slot();
  sessionStorage["generation"] = gps::session_storage_generation();
  sessionStorage["save_failures"] = gps::session_storage_save_failures();
  sessionStorage["recoveries"] = gps::session_storage_recoveries();
  sessionStorage["history_count"] = gps::session_history_count();
  JsonObject dailyStorage = gps["daily_journal"].to<JsonObject>();
  dailyStorage["slot"] = gps::daily_journal_slot();
  dailyStorage["generation"] = gps::daily_journal_generation();
  dailyStorage["save_failures"] = gps::daily_journal_save_failures();
  dailyStorage["last_completed_date"] = gps::last_completed_date();
  const int64_t age_last_byte = gps::has_byte_observation()
                                    ? static_cast<int64_t>(time_utils::age_ms(
                                          now_ms, gps::last_byte_ms()))
                                    : -1;
  const int64_t age_last_fix = gps::has_fix_observation()
                                   ? static_cast<int64_t>(time_utils::age_ms(
                                         now_ms, gps::last_fix_ms()))
                                   : -1;
  const int64_t age_last_gga = gps::has_gga_observation()
                                  ? static_cast<int64_t>(time_utils::age_ms(
                                        now_ms, gps::last_gga_ms()))
                                  : -1;
  gps["age_last_byte_ms"] = age_last_byte;
  gps["age_last_fix_ms"] = age_last_fix;
  gps["age_last_gga_ms"] = age_last_gga;

  JsonObject geo = doc["geofence"].to<JsonObject>();
  geo["set"] = geofence::is_set();
  geo["source"] = geofence::source_name(geofence::source());
  geo["home_lat"] = geofence::home_lat();
  geo["home_lon"] = geofence::home_lon();
  JsonObject homeStorage = geo["storage"].to<JsonObject>();
  homeStorage["slot"] = geofence::storage_slot();
  homeStorage["generation"] = geofence::storage_generation();
  homeStorage["save_failures"] = geofence::storage_save_failures();
  const float dist_m = geofence::distance_to_home_m();
  geo["distance_m"] = (dist_m >= 0.0f) ? dist_m : -1.0f;
  const int geo_range = (geofence::is_set() && dist_m >= 0.0f)
                            ? static_cast<int>(geofence::geofence_range(dist_m))
                            : -1;
  geo["range"] = geo_range;

  JsonObject led = doc["led"].to<JsonObject>();
  const led::LedState &active_state = led_ui::current_state();
  led["mode"] = led::led_mode_name(active_state.mode);
  led["intent"] = led::led_intent_name(active_state.intent);
  led["priority"] = active_state.priority;
  led["body_enabled"] = active_state.body_enabled;
  led["status_enabled"] = active_state.status_enabled;
  led["homogeneous"] = active_state.homogeneous;
  led["critical_alert"] = active_state.critical_alert;
  led["brightness"] = active_state.brightness;
  const auto &power_diag = led_ui::power_diagnostics();
  JsonObject power = led["power"].to<JsonObject>();
  power["enabled"] = cfg.led_power_limit_enabled;
  power["budget_ma"] = cfg.led_power_budget_ma;
  power["base_current_ma"] = cfg.led_base_current_ma;
  power["rgb_channel_ma"] = cfg.led_rgb_channel_ma;
  power["white_channel_ma"] = cfg.led_white_channel_ma;
  power["requested_ma"] = power_diag.requested_ma;
  power["estimated_ma"] = power_diag.estimated_ma;
  power["peak_requested_ma"] = power_diag.peak_requested_ma;
  power["scale"] = power_diag.scale;
  power["scale_percent"] =
      (static_cast<uint16_t>(power_diag.scale) * 100U) / 255U;
  power["frames_limited"] = power_diag.frames_limited;
  power["estimate_only"] = true;
  led["range"] = active_state.range;
  JsonObject effA = led["effect_a"].to<JsonObject>();
  append_state_effect(effA, active_state.effect_a, active_state.speed,
                      active_state.intensity);
  JsonObject effB = led["effect_b"].to<JsonObject>();
  append_state_effect(effB, active_state.effect_b, active_state.speed,
                      active_state.intensity);
  JsonObject baseRgb = led["base_rgb"].to<JsonObject>();
  baseRgb["r"] = active_state.base.r;
  baseRgb["g"] = active_state.base.g;
  baseRgb["b"] = active_state.base.b;
  JsonObject simple = led["simple"].to<JsonObject>();
  simple["effect"] = cfg.single.effect_id;
  simple["name"] = led_ui::effect_name(cfg.single.effect_id);
  simple["speed"] = cfg.single.speed;
  simple["intensity"] = cfg.single.intensity;
  JsonObject simpleRgb = simple["rgb"].to<JsonObject>();
  simpleRgb["r"] = cfg.single.base_r;
  simpleRgb["g"] = cfg.single.base_g;
  simpleRgb["b"] = cfg.single.base_b;
  JsonObject show = led["show"].to<JsonObject>();
  const uint8_t show_id = led_ui::current_show_effect();
  show["effect"] = show_id;
  show["name"] = led_ui::effect_name(show_id);

  JsonObject day = doc["day_mode"].to<JsonObject>();
  day["enabled"] = day_mode::enabled();
  day["active"] = day_mode::active_now();
  day["state"] = day_mode::state_name();
  day["time_available"] = day_mode::time_available();
  day["local_min"] = day_mode::time_available() ? static_cast<int>(day_mode::local_min()) : -1;
  day["start_min"] = DAY_MODE_START_MIN;
  day["end_min"] = DAY_MODE_END_MIN;
  day["tz_offset_min"] = DAY_MODE_TZ_OFFSET_MIN;

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

bool track_json_cb(const gps::TrackPoint &p, void *ctx) {
  CoordinateStreamContext *output = reinterpret_cast<CoordinateStreamContext *>(ctx);
  if (!output->first && !output->stream->append(",")) {
    return false;
  }
  output->first = false;
  char line[64];
  const float lat = p.lat_e7 * 1e-7f;
  const float lon = p.lon_e7 * 1e-7f;
  snprintf(line, sizeof(line), "[%.7f,%.7f]", lat, lon);
  return output->stream->append(line);
}

bool track_csv_cb(const gps::TrackPoint &p, void *ctx) {
  CsvStreamContext *output = reinterpret_cast<CsvStreamContext *>(ctx);
  const uint32_t date = track_point_date(*output->view, p);
  char line[72];
  const float lat = p.lat_e7 * 1e-7f;
  const float lon = p.lon_e7 * 1e-7f;
  snprintf(line, sizeof(line), "%lu,%u,%.7f,%.7f\n", static_cast<unsigned long>(date), p.t_min, lat, lon);
  return output->stream->append(line);
}

bool track_geojson_cb(const gps::TrackPoint &p, void *ctx) {
  CoordinateStreamContext *output = reinterpret_cast<CoordinateStreamContext *>(ctx);
  if (!output->first && !output->stream->append(",")) {
    return false;
  }
  output->first = false;
  char line[64];
  const float lat = p.lat_e7 * 1e-7f;
  const float lon = p.lon_e7 * 1e-7f;
  snprintf(line, sizeof(line), "[%.7f,%.7f]", lon, lat);
  return output->stream->append(line);
}

void handle_track_get() {
  note_activity();
  int session_id = -1;
  if (!parse_track_session(server.arg("session"), session_id)) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"session\"}");
    return;
  }
  const uint16_t max_points = parse_max_points(server.arg("max_points"));
  gps::TrackView view = {};
  if (!gps::track_get_view(session_id, view)) {
    server.send(200, "application/json", "{\"count\":0,\"status\":\"no_data\"}");
    return;
  }

  gps::tick();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  gps::tick();
  TrackStream stream;
  char head[256];
  const float min_lat = view.min_lat_e7 * 1e-7f;
  const float max_lat = view.max_lat_e7 * 1e-7f;
  const float min_lon = view.min_lon_e7 * 1e-7f;
  const float max_lon = view.max_lon_e7 * 1e-7f;
  snprintf(head, sizeof(head),
           "{\"count\":%u,\"open\":%s,\"sample_ms\":%u,\"start_date\":%lu,\"start_min\":%u,"
           "\"end_date\":%lu,\"end_min\":%u,"
           "\"bbox\":{\"min_lat\":%.7f,\"max_lat\":%.7f,\"min_lon\":%.7f,\"max_lon\":%.7f},\"points\":[",
           view.count,
           view.open ? "true" : "false",
           view.sample_ms,
           static_cast<unsigned long>(view.start_date),
           view.start_min,
           static_cast<unsigned long>(view.end_date),
           view.end_min,
           min_lat,
           max_lat,
           min_lon,
           max_lon);
  stream.append(head);

  CoordinateStreamContext output = {&stream, true};
  gps::track_iter_points(view.slot, max_points, track_json_cb, &output);
  if (stream.healthy()) {
    stream.append("]}");
    stream.finish();
  }
}

void handle_track_csv() {
  note_activity();
  int session_id = -1;
  if (!parse_track_session(server.arg("session"), session_id)) {
    server.send(400, "text/plain", "session");
    return;
  }
  const uint16_t max_points = parse_max_points(server.arg("max_points"));
  gps::TrackView view = {};
  if (!gps::track_get_view(session_id, view)) {
    server.send(200, "text/csv", "date,min,lat,lon\n");
    return;
  }
  gps::tick();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv", "");
  gps::tick();
  TrackStream stream;
  stream.append("date,min,lat,lon\n");
  CsvStreamContext output = {&stream, &view};
  gps::track_iter_points(view.slot, max_points, track_csv_cb, &output);
  stream.finish();
}

void handle_track_geojson() {
  note_activity();
  int session_id = -1;
  if (!parse_track_session(server.arg("session"), session_id)) {
    server.send(400, "application/geo+json", "{\"status\":\"error\",\"reason\":\"session\"}");
    return;
  }
  const uint16_t max_points = parse_max_points(server.arg("max_points"));
  gps::TrackView view = {};
  if (!gps::track_get_view(session_id, view)) {
    server.send(200, "application/geo+json", "{\"type\":\"FeatureCollection\",\"features\":[]}");
    return;
  }
  gps::tick();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/geo+json", "");
  gps::tick();
  TrackStream stream;
  stream.append("{\"type\":\"FeatureCollection\",\"features\":[{\"type\":\"Feature\",\"geometry\":{\"type\":\"LineString\",\"coordinates\":[");
  CoordinateStreamContext output = {&stream, true};
  gps::track_iter_points(view.slot, max_points, track_geojson_cb, &output);
  if (stream.healthy()) {
    stream.append("]},\"properties\":{}}]}");
    stream.finish();
  }
}

void handle_config_get() {
  note_activity();
  JsonDocument doc;
  const RuntimeConfig &cfg = config::get();
  doc["version"] = config::version();
  doc["mode"] = config::mode_name(cfg.mode);
  doc["fence_max_m"] = cfg.fence_max_m;
  JsonObject led_cfg = doc["led"].to<JsonObject>();
  led_cfg["brightness"] = cfg.brightness;
  JsonObject power_cfg = led_cfg["power"].to<JsonObject>();
  power_cfg["enabled"] = cfg.led_power_limit_enabled;
  power_cfg["budget_ma"] = cfg.led_power_budget_ma;
  power_cfg["base_current_ma"] = cfg.led_base_current_ma;
  power_cfg["rgb_channel_ma"] = cfg.led_rgb_channel_ma;
  power_cfg["white_channel_ma"] = cfg.led_white_channel_ma;
  JsonObject day_cfg = doc["day_mode"].to<JsonObject>();
  day_cfg["enabled"] = cfg.day_mode_enabled;
  day_cfg["start_min"] = DAY_MODE_START_MIN;
  day_cfg["end_min"] = DAY_MODE_END_MIN;
  day_cfg["tz_offset_min"] = DAY_MODE_TZ_OFFSET_MIN;
  JsonObject gps_cfg = doc["gps"].to<JsonObject>();
  gps_cfg["min_fix_quality"] = cfg.gps_min_fix_quality;
  gps_cfg["min_sats"] = cfg.gps_min_sats;
  gps_cfg["max_hdop"] = cfg.gps_max_hdop;
  gps_cfg["max_gga_age_ms"] = cfg.gps_max_gga_age_ms;
  gps_cfg["min_segment_m"] = cfg.gps_min_segment_m;
  gps_cfg["hdop_factor"] = cfg.gps_hdop_factor;
  gps_cfg["max_min_segment_m"] = cfg.gps_max_min_segment_m;
  JsonArray ranges = doc["speed_ranges_kph"].to<JsonArray>();
  for (int i = 0; i < 9; ++i) {
    ranges.add(cfg.ranges[i]);
  }
  JsonObject effects = doc["effects"].to<JsonObject>();
  for (int i = 0; i < 10; ++i) {
    JsonObject r = effects[String("range") + String(i + 1)].to<JsonObject>();
    r["a"] = cfg.effects[i].effect_a;
    r["b"] = cfg.effects[i].effect_b;
    r["speed"] = cfg.effects[i].speed;
    r["intensity"] = cfg.effects[i].intensity;
  }
  JsonObject single = doc["single"].to<JsonObject>();
  single["effect"] = cfg.single.effect_id;
  single["speed"] = cfg.single.speed;
  single["intensity"] = cfg.single.intensity;
  JsonObject rgb = single["rgb"].to<JsonObject>();
  rgb["r"] = cfg.single.base_r;
  rgb["g"] = cfg.single.base_g;
  rgb["b"] = cfg.single.base_b;
  doc["wifi"]["ap_ssid"] = cfg.ap_ssid;
  doc["wifi"]["has_ap_pass"] = (cfg.ap_pass.length() >= 8);
  // Whether a home password is stored, never the password itself. Without this
  // the portal cannot tell "no password saved" from "saved, not shown", and the
  // user has no way to know if leaving the field blank wipes it.
  doc["wifi"]["has_sta_pass"] = (wifi_mgr::pass().length() > 0);
  doc["wifi"]["sta_ssid"] = wifi_mgr::ssid();
  doc["wifi"]["mdns"] = cfg.mdns;

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handle_config_post() {
  note_activity();
  if (!write_allowed()) {
    return;
  }
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"no body\"}");
    return;
  }
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"bad json\"}");
    return;
  }

  RuntimeConfig next = config::get();
  const int brightness = doc["led"]["brightness"] | next.brightness;
  if (brightness < 1 || brightness > 255) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"brightness\"}");
    return;
  }
  next.brightness = static_cast<uint8_t>(brightness);

  if (!doc["led"]["power"].isNull()) {
    JsonObject power_cfg = doc["led"]["power"].as<JsonObject>();
    if (power_cfg.isNull() ||
        (!power_cfg["enabled"].isNull() &&
         !power_cfg["enabled"].is<bool>())) {
      server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"led_power\"}");
      return;
    }
    const bool enabled = power_cfg["enabled"] | next.led_power_limit_enabled;
    const int budget_ma = power_cfg["budget_ma"] |
                          static_cast<int>(next.led_power_budget_ma);
    const int base_current_ma = power_cfg["base_current_ma"] |
                                static_cast<int>(next.led_base_current_ma);
    const int rgb_channel_ma = power_cfg["rgb_channel_ma"] |
                               static_cast<int>(next.led_rgb_channel_ma);
    const int white_channel_ma = power_cfg["white_channel_ma"] |
                                 static_cast<int>(next.led_white_channel_ma);
    if (budget_ma < LED_POWER_BUDGET_MA_MIN ||
        budget_ma > LED_POWER_BUDGET_MA_MAX ||
        base_current_ma < 0 ||
        base_current_ma > LED_BASE_CURRENT_MA_MAX ||
        base_current_ma >= budget_ma ||
        rgb_channel_ma < LED_CHANNEL_MA_MIN ||
        rgb_channel_ma > LED_CHANNEL_MA_MAX ||
        white_channel_ma < LED_CHANNEL_MA_MIN ||
        white_channel_ma > LED_CHANNEL_MA_MAX) {
      server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"led_power\"}");
      return;
    }
    next.led_power_limit_enabled = enabled;
    next.led_power_budget_ma = static_cast<uint16_t>(budget_ma);
    next.led_base_current_ma = static_cast<uint16_t>(base_current_ma);
    next.led_rgb_channel_ma = static_cast<uint8_t>(rgb_channel_ma);
    next.led_white_channel_ma = static_cast<uint8_t>(white_channel_ma);
  }

  if (!doc["mode"].isNull()) {
    const char *mode_str = doc["mode"];
    uint8_t parsed_mode = next.mode;
    if (!config::parse_mode(mode_str, parsed_mode)) {
      server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"mode\"}");
      return;
    }
    next.mode = parsed_mode;
  }

  if (!doc["fence_max_m"].isNull()) {
    const int fence_max = doc["fence_max_m"] | static_cast<int>(next.fence_max_m);
    if (fence_max < GEOFENCE_MAX_M_MIN || fence_max > GEOFENCE_MAX_M_MAX) {
      server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"fence_max\"}");
      return;
    }
    next.fence_max_m = static_cast<uint16_t>(fence_max);
  }

  if (!doc["day_mode"].isNull()) {
    JsonObject day_cfg = doc["day_mode"].as<JsonObject>();
    if (day_cfg.isNull()) {
      server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"day_mode\"}");
      return;
    }
    if (!day_cfg["enabled"].is<bool>()) {
      server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"day_mode\"}");
      return;
    }
    next.day_mode_enabled = day_cfg["enabled"].as<bool>();
  }

  if (!doc["gps"].isNull()) {
    JsonObject gps_cfg = doc["gps"].as<JsonObject>();
    if (gps_cfg.isNull()) {
      server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"gps\"}");
      return;
    }
    const int min_fix_quality = gps_cfg["min_fix_quality"] | next.gps_min_fix_quality;
    const int min_sats = gps_cfg["min_sats"] | next.gps_min_sats;
    const float max_hdop = gps_cfg["max_hdop"] | next.gps_max_hdop;
    const int max_gga_age = gps_cfg["max_gga_age_ms"] | static_cast<int>(next.gps_max_gga_age_ms);
    const float min_segment_m = gps_cfg["min_segment_m"] | next.gps_min_segment_m;
    const float hdop_factor = gps_cfg["hdop_factor"] | next.gps_hdop_factor;
    const float max_min_segment_m = gps_cfg["max_min_segment_m"] | next.gps_max_min_segment_m;
    if (min_fix_quality < GPS_MIN_FIX_QUALITY_MIN || min_fix_quality > GPS_MIN_FIX_QUALITY_MAX ||
        min_sats < GPS_MIN_SATS_MIN || min_sats > GPS_MIN_SATS_MAX ||
        !(max_hdop >= GPS_MAX_HDOP_MIN && max_hdop <= GPS_MAX_HDOP_MAX) ||
        max_gga_age < GPS_MAX_GGA_AGE_MS_MIN || max_gga_age > GPS_MAX_GGA_AGE_MS_MAX ||
        !(min_segment_m >= GPS_MIN_SEGMENT_M_MIN && min_segment_m <= GPS_MIN_SEGMENT_M_MAX) ||
        !(hdop_factor >= GPS_HDOP_FACTOR_MIN && hdop_factor <= GPS_HDOP_FACTOR_MAX) ||
        !(max_min_segment_m >= GPS_MAX_MIN_SEGMENT_M_MIN && max_min_segment_m <= GPS_MAX_MIN_SEGMENT_M_MAX) ||
        min_segment_m > max_min_segment_m) {
      server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"gps\"}");
      return;
    }
    next.gps_min_fix_quality = static_cast<uint8_t>(min_fix_quality);
    next.gps_min_sats = static_cast<uint8_t>(min_sats);
    next.gps_max_hdop = max_hdop;
    next.gps_max_gga_age_ms = static_cast<uint16_t>(max_gga_age);
    next.gps_min_segment_m = min_segment_m;
    next.gps_hdop_factor = hdop_factor;
    next.gps_max_min_segment_m = max_min_segment_m;
  }

  // Every other field here is optional, so these were too until now: omitting
  // them meant a 400 rather than "leave them alone". Present but malformed is
  // still an error.
  if (!doc["speed_ranges_kph"].isNull()) {
    JsonArray ranges = doc["speed_ranges_kph"].as<JsonArray>();
    if (ranges.size() != 9) {
      server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"ranges\"}");
      return;
    }
    for (int i = 0; i < 9; ++i) {
      next.ranges[i] = ranges[i].as<float>();
      if (next.ranges[i] <= 0.0f) {
        server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"ranges value\"}");
        return;
      }
    }
  }
  if (!config::validate_ranges(next.ranges)) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"ranges order\"}");
    return;
  }

  if (!doc["effects"].isNull()) {
    JsonObject effects = doc["effects"].as<JsonObject>();
    for (int i = 0; i < 10; ++i) {
      JsonObject r = effects[String("range") + String(i + 1)];
      if (r.isNull()) {
        server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"effects\"}");
        return;
      }
      const int eff_a = r["a"] | next.effects[i].effect_a;
      const int eff_b = r["b"] | next.effects[i].effect_b;
      const int eff_speed = r["speed"] | next.effects[i].speed;
      const int eff_intensity = r["intensity"] | next.effects[i].intensity;
      if (!led::effect_id_valid(eff_a) || !led::effect_id_valid(eff_b) ||
          eff_speed < 0 || eff_speed > 255 || eff_intensity < 0 || eff_intensity > 255) {
        server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"effect values\"}");
        return;
      }
      next.effects[i].effect_a = static_cast<uint8_t>(eff_a);
      next.effects[i].effect_b = static_cast<uint8_t>(eff_b);
      next.effects[i].speed = static_cast<uint8_t>(eff_speed);
      next.effects[i].intensity = static_cast<uint8_t>(eff_intensity);
    }
  }
  if (!config::validate_effects(next.effects)) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"effect id\"}");
    return;
  }

  if (!doc["single"].isNull()) {
    JsonObject single = doc["single"].as<JsonObject>();
    if (single.isNull()) {
      server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"single\"}");
      return;
    }
    const int single_eff = single["effect"] | next.single.effect_id;
    const int single_speed = single["speed"] | next.single.speed;
    const int single_intensity = single["intensity"] | next.single.intensity;
    int single_r = next.single.base_r;
    int single_g = next.single.base_g;
    int single_b = next.single.base_b;
    JsonObject rgb = single["rgb"].as<JsonObject>();
    if (!rgb.isNull()) {
      single_r = rgb["r"] | single_r;
      single_g = rgb["g"] | single_g;
      single_b = rgb["b"] | single_b;
    }
    if (!led::effect_id_valid(single_eff) ||
        single_speed < 0 || single_speed > 255 ||
        single_intensity < 0 || single_intensity > 255 ||
        single_r < 0 || single_r > 255 ||
        single_g < 0 || single_g > 255 ||
        single_b < 0 || single_b > 255) {
      server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"single values\"}");
      return;
    }
    next.single.effect_id = static_cast<uint8_t>(single_eff);
    next.single.speed = static_cast<uint8_t>(single_speed);
    next.single.intensity = static_cast<uint8_t>(single_intensity);
    next.single.base_r = static_cast<uint8_t>(single_r);
    next.single.base_g = static_cast<uint8_t>(single_g);
    next.single.base_b = static_cast<uint8_t>(single_b);
  }

  const String ap_ssid = doc["wifi"]["ap_ssid"] | next.ap_ssid;
  const String ap_pass = doc["wifi"]["ap_pass"] | String("");
  const bool ap_open = doc["wifi"]["ap_open"] | false;
  const String mdns = doc["wifi"]["mdns"] | next.mdns;
  if (!config::valid_ap_ssid(ap_ssid)) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"ssid\"}");
    return;
  }
  if (!ap_open && ap_pass.length() == 0 && next.ap_pass.length() == 0) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"pass required\"}");
    return;
  }
  if (!ap_open && ap_pass.length() > 0 && !config::valid_ap_pass(ap_pass)) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"pass\"}");
    return;
  }
  if (!config::valid_mdns(mdns)) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"mdns\"}");
    return;
  }
  next.ap_ssid = ap_ssid;
  if (ap_open) {
    next.ap_pass = "";
  } else if (ap_pass.length() > 0) {
    next.ap_pass = ap_pass;
  }
  next.mdns = mdns;

  RuntimeConfig previous = config::get();
  config::get_mut() = next;
  if (!persist_config_or_restore(previous)) {
    server.send(500, "application/json", "{\"status\":\"error\",\"reason\":\"storage\"}");
    return;
  }
  config::apply(previous);
  const bool wifi_restart = (config::get().ap_ssid != previous.ap_ssid || config::get().ap_pass != previous.ap_pass);
  if (wifi_restart) {
    wifi_mgr::schedule_ap_restart();
  }
  server.send(200, "application/json", wifi_restart ? "{\"status\":\"ok\",\"wifi_restart\":true}"
                                                     : "{\"status\":\"ok\",\"wifi_restart\":false}");
}

void handle_config_reset() {
  note_activity();
  if (!write_allowed()) {
    return;
  }
  RuntimeConfig previous = config::get();
  config::set_defaults();
  if (!persist_config_or_restore(previous)) {
    server.send(500, "application/json", "{\"status\":\"error\",\"reason\":\"storage\"}");
    return;
  }
  config::apply(previous);
  if (config::get().ap_ssid != previous.ap_ssid || config::get().ap_pass != previous.ap_pass) {
    wifi_mgr::schedule_ap_restart();
  }
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handle_wifi_ap_save() {
  note_activity();
  if (!write_allowed()) {
    return;
  }
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"no body\"}");
    return;
  }
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"bad json\"}");
    return;
  }

  RuntimeConfig next = config::get();
  const String ap_ssid = doc["ap_ssid"] | next.ap_ssid;
  const String ap_pass = doc["ap_pass"] | String("");
  const bool ap_open = doc["ap_open"] | false;
  const String mdns = doc["mdns"] | next.mdns;
  if (!config::valid_ap_ssid(ap_ssid)) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"ssid\"}");
    return;
  }
  if (!ap_open && ap_pass.length() == 0 && next.ap_pass.length() == 0) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"pass required\"}");
    return;
  }
  if (!ap_open && ap_pass.length() > 0 && !config::valid_ap_pass(ap_pass)) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"pass\"}");
    return;
  }
  if (!config::valid_mdns(mdns)) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"mdns\"}");
    return;
  }

  next.ap_ssid = ap_ssid;
  if (ap_open) {
    next.ap_pass = "";
  } else if (ap_pass.length() > 0) {
    next.ap_pass = ap_pass;
  }
  next.mdns = mdns;

  RuntimeConfig previous = config::get();
  config::get_mut() = next;
  if (!persist_config_or_restore(previous)) {
    server.send(500, "application/json", "{\"status\":\"error\",\"reason\":\"storage\"}");
    return;
  }
  config::apply(previous);
  const bool wifi_restart = (config::get().ap_ssid != previous.ap_ssid || config::get().ap_pass != previous.ap_pass);
  if (wifi_restart) {
    wifi_mgr::schedule_ap_restart();
  }
  server.send(200, "application/json", wifi_restart ? "{\"status\":\"ok\",\"wifi_restart\":true}"
                                                     : "{\"status\":\"ok\",\"wifi_restart\":false}");
}

void handle_config_page() {
  note_activity();
  server.send(200, "text/html", web_pages::html_config_page());
}

void handle_home_get() {
  note_activity();
  JsonDocument doc;
  doc["home_set"] = geofence::is_set();
  doc["home_source"] = geofence::source_name(geofence::source());
  doc["home_lat"] = geofence::is_set() ? geofence::home_lat() : 0.0f;
  doc["home_lon"] = geofence::is_set() ? geofence::home_lon() : 0.0f;
  doc["gps_fix"] = gps::has_fix();
  doc["current_lat"] = gps::has_current_fix() ? gps::current_lat_deg() : 0.0f;
  doc["current_lon"] = gps::has_current_fix() ? gps::current_lon_deg() : 0.0f;
  const float dist = geofence::distance_to_home_m();
  doc["distance_m"] = (dist >= 0.0f) ? dist : -1.0f;

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handle_home_set() {
  note_activity();
  if (!write_allowed()) {
    return;
  }
  if (!gps::has_fix() || !gps::has_current_fix()) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"no_gps\"}");
    return;
  }
  if (!geofence::set_home(gps::current_lat_deg(), gps::current_lon_deg(), 2)) {
    server.send(500, "application/json", "{\"status\":\"error\",\"reason\":\"storage\"}");
    return;
  }
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handle_home_clear() {
  note_activity();
  if (!write_allowed()) {
    return;
  }
  if (!geofence::clear_home()) {
    server.send(500, "application/json", "{\"status\":\"error\",\"reason\":\"storage\"}");
    return;
  }
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// Scanning is split into start (POST) and poll (GET) because the radio needs a
// few seconds and this server is single-threaded: a blocking scan would stall
// every other request, including the page the user is looking at.
void handle_wifi_scan_start() {
  note_activity();
  if (!write_allowed()) {
    return;
  }
  if (!wifi_mgr::scan_begin()) {
    server.send(503, "application/json", "{\"status\":\"error\",\"reason\":\"radio\"}");
    return;
  }
  server.send(200, "application/json", "{\"status\":\"scanning\"}");
}

void handle_wifi_scan_get() {
  note_activity();
  // At most this many networks are reported. A busy neighbourhood can return
  // far more, and every extra entry is heap on a device that has little of it.
  const int16_t MAX_REPORTED = 20;

  JsonDocument doc;
  const wifi_mgr::ScanState state = wifi_mgr::scan_state();
  switch (state) {
    case wifi_mgr::ScanState::Idle:
      doc["state"] = "idle";
      break;
    case wifi_mgr::ScanState::Running:
      doc["state"] = "scanning";
      break;
    case wifi_mgr::ScanState::Failed:
      doc["state"] = "failed";
      break;
    case wifi_mgr::ScanState::Ready:
      doc["state"] = "ready";
      break;
  }

  if (state == wifi_mgr::ScanState::Ready) {
    JsonArray nets = doc["networks"].to<JsonArray>();
    const int16_t found = wifi_mgr::scan_count();
    int16_t reported = 0;
    for (int16_t i = 0; i < found && reported < MAX_REPORTED; ++i) {
      String ssid;
      int32_t rssi = 0;
      bool open = false;
      if (!wifi_mgr::scan_entry(i, ssid, rssi, open)) {
        continue;
      }
      if (ssid.length() == 0) {
        continue;  // hidden network: nothing useful to show or tap
      }
      bool duplicate = false;
      for (JsonObject seen : nets) {
        if (ssid == seen["ssid"].as<const char *>()) {
          duplicate = true;
          break;
        }
      }
      if (duplicate) {
        continue;  // mesh/repeater advertising the same name on several radios
      }
      JsonObject net = nets.add<JsonObject>();
      net["ssid"] = ssid;
      net["rssi"] = rssi;
      net["open"] = open;
      ++reported;
    }
    doc["total"] = found;
  }

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);

  // The client has the list now; hand the driver's buffer back.
  if (state == wifi_mgr::ScanState::Ready) {
    wifi_mgr::scan_release();
  }
}

void handle_lock_get() {
  note_activity();
  server.send(200, "application/json",
              portal_lock::enabled() ? "{\"enabled\":true}" : "{\"enabled\":false}");
}

// Setting, changing or clearing the PIN is itself a guarded write, so a locked
// portal cannot be unlocked without the current PIN.
void handle_lock_post() {
  note_activity();
  if (!write_allowed()) {
    return;
  }
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"no body\"}");
    return;
  }
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"bad json\"}");
    return;
  }
  const bool enable = doc["enabled"] | false;
  const String pin = doc["pin"] | String("");
  if (enable && !portal_lock::valid_pin(pin)) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"pin\"}");
    return;
  }
  if (!portal_lock::set_pin(enable ? pin : String(""))) {
    server.send(500, "application/json", "{\"status\":\"error\",\"reason\":\"storage\"}");
    return;
  }
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handle_wifi_save() {
  note_activity();
  if (!write_allowed()) {
    return;
  }
  if (!server.hasArg("ssid")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"ssid\"}");
    return;
  }
  const String ssid = server.arg("ssid");
  const String pass = server.arg("pass");
  // Home-network credentials, not our own AP's: validated accordingly.
  if (!config::valid_sta_ssid(ssid)) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"ssid\"}");
    return;
  }
  if (!config::valid_sta_pass(pass)) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"pass\"}");
    return;
  }
  if (!wifi_mgr::save_creds(ssid, pass)) {
    server.send(500, "application/json", "{\"status\":\"error\",\"reason\":\"storage\"}");
    return;
  }
  wifi_mgr::start_sta_mode();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}
} // namespace

void begin() {
  portal_lock::begin();
  // WebServer discards headers it was not told to keep, so csrf_ok() would
  // never see the guard header without this.
  server.collectHeaders(COLLECTED_HEADERS,
                        sizeof(COLLECTED_HEADERS) / sizeof(COLLECTED_HEADERS[0]));
  server.on("/", HTTP_GET, handle_root);
  server.on("/api/summary", HTTP_GET, handle_summary);
  server.on("/api/status", HTTP_GET, handle_status_get);
  server.on("/api/dev", HTTP_GET, handle_dev_get);
  server.on("/api/v1/led/state", HTTP_GET, handle_led_state_get);
  server.on("/api/v1/led/capabilities", HTTP_GET,
            handle_led_capabilities_get);
  server.on("/api/track", HTTP_GET, handle_track_get);
  server.on("/api/track.csv", HTTP_GET, handle_track_csv);
  server.on("/api/track.geojson", HTTP_GET, handle_track_geojson);
  server.on("/api/config", HTTP_GET, handle_config_get);
  server.on("/api/config", HTTP_POST, handle_config_post);
  server.on("/api/config/reset", HTTP_POST, handle_config_reset);
  server.on("/config", HTTP_GET, handle_config_page);
  server.on("/dev", HTTP_GET, handle_dev_page);
  server.on("/api/lock", HTTP_GET, handle_lock_get);
  server.on("/api/lock", HTTP_POST, handle_lock_post);
  server.on("/api/home", HTTP_GET, handle_home_get);
  server.on("/api/home/set", HTTP_POST, handle_home_set);
  server.on("/api/home/clear", HTTP_POST, handle_home_clear);
  server.on("/wifi", HTTP_GET, handle_wifi_page);
  server.on("/api/wifi", HTTP_POST, handle_wifi_save);
  server.on("/api/wifi/ap", HTTP_POST, handle_wifi_ap_save);
  server.on("/api/wifi/scan", HTTP_POST, handle_wifi_scan_start);
  server.on("/api/wifi/scan", HTTP_GET, handle_wifi_scan_get);
  server.on("/generate_204", HTTP_GET, handle_captive_probe);
  server.on("/gen_204", HTTP_GET, handle_captive_probe);
  server.on("/hotspot-detect.html", HTTP_GET, handle_captive_probe);
  server.on("/library/test/success.html", HTTP_GET, handle_captive_probe);
  server.on("/ncsi.txt", HTTP_GET, handle_captive_probe);
  server.on("/connecttest.txt", HTTP_GET, handle_captive_probe);
  server.onNotFound(redirect_to_portal);
  server.begin();
}

void handle_client() {
  sync_dns();
  server.handleClient();
}
} // namespace portal_http
