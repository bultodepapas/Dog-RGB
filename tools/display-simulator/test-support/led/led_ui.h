#pragma once
#include "led/led_state.h"
namespace led_ui {
inline led::LedState state;
inline bool transport = true;
inline const led::LedState &current_state() { return state; }
inline bool transport_enabled() { return transport; }
} // namespace led_ui
