# Day Mode

**Status:** Current implemented behavior. The filename is retained for compatibility with earlier Spanish links.

Day Mode reduces LED load during daylight hours without stopping GNSS, metrics, route recording, storage, Wi-Fi, the portal, or status alerts.

## State table

| Condition | Effect pixels | Status pixels | Tracking/portal |
| --- | --- | --- | --- |
| Disabled | Normal selected mode | Normal | Normal |
| Enabled, trusted local time 06:00–15:59 | Off | Normal | Normal |
| Enabled, outside window | Normal selected mode | Normal | Normal |
| Enabled, time missing/stale | Normal selected mode | Normal | Normal |
| Startup welcome | Welcome animation | Welcome animation | Boot continues |

The interval is start-inclusive/end-exclusive: `06:00 <= local time < 16:00`.

## Trusted time

RMC time is UTC and uses a fixed offset:

- `DAY_MODE_TZ_OFFSET_MIN = -300` (America/Bogota, UTC-5);
- `DAY_MODE_START_MIN = 360`;
- `DAY_MODE_END_MIN = 960`;
- `DAY_MODE_TIME_STALE_MS = 300000`.

Time is available only when the firmware has a nonzero accepted GNSS date, received time from a trusted fix, and that observation is no more than five minutes old. There is no network time or daylight-saving database.

`day_mode::state_name()` returns:

- `disabled` — user setting off;
- `waiting_time` — setting on but trusted recent time unavailable;
- `day` — active window, body effects off;
- `night` — outside window, normal effects.

The fail-open `waiting_time` behavior avoids an incorrect/stale clock silently darkening the collar.

## Configuration and diagnostics

- Toggle: `/config` or `day_mode.enabled` in `POST /api/config`.
- Fixed window/offset: reported by `GET /api/config`.
- Live state: `/api/status` and `/api/dev`.
- Persistence: runtime config schema version 6, CRC-protected A/B record with schema-5 migration.

## Rendering integration

The welcome animation finishes before the gate. Speed, Geofence, Show, and Simple each check Day Mode before normal effect rendering. When active, shared status rendering clears the strips and repaints only Wi-Fi/GNSS/critical status pixels.

Host contract tests verify the declaration, trusted-time window, status preservation, and welcome ordering. Wokwi `modes` exercises the integrated behavior; physical energy savings still require measurement.
