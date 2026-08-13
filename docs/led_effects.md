# LED Effect Catalog

**Status:** Current runtime engine, verified against `src/led/led_ui.cpp` on 2026-08-12.

Dog-RGB uses Adafruit NeoPixel with custom non-blocking effect state. The physical pixel type is configured as SK6812 RGBW (`NEO_GRBW + NEO_KHZ800`), while the effect API currently supplies RGB base values.

## Effect IDs

| ID | Name | Behavior | Uses base RGB directly? |
| ---: | --- | --- | --- |
| 0 | `SOLID` | Constant color | Yes |
| 1 | `PULSE` | Repeating intensity pulse | Yes |
| 2 | `BREATH` | Smooth breathing curve | Yes |
| 3 | `CHASE` | Moving point/pattern | Yes |
| 4 | `COMET` | Moving head with fading tail | Yes |
| 5 | `SINELON` | Oscillating point/bar | Yes |
| 6 | `CONFETTI` | Random sparkles | Yes |
| 7 | `JUGGLE` | Multiple moving points | Yes |
| 8 | `BPM` | Beat-modulated brightness | Yes |
| 9 | `RAINBOW` | Internal HSV rainbow | No |
| 10 | `FIRE` | Heat-cell fire simulation | No; uses heat palette |
| 11 | `GRADIENT_WAVE` | Internal HSV gradient wave | No |

Unknown IDs are rejected by runtime validation and reported as `UNKNOWN` only by the diagnostic name helper.

## Inputs

- `speed` (`0..255`) controls animation timing/motion according to the effect.
- `intensity` (`0..255`) controls effect energy/density/brightness according to the effect.
- `base` is the mode/range RGB tint where supported.
- Global runtime brightness (`1..255`) is applied by the strip driver.

`speed` and `intensity` do not have identical perceptual meaning across every effect. Treat them as effect parameters, not physical units.

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
- Selects a curated random base/target color and interpolates between them when the effect supports base RGB.
- Applies safe internal variation to speed/intensity; Fire uses its own range.
- Changes effect every 30 seconds with a 500 ms transition fade.
- Keeps the same effect/parameters on both strips.
- Normally renders the body while status pixels remain active.
- Day Mode clears effects and keeps status; a Wi-Fi-off homogeneous state can render the effect across the full strips.

`RAINBOW`, `FIRE`, and `GRADIENT_WAVE` will not visibly follow Show's chosen base color; this is intentional engine behavior.

## Simple mode

Simple mode stores one effect, speed, intensity, and RGB value. It normally fills the entire physical strip, including status pixels. Day Mode has higher priority and restores status-only rendering during its active window.

## Runtime API

Use `/config` or `POST /api/config`. If the `effects` object is supplied, all ten range objects must be present; `single` may be partial. Effect IDs outside `0..11` and parameter values outside `0..255` are rejected before persistence.

## Performance and safety

- Effect state updates at 50 ms (20 Hz).
- The production transport updates both strips; Wokwi caps only expensive virtual pixel transport while preserving the 50 ms effect-state cadence.
- The global limiter bounds the configured two-bus estimate and preserves RGBW ratios. It is not a sensor or proof of the physical ceiling: calibrate its base/channel profile and measure strip, boost, wiring, rails, battery, and temperature for every allowed budget.
