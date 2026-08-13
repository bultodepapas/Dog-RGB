# LED User-Interface Specification

**Status:** Current implemented behavior, verified 2026-08-13.

The LED system combines activity effects with an always-visible local health interface wherever the selected mode allows it.

## Default layout

- Two independent strips (`LED_STRIP_MODE = 2`).
- 24 SK6812 RGBW pixels per strip.
- First two pixels per strip reserved for status.
- Remaining 22 pixels per strip form the effect body.
- Bus A body is declared `forward`; bus B body is declared `reverse`.
- Equal A/B effects use layout-level mirror by default; different effects keep independent branches.
- Global brightness defaults to 77/255.
- Global estimated-current limit defaults to 1,000 mA including a provisional 200 mA base load.

## Status pixels

| Pixel | State | Rendering |
| --- | --- | --- |
| 0 (Wi-Fi) | Station connected | Solid green |
| 0 | Station connecting | Green pulse |
| 0 | AP active, no client | Solid yellow |
| 0 | AP active, client present | Yellow pulse |
| 0 | Station failed with fallback | Solid red |
| 0 | Explicit Wi-Fi-off state | Amber double pulse |
| 1 (GNSS) | Trusted fix | Solid blue |
| 1 | No trusted fix | Blue pulse |
| 0 + 1 | Critical no-GNSS/no-station timeout | Red two-level flash (`System` alert) |
| 0 + 1 | Valid Geofence distance at/above `fence_max_m` | Red pulse (`Geofence` alert) |

The automatic AP idle policy currently stops SoftAP without forcing the whole Wi-Fi subsystem OFF, so the amber/off and homogeneous paths are retained capabilities rather than the common idle outcome.

## Mode behavior

| Mode | Range/input | Effect area | Status behavior |
| --- | --- | --- | --- |
| Speed | Trusted usable GNSS speed | Body; rainbow fallback without fix | Status pixels retained |
| Geofence | Distance from Home with hysteresis | Body; rainbow without fix, amber breath without Home | Status pixels retained |
| Show | Shuffled eligible-scene catalog (4 built-ins + up to 4 user slots) | Body | Status pixels retained |
| Simple | One configured effect/RGB | Body | Status pixels retained |

Speed/Geofence use the same ten effect records. Geofence changes the range selector, not the effect engine.

A manual scene is a volatile override, not a fifth persisted mode. It snapshots one built-in/user recipe, reports `LedIntent::SceneManual`, and replaces the normal Speed/Geofence/Show/Simple body until cancel, explicit mode change, another apply, or reboot. It never takes ownership of status/alert regions.

## Priority and composition

`LedPolicyEngine` receives value-only inputs and produces the retained `LedState`. Priorities are explicit:

| Priority | Intent/layer | Result |
| ---: | --- | --- |
| 100 | Welcome | Owns both complete strips during startup |
| 90 | System or Geofence alert | Overrides only `alert`/status while preserving the underlying body intent |
| 80 | Day status | Disables the body and keeps status pixels; a simultaneous critical alert reports priority 90 |
| 30 | Range, Show, Simple, SceneManual | Normal configured or overridden body recipe |
| 20 | Idle, Home missing | Fallback/guidance behavior |

The practical render order is:

1. **Welcome** owns the strips until its startup sequence completes.
2. `ScenePlayer` consumes any pending apply/cancel and advances Show from a fixed shuffled bag.
3. `LedPolicyEngine` combines the optional scene snapshot with device state and produces body effects, palettes, mirror, transition, body level and alert state.
4. Effects render only logical body coordinates; status renders independently.
5. `LedCompositor` scales the target body, maps A/B orientation, mirrors one logical branch when requested, and crossfades the body.
6. The current status frame bypasses decorative fades.
7. A System/Geofence alert interrupts an active fade and overwrites the two reserved status pixels on both buses.
8. Day Mode clears only the body before this composition.

`critical_alert` and the typed `alert` field are separate from `intent`, so an alert does not silently destroy the current decorative scene. The retained `homogeneous` flag still reports Wi-Fi-off/GNSS eligibility for compatibility, but semantic status ownership is no longer surrendered to an effect.

`body_level` is `1..255` relative to global brightness. It is applied to the requested body target before crossfade; status/alerts remain unscaled by the recipe, and the later global brightness plus `PowerLimiter` remain authoritative.

## Semantic regions and transitions

`LedLayout` exposes `status`, `body_left`, `body_right`, `body_all`, and `alert`. `body_all` has 22 logical pixels in mirror mode and 44 in independent/continuous mode. Reverse applies to the body mapping; physical status indices remain fixed at `0..1`.

A normal visual change starts a 500 ms buffer-to-buffer crossfade. The compositor snapshots the last requested physical frame, maps the new logical frame, and blends only body pixels. A change during an active transition starts again from the last composed frame, not from black. Alerts cancel the transition immediately at the next 50 ms LED tick.

## Day Mode

Day Mode is a power-saving gate, not a fifth visual mode. It is off by default. During 06:00–16:00 local (fixed UTC-5) with recent trusted GNSS time:

- effect pixels are black;
- Wi-Fi/GNSS/critical status remains visible;
- GNSS, metrics, route recording, Wi-Fi, storage, HTTP, and BLE configuration state continue;
- the boot welcome remains visible before the gate is evaluated.

Without trusted recent time, the state is `waiting_time` and effects remain active.

## Range colors

Default Speed colors progress from cyan at the lowest range to red at the highest. Geofence uses the same palette from near to far. Runtime thresholds and effects can change, while palette values are compiled in. See [Color reference](color-reference.md).

## Timing

| Behavior | Timing |
| --- | ---: |
| LED state update | 50 ms |
| Slow status pulse | About 1.5 s cycle |
| Critical flash | 200 ms phases |
| AP-off double pulse | 3 s period, 200 ms pulse width |
| Show scene visible time | 30 s of active playback; Welcome and Day Mode pause the clock |
| Normal body crossfade | 500 ms |
| Homogeneous eligibility | 5 min stable GNSS while Wi-Fi-off state is true |
| Critical health timeout | 10 min |

## Diagnostics and testing

`/api/v1/led/state` exposes mode, intent, typed alert, priority, mirror, effects/palettes, parameters, base/accent RGB, body level, active/pending/stale scene metadata, transition counters/progress, brightness, and the latest limiter snapshot. `/api/dev` uses that same state and adds scene-store/player timing/recovery counters instead of recomputing a second decision. `/api/v1/led/capabilities` exposes effect/palette/scene registry versions, semantic layout/orientation, scene limits and feature flags.

The Phase 2 harness retains the 12 legacy effect goldens with no selected palette and validates policy boundaries. The Phase 3 harness adds exact layout maps, canonical RGBW palette round-trips, five palette-aware effects, mirror, status-preserving crossfade, no-black midpoint, and immediate alert interruption. Phase 4 adds exact scene/record wire vectors, built-in goldens, player bag/timer/override behavior, store fault injection and JSON round-trip/hostile-input tests. Physical strips still require orientation, current, write-gap, heap, timing, temperature and perceptual checks.

## Frame, transport, and power boundary

Effects render RGB into a logical `LedFrame` containing branches A/B. `SceneCatalog`/`ScenePlayer` provide only a validated recipe snapshot; they do not render or own pixels. `LedCompositor` applies its body level and produces the orientation-aware physical frame, then `LedBus` owns Adafruit NeoPixel and is the only layer that writes GPIO. Before transport, `PowerLimiter` evaluates both active buses as one load and applies the same scale to every RGBW channel.

The effect renderer itself is `effect_registry.cpp`. It consumes explicit time, PRNG state, effect runtime, base/accent color, palette ID, parameters, and a bounded pixel span. It does not import GPS, Wi-Fi, geofence, NVS, Arduino time/random, or the physical bus. `led_ui.cpp` snapshots product domains and converts schema-6 `RangeEffect` records plus the optional `ScenePlayer` snapshot to `LedPolicyConfig`; `LedLayout`/`LedCompositor` own physical composition. No function in the scene/render tick reads NVS or uses ArduinoJson/`String`/dynamic allocation.

The limit is enabled by default. Its runtime profile is advanced configuration: total budget, non-LED base current, full R/G/B channel current, and full white-channel current. Reduction is immediate; recovery is gradual to reduce visible pumping. The estimate is intentionally rounded upward but remains a model rather than a current sensor.
