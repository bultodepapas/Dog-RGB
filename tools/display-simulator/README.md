# Shared Display simulator — I6d + VIS-1/2

This optional PC tool builds LVGL **8.4.0** and the firmware's actual
`WalkView`, `ConnectionView`, `StatusView` and formatters with the same `lv_conf.h`. It exports
fourteen 240×280 PNGs: Activity searching/fix/stale and six Connection scenarios
(AP, AP+STA, connecting, disconnected, radio-off, long names), plus five State
scenarios (no-data, Day Mode, alert, paused output, fix). There is no second UI
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
`output/connection-*.png` and `output/status-*.png`. `manifest.json`
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
against read-only stubs; a fifth does the same for the real LED snapshot adapter. Timing from those fakes is not a benchmark.

The [official PC port](https://github.com/lvgl/lv_port_pc_vscode) informed the
CMake/shared-source layout. For this first static increment we use a headless
framebuffer callback instead of SDL: no interactive window/input is needed to
produce the captures. Python only losslessly packages the resulting RGB
pixels into PNG. SDL and temporal capture remain later options, not prerequisites
for the collar. The device uses the same RGB565 partial buffer size but writes
through the existing Arduino_GFX driver.

Full procedure and limits: [I5 guide](../../Platformio/Dog-RGB/docs/display-i5.md).
Current pages/input: [I6d guide](../../Platformio/Dog-RGB/docs/display-i6d.md).

The I6c service contract also checks the opt-in inactivity deadline, rollover,
dark redraws, stale data and disabled boot policy. I6d extends this to thirty
timeout/wake cycles over three pages and thirty full navigation cycles; see
[I6c behavior](../../Platformio/Dog-RGB/docs/display-i6c.md). GPIO tests here use
fake inputs; actual BOOT/wake observations are recorded separately in the baseline.

## Contact QR (VIS-1a)

The sixth CTest builds the firmware `ContactQr` and pure phone formatter. It
checks integer modules, every quiet-zone pixel, max phone length, disabled and
invalid contact recovery, unchanged input causing no generation/flush, and
stable LVGL memory over 30 cycles. `render.py` runs this contract too.

Independent decoding is an optional host tool with pinned Python dependencies:

```powershell
python -m venv tools/display-simulator/build/qr-venv
tools/display-simulator/build/qr-venv/Scripts/python.exe -m pip install -r tools/display-simulator/requirements-qr.txt
tools/display-simulator/build/qr-venv/Scripts/python.exe tools/display-simulator/render_qr.py
```

It exports eight 240×280 PNGs under `output/qr`, decodes each with ZXing and
requires the exact payload/count (zero codes for fallback). Its manifest records
source/image hashes and decoder version. The fixture name/labels only demonstrate
the QR component; final IdentityView typography, Unicode and A/B layout are VIS-1b.
Use `--name` and `--phone` together for a local short ASCII name/international
phone preview. Output contains contact data and is ignored by Git; public fixtures
use intentionally non-dialable synthetic numbers. No URLs are opened.

Evidence and remaining hardware measurements:
[VIS-1a baseline](../../docs/baselines/display-vis1a-2026-09-12.md).

## Identity layouts (VIS-1b)

The seventh CTest builds `IdentityView`, the bounded UTF-8 name formatter and
the generated fonts. It keeps the three I6d views allocated, then verifies safe
bounds, pairwise overlap, complete text, every supported glyph, 24-character
extremes, decomposed/invalid Unicode, QR caching and 30 update cycles.
NFC input is accepted for the explicit alphabet; decomposed accents return
`NeedsNormalization`. Portal normalization/persistence remains VIS-2.

Using the QR decoder environment above:

```powershell
tools/display-simulator/build/qr-venv/Scripts/python.exe tools/display-simulator/render_identity.py
```

Thirteen public fixtures go to `output/identity-4bpp`: short/accent/ñ names,
maximum phone, long/unbroken/word-wrapped names, disabled QR, invalid/empty data,
NFD/unsupported names and recovery. Each image is independently checked for its
exact QR payload or absence of codes. Add `--name` and `--phone` for a local
preview; those captures are not public test defaults.

The optional [font recipe](../display-fonts/README.md) enables `--bpp 2` for the
alternative font build. This separate build leaves the normal 4 bpp CMake cache
intact. [VIS-1b baseline](../../docs/baselines/display-vis1b-2026-09-12.md).

## Identity storage and HTTP (VIS-2)

`render.py` now runs ten CTest contracts. Three additions compile the real
identity store with an in-memory Preferences transport and the actual HTTP
handler source with a recording WebServer/guard for Display and Classic.
ArduinoJson comes from the pinned Display diagnostic dependencies above.
These test software persistence and request/response behavior, not ESP32 NVS
power loss, radio or HTTP timing. The ordinary portal guard tests remain separate.

Covered: alternating 84-byte A/B records, CRC/canonical data rejection, reboot,
partial/corrupt/rejected writes, indeterminate readback error, no-op, generation
conflict/rollover, clear and unsupported Classic. Handler extraction follows
the current source at CMake configuration; changed boundaries fail explicitly.

Portal draft/NFC tests: `npm run webui:unit`; browser checks use the generated
page and existing API fixtures. [API contract](../../docs/display-identity-api.md)
and [VIS-2 evidence](../../docs/baselines/display-vis2-2026-09-12.md).
