# I6d — State page

Experimental third page: **Actividad → Wi-Fi → Estado → Actividad**. Short BOOT
release navigates; the first release while dark wakes the selected page. The
[I6c timer](display-i6c.md) remains stage-3 opt-in, disabled on every boot.
See [evidence](../../../docs/baselines/display-i6d-2026-09-12.md) and the
[governing plan](../../../docs/PLANS/2026-09-12_display-incremental-delivery.md).

## Data contract

- GPS reuses the same reception classification as Activity: no data, receiving,
  searching, untrusted, current fix or stale. Only current fix is green.
- `LedStatusSnapshot` copies the active LED policy and transport-enabled flag.
  It does not change modes, transport, radio, configuration or storage.
- Intent, body-effect enablement and alert are separate. Day Mode can coexist
  with an alert; `Aviso GPS / Wi-Fi` describes the combined domain condition.
- `Salida pausada` means transmission is suspended. No physical strip response,
  applied brightness, current consumption or hardware health is inferred.
- Storage health, battery and satellite counts are omitted: this increment has
  no validated current-health, calibrated-battery or fresh-GGA contract.
- Stage-3 `f` simulates display GPS only. State says `RGB DOG / GPS DEMO`;
  LED policy and Wi-Fi remain real. PC-only fixtures may simulate LED states.

## Rendering and controls

Two groups, GPS and `Luces / control`, on black, with bounded text and unchanged
values skipped. Three page containers are created once. The shared partial
buffer, pool, SPI rate, driver and instant page changes remain as in I6a.

Stage 3 adds `e` to select State directly; `a/c` select Activity/Wi-Fi and `n`
injects the same short-release action as BOOT. Direct selection is a bench
control and does not renew inactivity. `i/o` enable/disable the 30 s timer.
Serial commands are absent from the product build.

## Reproduce

From repository root: `python tools/display-simulator/render.py`. Five CTest
contracts export fourteen PNGs. Added checks cover six GPS states, all LED
intents, unknown enums, Day Mode with alerts, paused transport, stable memory,
unchanged-state no-flush, thirty three-page navigation cycles and thirty
timeout/wake cycles across all three pages. The fifth test compiles the actual
LED snapshot adapter against read-only stubs.

From `Platformio/Dog-RGB`:

```powershell
python -m unittest discover -s test -p 'test_*.py' -v
pio run -e seeed_xiao_esp32s3 -e waveshare_lcd169 -e waveshare_lcd169_displaycheck
```

Classic must remain free of graphics symbols. PC captures establish composition,
not panel contrast, visible latency, LED output or GPS reception. Joint-load
V3 and portable electrical acceptance remain open.
