# LED User-Interface Specification

**Status:** Current implemented behavior, verified 2026-08-12.

The LED system combines activity effects with an always-visible local health interface wherever the selected mode allows it.

## Default layout

- Two independent strips (`LED_STRIP_MODE = 2`).
- 24 SK6812 RGBW pixels per strip.
- First two pixels per strip reserved for status.
- Remaining 22 pixels per strip form the effect body.
- Global brightness defaults to 77/255.

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
| 0 + 1 | Critical no-GNSS/no-station timeout | Fast red flash |

The automatic AP idle policy currently stops SoftAP without forcing the whole Wi-Fi subsystem OFF, so the amber/off and homogeneous paths are retained capabilities rather than the common idle outcome.

## Mode behavior

| Mode | Range/input | Effect area | Status behavior |
| --- | --- | --- | --- |
| Speed | Trusted usable GNSS speed | Body; rainbow fallback without fix | Status pixels retained |
| Geofence | Distance from Home with hysteresis | Body; rainbow without fix, amber breath without Home | Status pixels retained |
| Show | Shuffled 12-effect demo | Body | Status pixels retained |
| Simple | One configured effect/RGB | Full strip | Status hidden by design |

Speed/Geofence use the same ten effect records. Geofence changes the range selector, not the effect engine.

## Priority and composition

The practical render order is:

1. **Welcome** owns the strips until its startup sequence completes.
2. **Day Mode active** clears effect pixels and renders the normal status pixels, in every runtime mode.
3. **Simple** fills the strips when Day Mode is not active.
4. **Speed/Geofence/Show** render the body.
5. **Homogeneous state**, when Wi-Fi is explicitly OFF and GNSS has been stable for five minutes, can extend the selected effect to all pixels.
6. Otherwise status rendering paints the reserved pixels, including the critical red override.

The critical override has priority within the status layer; it cannot appear while Simple owns the full strip, but Day Mode restores status rendering.

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
| Show effect | 30 s |
| Show transition | 500 ms |
| Homogeneous eligibility | 5 min stable GNSS while Wi-Fi-off state is true |
| Critical health timeout | 10 min |

## Diagnostics and testing

`/api/dev` exposes current mode, brightness, speed/geofence range, chosen effects/base color, Simple configuration, and current Show effect. The Wokwi `modes` scenario and host Day Mode contracts validate mode selection/persistence and status preservation; physical strips still require current/temperature/visual checks.
