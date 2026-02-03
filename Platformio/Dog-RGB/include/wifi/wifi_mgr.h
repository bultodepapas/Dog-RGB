#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace wifi_mgr {
void begin();
void tick(unsigned long now_ms);

void start_sta_mode();
void start_ap_mode();
void save_creds(const String &ssid, const String &pass);

const String &ssid();
const String &pass();

bool sta_connected();
bool sta_connecting();
bool ap_enabled();
bool wifi_off();
uint8_t ap_station_count();
bool is_ap_mode();

void schedule_ap_restart();
void apply_mdns(const String &previous, const String &current);
}
