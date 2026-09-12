# Hardware Area

Dog-RGB's physical design is still a prototype. This directory is reserved for the eventual versioned schematic, BOM, PCB/enclosure assets, and measured validation records.

Current references:

- [Hardware baseline](../docs/phase0_freeze.md)
- [English build guide](../docs/manual_de_construccion.en.md)
- [BOM and power worksheet](../docs/bom_power_budget.md)
- [SK6812 wiring](../docs/sk6812_wiring.md)
- [XIAO pin map](../xiao_s3_pin.md)
- [Proposed Waveshare display variant](../docs/PLANS/2026-09-12_waveshare-display-variant.md) — separate board profile and power/wiring investigation; Classic remains the current baseline, and the physical Waveshare revision is not yet confirmed.
- [Implemented I0–I3 board diagnostics](../Platformio/Dog-RGB/docs/boards.md) — candidate V2 power/USB diagnostics and command-driven RGBW bench checks and normal GPS/LED diagnostics capped at brightness 16/255 and a 1000 mA estimated budget; physical acceptance pending. I2 uses the confirmed external GNSS wiring and normal persistence; it continues without USB. I3 adds ST7789/backlight on the reserved LCD pins, with [bars and physical acceptance procedure](../Platformio/Dog-RGB/docs/display-i3.md). The old estimated-current model is not recalibrated for the lit LCD.
- [Display incremental implementation contract](../docs/PLANS/2026-09-12_display-incremental-delivery.md) — identify revision/power controls before physical bring-up, then LEDs, GPS and simple LCD. Bench acceptance is separate from battery/charging and portable mounting validation; Classic wiring is not a Waveshare wiring recipe.

Do not treat firmware defaults or planning estimates as a finished schematic, safe load rating, waterproofing claim, or runtime guarantee.
