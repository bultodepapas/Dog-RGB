#include "sim/wokwi_control.h"

#if defined(DOG_RGB_WOKWI_SIM)

#include <Arduino.h>
#include <esp_system.h>
#include <string.h>

#include "config/runtime_config.h"
#include "geofence/home.h"
#include "gps/gps.h"
#include "power/day_mode.h"

namespace wokwi_control {
namespace {
constexpr size_t COMMAND_CAPACITY = 96;
char command_buffer[COMMAND_CAPACITY] = {};
size_t command_length = 0;
bool command_overflow = false;

void print_storage_state() {
  Serial.print(" slot=");
  Serial.print(config::storage_slot());
  Serial.print(" generation=");
  Serial.println(config::storage_generation());
}

bool save_config_transaction(const RuntimeConfig &previous) {
  if (config::save()) {
    config::apply(previous);
    return true;
  }
  const RuntimeConfig failed = config::get();
  config::get_mut() = previous;
  config::apply(failed);
  return false;
}

void handle_mode(const char *value) {
  uint8_t mode = config::get().mode;
  if (!config::parse_mode(value, mode)) {
    Serial.println("[SIM_CTRL] error command=mode reason=value");
    return;
  }
  const RuntimeConfig previous = config::get();
  config::get_mut().mode = mode;
  if (!save_config_transaction(previous)) {
    Serial.println("[SIM_CTRL] error command=mode reason=storage");
    return;
  }
  Serial.print("[SIM_CTRL] ok command=mode value=");
  Serial.print(config::mode_name(mode));
  print_storage_state();
}

void handle_day(const char *value) {
  bool enabled = false;
  if (strcmp(value, "on") == 0) {
    enabled = true;
  } else if (strcmp(value, "off") != 0) {
    Serial.println("[SIM_CTRL] error command=day reason=value");
    return;
  }
  const RuntimeConfig previous = config::get();
  config::get_mut().day_mode_enabled = enabled;
  if (!save_config_transaction(previous)) {
    Serial.println("[SIM_CTRL] error command=day reason=storage");
    return;
  }
  Serial.print("[SIM_CTRL] ok command=day value=");
  Serial.print(enabled ? "on" : "off");
  print_storage_state();
}

void handle_home(const char *value) {
  if (strcmp(value, "clear") == 0) {
    if (!geofence::clear_home()) {
      Serial.println("[SIM_CTRL] error command=home reason=storage");
      return;
    }
    Serial.println("[SIM_CTRL] ok command=home value=clear");
    return;
  }
  if (strcmp(value, "here") != 0) {
    Serial.println("[SIM_CTRL] error command=home reason=value");
    return;
  }
  if (!gps::trusted_fix() || !gps::has_current_fix()) {
    Serial.println("[SIM_CTRL] error command=home reason=no_trusted_fix");
    return;
  }
  if (!geofence::set_home(gps::current_lat_deg(), gps::current_lon_deg(), 2)) {
    Serial.println("[SIM_CTRL] error command=home reason=storage");
    return;
  }
  Serial.print("[SIM_CTRL] ok command=home value=here lat=");
  Serial.print(geofence::home_lat(), 6);
  Serial.print(" lon=");
  Serial.println(geofence::home_lon(), 6);
}

void print_status() {
  Serial.print("[SIM_STATE] mode=");
  Serial.print(config::mode_name(config::get().mode));
  Serial.print(" day_enabled=");
  Serial.print(day_mode::enabled() ? "1" : "0");
  Serial.print(" day_state=");
  Serial.print(day_mode::state_name());
  Serial.print(" home=");
  Serial.print(geofence::is_set() ? "1" : "0");
  Serial.print(" fix=");
  Serial.print(gps::has_fix() ? "1" : "0");
  Serial.print(" trusted=");
  Serial.print(gps::trusted_fix() ? "1" : "0");
  Serial.print(" speed_kph=");
  Serial.print(gps::last_speed_kph(), 2);
  Serial.print(" slot=");
  Serial.print(config::storage_slot());
  Serial.print(" generation=");
  Serial.println(config::storage_generation());
}

void process_command(char *line) {
  if (strncmp(line, "sim ", 4) != 0) {
    Serial.println("[SIM_CTRL] error command=unknown reason=prefix");
    return;
  }
  char *context = nullptr;
  char *command = strtok_r(line + 4, " ", &context);
  char *value = strtok_r(nullptr, " ", &context);
  char *extra = strtok_r(nullptr, " ", &context);
  if (command == nullptr || extra != nullptr) {
    Serial.println("[SIM_CTRL] error command=unknown reason=syntax");
    return;
  }
  if (strcmp(command, "mode") == 0 && value != nullptr) {
    handle_mode(value);
  } else if (strcmp(command, "day") == 0 && value != nullptr) {
    handle_day(value);
  } else if (strcmp(command, "home") == 0 && value != nullptr) {
    handle_home(value);
  } else if (strcmp(command, "status") == 0 && value == nullptr) {
    print_status();
  } else if (strcmp(command, "reboot") == 0 && value == nullptr) {
    Serial.println("[SIM_CTRL] ok command=reboot");
    Serial.flush();
    delay(20);
    ESP.restart();
  } else if (strcmp(command, "help") == 0 && value == nullptr) {
    Serial.println("[SIM_CTRL] commands=mode,day,home,status,reboot");
  } else {
    Serial.println("[SIM_CTRL] error command=unknown reason=syntax");
  }
}
}  // namespace

void begin() {
  command_length = 0;
  command_overflow = false;
  Serial.println("[SIM_CTRL] ready prefix=sim commands=mode,day,home,status,reboot");
}

void tick() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      if (command_overflow) {
        Serial.println("[SIM_CTRL] error command=unknown reason=line_too_long");
      } else if (command_length > 0) {
        command_buffer[command_length] = '\0';
        process_command(command_buffer);
      }
      command_length = 0;
      command_overflow = false;
      continue;
    }
    if (command_length + 1 < COMMAND_CAPACITY && !command_overflow) {
      command_buffer[command_length++] = c;
    } else {
      command_overflow = true;
    }
  }
}
}  // namespace wokwi_control

#else

namespace wokwi_control {
void begin() {}
void tick() {}
}

#endif
