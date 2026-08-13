# Configuration Parameters

**Status:** Current defaults and ownership, verified against `include/config.h` and `RuntimeConfig` on 2026-08-12.

Dog-RGB has two configuration classes:

- **Compile-time constants** describe physical hardware, hard limits, scheduler timing, safety fallbacks, and diagnostic builds. They require a rebuild/flash.
- **Runtime settings** are user-facing fields validated and stored as a schema-versioned A/B record. They are editable through `/config` or `/api/config`.

## Compile-time hardware defaults

| Constant | Default | Meaning |
| --- | ---: | --- |
| `LED_STRIP_MODE` | `2` | One or two independently driven strips |
| `LED_STRIP_COUNT` | `24` | Pixels per strip |
| `LED_STATUS_COUNT` | `2` | Reserved leading status pixels per strip |
| `LED_BUS_A_REVERSED` | `false` | Physical body orientation for bus A |
| `LED_BUS_B_REVERSED` | `true` | Physical body orientation for bus B; inherited software baseline pending collar validation |
| `LED_LAYOUT_MIRROR_DEFAULT` | `true` | Mirror one logical body across both buses when their effects match |
| `LED_TRANSITION_MS` | `500` | Normal body crossfade duration; status/alerts bypass it |
| `LED_BRIGHTNESS` | `77` | Fallback/runtime default, about 30% of 255 |
| `LED_POWER_LIMIT_ENABLED_DEFAULT` | `true` | Enable the global two-bus estimated-current ceiling |
| `LED_POWER_BUDGET_MA_DEFAULT` | `1000` | Provisional whole-device budget until bench calibration |
| `LED_BASE_CURRENT_MA_DEFAULT` | `200` | Provisional non-LED current included in the model |
| `LED_RGB_CHANNEL_MA_DEFAULT` | `20` | Estimated current at 255 for each R/G/B channel |
| `LED_WHITE_CHANNEL_MA_DEFAULT` | `20` | Estimated current at 255 for the white channel |
| `LED_UPDATE_MS` | `50` | Effect-state update cadence |
| `GPS_BAUD` | `9600` | GNSS UART baud rate |
| `GPS_RX_BUFFER_SIZE` | `16384` | UART margin for slow synchronous HTTP/storage work |
| `GPS_SAMPLE_MS` | `1000` | Metric/route observation cadence |
| `GPS_ACTIVE_MAX_GAP_MS` | `3000` | Longest active-time interval bridged |
| `SAVE_INTERVAL_MS` | `60000` | Normal metric/session persistence interval |
| `BLE_ENABLED` | `false` | BLE summary compile-time gate |

Pin assignments live in `include/pins.h`:

- strip A: GPIO1 / D0;
- strip B: GPIO2 / D1;
- external status: GPIO3 / D2;
- GNSS ESP TX: GPIO43 / D6;
- GNSS ESP RX: GPIO44 / D7.

Change hardware constants only when the assembled design changes, then update the build/wiring docs and Wokwi diagram together.

## Runtime defaults

The current runtime schema version is `6`.

| Field | Default | Validation / notes |
| --- | --- | --- |
| `mode` | `speed` | `speed`, `geofence`, `show`, or `simple` |
| `led.brightness` | `77` | `1..255`; higher values require physical current/thermal validation |
| `led.power.enabled` | `true` | Boolean; advanced lab override may disable the model ceiling |
| `led.power.budget_ma` | `1000` | `250..5000`; whole-device estimate |
| `led.power.base_current_ma` | `200` | `0..1500` and strictly below budget |
| `led.power.rgb_channel_ma` | `20` | `1..40` mA at channel value 255 |
| `led.power.white_channel_ma` | `20` | `1..40` mA at channel value 255 |
| `day_mode.enabled` | `false` | Boolean; window/timezone remain compile-time constants |
| `fence_max_m` | `300` | `50..5000` m; divided into ten equal bands |
| `speed_ranges_kph` | `2,4,6,8,10,12,14,16,18` | Nine positive strictly increasing thresholds |
| `effects.rangeN.a/b` | `7` (`JUGGLE`) | Effect IDs `0..11` |
| `effects.rangeN.speed` | `40..200` | `0..255`, increasing defaults by range |
| `effects.rangeN.intensity` | `80..200` | `0..255`, increasing defaults by range |
| `single.effect` | `0` (`SOLID`) | `0..11` |
| `single.speed` | `80` | `0..255` |
| `single.intensity` | `140` | `0..255` |
| `single.rgb` | `(0,60,60)` | Each channel `0..255` |
| `wifi.ap_ssid` | `DogRGB` | 1–32 bytes |
| `wifi.ap_pass` | `Dog12345` | Empty only for explicit open AP; otherwise 8–63 characters |
| `wifi.mdns` | `dog-collar` | 1–32 letters/digits/hyphens |
| `gps.min_fix_quality` | `1` | `0..8` |
| `gps.min_sats` | `6` | `3..12` |
| `gps.max_hdop` | `2.5` | `0.5..20.0` |
| `gps.max_gga_age_ms` | `2000` | `500..10000` ms |
| `gps.min_segment_m` | `3.0` | `0.5..20.0` m |
| `gps.hdop_factor` | `2.0` | `0.0..5.0` |
| `gps.max_min_segment_m` | `10.0` | `1.0..50.0` m |

The effective minimum distance segment adapts with HDOP, bounded by `min_segment_m` and `max_min_segment_m`. Speed above `SPEED_MAX_VALID_KPH` (40 km/h) is rejected independently.

The LED current values are a conservative software model, not measurements or component ratings. Schema-5 records are retained and migrated with these defaults; calibrate them against the exact physical revision before increasing the budget.

## Fixed behavior constants

| Group | Current values |
| --- | --- |
| Day Mode | 06:00 inclusive to 16:00 exclusive; UTC-5; trusted time stale after 300 s |
| Show Mode | 12 effects; 30 s per effect; base speed 150; intensity 200 |
| Geofence | auto-Home after 10 s stable fix; 3% hysteresis with 5 m minimum |
| AP boot | channel 1 when not constrained by STA; max 2 clients; 3 boot attempts |
| Station retry | 10 s initial/watchdog interval, bounded to 5 minutes |
| AP retry | 1 s initial exponential backoff, bounded to 30 s |
| AP holds | 15 min after start; 5 min after portal activity; 10 min no-client idle timeout |
| Stationary trigger | enter at `<=2.0 km/h` for 2 min; leave at `>=2.5 km/h` |
| Homogeneous eligibility | retained after explicit Wi-Fi-OFF + stable GNSS for 5 min; semantic status remains reserved and automatic AP idle shutdown does not enter Wi-Fi OFF |
| Critical status | no trusted GNSS and no station success for 10 min |

## Diagnostic compile-time switches

- `LED_DEBUG_BRIGHTNESS_ENABLED` overrides persisted brightness with the low diagnostic value.
- `DEBUG_AP_ONLY_MINIMAL` starts only AP + portal, excluding GNSS, LEDs, BLE, and station mode.
- `DOG_RGB_WOKWI_SIM` selects simulation UART routing/console speed and transport behavior through the `wokwi` PlatformIO environment.

These switches are for isolation/testing and should not become hidden runtime product modes.

## Separately persisted state

The following are not part of the runtime configuration record and are not erased by `/api/config/reset`:

- station SSID/password;
- Home/geofence coordinate and source;
- optional portal PIN;
- daily metrics and completed-day journal;
- current/completed session summaries;
- route history.

See [Runtime configuration](portal_config.md) for JSON and [Architecture](architecture.md#persistence-model) for storage/recovery.
