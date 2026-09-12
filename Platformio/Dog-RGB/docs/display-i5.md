# I5 — one shared LVGL walking view

**Software development with USB-only board verification.** The owner confirmed
that the I4 demo was visible and authorized continuing. That validates visible
operation of the previous page, not a separate color-bar/PCB revision check or
the still-open I4 GPS/LED/HTTP joint-load gate. I5 develops the independent
display work while those physical peripherals are unavailable.

## Composition and behavior

A dark green background, off-white speed at 48 px, restrained brand/title,
explicit GPS status, daily distance with recorded date and the real LED mode.
Amber identifies unavailable/untrusted data; valid fix uses the existing mint
color. No decorative images, new metrics, navigation or animation. The large
speed value is the visual anchor; the lower section retains useful context.

`src/display/ui/walk_view.cpp` consumes the existing `TextView`. All validity,
zero-speed, retained-distance and mode-name semantics remain in the same
formatter. The firmware samples at 1 Hz. Labels are updated only if their text
or state color changes. Fixtures are still stage-3-only, marked `RGB DOG / DEMO`,
and never written into the domain or storage. Product starts with live values.

## Stack and execution

- LVGL **8.4.0**, pinned in PlatformIO and checked by the PC build. This is a
  deliberate compatibility pin, not a claim that it is the latest release.
- Existing Arduino_GFX **1.6.7** and ST7789 parameters remain unchanged. One
  panel/SPI owner; native RGB565 `LV_COLOR_16_SWAP=0`. The driver handles wire
  byte order. No DMA, separate rendering task or PSRAM framebuffer.
- One 240×20 RGB565 buffer: **9,600 bytes**, plus a fixed **48 KiB LVGL pool**.
  Font sizes 12/14/20/48; only labels and base objects enabled. Shared config
  lives in `include/lv_conf.h`. Classic and I0–I2 exclude the whole display tree
  and graphics libraries.
- Setup initializes the existing panel, registers the LVGL display and renders
  the first full frame before lighting it. Normal `display::tick()` remains
  after HTTP in the existing cooperative loop. It advances LVGL time from the
  unsigned elapsed milliseconds and calls its handler at most every 5 ms.
- The flush callback writes synchronously and then calls `lv_disp_flush_ready`.
  A 20-row buffer bounds each transfer, **not** an entire `lv_timer_handler`
  call: that call may draw multiple regions. Measure aggregate service time.
- No periodic full refresh; full invalidation occurs only at initialization,
  explicit backend/view commands or service resume. A disabled service makes
  no rendering calls. Backlight-off keeps rendering and domain work active.

Implementation references: [LVGL 8.4 display port](https://github.com/lvgl/lvgl/blob/v8.4.0/examples/porting/lv_port_disp_template.c),
[configuration template](https://github.com/lvgl/lvgl/blob/v8.4.0/lv_conf_template.h).
The [simulator README](../../../tools/display-simulator/README.md) explains the
headless CMake choice and its relation to the official PC/SDL reference.

## Diagnostic controls

`waveshare_lcd169_displaycheck` remains stage 3; no additional application or
stage-5 target. The normal product has no serial display commands.

| Command | Result |
| --- | --- |
| `f` | Explicit LCD-only demo using the selected backend |
| `v` | Live values using the selected backend; clears demo |
| `s` | Simple I3 text page for comparison; retains live/demo selection |
| `l` | LVGL Paseo page; retains live/demo selection |
| `t` | Existing bars/border pattern; suspends LVGL drawing |
| `b` | Backlight toggle; domain and rendering continue |
| `d` | Pause/resume display service; resume redraws the selected page |
| `r` | Reset display draw/service and LVGL flush counters |

Backend/view commands do not resume a paused service. A resumed page is fully
rendered before relighting. Basic text and LVGL never draw concurrently.
The previous I4 flashed image is preserved for comparison/restoration; choosing
`s` compares rendering within the new firmware and does not remove its LVGL RAM.

`[LCD]` adds `ui=text|lvgl`, cumulative `flushes`, `pixels`, `flush_max_us`,
`lv_free` and `lv_largest` (LVGL pool bytes). Existing `draw_ticks`, maximum
aggregate service time and histogram-based p95 apply to either backend.
`rows` counts only the basic text renderer; LVGL uses `flushes`/`pixels` instead.
Pool free space is separate from ESP32 free heap. Reset counters between matched
windows and distinguish manual full-redraw costs from routine updates.

## Verification and next work

Run the [PC renderer](../../../tools/display-simulator/README.md), inspect all
three PNGs, then build Display product, stage 3 and Classic. The CI matrix
retains seven firmware targets; a separate PC job runs the actual UI/service
contracts and uploads captures. A configured job is not a remote passing run.

On the board compare matched demo windows with `l` and `s`, then backlight and
service-off windows. Record image hash, USB power, heap/pool, aggregate service
and flush timings, counter deltas and resets. Return to `l`/`f` for visual review.
Inspect actual colors, orientation and readability; PC captures cannot prove
those properties. GNSS/LED/portal load still requires I4's physical procedure.

Acceptance status, exact results and remaining checks are recorded in the
[I5 baseline](../../../docs/baselines/display-i5-2026-09-12.md).
I6 button/navigation/animation is not part of this increment.
