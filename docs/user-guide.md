# Dog-RGB User Guide

**Status:** Current user-facing behavior, verified against the active firmware on 2026-08-13.

Dog-RGB works without an app or cloud account. Use the LEDs for quick status and the collar's local Wi-Fi portal for metrics, route history, configuration, and diagnostics.

## Safety before use

- Complete the [bench-test sequence](manual_de_construccion.en.md#bench-acceptance-checklist) before putting the collar on a dog.
- Treat the battery, charger, BMS, boost converter, wiring, enclosure, and seals as unvalidated until physically measured and inspected.
- Do not charge the collar while it is being worn.
- Do not use it in rain or immerse it unless the finished enclosure and every cable entry have been validated for the intended exposure.
- Check surface temperature, cable strain, sharp edges, and fit. You should be able to place a finger comfortably between collar and neck.
- The lights improve visibility; they are not a substitute for a leash, identification tag, or certified tracker.

## First start

1. Move outdoors with a clear view of the sky.
2. Power the collar on. The welcome animation runs before the normal LED mode.
3. Look for the `DogRGB` Wi-Fi network. On first use, join it with `Dog12345`.
4. Accept the captive-portal prompt, or open `http://192.168.4.1/` manually.
5. Change the default AP password on the Wi-Fi/configuration page.
6. Wait for the GNSS status pixel to change from searching to a trusted fix before evaluating activity metrics.

The E108-GN02 may take tens of seconds or longer for a first fix. Indoors, near buildings, or near the boost converter and LED power wiring, acquisition can be slower or impossible.

## LED status

With the default two-strip layout, the first two pixels of each strip are reserved for status in every normal mode, including Simple.

| Pixel | Appearance | Meaning |
| --- | --- | --- |
| Wi-Fi (0) | Solid green | Station connection is ready |
| Wi-Fi (0) | Pulsing green | Station connection is in progress |
| Wi-Fi (0) | Solid/pulsing yellow | Access point is active; pulse indicates a connected AP client |
| Wi-Fi (0) | Solid red | Station connection failed and AP fallback is available |
| Wi-Fi (0) | Amber double pulse | Explicit/experimental Wi-Fi-OFF state |
| GNSS (1) | Solid blue | Trusted GNSS fix is available |
| GNSS (1) | Pulsing blue | Searching or current quality gates are not satisfied |
| Both | Fast red flash | Neither trusted GNSS nor station connectivity has been healthy for the critical timeout |
| Both | Red pulse | Valid Geofence distance is at or beyond the configured maximum boundary |

Effects now use only the semantic body region, so Simple and the retained homogeneous-eligibility state do not hide status. Day Mode preserves the same status pixels while the body is off. A new System/Geofence alert interrupts an in-progress body transition at the next LED update.

See [LED UI](led_ui_spec.md) and [Color reference](color-reference.md) for the complete priority rules.

## Portal pages

| Path | Purpose |
| --- | --- |
| `/` | Daily metrics, current/completed sessions, and route preview/export |
| `/wifi` | Nearby-network scan, home-network credentials, AP name/password, and connection state |
| `/config` | LED mode, brightness, advanced LED power profile, Day Mode, GNSS gates, geofence home, effects, and optional write PIN |
| `/dev` | Technical health, LED current estimate, scene store/player counters, storage state, loop timing, GNSS parser statistics, and raw JSON |

When station mode is connected, use `http://<configured-mdns>.local/` (default `http://dog-collar.local/`) or the station IP shown by the portal. mDNS support depends on the client operating system and network.

## LED modes

- **Speed** maps ten speed ranges to a color and independently configurable effect for strips A and B.
- **Geofence** maps distance from the stored Home point into ten ranges. Home is set automatically after a stable fix when none exists, or manually from the current trusted position.
- **Show** shuffles the four built-in scenes plus each eligible saved user scene. Every eligible scene appears once per bag, with no immediate repeat between bags, and changes after roughly 30 seconds of visible playback using its own transition.
- **Simple** applies one effect and RGB base color to the body while retaining status.

Brightness is 1–255 and defaults to 77 (about 30% of the software range). A global estimated-current limit is enabled by default at 1,000 mA total, with a provisional 200 mA base and 20/20 mA RGB/W channel profile. It scales both strips uniformly. This is a model rather than a sensor: higher budgets and profile changes still require physical current, rail, and temperature validation.

### Scenes

The built-in scene catalog is deliberately small:

| Scene | Visual recipe | Relative body level |
| --- | --- | ---: |
| Alta visibilidad | Chase + Safety Amber | 255 |
| Calmado | Breath + Night Red | 110 |
| Activo | Comet + Forest | 200 |
| Fiesta | Rainbow + Pride | 180 |

Four additional user slots can be saved, exported, and imported through the [local HTTP API](api-reference.md#led-scenes-api-v1). Applying a scene manually is volatile: it does not change the configured mode or write flash, and it lasts until cancelled, another scene is applied, the LED mode changes, or the collar reboots. Day Mode can temporarily hide it and alerts remain visible.

The embedded `/config` page includes a capabilities-driven scene and palette editor. It can apply built-ins, copy or edit the four user slots, control Show eligibility, save/delete, and export/import the complete user bank. Its dual-strip collar preview is intentionally approximate; it does not claim exact RGBW, power, or physical-strip behavior. Custom clients should still read capabilities/catalog first and use the documented generation value when saving, deleting, or importing.

### Day Mode

Day Mode is optional and off by default. When enabled, trusted GNSS time is converted to America/Bogota (UTC-5). Between 06:00 inclusive and 16:00 exclusive, effect pixels turn off while status LEDs, GNSS, route recording, storage, Wi-Fi, and the portal continue normally.

If GNSS time is missing or stale, the state is `waiting_time` and effects remain on. This fail-open behavior avoids an incorrect clock silently darkening the collar.

## Metrics and route history

The dashboard reports today's distance, average active speed, maximum valid speed, GNSS state, and session history. Average speed is derived from accumulated distance and active time, not the full wall-clock day.

Route recording keeps a rolling window of approximately two hours at a nominal five-second interval (up to 1,440 points). From the dashboard, select the current route or one of the retained completed sessions and export:

- JSON coordinate arrays for the portal and custom tooling;
- CSV rows with date, minute-of-day, latitude, and longitude;
- GeoJSON `FeatureCollection` with a `LineString`.

An export is a local snapshot. Corrupt/truncated route chunks are rejected independently, and a disconnected client stops further output work.

## Wi-Fi behavior

The access point is intentionally dynamic to save energy:

- no trusted GNSS fix forces AP availability;
- remaining at or below 2.0 km/h for about two minutes can enable it;
- a newly started AP is held for 15 minutes;
- portal activity extends availability for five minutes;
- no clients/activity for ten minutes allows the AP to turn off;
- idle shutdown stops SoftAP; the current automatic policy does not force the complete radio into its retained OFF state;
- failures use bounded exponential retry instead of retrying every loop.

Use the Wi-Fi page's explicit **Scan** action to list up to 20 unique visible networks. Scanning hops channels and may briefly disturb the AP connection, so it never runs on a timer. Open networks are marked before saving.

## Optional portal PIN

The configuration page can enable a 4–8 digit PIN for write actions. It is off by default to keep DIY recovery simple.

- The PIN protects configuration, reset, Home, Wi-Fi, scan, AP, lock, and every scene mutation including volatile apply/cancel.
- It does not encrypt traffic and does not hide read-only dashboard/diagnostic data from a client already on the local network.
- A corrupt PIN record fails open so the local configuration surface cannot become permanently unreachable.
- Changing or disabling the PIN requires the current PIN while the lock is enabled.

## Reset behavior

**Restore defaults** resets the runtime LED/GNSS/AP/mDNS configuration through the same validated A/B persistence path. It does not erase user scenes, route history, daily metrics, completed sessions, station credentials, or the separately stored Home record. Scene slots use their own A/B store and must be deleted/imported through the scene API.

Changing the AP name or password schedules an AP restart and disconnects the current phone. Rejoin with the new credentials.

## BLE behavior

The read-only BLE summary code and 16-byte wire format exist, but production builds set `BLE_ENABLED = false`. The XIAO ESP32-S3 shares one antenna between BLE and Wi-Fi; enabling BLE can destabilize SoftAP visibility. Treat BLE as an experimental compile-time option, not a normal user workflow.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| `DogRGB` is missing | Confirm power and boot logs; move outdoors so the no-fix policy requests AP; reboot once; inspect `/dev` after recovery for AP retry counters |
| Captive portal did not open | Browse directly to `http://192.168.4.1/`; disable mobile-data auto-switching for the no-Internet network if needed |
| Station connection fails | Rescan, verify SSID/password, and reconnect to the AP fallback; open networks use an empty password |
| `dog-collar.local` fails | Use the station IP shown on `/wifi` or `/dev`; some networks/clients block mDNS |
| Metrics remain empty | Test outdoors, check the blue status pixel, satellites, fix quality, HDOP, and GGA/RMC ages on `/dev` |
| Distance jumps or stays flat | Review rejected-segment and speed-usability diagnostics; overly strict GNSS gates or poor sky view can reject motion |
| LEDs flicker/reset the board | Stop use and inspect 5 V sag, grounds, level shifting, data resistors, bulk capacitance, boost capacity, and wiring temperature |
| A write returns `403 csrf` | Custom API clients must send `X-Dog-Portal`; the built-in UI already does |
| A write returns `401 locked` | Supply the configured PIN via the UI or `X-Dog-Pin`; read-only requests do not need it |
| A scene write returns `409 generation_conflict` | Read `/api/v1/led/scenes` again and retry deliberately with its current generation; another client changed the bank |
| User scenes are unavailable but built-ins work | Inspect `scene_store.health` and recovery counters on `/api/dev`; do not force recovery until the store state is understood |

For exact routes and payloads, see the [Local HTTP API](api-reference.md). For diagnostics and test procedures, see [Testing and simulation](testing.md).
