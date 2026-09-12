# Shared Display simulator — I5 / I6a

This optional PC tool builds LVGL **8.4.0** and the firmware's actual
`WalkView`, `ConnectionView` and formatters with the same `lv_conf.h`. It exports
nine 240×280 PNGs: Activity searching/fix/stale and six Connection scenarios
(AP, AP+STA, connecting, disconnected, radio-off, long names). There is no second UI
implementation in HTML, Python or a drawing mock.

Requirements: Python 3, CMake ≥3.20, Ninja and a C/C++17 compiler. Resolve the
pinned dependency once from `Platformio/Dog-RGB`:

```powershell
pio pkg install -e waveshare_lcd169_displaycheck
```

Then, from the repository root:

```powershell
python tools/display-simulator/render.py
```

Open `output/searching.png`, `output/fix.png`, `output/stale.png` and
`output/connection-*.png`. `manifest.json`
records LVGL version, configuration/source hashes and PNG hashes. The runner
returns nonzero for failed builds or contracts. The default build uses the
compiler CMake discovers; `CC`/`CXX` may select one before first configuration.
For a separate LVGL checkout, configure CMake with `-DLVGL_SOURCE_DIR=...`;
its metadata must specify 8.4.0. Firmware dependencies remain authoritative.

The static fixture test checks bounds, pairwise overlap and wrapped text including long/invalid values,
valid zero, GPS trust states, retained daily distance/date, LED modes, unchanged
values causing no flush, demo labeling and stable pool usage after repeated
updates. Two additional tests compile the real display service and LVGL port
with fake Arduino/SPI: initial full frame before backlight, init failure without
retries, bounded commands, pause/backlight independence, text/LVGL switching,
fixture isolation, page changes, BOOT bounce/long holds, wake-only first click
and clock rollover. A fourth contract compiles the real Wi-Fi snapshot adapter
against read-only stubs. Timing from those fakes is not a benchmark.

The [official PC port](https://github.com/lvgl/lv_port_pc_vscode) informed the
CMake/shared-source layout. For this first static increment we use a headless
framebuffer callback instead of SDL: no interactive window/input is needed to
produce the captures. Python only losslessly packages the resulting RGB
pixels into PNG. SDL and temporal capture remain later options, not prerequisites
for the collar. The device uses the same RGB565 partial buffer size but writes
through the existing Arduino_GFX driver.

Full procedure and limits: [I5 guide](../../Platformio/Dog-RGB/docs/display-i5.md).
Current pages/input: [I6a guide](../../Platformio/Dog-RGB/docs/display-i6.md).
