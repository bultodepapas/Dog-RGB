#pragma once

#include <stdint.h>

namespace day_mode {
bool enabled();
bool time_available();
uint16_t local_min();
bool active_now();
const char *state_name();
}
