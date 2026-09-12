# I3 — simple text display

Software implemented for the **Waveshare LCD 1.69 No Touch V2 candidate**.
Physical I0–I3 acceptance remains open. Follow [board identification and bench
order](boards.md) before uploading; the display does not identify the PCB.

## Targets

```powershell
pio run -e waveshare_lcd169_displaycheck
pio run -e waveshare_lcd169
```

`waveshare_lcd169_displaycheck` is stage 3: the normal GPS/LED/portal/storage
core, I2 transport limits (brightness at most 16/255, estimator enabled with
budget at most 1000 mA), welcome omitted, LCD and USB diagnostic controls.
It continues without USB and does not automatically stop LEDs. Normal
configuration and GPS data persist; the transport limits do not save themselves.

`waveshare_lcd169` now includes the same live text page with normal product
brightness/welcome and **no diagnostic console commands**. I0/I1/I2 retain
their earlier behavior and exclude both display sources and the graphics
library. Classic/Wokwi also exclude them. Their configuration is separate from
the display-enabled flags/dependency; the common firmware core is unchanged.

## Driver and data

The only new library is **GFX Library for Arduino 1.6.7**, pinned for the two
display-enabled targets. One static `Arduino_ESP32SPI` and `Arduino_ST7789`
own the panel. DC/CS/SCK/MOSI/RESET are GPIO4/5/6/7/8, no MISO; backlight is
GPIO15, active high. Portrait rotation 0, IPS inversion, 240×280, offsets
`0,20,0,0`, RGB565, explicit 40 MHz SPI. The clock is an initial project setting
to validate, not a measured result. No framebuffer, LVGL, PSRAM allocation,
touch, sensor, image asset or new persistent configuration is introduced.

Pin/window/inversion references: the official
[No Touch demo archive](https://files.waveshare.com/wiki/ESP32-S3-LCD-1.69/ESP32-S3-LCD-1.69_DemoCode.zip),
`Arduino-v3.0.5/example/01_HelloWorld/01_HelloWorld.ino` and
`Arduino-v3.0.5/libraries/Mylibrary/pin_config.h`; compare with the
[V2 schematic](https://files.waveshare.com/wiki/ESP32-S3-LCD-1.69/ESP32-S3-LCD-1.69_V2.pdf).
The demo's old framework/library bundle is not installed. The pinned
[ST7789 driver](https://github.com/moononournation/Arduino_GFX/blob/v1.6.7/src/display/Arduino_ST7789.cpp)
and [ESP32 SPI implementation](https://github.com/moononournation/Arduino_GFX/blob/v1.6.7/src/databus/Arduino_ESP32SPI.cpp)
were reviewed and compiled against our existing Arduino 3.3.11 core.

`DisplaySnapshot` is value-only and independent of Arduino/graphics/storage.
The adapter reads the existing typed GPS state, trusted usable speed, daily
distance/date and current `LedState.mode`. Presentation never reparses NMEA,
calculates distance or changes domain policy. The existing bitmap font uses
ASCII labels in this first page:

- GPS: no data, bytes without RMC, searching, insufficient quality, trusted fix
  or stale data, shown explicitly rather than by color alone.
- Speed: one decimal, `km/h`; invalid/nonfinite/negative data show `--`, valid
  zero shows `0.0`. Values above 999.9 show `999+` to preserve layout.
- `DIST. DIA REGISTRADO`: retained daily meters plus recorded date. Unknown
  date is explicit. This is neither trip distance nor automatically “today.”
  Values above 999999 m show `>999 km`; invalid distance shows `-- m`.
- LED: the existing domain mode name; this does not claim which effect is
  currently visible under welcome, Day Mode, scene or alert overrides.

## Scheduling and failure behavior

`display::begin()` runs once at the end of normal boot, after GPS/LEDs and
BLE→Wi-Fi/portal. It performs the driver's fixed initialization sequence and
draws the first complete frame before turning on the backlight. A false driver
return leaves UI disabled operationally (`ready=0`), with no retries or reset
of the collar. The normal queued LCD report records initialization duration.
Internal driver startup waits are confined to initialization; their elapsed
time still needs measurement. Successful write-only SPI does **not** prove
that a panel is attached, oriented correctly or showing pixels.

In the loop GPS remains first. `display::tick()` follows the existing LED/HTTP
work and is included in total/work loop timing. It samples at most once per
second, compares formatted rows, and sends at most **one changed row per loop**.
It finishes a pending sample before taking another. Normal updates do not clear
the full screen or allocate memory. The few rows can briefly represent adjacent
samples while being refreshed; `sample_ms` and the pending-row mask identify the
current update in logs. Expect up to one sample interval plus loop/row latency.
Explicit diagnostic screen changes perform a full redraw and bypass this normal
sampling schedule. No UI operation waits for GPS fix or a USB host.

## Stage-3 USB commands

Monitor at 115200. Commands are single lowercase characters; newlines/unknown
characters are ignored. At most eight input characters are handled per loop.
All commands affect only the LCD service; there is no physical-button UI yet.

| Command | Action |
| --- | --- |
| `t` | Show RGB/white bars, one-pixel outer border, top label and dimensions |
| `v` | Return to real live data, disable demo and redraw; enables backlight when service is running |
| `f` | Explicit LCD-only synthetic cycle labeled DEMO; see [I4 guide](display-i4.md). Never injects GPS data or writes fixture metrics |
| `b` | Toggle backlight; updates continue while it is dark |
| `d` | Toggle the display service; pause turns backlight off, resume redraws the selected page |
| `r` | Reset LCD timing/row counters for a new observation window |

`t`/`v`/`f` do not resume a paused service; use `d` first. These are not I1's LED
commands. They do not alter GPS reception, LED transport or NVS.

Normal bounded serial reports add `[LCD] ready/enabled/light/test`, `sample_ms`,
pending-row bitmask, `init_us`, drawing tick/row counts, `draw_max_us`,
`p95_upper_us` and `tick_max_us`. Percentile is an **upper bound** from drawing
tick buckets at 1/5/10/20/50 ms, using the observed maximum for the overflow
bucket; idle calls do not dilute it. `tick_max_us` includes non-drawing service
calls as well. These are runtime measurements only when obtained on hardware;
synthetic host durations are not performance evidence. Existing loop, heap,
UART and queue-drop diagnostics remain available. Logs use the existing queue,
not a blocking report per frame.

## Physical acceptance

After physical I0, I1 and I2, upload only the identified stage-3 target:

1. Send `t`. Inspect R/G/B/white order, orientation, clipping and border/visible
   window. Rounded panel corners can mask the extreme corner pixels. Record
   any offset/inversion problem before adjusting the profile.
2. Send `v`, then `r`. Compare live GPS, speed, recorded distance/date and LED
   mode with the domain/portal at the same sample period. Exercise searching,
   trusted fix, data loss/stale and recovery; invalid speed must be explicit.
3. Run GPS+both strips+LCD for at least 15 minutes, with configuration and power
   recorded. Compare counter deltas: no new UART overflows or unexpected resets.
4. Test `b` separately from `d`; tracking, LEDs and portal must continue with
   backlight off and with the UI paused. Compare matched enabled/paused windows
   and record LCD timings, loop statistics, minimum heap and log drops.
5. After resetting counters outside manual calibration, initial targets remain
   p95 ≤20 ms and maximum ≤50 ms per display tick. A nonzero drawing sample count
   is required. Bucket upper bounds can demonstrate a pass, not an exact p95.
   No timing/power target is declared passed by compilation or host previews.

Known host/build results and unresolved physical gates are in the
[I3 baseline](../../../docs/baselines/display-i3-2026-09-12.md). I4 consolidates
portal/persistence under joint load; LVGL and its simulator remain I5.
