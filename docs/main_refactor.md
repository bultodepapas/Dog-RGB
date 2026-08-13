# `main.cpp` Modularization — Result

**Status:** Completed design note. This is no longer an implementation plan.

The refactor moved domain logic out of `Platformio/Dog-RGB/src/main.cpp`. The entry point now owns boot/loop orchestration, heartbeat, serial diagnostics, and phase timing; modules own their internal state.

## Resulting boundaries

```text
include/                     src/
├── ble/summary_ble.h        ├── ble/summary_ble.cpp
├── config/runtime_config.h  ├── config/runtime_config.cpp
├── geofence/home.h          ├── geofence/home.cpp
├── gps/gps.h                ├── gps/gps.cpp
├── led/led_ui.h             ├── led/led_ui.cpp
├── power/day_mode.h         ├── power/day_mode.cpp
├── sim/wokwi_control.h      ├── sim/wokwi_control.cpp
├── storage/nvs_store.h      ├── storage/nvs_store.cpp
├── util/*.h                 ├── util/geo.cpp
├── web/{pages,portal_*}.h   ├── web/{pages,portal_*}.cpp
├── wifi/wifi_mgr.h          └── wifi/wifi_mgr.cpp
├── config.h
└── pins.h
```

## Decisions retained

- Module-local namespace state instead of one global `AppState`.
- Narrow getters/actions instead of direct cross-module variable access.
- Wi-Fi callbacks enqueue events; `wifi_mgr::tick()` owns state transitions.
- `main.cpp` calls bounded domain ticks in a stable order and records phase maxima.
- Hardware/default constants remain centralized in `config.h` and `pins.h`.
- Simulation-only behavior is compile-time isolated.

## Current boot and loop

BLE, when explicitly enabled, initializes before Wi-Fi for coexistence reasons. The normal boot order is storage → config → GNSS → geofence → LEDs/welcome → optional BLE → Wi-Fi → portal → Wokwi control.

The loop is GNSS → simulation control → geofence → persistence/route → heartbeat/logging → optional BLE + Wi-Fi → LEDs → HTTP/DNS → serial drain.

The full rationale, concurrency model, persistence design, and current module table live in [Architecture](architecture.md). Future file splits should be driven by an actual change/testing seam, not module count alone.
