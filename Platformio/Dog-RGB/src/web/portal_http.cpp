#include "web/portal_http.h"

#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>
#include <math.h>

#include "config/runtime_config.h"
#include "config.h"
#include "geofence/home.h"
#include "gps/gps.h"
#include "led/led_ui.h"
#include "storage/nvs_store.h"
#include "web/pages.h"
#include "wifi/wifi_mgr.h"

namespace portal_http {
namespace {
WebServer server(80);

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

void handle_root() {
  server.send(200, "text/html", web_pages::html_page());
}

void handle_wifi_page() {
  server.send(200, "text/html", web_pages::html_wifi_page());
}

void handle_dev_page() {
  server.send(200, "text/html", web_pages::html_dev_page());
}

void handle_summary() {
  server.send(200, "application/json", gps::build_summary_json());
}

void handle_status_get() {
  StaticJsonDocument<896> doc;
  const RuntimeConfig &cfg = config::get();
  doc["mode"] = config::mode_name(cfg.mode);
  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["ap_enabled"] = wifi_mgr::ap_enabled();
  wifi["ap_ssid"] = cfg.ap_ssid;
  wifi["ap_stations"] = wifi_mgr::ap_station_count();
  wifi["sta_connected"] = wifi_mgr::sta_connected();
  wifi["mdns"] = cfg.mdns;

  JsonObject gps = doc["gps"].to<JsonObject>();
  gps["fix"] = gps::has_fix();
  gps["raw_fix"] = gps::raw_fix();
  gps["quality_ok"] = gps::quality_ok();
  gps["sats"] = gps::sats();
  gps["fix_quality"] = gps::fix_quality();
  const float hdop_status = gps::hdop();
  gps["hdop"] = isnan(hdop_status) ? -1.0f : hdop_status;

  JsonObject home = doc["home"].to<JsonObject>();
  home["set"] = geofence::is_set();
  home["source"] = geofence::source_name(geofence::source());
  const float dist = geofence::distance_to_home_m();
  home["distance_m"] = (dist >= 0.0f) ? dist : -1.0f;

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handle_dev_get() {
  StaticJsonDocument<4096> doc;
  const RuntimeConfig &cfg = config::get();
  const unsigned long now_ms = millis();

  JsonObject time = doc["time"].to<JsonObject>();
  time["uptime_ms"] = now_ms;
  time["build"] = String(__DATE__) + " " + String(__TIME__);

  JsonObject system = doc["system"].to<JsonObject>();
  system["free_heap"] = ESP.getFreeHeap();

  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["mode"] = wifi_mode_name(WiFi.getMode());
  wifi["sta_connected"] = wifi_mgr::sta_connected();
  wifi["sta_connecting"] = wifi_mgr::sta_connecting();
  wifi["ap_enabled"] = wifi_mgr::ap_enabled();
  wifi["ap_stations"] = wifi_mgr::ap_station_count();
  wifi["wifi_off"] = wifi_mgr::wifi_off();
  wifi["ap_ssid"] = cfg.ap_ssid;
  wifi["mdns"] = cfg.mdns;
  wifi["sta_ip"] = WiFi.localIP().toString();
  wifi["ap_ip"] = WiFi.softAPIP().toString();
  wifi["rssi"] = WiFi.RSSI();

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
  gps["speed_kph"] = gps::last_speed_kph();
  gps["lat"] = gps::current_lat_deg();
  gps["lon"] = gps::current_lon_deg();
  gps["date"] = gps::current_date();
  gps["last_update_min"] = gps::last_update_min();
  gps["bytes_rx"] = gps::bytes_rx();
  gps["sentences_rx"] = gps::sentences_rx();
  gps["rmc_seen"] = gps::rmc_seen();
  gps["rmc_valid"] = gps::rmc_valid();
  gps["gga_seen"] = gps::gga_seen();
  gps["overflow"] = gps::overflow();
  const long age_last_byte = (gps::last_byte_ms() > 0 && now_ms >= gps::last_byte_ms())
                                 ? static_cast<long>(now_ms - gps::last_byte_ms())
                                 : -1;
  const long age_last_fix = (gps::last_fix_ms() > 0 && now_ms >= gps::last_fix_ms())
                                ? static_cast<long>(now_ms - gps::last_fix_ms())
                                : -1;
  const long age_last_gga = (gps::last_gga_ms() > 0 && now_ms >= gps::last_gga_ms())
                                ? static_cast<long>(now_ms - gps::last_gga_ms())
                                : -1;
  gps["age_last_byte_ms"] = age_last_byte;
  gps["age_last_fix_ms"] = age_last_fix;
  gps["age_last_gga_ms"] = age_last_gga;

  JsonObject geo = doc["geofence"].to<JsonObject>();
  geo["set"] = geofence::is_set();
  geo["source"] = geofence::source_name(geofence::source());
  geo["home_lat"] = geofence::home_lat();
  geo["home_lon"] = geofence::home_lon();
  const float dist_m = geofence::distance_to_home_m();
  geo["distance_m"] = (dist_m >= 0.0f) ? dist_m : -1.0f;
  const int geo_range = (geofence::is_set() && dist_m >= 0.0f)
                            ? static_cast<int>(geofence::geofence_range(dist_m))
                            : -1;
  geo["range"] = geo_range;

  JsonObject led = doc["led"].to<JsonObject>();
  led["mode"] = config::mode_name(cfg.mode);
  led["brightness"] = cfg.brightness;
  int range = -1;
  if (cfg.mode == MODE_SPEED && gps::has_fix()) {
    range = static_cast<int>(led_ui::speed_range(gps::last_speed_kph()));
  } else if (cfg.mode == MODE_GEOFENCE && geo_range > 0) {
    range = geo_range;
  }
  led["range"] = range;
  if (range >= 1 && range <= 10) {
    int eff_a = 0;
    int eff_b = 0;
    uint8_t eff_speed = 0;
    uint8_t eff_intensity = 0;
    led_ui::get_range_config(static_cast<uint8_t>(range), eff_a, eff_b, eff_speed, eff_intensity);
    JsonObject effA = led["effect_a"].to<JsonObject>();
    effA["id"] = eff_a;
    effA["name"] = led_ui::effect_name(static_cast<uint8_t>(eff_a));
    effA["speed"] = eff_speed;
    effA["intensity"] = eff_intensity;
    JsonObject effB = led["effect_b"].to<JsonObject>();
    effB["id"] = eff_b;
    effB["name"] = led_ui::effect_name(static_cast<uint8_t>(eff_b));
    effB["speed"] = eff_speed;
    effB["intensity"] = eff_intensity;
    const led_ui::Rgb base = led_ui::base_color_for_range(static_cast<uint8_t>(range));
    JsonObject baseRgb = led["base_rgb"].to<JsonObject>();
    baseRgb["r"] = base.r;
    baseRgb["g"] = base.g;
    baseRgb["b"] = base.b;
  }
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

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handle_mode_post() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"no body\"}");
    return;
  }
  StaticJsonDocument<256> doc;
  const DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"bad json\"}");
    return;
  }
  const char *mode_str = doc["mode"];
  uint8_t parsed_mode = config::get().mode;
  if (!config::parse_mode(mode_str, parsed_mode)) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"mode\"}");
    return;
  }
  if (parsed_mode != config::get().mode) {
    RuntimeConfig previous = config::get();
    config::get_mut().mode = parsed_mode;
    config::save();
    config::apply(previous);
  }
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handle_config_get() {
  StaticJsonDocument<4096> doc;
  const RuntimeConfig &cfg = config::get();
  doc["version"] = config::version();
  doc["mode"] = config::mode_name(cfg.mode);
  doc["fence_max_m"] = cfg.fence_max_m;
  doc["led"]["brightness"] = cfg.brightness;
  JsonObject gps_cfg = doc["gps"].to<JsonObject>();
  gps_cfg["min_fix_quality"] = cfg.gps_min_fix_quality;
  gps_cfg["min_sats"] = cfg.gps_min_sats;
  gps_cfg["max_hdop"] = cfg.gps_max_hdop;
  gps_cfg["max_gga_age_ms"] = cfg.gps_max_gga_age_ms;
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
  doc["wifi"]["mdns"] = cfg.mdns;

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handle_config_post() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"no body\"}");
    return;
  }
  StaticJsonDocument<4096> doc;
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

  if (doc.containsKey("mode")) {
    const char *mode_str = doc["mode"];
    uint8_t parsed_mode = next.mode;
    if (!config::parse_mode(mode_str, parsed_mode)) {
      server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"mode\"}");
      return;
    }
    next.mode = parsed_mode;
  }

  if (doc.containsKey("fence_max_m")) {
    const int fence_max = doc["fence_max_m"] | static_cast<int>(next.fence_max_m);
    if (fence_max < GEOFENCE_MAX_M_MIN || fence_max > GEOFENCE_MAX_M_MAX) {
      server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"fence_max\"}");
      return;
    }
    next.fence_max_m = static_cast<uint16_t>(fence_max);
  }

  if (doc.containsKey("gps")) {
    JsonObject gps_cfg = doc["gps"].as<JsonObject>();
    if (gps_cfg.isNull()) {
      server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"gps\"}");
      return;
    }
    const int min_fix_quality = gps_cfg["min_fix_quality"] | next.gps_min_fix_quality;
    const int min_sats = gps_cfg["min_sats"] | next.gps_min_sats;
    const float max_hdop = gps_cfg["max_hdop"] | next.gps_max_hdop;
    const int max_gga_age = gps_cfg["max_gga_age_ms"] | static_cast<int>(next.gps_max_gga_age_ms);
    if (min_fix_quality < GPS_MIN_FIX_QUALITY_MIN || min_fix_quality > GPS_MIN_FIX_QUALITY_MAX ||
        min_sats < GPS_MIN_SATS_MIN || min_sats > GPS_MIN_SATS_MAX ||
        !(max_hdop >= GPS_MAX_HDOP_MIN && max_hdop <= GPS_MAX_HDOP_MAX) ||
        max_gga_age < GPS_MAX_GGA_AGE_MS_MIN || max_gga_age > GPS_MAX_GGA_AGE_MS_MAX) {
      server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"gps\"}");
      return;
    }
    next.gps_min_fix_quality = static_cast<uint8_t>(min_fix_quality);
    next.gps_min_sats = static_cast<uint8_t>(min_sats);
    next.gps_max_hdop = max_hdop;
    next.gps_max_gga_age_ms = static_cast<uint16_t>(max_gga_age);
  }

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
  if (!config::validate_ranges(next.ranges)) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"ranges order\"}");
    return;
  }

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
    if (eff_a < 0 || eff_a > 11 || eff_b < 0 || eff_b > 11 ||
        eff_speed < 0 || eff_speed > 255 || eff_intensity < 0 || eff_intensity > 255) {
      server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"effect values\"}");
      return;
    }
    next.effects[i].effect_a = static_cast<uint8_t>(eff_a);
    next.effects[i].effect_b = static_cast<uint8_t>(eff_b);
    next.effects[i].speed = static_cast<uint8_t>(eff_speed);
    next.effects[i].intensity = static_cast<uint8_t>(eff_intensity);
  }
  if (!config::validate_effects(next.effects)) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"effect id\"}");
    return;
  }

  if (doc.containsKey("single")) {
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
    if (single_eff < 0 || single_eff >= EFFECT_COUNT ||
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
  if (ap_ssid.length() < 1 || ap_ssid.length() > 32) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"ssid\"}");
    return;
  }
  if (!ap_open && ap_pass.length() > 0 && ap_pass.length() < 8) {
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
  config::save();
  config::apply(previous);
  const bool wifi_restart = (config::get().ap_ssid != previous.ap_ssid || config::get().ap_pass != previous.ap_pass);
  if (wifi_restart) {
    wifi_mgr::schedule_ap_restart();
  }
  server.send(200, "application/json", wifi_restart ? "{\"status\":\"ok\",\"wifi_restart\":true}"
                                                     : "{\"status\":\"ok\",\"wifi_restart\":false}");
}

void handle_config_reset() {
  storage::prefs_cfg().clear();
  RuntimeConfig previous = config::get();
  config::set_defaults();
  config::save();
  config::apply(previous);
  if (config::get().ap_ssid != previous.ap_ssid || config::get().ap_pass != previous.ap_pass) {
    wifi_mgr::schedule_ap_restart();
  }
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handle_config_page() {
  server.send(200, "text/html", web_pages::html_config_page());
}

void handle_home_get() {
  StaticJsonDocument<512> doc;
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
  if (!gps::has_fix() || !gps::has_current_fix()) {
    server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"no_gps\"}");
    return;
  }
  geofence::set_home(gps::current_lat_deg(), gps::current_lon_deg(), 2);
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handle_home_clear() {
  geofence::clear_home();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handle_wifi_save() {
  if (!server.hasArg("ssid")) {
    server.send(400, "text/plain", "missing ssid");
    return;
  }
  const String ssid = server.arg("ssid");
  const String pass = server.arg("pass");
  wifi_mgr::save_creds(ssid, pass);
  wifi_mgr::start_sta_mode();
  server.send(200, "text/plain", "saved, connecting");
}
} // namespace

void begin() {
  server.on("/", HTTP_GET, handle_root);
  server.on("/api/summary", HTTP_GET, handle_summary);
  server.on("/api/status", HTTP_GET, handle_status_get);
  server.on("/api/dev", HTTP_GET, handle_dev_get);
  server.on("/api/mode", HTTP_POST, handle_mode_post);
  server.on("/api/config", HTTP_GET, handle_config_get);
  server.on("/api/config", HTTP_POST, handle_config_post);
  server.on("/api/config/reset", HTTP_POST, handle_config_reset);
  server.on("/config", HTTP_GET, handle_config_page);
  server.on("/dev", HTTP_GET, handle_dev_page);
  server.on("/api/home", HTTP_GET, handle_home_get);
  server.on("/api/home/set", HTTP_POST, handle_home_set);
  server.on("/api/home/clear", HTTP_POST, handle_home_clear);
  server.on("/wifi", HTTP_GET, handle_wifi_page);
  server.on("/api/wifi", HTTP_POST, handle_wifi_save);
  server.begin();
}

void handle_client() {
  server.handleClient();
}
} // namespace portal_http
