# 2026-01-20_led-effects-fastled.md

## Scope
- Migrate LED effects from fixed colors (Adafruit NeoPixel) to FastLED.
- Implement effect selection per speed range for Segment B.
- Keep Segment A as system status UI (GPS/Wi-Fi).

## Non-scope
- Portal control of effects.
- New GPS logic.
- BLE updates.

## Library choice
- FastLED (more effects, palettes, flexible control).

## Risks
- Timing conflicts with Wi-Fi and GPS loops.
- Increased CPU usage; must keep FPS modest.
- Need to ensure SK6812 compatibility in FastLED config.

## Implementation steps
1) Add FastLED dependency to PlatformIO and remove Adafruit NeoPixel usage.
2) Define LED buffers for strip A and B.
3) Implement effect catalog and dispatcher.
4) Map speed ranges -> effect IDs from config.h.
5) Render Segment A (status) on top of Segment B.
6) Add frame timing (target FPS) and watchdog for stability.

## Validation
- Compile with PlatformIO.
- Verify LED output for each range (manual speed override).
- Confirm Segment A states still visible.
- Check Wi-Fi portal remains responsive.
