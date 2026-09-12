# I4 — consolidation and the bare-board bench

**In progress.** I4 acceptance requires GPS, both strips, LCD and portal under
joint load. A board with only USB and its integrated LCD can validate boot,
display operation and diagnostic behavior, but cannot close that acceptance.
The owner authorized using the connected board without soldered GPS/LEDs and
showing simulated data. This guide distinguishes that subset from full I4.

Subsequently the owner confirmed the demo was visible and authorized independent
[I5 static LVGL development](display-i5.md). I5 adds `s`/`l` backend controls;
the I4 results below refer to the preserved basic-renderer image.

## Available now

Use `waveshare_lcd169_displaycheck` (stage 3), not a new firmware application or
stage-4 build. It retains normal domain/storage/portal behavior and I2 LED bench
limits. The new **`f`** USB command enables a repeating 30-second LCD-only demo:
searching → stationary fix → moving fix → insufficient quality → stale →
recovered moving fix, five seconds each. It uses the same snapshot/formatter/
partial drawing as live data and marks the header **RGB DOG DEMO**.

Demo distance/date are explicitly fixtures (1842 m, 2026-09-12); the LED mode
still comes from the real domain. No fake NMEA enters UART or the parser, no fake
distance/session/route is persisted, and LED policy continues to see real GPS
state. `[I3]` reports therefore remain `no-data`/zero RX without a GNSS module
even while the LCD demonstrates fix/motion. `[LCD] demo=1` identifies the fixture.
**`v`** returns to live values. Reset also clears demo mode. `t`, `b`, `d` and
`r` retain their [I3 meanings](display-i3.md). `f` does not resume a paused
service; use `d` to resume. No fixtures or command controller are enabled in
the product target or Classic.

The new host persistence check compiles the real runtime configuration codec,
A/B save/load and LED apply path for Classic and stage 3. It runs ten settings
changes, starts a fresh process to reload each, and verifies fallback after a
truncated write. Preferences/radio/NeoPixel transport are fake. Stored requested
brightness survives even though stage 3 applies the 16/255 transport cap. These
are stronger software checks than a Python codec model, not physical NVS,
HTTP-handler, RF or LED evidence.

## Optional serial capture

The helper uses PlatformIO's installed Python/pyserial. It is optional and does
not flash, reset, change configuration, inject GNSS or grade physical acceptance.
Use only an already identified/flashed I3 device. It opens native USB CDC with
DTR for logging and RTS inactive. Specify the actual port, not an old COM number.

From `Platformio/Dog-RGB`:

```powershell
python tools/display_bench.py --port COM6 --seconds 600 --step 0:f --step 5:r --step 180:b --step 240:b --step 300:d --step 360:d --step 365:r --output artifacts/display-i4/lcd-demo-10min.txt
python tools/display_bench.py --analyze artifacts/display-i4/lcd-demo-10min.txt --output artifacts/display-i4/summary.json
```

If the local Python lacks pyserial, invoke the same command with
`& "$env:USERPROFILE/.platformio/penv/Scripts/python.exe"` instead of `python`.
Analysis alone uses the standard library and reuses the existing log parser.
Steps are seconds after capture starts. Only `t/v/b/d/f/r` are accepted; no
arbitrary serial command is generated. Capture is capped at one hour and writes
raw logs plus a JSON observation summary. It records missing logs, fatal markers,
clock regressions, malformed joined records, mode/enable/backlight observations, GPS counters, heap,
maximum LCD tick time and drawing p95 bucket bounds. An empty file cannot become
a passing test. A serial log proves reported state, not visible pixels.

The example first measures normal fixture updates, turns off only the backlight
for one minute, then pauses the UI for another minute and resumes. Counters are
reset after initial/manual redraws, so use both the all-capture maximum and the
final normal-drawing window. A whole-capture maximum of cumulative percentile
fields is **not** a single percentile of the complete capture. Record counters
by differences: preexisting log drops from a disconnected monitor must not be
misreported as new losses during the observed window. A zero-RX GNSS counter on
a bare board cannot demonstrate UART capacity or overflow tolerance.

## Full I4 acceptance when peripherals/network are available

1. Finish the unresolved board/revision, electrical, RGBW, real GNSS and panel
   window/color checks from I0–I3. Record supply, wiring, strip length and image
   hash. Distinguish USB-reset checks from actual power/battery cycling.
2. Start a live-data (`v`) capture for at least 30 minutes. Record initial real
   GPS counters, config generation/slot, stored daily value/date, current route
   and heap after warm-up. Open the actual embedded portal on a phone/host.
3. Make ten changes among the existing modes; record requested mode, resulting
   domain/LED state and LCD mode after its sampling interval. Account for Day
   Mode, scene/manual, welcome and alert overrides rather than bypassing them.
4. Perform three deliberate save → requested reset → read cycles through the
   normal portal. Compare settings with the recorded values, and restore the
   original test settings at completion. Verify config generation and storage
   error diagnostics. Do not erase NVS to make recovery pass.
5. Query/export an actual populated route in JSON/CSV/GeoJSON during GPS/LED/LCD
   operation. Check point counts/coordinate order and the expected date/session;
   include slow/aborted transfer and inspect GPS overflow and loop diagnostics.
   A mock response or empty export does not prove this load case.
6. Compare display enabled, backlight dark and UI paused windows; tracking,
   LEDs and portal must keep working. Record resets, minimum heap, counter deltas
   and latency. Verify memory after repeated settings/save/export activity,
   not merely a single stable value on an idle board.
7. Record a brief outdoor check. Close as a **bench base** only if applicable
   joint-load checks pass; portable use additionally needs the separate power,
   battery and mechanical validation. LVGL/animations remain I5/I6.

## Portal software checks

Use pinned Node 24.18.0 and npm 11.6.2 with repository dependencies installed:

```powershell
npm run webui:check
npm run webui:unit
npm run smoke
npx playwright test --project=iphone-13-pro-max-chromium
```

The root Playwright config now excludes `tests/portal-e2e/`, whose separate
application/fixture lifecycle belongs to `playwright.portal.config.ts`. This
prevents the embedded AP checks from importing unrelated environment-dependent
tests. Those tests remain in their own configuration. The embedded browser suite
uses mocked APIs and production portal bundles; it cannot replace real ESP32
HTTP/NVS/radio checks.

Measured results, backup/flashing history and remaining limits are in the
[I4 progress baseline](../../../docs/baselines/display-i4-2026-09-12.md).
