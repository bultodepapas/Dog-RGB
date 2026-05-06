# Dog-RGB ESP32-S3 Access Point Comprehensive Review

Prepared: 2026-05-05 America/Bogota  
Scope: ESP32-S3 Access Point behavior, AP+STA policy, portal reachability, connection reliability, and hardening plan.

## Executive Summary

The initial AP implementation reviewed here was functional but fragile for real users. It started an ESP32 SoftAP, served HTTP on port 80, and exposed status/config pages, but relied on the default Arduino `WiFi.softAP(ssid, pass)` behavior, did not check return values, did not use Wi-Fi events for AP station lifecycle, did not run DNS/captive portal support, and intentionally turned the AP off based on GPS and station-count policy. These choices made the AP easy to miss, easy to interpret as broken, and potentially unstable during STA retries.

The first hardening pass has now been implemented. The firmware pins the PlatformIO platform, centralizes checked AP startup, adds Wi-Fi diagnostics and event counters, adds captive portal DNS/redirect behavior, validates AP identity config more strictly, holds the AP on during setup/portal activity, and backs off STA retries instead of disturbing the AP every fixed interval. Remaining work is mostly hardware validation, phone matrix testing, BLE coexistence measurement, and deeper disconnect-reason classification.

The most likely causes of "devices do not connect easily" are:

1. AP availability is policy-driven, not always-on. With GPS fix and no connected client, the firmware can turn the AP off after `AP_IDLE_TIMEOUT_MS` and then place Wi-Fi in `WIFI_OFF`.
2. AP+STA has a single-radio channel limitation. When the ESP32-S3 is scanning or joining a router as STA, the SoftAP channel can be moved or disrupted. The firmware retries STA every 10 seconds when credentials exist.
3. The portal is not a captive portal. Phones may connect to the AP but not automatically open the page, may prefer cellular data, or may leave a no-Internet Wi-Fi network.
4. The AP configuration is implicit. Channel, max station count, IP/subnet, country/channel policy, bandwidth, power-save policy, and AP auth behavior are not explicitly controlled or logged.
5. Hardware can be a real factor. XIAO ESP32S3 requires a correctly installed external antenna, and Wi-Fi TX can expose power rail weakness.
6. BLE advertising is always started alongside Wi-Fi. ESP32-S3 supports Wi-Fi/BLE coexistence, but SoftAP connected scenarios are documented as less stable than plain STA in some coexistence combinations.

The recommended direction is to treat AP as a first-class subsystem: explicit AP configuration, event-driven state, bounded STA retries, captive portal DNS, richer diagnostics, and a test matrix that covers iOS, Android, Windows, macOS, AP-only, AP+STA, bad STA credentials, GPS fix/no-fix, BLE on/off, and weak battery.

## Files Audited

- `Platformio/Dog-RGB/platformio.ini`
- `Platformio/Dog-RGB/include/config.h`
- `Platformio/Dog-RGB/include/wifi/wifi_mgr.h`
- `Platformio/Dog-RGB/src/wifi/wifi_mgr.cpp`
- `Platformio/Dog-RGB/src/web/portal_http.cpp`
- `Platformio/Dog-RGB/src/web/pages.cpp`
- `Platformio/Dog-RGB/src/config/runtime_config.cpp`
- `Platformio/Dog-RGB/src/ble/summary_ble.cpp`
- `docs/wifi_portal_spec.md`
- `docs/wifi_portal_state_diagram.md`
- `docs/portal_config.md`
- `docs/ap_analysis.md`

## Current Implementation Snapshot

### Platform and Dependencies

`platformio.ini` uses:

- `platform = espressif32@6.7.0`
- `board = seeed_xiao_esp32s3`
- `framework = arduino`
- `CORE_DEBUG_LEVEL=0`

The PlatformIO platform is now pinned after the hardening pass. Local build verification used PlatformIO Core `6.1.19`, Espressif32 platform `6.7.0`, and Arduino-ESP32 `2.0.16`.

### Boot Flow

In `main.cpp`, setup order is:

1. `storage::begin()`
2. `config::load()`
3. GPS/geofence/LED setup
4. `wifi_mgr::begin()`
5. `portal_http::begin()`
6. `summary_ble::begin()`

Wi-Fi is started before the HTTP server and before BLE advertising. This is generally sensible, but BLE starts unconditionally after AP/STA and can compete for RF time.

### AP and STA State

`wifi_mgr.cpp` owns these key state variables:

- `wifi_ssid`, `wifi_pass`
- `wifi_sta_connected`
- `wifi_sta_connecting`
- `ap_enabled_state`
- `wifi_off_state`
- `last_ap_client_ms`
- `ap_station_count_state`
- `stationary_ms`
- `pending_ap_restart`

The AP is started by:

```cpp
WiFi.mode(WIFI_AP);
WiFi.softAP(cfg.ap_ssid.c_str(), cfg.ap_pass.c_str());
```

or, in AP+STA:

```cpp
WiFi.mode(WIFI_AP_STA);
WiFi.softAP(cfg.ap_ssid.c_str(), cfg.ap_pass.c_str());
WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
```

The code does not check the boolean result from `WiFi.softAP()`. It also does not explicitly set AP IP/subnet, AP channel, hidden flag, max clients, DHCP lease start, DNS, bandwidth, sleep, or country/channel policy.

### AP Power Policy

The AP policy is implemented in `update_ap_policy(now_ms)`:

- If there is no GPS fix, AP is forced on.
- If GPS fix exists and speed is <= `AP_STATIONARY_ON_KPH` for `AP_STATIONARY_MS`, AP is requested on.
- If AP has no stations for `AP_IDLE_TIMEOUT_MS`, AP is disabled.
- If AP is disabled and STA is not connected, Wi-Fi is turned off.

Defaults in `config.h`:

- `STA_CONNECT_TIMEOUT_MS = 10000`
- `WIFI_RETRY_INTERVAL_MS = 10000`
- `AP_IDLE_TIMEOUT_MS = 600000`
- `AP_STATIONARY_MS = 120000`
- `AP_CLIENT_POLL_MS = 1000`
- `AP_STATIONARY_ON_KPH = 2.0`
- `AP_STATIONARY_OFF_KPH = 2.5`

The documentation still contains an older 5 minute AP idle value in places, while the code now uses 10 minutes. That mismatch can confuse testing.

### Portal HTTP

`portal_http.cpp` uses `WebServer server(80)` and registers:

- `/`
- `/api/summary`
- `/api/status`
- `/api/dev`
- `/api/config`
- `/config`
- `/wifi`
- `/api/wifi`

The hardening pass added `DNSServer`, wildcard DNS while the AP is active, common captive-check routes, and `server.onNotFound()` redirect handling. Users can still manually visit `http://192.168.4.1` when a phone OS does not show a captive portal prompt.

### Runtime AP Config

AP SSID, password, and mDNS are persisted in `runtime_config.cpp`.

Validation during `POST /api/config`:

- AP SSID: length 1..32
- AP password: if provided and not open, length must be >= 8
- mDNS: length 1..32, letters, digits, hyphen

Original gaps addressed by the first hardening pass:

- Stored AP SSID/password/mDNS are validated in `read_common_config()`.
- Open AP startup now passes `nullptr` instead of an empty passphrase.
- AP password validation now enforces empty/open or 8..63 characters.
- mDNS validation rejects leading/trailing hyphens.
- AP SSID validation rejects control characters and leading/trailing spaces.

### BLE Coexistence

`summary_ble.cpp` starts BLE advertising unconditionally:

```cpp
BLEDevice::init(BLE_DEVICE_NAME);
...
adv->setScanResponse(true);
adv->start();
```

This is useful for summary reads, but it should be tested against AP stability because ESP32-S3 Wi-Fi and BLE share RF resources.

## External Research Findings

### Source Index

The review used the following sources. Official documentation is prioritized.

| ID | Source | Type | Key relevance |
|---|---|---|---|
| S01 | [Arduino ESP32 Wi-Fi API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html) | Official | `WiFi.softAP()` signature, defaults, events, AP APIs |
| S02 | [Arduino ESP32 documentation root](https://docs.espressif.com/projects/arduino-esp32/en/latest/) | Official | Current docs version and ESP-IDF base |
| S03 | [ESP-IDF ESP32-S3 Wi-Fi Driver guide v4.4.6](https://docs.espressif.com/projects/esp-idf/en/v4.4.6/esp32s3/api-guides/wifi.html) | Official | Robust Wi-Fi application model, AP+STA channel priority, events, sleep |
| S04 | [ESP-IDF ESP32-S3 Wi-Fi API reference v4.4.6](https://docs.espressif.com/projects/esp-idf/en/v4.4.6/esp32s3/api-reference/network/esp_wifi.html) | Official | Inactive-time behavior, `esp_wifi_*` lower-level APIs |
| S05 | [ESP-IDF ESP32 latest Wi-Fi API reference](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_wifi.html) | Official | Channel/country API constraints and current behavior |
| S06 | [ESP32-S3 Series Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf) | Official | 2.4 GHz Wi-Fi, AP+STA support, scan/channel note |
| S07 | [ESP32-S3 Hardware Design Guidelines - Schematic Checklist](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/schematic-checklist.html) | Official | 500 mA supply guidance, RF/power rail recommendations |
| S08 | [ESP32-S3 Hardware Design Guidelines](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/index.html) | Official | Hardware integration baseline |
| S09 | [Seeed XIAO ESP32S3 Wi-Fi Usage](https://wiki.seeedstudio.com/xiao_esp32s3_wifi_usage/) | Official vendor | XIAO antenna installation and warning |
| S10 | [Seeed XIAO ESP32S3 Getting Started](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/) | Official vendor | Board setup and antenna context |
| S11 | [ESP-IDF ESP32-S3 RF Coexistence](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/coexist.html) | Official | Wi-Fi/BLE single RF path and SoftAP coexistence caveats |
| S12 | [Espressif ESP-FAQ Wi-Fi](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/wifi.html) | Official | SoftAP default subnet, web provisioning support, DHCP/DNS options |
| S13 | [PlatformIO Espressif32 docs](https://docs.platformio.org/en/latest/platforms/espressif32.html) | Official | Platform pinning/updating implications |
| S14 | [Arduino-ESP32 `WiFiAP.h`](https://github.com/espressif/arduino-esp32/blob/master/libraries/WiFi/src/WiFiAP.h) | Official source | AP defaults and newer AP APIs |
| S15 | [Arduino-ESP32 CaptivePortal example](https://github.com/espressif/arduino-esp32/blob/master/libraries/DNSServer/examples/CaptivePortal/CaptivePortal.ino) | Official source | DNSServer captive portal pattern |
| S16 | [ESP-IDF issue #6108](https://github.com/espressif/esp-idf/issues/6108) | Official tracker | Real SoftAP symptom: joins but DHCP/IP fails |
| S17 | [Arduino-ESP32 issue #1832](https://github.com/espressif/arduino-esp32/issues/1832) | Official tracker | SoftAP not visible/connectable reports |
| S18 | [Arduino-ESP32 issue #11326](https://github.com/espressif/arduino-esp32/issues/11326) | Official tracker | ESP32-S3 SoftAP unresponsive with one client type |
| S19 | [ESPBoards captive portal troubleshooting](https://www.espboards.dev/troubleshooting/issues/wifi/esp32-captive-portal-issues/) | Technical reference | Phone OS portal behavior, DNS interception, manual fallback |
| S20 | [ESPBoards Wi-Fi connection failure troubleshooting](https://www.espboards.dev/troubleshooting/issues/wifi/wifi-connection-failure/) | Technical reference | Credentials, signal, security, power, retry practices |
| S21 | [ESP-Techpedia SoftAP example](https://docs.espressif.com/projects/esp-techpedia/en/latest/esp-friends/get-started/case-study/wifi-examples/softap-example.html) | Espressif technical docs | SoftAP event monitoring pattern |
| S22 | [ESP-AT Web Server Captive Portal example](https://docs.espressif.com/projects/esp-at/en/latest/esp32/AT_Command_Examples/Web_server_AT_Examples.html) | Official | Web provisioning and captive portal UX |
| S23 | [ESP-NETIF reference](https://docs.espressif.com/projects/esp-idf/en/v4.4.4/esp32/api-reference/network/esp_netif.html) | Official | AP DHCP server and network interface model |
| S24 | [Random Nerd Tutorials ESP32 AP Web Server](https://randomnerdtutorials.com/esp32-access-point-ap-web-server/) | Recognized tutorial | Common Arduino AP web-server baseline |

### Research Conclusions

1. Arduino `WiFi.softAP()` exposes important parameters the firmware does not use. The official signature includes SSID, passphrase, channel, hidden flag, max connection count, and FTM responder. Current code only passes SSID and password, inheriting channel 1 and max 4 behavior without recording it.
2. Arduino and ESP-IDF both support Wi-Fi events. ESP-IDF documentation explicitly treats robust event and error recovery as foundational for reliable Wi-Fi applications. Current code mostly polls `WiFi.status()` and `softAPgetStationNum()`.
3. ESP32-S3 AP+STA channel behavior is constrained by one radio. The external STA AP channel has priority over the ESP SoftAP channel. The datasheet also notes SoftAP channel changes during station scans.
4. ESP-IDF documents AP station inactivity. SoftAP can deauth a station if it receives no data during inactive time, default 300 seconds. The firmware adds its own AP-off policy on top of this.
5. ESP32-S3 Wi-Fi and BLE coexist through time division on one RF module. The coexistence table marks some SoftAP connected combinations as supported but unstable, so BLE advertising should be measured during AP sessions.
6. Phone OS captive portal behavior is inconsistent. Without DNS interception and HTTP fallback handling, a phone may connect but not present the portal, may keep using cellular data, or may mark the AP as "no Internet".
7. XIAO ESP32S3 hardware depends on the external antenna. Seeed explicitly warns Wi-Fi may not connect if the antenna is not installed.
8. Power matters. Espressif recommends at least 500 mA supply capacity and local capacitance because RF TX can cause sudden current draw and rail collapse.
9. Unpinned PlatformIO platform versions make Wi-Fi behavior less reproducible. For AP reliability work, exact versions matter.

## Findings

### Critical

#### C1. AP+STA retry loop can disturb the AP every 10 seconds (addressed in first hardening pass)

When STA credentials exist and STA is not connected, `tick()` calls `start_sta_mode_internal()` every `WIFI_RETRY_INTERVAL_MS` once it is not in `wifi_sta_connecting`. That function enters `WIFI_AP_STA`, restarts SoftAP, and calls `WiFi.begin()`.

Risk:

- Wrong STA credentials or an unreachable router create repeated scan/connect attempts.
- ESP32-S3 scan/connect activity can affect SoftAP channel and responsiveness.
- A user trying to connect to the AP during recovery may see association failures, DHCP delays, disappearing SSID, or stalled portal loads.

Original evidence:

- Code: `wifi_mgr.cpp` lines 49-64, 199-220.
- Research: S03, S06.

#### C2. AP availability is not aligned with setup UX (partially addressed in first hardening pass)

The AP can be disabled after `AP_IDLE_TIMEOUT_MS` if no client is counted. Then, if STA is absent or disconnected, Wi-Fi is turned off. This is good for battery but bad for initial setup and support.

Risk:

- User scans for the network and cannot find it.
- Phone briefly disconnects or sleeps, station count drops to zero, then the AP disappears later.
- The AP is only guaranteed when there is no GPS fix, which is not an intuitive provisioning rule.

Original evidence:

- Code: `wifi_mgr.cpp` lines 121-184.
- Config: `config.h` lines 106-112.

#### C3. No event-driven AP station lifecycle (partially addressed in first hardening pass)

The firmware polls `WiFi.softAPgetStationNum()` every second. It does not subscribe to `ARDUINO_EVENT_WIFI_AP_STACONNECTED`, `ARDUINO_EVENT_WIFI_AP_STADISCONNECTED`, `ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED`, `ARDUINO_EVENT_WIFI_STA_DISCONNECTED`, or `ARDUINO_EVENT_WIFI_STA_GOT_IP`.

Risk:

- No disconnect reason codes.
- No precise association/DHCP timing.
- No way to distinguish "station associated but DHCP failed" from "station never associated".
- AP idle policy is based on coarse polling rather than event timestamps.

Original evidence:

- Code: no `WiFi.onEvent()` use.
- Research: S01, S03, S21.

### High

#### H1. `WiFi.softAP()` result is ignored (addressed in first hardening pass)

Every AP start/restart ignores the return value from `WiFi.softAP()`.

Risk:

- Failed AP start is invisible except indirectly through client symptoms.
- Config errors, memory pressure, or driver failures are not surfaced in `/api/dev` or serial logs.

Original evidence:

- Code: `wifi_mgr.cpp` lines 37-40, 49-60, 66-80, 223-233.
- Research: S01, S03.

#### H2. AP config is too implicit (partially addressed in first hardening pass)

The firmware does not explicitly set:

- AP channel
- max clients
- hidden flag
- IP/subnet/DHCP range
- DNS/captive portal behavior
- AP hostname
- bandwidth
- country/channel policy

Risk:

- Behavior depends on framework defaults.
- Debugging is harder because the firmware cannot report the intended AP contract.
- If STA is on a conflicting channel or the default subnet overlaps an external router, behavior can be confusing.

Original evidence:

- Code: original `wifi_mgr.cpp` only called `WiFi.softAP(ssid, pass)`.
- Research: S01, S03, S05, S12, S14.

#### H3. No captive portal DNS (addressed in first hardening pass)

The portal requires manual navigation to `http://192.168.4.1`. There is no DNS catch-all and no not-found redirect.

Risk:

- Users interpret "connected but no page opened" as "cannot connect".
- Android/iOS/macOS/Windows behave differently around no-Internet APs.
- mDNS is not reliable in AP mode and is only started after STA connects.

Original evidence:

- Code: original portal code had no `DNSServer`.
- Research: S15, S19, S22.

#### H4. Runtime config load does not validate Wi-Fi fields (addressed in first hardening pass)

`read_common_config()` loads persisted AP SSID, AP pass, and mDNS without validating length or allowed characters.

Risk:

- A bad saved config can survive reboot.
- Invalid AP settings may cause AP startup failure or confusing behavior.

Evidence:

- Code: `runtime_config.cpp` lines 64-82.

#### H5. Open AP handling should be explicit

Arduino documentation says open APs use `NULL` passphrase. The code passes `cfg.ap_pass.c_str()` even when the string is empty.

Risk:

- This may work in current Arduino-ESP32, but it is less explicit and less portable across framework versions than passing `nullptr` when AP is intentionally open.

Evidence:

- Code: `wifi_mgr.cpp` lines 37-40.
- Research: S01, S14.

### Medium

#### M1. AP password upper bound is not enforced

The code rejects passwords shorter than 8 only when a new password is provided. It does not reject passwords longer than 63 characters.

Risk:

- Invalid WPA passphrase can be saved.

Evidence:

- Code: `portal_http.cpp` lines 582-603.

#### M2. No bad-credential backoff or classification

STA failure is treated generically. The firmware does not record reason codes such as no AP found, auth failure, handshake timeout, or beacon timeout.

Risk:

- Bad credentials can keep the device in a noisy AP+STA retry state.
- UI cannot tell the user what to fix.

Evidence:

- Code: `wifi_mgr.cpp` lines 199-220.
- Research: S03, S20.

#### M3. BLE advertising is always on during AP sessions

BLE summary service starts unconditionally.

Risk:

- Coexistence may reduce AP responsiveness for some client combinations.
- This is probably not the primary bug, but it should be measured.

Evidence:

- Code: `summary_ble.cpp` lines 17-27.
- Research: S11.

#### M4. AP idle policy uses station count, not active HTTP/session state

`last_ap_client_ms` updates only when station count is positive. There is no portal session lease or "configuration in progress" guard.

Risk:

- AP can be scheduled for shutdown based on lower-layer station behavior, not user workflow.
- If the phone roams away and returns, the AP may be gone.

Evidence:

- Code: `wifi_mgr.cpp` lines 149-180.

#### M5. Documentation drift

Docs still describe 5 minute AP idle in some places while code uses 10 minutes.

Risk:

- Testers may report false positives or false negatives.

Evidence:

- Code: `config.h` line 108.
- Docs: `docs/manual_de_uso.md`, `docs/wifi_portal_spec.md`, older `docs/ap_analysis.md`.

### Low

#### L1. AP SSID default is very generic

Default SSID is `dog`.

Risk:

- SSID collision is possible.
- Users may not know which collar is theirs.

Recommendation:

- Default to `Dog-RGB-XXXX`, where `XXXX` comes from AP MAC suffix.

#### L2. `/api/dev` lacks AP failure counters

The dev endpoint includes mode, AP stations, IPs, and RSSI, but not AP start success, channel, MAC, disconnect reasons, DHCP assignment events, captive DNS state, STA retry count, or last AP event.

Risk:

- Field diagnosis remains guesswork.

## Symptom Mapping

| Symptom | Likely causes |
|---|---|
| AP does not appear | AP policy turned Wi-Fi off, antenna missing, power instability, AP start failure, SoftAP channel movement during STA scan |
| Phone sees AP but fails to join | password/auth issue, weak RF, SoftAP driver/client compatibility, BLE coexistence, AP restarting during STA retry |
| Phone joins but no IP | DHCP/server issue, AP restart during DHCP, client-specific ESP32-S3 behavior |
| Phone joins but portal does not open | no captive DNS, OS no-Internet behavior, HTTPS/DoH, cellular fallback |
| Portal opens then drops | AP restart after config save, AP+STA channel change, weak power/RF, BLE coexistence |
| Works indoors but not outdoors or vice versa | GPS fix changes AP policy, motion/stationary state changes AP availability |

## Improvement Plan

### Phase 0 - Reproducibility and Baseline

Objective: make the AP problem observable before changing behavior.

Subphases:

0.1 Version and build baseline

- Pin `platform = platformio/espressif32@<known version>` after selecting the target Arduino-ESP32/ESP-IDF combination.
- Record Arduino-ESP32, ESP-IDF, board package, and compiler versions in `/api/dev`.
- Enable a debug build option with `CORE_DEBUG_LEVEL` configurable by environment.

0.2 AP diagnostics

- Add a `WifiDiagnostics` struct with:
  - last AP start result
  - AP start count
  - AP stop count
  - AP restart count
  - STA retry count
  - last STA disconnect reason
  - last AP station connect/disconnect timestamps
  - last AP station MAC
  - DHCP/IP assigned event timestamp if available
  - current AP channel
  - current STA channel
  - AP MAC
  - heap at AP start
- Expose diagnostics in `/api/dev`.

0.3 Test matrix

- Devices: Android, iPhone/iPad, Windows, macOS.
- Modes: AP-only, AP+STA with valid credentials, AP+STA with bad credentials, AP+STA with router off.
- Conditions: BLE on/off, GPS fix/no-fix, stationary/moving simulated, USB power, battery power, low battery.
- Distance: 0.5 m, 3 m, 10 m, through one wall.

Metrics:

- AP visible within 5 seconds of boot in setup mode.
- Association success rate >= 95% over 20 attempts per client type.
- DHCP/IP assigned within 5 seconds for >= 95% of attempts.
- `http://192.168.4.1/` first byte within 2 seconds after IP assignment.
- No AP restarts during a 10 minute active portal session.

Deliverables:

- Version-pinned build.
- `/api/dev` Wi-Fi diagnostics extension.
- AP test checklist and baseline results table.

### Phase 1 - Deterministic AP Core

Objective: make AP startup explicit, checked, and recoverable.

Subphases:

1.1 Centralize AP start

- Replace repeated `WiFi.softAP(...)` blocks with one function:
  - `bool start_ap(const char *reason)`
  - `bool stop_ap(const char *reason)`
  - `bool restart_ap(const char *reason)`
- Always log and store return values.
- Use explicit open AP handling:
  - password pointer is `nullptr` when AP is open.
  - password length must be 8..63 when secured.

1.2 Explicit AP config

- Call `WiFi.softAPConfig(local_ip, gateway, subnet, lease_start, dns)` where supported.
- Consider moving AP subnet away from `192.168.4.0/24`, for example `192.168.44.1/24`, to reduce conflict with routers using `192.168.4.x`.
- Set AP channel explicitly in config, with default 1, 6, or 11.
- Set `max_connection` explicitly, likely 2 for this product unless multi-user configuration is required.
- Keep SSID broadcast visible.

1.3 Validate persisted config

- Validate AP SSID, password, and mDNS during config load.
- On invalid config, restore only Wi-Fi identity fields to defaults instead of wiping unrelated LED/GPS config.
- Reject SSID control characters and trim whitespace.
- Reject password length > 63.
- Reject mDNS leading/trailing hyphen.

Metrics:

- AP start failure count is zero across 100 boots.
- Invalid saved AP config self-recovers on boot.
- AP mode, IP, channel, max clients, and auth are visible in `/api/dev`.

Deliverables:

- Central AP start/stop code path.
- Explicit AP configuration constants/runtime fields.
- Config validation tests or host-side validation harness where practical.

### Phase 2 - Captive Portal and User Entry Reliability

Objective: reduce the user's steps from "connect and guess the IP" to "connect and portal appears or obvious fallback exists".

Subphases:

2.1 DNS captive portal

- Add `DNSServer`.
- Start DNS after AP start and stop DNS after AP stop.
- Redirect `*` to `WiFi.softAPIP()`.
- Call DNS processing in the main loop before/near `server.handleClient()`.

2.2 HTTP fallback routes

- Add `server.onNotFound()` to redirect HTTP requests to `/`.
- Add known OS captive-check endpoints as plain HTTP responses or redirects:
  - Android connectivity checks
  - Apple captive portal check
  - Windows connect test
  - common `/generate_204` style endpoints
- Avoid HTTPS assumptions.

2.3 Physical/manual fallback

- Add a QR code or printed/manual instruction in docs for `http://192.168.44.1/`.
- Add LED state for "AP active and portal ready" distinct from "AP active but no web/DNS".

Metrics:

- Captive prompt or portal page appears automatically on at least Android and iOS in >= 80% of attempts.
- Manual IP fallback works in 100% of associated/DHCP-success cases.
- DNS query count visible in `/api/dev`.

Deliverables:

- Captive DNS service.
- Not-found redirect.
- Portal readiness status in `/api/dev` and LED state.
- Updated user manual.

### Phase 3 - AP/STA Policy Hardening

Objective: prevent STA recovery and power policy from making setup unreliable.

Subphases:

3.1 Add AP policy modes

- `SETUP`: AP always on, STA optional, no idle shutdown.
- `AUTO`: current battery-aware behavior, but safer.
- `OFF`: Wi-Fi disabled by explicit user choice or deep power mode.

Default boot policy should be:

- If no STA credentials: `SETUP`.
- If STA credentials exist but last STA success is unknown or old: AP remains available for a grace window.
- If user opens `/config` or `/wifi`: hold AP on with a session lease.

3.2 Bounded STA retry strategy

- Replace fixed 10 second retries with exponential backoff and reason-aware behavior.
- Stop aggressive retries after repeated auth failures.
- Keep AP stable while the user is connected.
- Provide a UI state: "home Wi-Fi failed: wrong password/no router/weak signal".

3.3 AP+STA channel management

- Report current AP/STA channel.
- If STA is connecting, show that AP may be temporarily slower.
- Avoid scans while an AP client is actively configuring if possible.
- Consider AP-only setup mode until credentials are saved, then transition to STA after showing a countdown.

3.4 AP idle policy refinement

- Track:
  - station connected
  - station IP assigned
  - HTTP activity
  - config page active
  - captive DNS activity
- Idle shutdown should require no station, no HTTP/DNS activity, no config session, and no setup hold.

Metrics:

- Bad STA credentials do not reduce AP association success below 95%.
- AP remains available for at least 15 minutes during setup mode.
- STA retries are capped and visible.
- No AP off transition occurs while a station is connected or within 2 minutes of HTTP activity.

Deliverables:

- Explicit Wi-Fi state machine.
- Reason-aware retry/backoff.
- AP session lease.
- UI status for STA failure causes.

### Phase 4 - RF, Power, and Coexistence Hardening

Objective: remove non-firmware and shared-radio failure modes from the critical path.

Subphases:

4.1 Hardware validation

- Verify antenna is installed correctly on every XIAO ESP32S3 unit.
- Test with the stock antenna and a known-good external antenna.
- Test USB supply, battery supply, and worst-case LED brightness.
- Capture reset reason and brownout indicators in `/api/dev` if available.

4.2 Power budget

- Confirm the regulator and battery path can tolerate Wi-Fi TX bursts plus LED load.
- Add local capacitance if the hardware revision allows it.
- Test AP association while LEDs run high-current effects.

4.3 BLE coexistence

- Add a config option to disable BLE while AP setup is active.
- A/B test AP reliability with BLE advertising on and off.
- If BLE hurts AP reliability, defer BLE advertising until AP idle or STA connected.

4.4 Sleep and inactivity tuning

- Evaluate `WiFi.setSleep(false)` or lower-level power-save tuning during setup mode.
- Evaluate `esp_wifi_set_inactive_time(WIFI_IF_AP, value)` if Arduino/IDF integration allows it safely.
- Keep any low-level calls isolated behind compatibility checks.

Metrics:

- No brownout or reset during 30 minutes AP-on, LEDs active, BLE on/off tests.
- AP throughput and ping latency measured with BLE on/off.
- Association success rate unchanged under LED load.

Deliverables:

- Hardware test report.
- BLE AP coexistence decision.
- Power/sleep tuning patch if measurement supports it.

### Phase 5 - Release Validation and Documentation

Objective: prevent regressions and make AP behavior understandable.

Subphases:

5.1 Soak tests

- 8 hour AP-only idle test.
- 8 hour AP+STA valid credentials test.
- 2 hour bad STA credential test.
- 50 AP restart/config-save cycles.
- 50 phone reconnect cycles.

5.2 Field diagnostics

- Add a downloadable or copyable diagnostic JSON from `/api/dev`.
- Include last 20 Wi-Fi events in a ring buffer.
- Include firmware version and framework version.

5.3 Documentation alignment

- Update `docs/wifi_portal_spec.md`.
- Update `docs/wifi_portal_state_diagram.md`.
- Update `docs/manual_de_uso.md`.
- Update `docs/config_params.md`.
- Keep AP idle timeout values synchronized with `config.h`.

Metrics:

- Zero unexplained AP disappearances in soak tests.
- All AP policy transitions have logged reasons.
- User manual covers AP SSID, password, no-Internet behavior, captive portal fallback, and reset/recovery.

Deliverables:

- Release checklist.
- Updated docs.
- AP regression test log.

## Suggested Implementation Order

1. Add diagnostics and Wi-Fi event logging first. This gives evidence before behavior changes.
2. Centralize and check AP start/stop. This reduces duplicated fragile logic.
3. Add explicit AP parameters and config validation.
4. Add captive portal DNS and HTTP fallbacks.
5. Refactor AP/STA retry policy.
6. Measure BLE and power effects, then tune.
7. Update docs and run soak tests.

## Acceptance Criteria

The AP implementation should be considered robust when:

- A first-time user can find the AP within 5 seconds of boot.
- Android and iOS can associate and receive IP reliably.
- The portal opens automatically where OS captive behavior permits, and manual IP fallback always works.
- Bad home Wi-Fi credentials do not destabilize the AP.
- AP remains on during setup/configuration sessions.
- AP start, stop, restart, station join, station leave, DHCP assignment, STA connect, and STA disconnect are all logged and exposed in diagnostics.
- AP behavior is reproducible because platform/framework versions are pinned.
- Hardware tests confirm antenna and power are not the hidden cause.

## Immediate Next Engineering Tasks

1. Implement `WifiDiagnostics` and event handlers.
2. Pin the PlatformIO Espressif32 platform after confirming the desired Arduino-ESP32 core.
3. Replace duplicate `WiFi.softAP()` calls with a checked `start_ap()` wrapper.
4. Add AP config validation at load time.
5. Add a setup-mode AP hold so the AP cannot turn off during provisioning.
6. Add DNSServer captive portal support.
7. Add STA retry backoff and bad-credential classification.

## Implementation Update - 2026-05-05

Implemented in the firmware after this review:

- PlatformIO platform pinned to `espressif32@6.7.0`, matching the local build that uses Arduino-ESP32 `2.0.16`.
- AP start/restart centralized through checked `WiFi.softAP()` calls with explicit IP, channel, visible SSID, max clients, and open-AP handling.
- `WifiDiagnostics` added and exposed under `/api/dev`.
- Wi-Fi event handler added for AP station and STA lifecycle counters.
- AP setup hold added after AP start/restart and portal activity hold added after HTTP activity.
- STA retry backoff added, with retries postponed while AP clients are connected.
- Captive portal DNS added with wildcard DNS and common OS captive-check HTTP routes.
- AP SSID, AP password, and mDNS validation tightened for runtime save and persisted config load.

Build verification:

- `~/.platformio/penv/bin/pio run` succeeded for `seeed_xiao_esp32s3`.

## Notes on Current Code Risk vs. Hardware Risk

The firmware contains enough AP policy and AP+STA retry risk to explain many connection issues without assuming defective hardware. However, hardware cannot be ignored on XIAO ESP32S3. A missing or poorly seated antenna, weak battery/regulator path, or LED-induced voltage dip can produce the same user-facing symptoms. The first hardening pass should therefore add diagnostics and run a controlled matrix before making broad assumptions.
