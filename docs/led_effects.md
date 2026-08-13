# LED Effect Catalog

**Status:** Current runtime engine, verified against `src/led/effect_registry.cpp` on 2026-08-13.

Dog-RGB has one versioned `EffectRegistry` for IDs, stable keys, names, useful controls, defaults, color/palette behavior, and safety class. Its renderer produces logical RGB from an explicit pixel span, time, PRNG state, and small effect runtime; `LedBus` separately owns SK6812 RGBW transport (`NEO_GRBW + NEO_KHZ800`).

## Effect IDs

| ID | Stable key | Name / behavior | Meaningful controls | Metadata default S/I | Color | Palette | Safety |
| ---: | --- | --- | --- | ---: | --- | --- | --- |
| 0 | `solid` | `SOLID`, constant color | color | 80 / 140 | base | none | calm |
| 1 | `pulse` | `PULSE`, repeating pulse | speed, color | 80 / 140 | base | none | active |
| 2 | `breath` | `BREATH`, softer breathing curve | speed, color | 60 / 90 | base | selectable; Custom A-B | calm |
| 3 | `chase` | `CHASE`, moving point | speed, intensity, color | 120 / 140 | base | selectable; Custom A-B | active |
| 4 | `comet` | `COMET`, moving head and tail | speed, intensity, color | 120 / 140 | base | selectable; Custom A-B | active |
| 5 | `sinelon` | `SINELON`, oscillating point | speed, intensity, color | 110 / 150 | base | none | active |
| 6 | `confetti` | `CONFETTI`, random sparkles | intensity, color | 100 / 150 | base | none | active |
| 7 | `juggle` | `JUGGLE`, four moving points | speed, intensity, color | 150 / 180 | base | none | advanced |
| 8 | `bpm` | `BPM`, beat-modulated brightness | speed, color | 100 / 150 | base | none | active |
| 9 | `rainbow` | `RAINBOW`, moving cyclic spectrum | speed | 120 / 180 | generated | selectable; Pride | active |
| 10 | `fire` | `FIRE`, heat-cell simulation | intensity | 155 / 200 | generated | internal; Heat | advanced |
| 11 | `gradient_wave` | `GRADIENT_WAVE`, moving gradient | speed | 120 / 180 | generated | selectable; Ocean | active |

IDs `0..11` retain their schema-6/NVS meaning. Unknown IDs are rejected through the registry before persistence and reported as `UNKNOWN` only by diagnostic serialization. Stable keys are API identifiers; labels are presentation text.

## Inputs

- `speed` (`0..255`) controls animation timing/motion only when descriptor `controls.speed` is true.
- `intensity` (`0..255`) controls tail/density/heat only when `controls.intensity` is true.
- `base` is the mode/range RGB tint where supported.
- `accent` is the second endpoint used by `Custom A-B`.
- `palette_id` selects one of eight curated RGBW palettes for descriptors marked `selectable`; `PALETTE_NONE` preserves the Phase 2 renderer contract.
- Global runtime brightness (`1..255`) is applied by the strip driver.

`speed` and `intensity` do not have identical perceptual meaning across effects. Values for currently irrelevant controls remain stored for binary/config compatibility; the portal disables those controls but does not erase them. Registry defaults are recommendations for consumers, not a migration or rewrite of existing range records.

## Range defaults

Speed and Geofence modes share ten effect records, independently selecting strips A and B. The current compile-time defaults use `JUGGLE` on both strips for all ranges, with increasing tuning:

| Range | Speed | Intensity |
| ---: | ---: | ---: |
| 1 | 40 | 80 |
| 2 | 58 | 95 |
| 3 | 76 | 110 |
| 4 | 94 | 125 |
| 5 | 112 | 140 |
| 6 | 130 | 155 |
| 7 | 148 | 170 |
| 8 | 166 | 180 |
| 9 | 184 | 190 |
| 10 | 200 | 200 |

Colors and default speed thresholds are in [LED color reference](color-reference.md).

## Show mode

- Shuffles all 12 IDs into a bag and consumes each once before reshuffling.
- Avoids using the previous bag's last effect as the next bag's first effect.
- Selects curated random base/accent colors and a registry palette when the effect supports one.
- Applies safe internal variation to speed/intensity; Fire uses its own range.
- Changes effect every 30 seconds with a 500 ms body crossfade from the visible frame; there is no fade-to-black stage.
- Keeps the same effect/parameters on both strips and lets layout-level mirror produce physical symmetry.
- Normally renders the body while status pixels remain active.
- Day Mode clears effects and keeps status. Semantic status pixels remain reserved in every normal scene.

`FIRE` retains its internal heat simulation. `RAINBOW` and `GRADIENT_WAVE` now take their colors from the selected palette rather than fixed HSV.

## Palette catalog

`PaletteRegistry` version 1 contains `Safety Amber`, `Night Red`, `Ocean`, `Forest`, `Pride`, `Heat`, `Ice`, and `Custom A-B`. Stops are stored as RGBW and sampled without allocation. The transport, power estimator and palettes use the same canonical RGB↔RGBW conversion, including intentional white-channel content.

## Simple mode

Simple mode stores one effect, speed, intensity, and RGB value. It renders the semantic body; status pixels remain visible. If the chosen effect supports palettes, its registry default is used until a future portal selector exposes explicit choice.

## Runtime API

`GET /api/v1/led/capabilities` is the authoritative catalog used by the portal. `GET /api/v1/led/state` returns the currently selected intent/effects. Writes stay compatible through `/config` and `POST /api/config`: if `effects` is supplied, all ten range objects must be present; `single` may be partial. IDs absent from the registry and parameter values outside `0..255` are rejected before persistence.

Capabilities publishes all eight palettes, each effect's `default_palette_id`, and whether palette use is `none`, `internal`, or `selectable`. Runtime v1 remains read-only; user-facing palette selection is intentionally deferred, so existing `/api/config` and NVS records do not change.

## Performance and safety

- Effect state updates at 50 ms (20 Hz).
- Characterization replays five frames per ID using fixed times, initial pixels/heat, runtime, base color, speed/intensity, and PRNG seed. The resulting 12 golden digests are native-tested.
- Those goldens run with `PALETTE_NONE`; a second native harness proves palette divergence and segment bounds for Breath, Chase, Comet, Rainbow, and Gradient Wave.
- `effect_registry.cpp` has no Arduino, GNSS, Wi-Fi, geofence, NVS, allocation, `millis()`, or process-global `random()` dependency.
- The production transport updates both strips; Wokwi caps only expensive virtual pixel transport while preserving the 50 ms effect-state cadence.
- The global limiter bounds the configured two-bus estimate and preserves RGBW ratios. It is not a sensor or proof of the physical ceiling: calibrate its base/channel profile and measure strip, boost, wiring, rails, battery, and temperature for every allowed budget.
