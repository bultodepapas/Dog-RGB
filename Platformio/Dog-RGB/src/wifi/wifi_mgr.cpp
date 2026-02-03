#include "wifi/wifi_mgr.h"

#include <ESPmDNS.h>
#include <WiFi.h>

#include "config/runtime_config.h"
#include "config.h"
#include "gps/gps.h"
#include "storage/nvs_store.h"

namespace wifi_mgr {
namespace {
String wifi_ssid;
String wifi_pass;
bool wifi_sta_connected = false;
bool wifi_sta_connecting = false;
unsigned long wifi_sta_start_ms = 0;
unsigned long last_wifi_check_ms = 0;
bool ap_enabled_state = true;
bool wifi_off_state = false;
unsigned long last_ap_client_ms = 0;
unsigned long last_ap_poll_ms = 0;
uint8_t ap_station_count_state = 0;
unsigned long stationary_ms = 0;
unsigned long last_stationary_ms = 0;

bool pending_ap_restart = false;
unsigned long pending_ap_at_ms = 0;
const unsigned long AP_RESTART_DELAY_MS = 500;

void load_wifi_creds() {
  Preferences &prefs = storage::prefs();
  wifi_ssid = prefs.getString("wifi_ssid", "");
  wifi_pass = prefs.getString("wifi_pass", "");
}

void start_ap_mode_internal() {
  const RuntimeConfig &cfg = config::get();
  WiFi.mode(WIFI_AP);
  WiFi.softAP(cfg.ap_ssid.c_str(), cfg.ap_pass.c_str());
  ap_enabled_state = true;
  wifi_off_state = false;
  ap_station_count_state = 0;
  last_ap_client_ms = millis();
  wifi_sta_connected = false;
  wifi_sta_connecting = false;
}

void start_sta_mode_internal() {
  const RuntimeConfig &cfg = config::get();
  if (ap_enabled_state) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(cfg.ap_ssid.c_str(), cfg.ap_pass.c_str());
    ap_station_count_state = 0;
    last_ap_client_ms = millis();
  } else {
    WiFi.mode(WIFI_STA);
  }
  wifi_off_state = false;
  WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
  wifi_sta_connected = false;
  wifi_sta_connecting = true;
  wifi_sta_start_ms = millis();
}

void enable_ap() {
  if (ap_enabled_state) {
    return;
  }
  if (wifi_ssid.length() > 0) {
    WiFi.mode(WIFI_AP_STA);
  } else {
    WiFi.mode(WIFI_AP);
  }
  const RuntimeConfig &cfg = config::get();
  WiFi.softAP(cfg.ap_ssid.c_str(), cfg.ap_pass.c_str());
  ap_enabled_state = true;
  wifi_off_state = false;
  ap_station_count_state = 0;
  last_ap_client_ms = millis();
}

void disable_ap() {
  if (!ap_enabled_state) {
    return;
  }
  WiFi.softAPdisconnect(true);
  ap_enabled_state = false;
  ap_station_count_state = 0;
  last_ap_client_ms = 0;
  WiFi.mode(WIFI_STA);
}

void set_wifi_off(bool off) {
  if (off) {
    if (wifi_off_state) {
      return;
    }
    WiFi.mode(WIFI_OFF);
    wifi_off_state = true;
    ap_enabled_state = false;
    ap_station_count_state = 0;
    wifi_sta_connected = false;
    wifi_sta_connecting = false;
    last_ap_client_ms = 0;
    last_ap_poll_ms = 0;
    return;
  }
  if (!wifi_off_state) {
    return;
  }
  wifi_off_state = false;
  ap_enabled_state = true;
  if (wifi_ssid.length() > 0) {
    start_sta_mode_internal();
  } else {
    start_ap_mode_internal();
  }
}

void update_ap_policy(unsigned long now_ms) {
  if (last_stationary_ms == 0) {
    last_stationary_ms = now_ms;
  }
  const unsigned long dt_ms = now_ms - last_stationary_ms;
  last_stationary_ms = now_ms;

  if (gps::has_fix()) {
    if (gps::last_speed_kph() <= AP_STATIONARY_ON_KPH) {
      stationary_ms = (stationary_ms + dt_ms > AP_STATIONARY_MS) ? AP_STATIONARY_MS : (stationary_ms + dt_ms);
    } else if (gps::last_speed_kph() >= AP_STATIONARY_OFF_KPH) {
      stationary_ms = 0;
    }
  } else {
    stationary_ms = 0;
  }

  const bool ap_force_on = !gps::has_fix();
  const bool ap_request_on = (stationary_ms >= AP_STATIONARY_MS);

  if (wifi_off_state) {
    if (ap_force_on || ap_request_on) {
      set_wifi_off(false);
    } else {
      return;
    }
  }

  if (ap_enabled_state && (now_ms - last_ap_poll_ms) >= AP_CLIENT_POLL_MS) {
    last_ap_poll_ms = now_ms;
    const int stations = WiFi.softAPgetStationNum();
    ap_station_count_state = (stations > 0) ? static_cast<uint8_t>(stations) : 0;
    if (stations > 0) {
      last_ap_client_ms = now_ms;
    }
  } else if (!ap_enabled_state) {
    ap_station_count_state = 0;
  }

  if (ap_force_on) {
    if (!ap_enabled_state) {
      enable_ap();
    }
    last_ap_client_ms = now_ms;
    return;
  }

  if (ap_request_on && !ap_enabled_state) {
    enable_ap();
  }

  if (ap_enabled_state) {
    if (last_ap_client_ms == 0) {
      last_ap_client_ms = now_ms;
    }
    if ((now_ms - last_ap_client_ms) >= AP_IDLE_TIMEOUT_MS) {
      disable_ap();
      stationary_ms = 0;
      if (!wifi_sta_connected || wifi_ssid.length() == 0) {
        set_wifi_off(true);
      }
    }
  }
}

} // namespace

void begin() {
  load_wifi_creds();
  ap_enabled_state = true;
  wifi_off_state = false;
  if (wifi_ssid.length() > 0) {
    start_sta_mode_internal();
  } else {
    start_ap_mode_internal();
  }
}

void tick(unsigned long now_ms) {
  if (!wifi_off_state && (now_ms - last_wifi_check_ms >= WIFI_RETRY_INTERVAL_MS)) {
    last_wifi_check_ms = now_ms;
    if (wifi_sta_connected && WiFi.status() != WL_CONNECTED) {
      wifi_sta_connected = false;
      if (wifi_ssid.length() > 0) {
        start_sta_mode_internal();
      } else {
        start_ap_mode_internal();
      }
    } else if (wifi_sta_connecting) {
      if (WiFi.status() == WL_CONNECTED) {
        wifi_sta_connected = true;
        wifi_sta_connecting = false;
        MDNS.begin(config::get().mdns.c_str());
      } else if ((now_ms - wifi_sta_start_ms) >= STA_CONNECT_TIMEOUT_MS) {
        wifi_sta_connecting = false;
        start_ap_mode_internal();
      }
    } else if (!wifi_sta_connected && wifi_ssid.length() > 0) {
      start_sta_mode_internal();
    }
  }

  if (pending_ap_restart && (now_ms - pending_ap_at_ms) >= AP_RESTART_DELAY_MS) {
    pending_ap_restart = false;
    const RuntimeConfig &cfg = config::get();
    if (ap_enabled_state) {
      if (wifi_sta_connected || wifi_ssid.length() > 0) {
        WiFi.mode(WIFI_AP_STA);
      } else {
        WiFi.mode(WIFI_AP);
      }
      WiFi.softAP(cfg.ap_ssid.c_str(), cfg.ap_pass.c_str());
      last_ap_client_ms = now_ms;
    }
  }

  update_ap_policy(now_ms);
}

void start_sta_mode() {
  start_sta_mode_internal();
}

void start_ap_mode() {
  start_ap_mode_internal();
}

void save_creds(const String &ssid, const String &pass) {
  Preferences &prefs = storage::prefs();
  prefs.putString("wifi_ssid", ssid);
  prefs.putString("wifi_pass", pass);
  wifi_ssid = ssid;
  wifi_pass = pass;
}

const String &ssid() {
  return wifi_ssid;
}

const String &pass() {
  return wifi_pass;
}

bool sta_connected() {
  return wifi_sta_connected;
}

bool sta_connecting() {
  return wifi_sta_connecting;
}

bool ap_enabled() {
  return ap_enabled_state;
}

bool wifi_off() {
  return wifi_off_state;
}

uint8_t ap_station_count() {
  return ap_station_count_state;
}

bool is_ap_mode() {
  return WiFi.getMode() == WIFI_AP;
}

void schedule_ap_restart() {
  pending_ap_restart = true;
  pending_ap_at_ms = millis();
}

void apply_mdns(const String &previous, const String &current) {
  if (previous == current) {
    return;
  }
  if (wifi_sta_connected) {
    MDNS.end();
    MDNS.begin(current.c_str());
  }
}
} // namespace wifi_mgr
