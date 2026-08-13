# LED Color and Status Reference

**Status:** Current default firmware behavior. Runtime configuration can change effects, thresholds, brightness, and Simple-mode color.

## Layout

- Two strips by default, 24 RGBW pixels per strip.
- Pixels `0..1` on each strip are reserved for system status in every normal mode.
- Pixels `2..23` show the selected activity effect.
- Simple and homogeneous eligibility keep the same semantic status/body split.
- Default global brightness is `77/255` (about 30% of the software scale).

## System indicators

| Indicator | Color/pattern | Meaning |
| --- | --- | --- |
| Wi-Fi | Green solid | Station connected |
| Wi-Fi | Green pulse | Station connection in progress |
| Wi-Fi | Yellow solid/pulse | AP active; pulse indicates an AP client |
| Wi-Fi | Red solid | Station failure with AP fallback |
| Wi-Fi | Amber double pulse | Wi-Fi off under power policy |
| GNSS | Blue solid | Trusted fix and quality gates pass |
| GNSS | Blue pulse | Searching or quality gates fail |
| Both status pixels | Fast red flash | Critical no-GNSS/no-station timeout |
| Both status pixels | Red pulse | Valid Geofence distance at/above `fence_max_m` |

Day Mode turns off effect pixels only; status indicators continue to work.

## Default speed colors

Nine thresholds create ten ranges. Threshold values are runtime-configurable; these are compile-time defaults.

| Range | Speed | Base RGB | Description |
| ---: | --- | --- | --- |
| 1 | `<= 2 km/h` | `(0, 60, 60)` | Cyan |
| 2 | `> 2–4 km/h` | `(0, 60, 35)` | Cyan-green |
| 3 | `> 4–6 km/h` | `(0, 60, 0)` | Green |
| 4 | `> 6–8 km/h` | `(25, 60, 0)` | Lime-green |
| 5 | `> 8–10 km/h` | `(60, 60, 0)` | Yellow |
| 6 | `> 10–12 km/h` | `(60, 45, 0)` | Amber |
| 7 | `> 12–14 km/h` | `(60, 30, 0)` | Orange |
| 8 | `> 14–16 km/h` | `(60, 20, 0)` | Deep orange |
| 9 | `> 16–18 km/h` | `(60, 10, 0)` | Red-orange |
| 10 | `> 18 km/h` | `(60, 0, 0)` | Red |

The firmware rejects reported speed above 40 km/h as a spike and does not treat it as usable activity evidence.

## Geofence colors

Geofence mode divides `fence_max_m` into ten equal distance bands and reuses the cyan-to-red range palette. With the default maximum of 300 m, each band is 30 m. Farther distance maps toward red.

- No trusted fix: palette-driven rainbow fallback.
- No Home: amber breathing fallback.
- At/above the configured maximum: red alert overlay on status while the body scene remains active.

## Curated RGBW palettes

The runtime registry provides Safety Amber, Night Red, Ocean, Forest, Pride, Heat, Ice, and Custom A-B. Breath, Chase, Comet, Rainbow and Gradient Wave can sample a selected palette; Fire retains internal heat behavior. The white values are intentional and use the same RGB↔RGBW conversion as power estimation and transport.
- Boundaries use hysteresis to avoid flicker.

## Built-in scenes

Show and manual scene playback use these immutable recipes:

| ID/key | Name | Effect/palette | Base/accent | Body level | Transition |
| --- | --- | --- | --- | ---: | ---: |
| `1/high_visibility` | Alta visibilidad | Chase / Safety Amber | `#FF5000` / `#FFDCA0` | 255 | 400 ms |
| `2/calm` | Calmado | Breath / Night Red | `#780000` / `#FF280A` | 110 | 900 ms |
| `3/active` | Activo | Comet / Forest | `#005A19` / `#64FFAA` | 200 | 500 ms |
| `4/party` | Fiesta | Rainbow / Pride | `#C800C8` / `#00C8FF` | 180 | 650 ms |

`body_level` is relative to global brightness and affects only body pixels before crossfade. It cannot brighten above the owner's setting or power budget, and it never dims Wi-Fi/GNSS status or alert pixels. Names such as “Alta visibilidad” describe appearance, not certified visibility or safety performance.

## Effect caveat

Most effects tint their output with the base RGB. `RAINBOW` and `GRADIENT_WAVE` generate their own HSV colors; `FIRE` uses an internal heat palette. Those three do not directly reflect the selected base color.

See [LED effect catalog](led_effects.md) for IDs and tuning fields. The Spanish translation is [manual_de_colores.md](manual_de_colores.md).
