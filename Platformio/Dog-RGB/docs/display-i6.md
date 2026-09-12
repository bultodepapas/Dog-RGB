# I6a — Activity, Connection and BOOT navigation

**Implemented; development paused after USB observation, 2026-09-12.** Remaining
physical checks and future packages are defined in the
[incremental plan](../../../docs/PLANS/2026-09-12_display-incremental-delivery.md).
The pause does not alter the loaded firmware or mark physical acceptance complete.

This increment implements the first two pages from the
[collar-use contract](../../../docs/PLANS/2026-09-12_display-use-and-screens.md).
It retains LVGL 8.4.0, Arduino_GFX 1.6.7, RGB565/swap 0, 40 MHz SPI, the existing
GPS/LED/Wi-Fi policies and Classic isolation. No animation, automatic backlight
timeout, rest classification, battery percentage or walk-session lifecycle is
introduced here. Exact builds and physical observations belong in the
[I6 baseline](../../../docs/baselines/display-i6-2026-09-12.md).

## Pages and data

**Actividad (1/2)** puts registered distance first, with meters/kilometers and the
record's date. It does not claim that the retained record belongs to today or
to one walk. Speed remains visible with its unit; invalid/stale speed is `--`.
GPS status uses plain wording. LED mode is labeled in Spanish and a compact
connection summary leads to the second page. The basic I3 text renderer retains
its original rows and formatting for diagnostic comparison.

**Wi-Fi (2/2)** separates the collar AP from its station connection. AP status,
SSID and IP remain visible when STA is also connected. Client counts are Wi-Fi
associations, not proof of an open browser. A disconnected interface loses its
address on the next sample. AP inactive is distinct from radio explicitly off;
STA connected is not a claim of Internet/cloud connectivity. The page does not
scan, reconnect, save credentials or extend AP lifetime.

`capture_connection()` reads the existing Wi-Fi manager in the normal loop,
copies bounded values, and queries the current station SSID/IP only when that
interface is connected. Configuration supplies the AP name. It never copies
passwords into display memory. The standalone formatter has no Arduino/LVGL
dependency, so every radio combination can be exercised on PC.

Up to 32 printable ASCII SSID bytes wrap across at most three lines; no marquee
or silent truncation is used. Built-in Montserrat lacks arbitrary Unicode SSID
coverage: unsupported/control bytes produce `Nombre no compatible`, while the
available IP remains visible. Full international SSID typography is a remaining
limitation. UI copy uses supported glyphs (`Wi-Fi`, `Actividad`, `Registrado`);
the title is not a misspelled accented word.

In stage-3 DEMO, only GPS/distance/date are fixtures. Wi-Fi still reflects the
real radio. The connection header explicitly says `GPS DEMO`; the production
target starts with real data and has no serial diagnostic command handler.

## Input

BOOT on GPIO0 is used as an active-low input with pull-up. The V2 schematic
connects its key to GPIO0; SYS_OUT and SYS_EN are not repurposed.
[Manufacturer schematic](https://files.waveshare.com/wiki/ESP32-S3-LCD-1.69/ESP32-S3-LCD-1.69_V2.pdf).
GPIO0 is now explicitly reserved by the Waveshare profile.

While the firmware is running, a short BOOT press and release changes page.
Input is debounced for 30 ms and emits one event on release. Holds of 1.5 s or
more produce no action or repeat. A button already held when the input starts
does not emit a release event. BOOT retains its normal bootloader role if held
during reset; use short presses after the application has started for UI checks.

If the backlight is off, the first click redraws/wakes the current page only.
The next click advances. The same wake behavior resumes a diagnostic `d` pause.
There is no automatic timeout yet: USB tests explicitly turn the light/service
off before testing wake. Physical press/visibility confirmation remains separate
from tests that inject the same event by serial.

## Stage-3 controls and observations

Existing `f/v` fixture/live, `t` bars, `s/l` basic/LVGL, `b` backlight, `d`
service pause and `r` draw-counter reset are retained. Added:

| Command | Result |
| --- | --- |
| `a` | Select Activity in LVGL |
| `c` | Select Connection in LVGL |
| `n` | Inject the same short-click event as BOOT; wake-only if off |

Page selection while paused is remembered and drawn on resume. `a/c` are direct
bench selection, whereas `n` follows the physical wake/navigation rules. The
input loop still consumes at most eight serial characters per tick. `[LCD]`
adds selected `page=activity|connection|text` and cumulative `clicks`; `r` does
not erase the interaction count. Basic/LVGL share one physical SPI owner.

The two content containers and one shared black root are allocated once. Only the active page is updated at
the existing 1 Hz sampling cadence, and identical strings do not invalidate
labels. A page switch refreshes the selected data before presenting it. Full
redraws and normal partial updates must be measured separately; there is no
animation frame-rate claim. Initial page-switch measurements exceeded 50 ms,
so navigation now invalidates the inner 192×244 content. LVGL 8.4 adds five
pixels per side to transformed invalidation: a switch transfers 51,308 pixels,
versus 67,200 for the original full frame (23.65% less). Native tests assert this
bound. Pixel-identical captures confirm that geometry and content did not change.
When returning from raw bars/basic text, four synchronous margin fills restore
the black exterior in one service step, then LVGL redraws content on the next
loop. Backlight stays off between these steps. Combining both initially exceeded
the 50 ms budget; splitting them lets ordinary domain work run between phases.
Those fills are included in
aggregate service time but not in the LVGL-only `pixels`/`flushes` counters.

## Reproducible checks

From the repository root, `python tools/display-simulator/render.py` builds the
real UI and service and runs four CTest contracts. It exports nine PNGs:
Activity searching/fix/stale, plus AP-only, AP+STA, connecting, disconnected,
radio-off and long-name connection examples. Bounds, pairwise overlap and
wrapped text height are checked against the actual LVGL layout.

Service tests cover repeated page/backend changes, unchanged updates, input
bounce/long holds, wake without accidental page advance, pause/resume and rollover.
A separate adapter test compiles the real `connection_snapshot.cpp` against
read-only Wi-Fi/config stubs, distinguishing actual connected SSID from saved
credentials and clearing addresses when disconnected. Fake transport timings
do not measure ESP32 performance.

On the USB board, record both pages, repeated changes, light-off/wake cycles,
memory, full/partial service times and reset/log counters. Keep the old image
and record the new binary hash. GPS/LED/HTTP joint-load acceptance still requires
the unsoldered peripherals and a client connection; a local AP status label
does not itself validate an HTTP request.
