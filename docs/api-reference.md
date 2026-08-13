# Dog-RGB Local HTTP API

**Status:** Current firmware contract, verified on 2026-08-12.

The ESP32-S3 serves a synchronous, local-only HTTP API on port 80. There is no TLS, cloud gateway, CORS API, or user-account layer. Use it only on a trusted local network or the collar's own AP.

## Base addresses

- SoftAP: `http://192.168.4.1`
- Station/mDNS default: `http://dog-collar.local`
- Station IP: available from `/api/status`, `/api/dev`, or the Wi-Fi page

All normal responses include `Cache-Control: no-store`, `X-Content-Type-Options: nosniff`, `X-Frame-Options: DENY`, and `Referrer-Policy: no-referrer`.

## Write guards

Every `POST` route requires this same-origin intent header:

```http
X-Dog-Portal: 1
```

If the optional portal lock is enabled, also send:

```http
X-Dog-Pin: 1234
```

The CSRF header is required regardless of whether the PIN is enabled. Missing intent returns `403 {"status":"error","reason":"csrf"}`; a missing or wrong enabled PIN returns `401 {"status":"error","reason":"locked"}`.

## HTML pages

| Method | Path | Description |
| --- | --- | --- |
| GET | `/` | Dashboard, session list, route preview, and export controls |
| GET | `/wifi` | AP/STA setup and on-demand nearby-network scan |
| GET | `/config` | Runtime configuration, Home, and optional write-lock UI |
| GET | `/dev` | Human-readable diagnostics and raw diagnostic JSON |

Unknown non-API paths redirect relatively to `/`. Captive probes at `/generate_204`, `/gen_204`, `/hotspot-detect.html`, `/library/test/success.html`, `/ncsi.txt`, and `/connecttest.txt` return a small portal link/redirect page.

## API route summary

| Method | Path | Guard | Purpose |
| --- | --- | --- | --- |
| GET | `/api/summary` | Read-only | Daily metrics, current session, last completed day, and up to three completed sessions |
| GET | `/api/status` | Read-only | Compact Wi-Fi, GNSS, Home, mode, and Day Mode state |
| GET | `/api/dev` | Read-only | Detailed health, counters, storage recovery, LED power estimate, and loop timing |
| GET | `/api/track` | Read-only | Stream route snapshot as JSON |
| GET | `/api/track.csv` | Read-only | Stream route snapshot as CSV |
| GET | `/api/track.geojson` | Read-only | Stream route snapshot as GeoJSON |
| GET | `/api/config` | Read-only | Current runtime configuration without passwords |
| POST | `/api/config` | CSRF + optional PIN | Validate, persist, and apply runtime configuration |
| POST | `/api/config/reset` | CSRF + optional PIN | Persist compile-time runtime defaults |
| GET | `/api/lock` | Read-only | Report whether the optional write lock is enabled |
| POST | `/api/lock` | CSRF + optional PIN | Enable/change/disable the write PIN |
| GET | `/api/home` | Read-only | Stored and current coordinates plus distance |
| POST | `/api/home/set` | CSRF + optional PIN | Store current trusted GNSS coordinate as Home |
| POST | `/api/home/clear` | CSRF + optional PIN | Persist an explicit unset Home state |
| POST | `/api/wifi` | CSRF + optional PIN | Save station credentials and start station mode |
| POST | `/api/wifi/ap` | CSRF + optional PIN | Save AP/mDNS settings and schedule restart if needed |
| POST | `/api/wifi/scan` | CSRF + optional PIN | Start an asynchronous, request-driven scan |
| GET | `/api/wifi/scan` | Read-only | Poll scan state and consume available results |

Known API paths called with the wrong method return `405`; unknown `/api/*` paths return `404`, both as JSON errors.

## Summary

`GET /api/summary` uses meters, centimeters per second, seconds, and minutes since UTC midnight:

```json
{
  "date": 20260812,
  "distance_m": 1843,
  "avg_speed_cmps": 176,
  "max_speed_cmps": 612,
  "last_update_min": 643,
  "gps_fix": true,
  "gps_raw_fix": true,
  "gps_quality_ok": true,
  "has_data": true,
  "last_completed_day": null,
  "history": [],
  "session_current": {
    "flags": 1,
    "start_date": 20260812,
    "start_min": 602,
    "end_date": 20260812,
    "end_min": 643,
    "distance_m": 1843,
    "avg_speed_cmps": 176,
    "max_speed_cmps": 612,
    "active_s": 1610
  }
}
```

`history` contains at most three completed session summaries. `last_completed_day` is the CRC-protected day-boundary journal entry when available. Clients must tolerate additive diagnostic fields.

## Status

`GET /api/status` is the preferred lightweight polling endpoint:

```json
{
  "mode": "speed",
  "wifi": {
    "ap_enabled": true,
    "ap_ssid": "DogRGB",
    "ap_stations": 1,
    "sta_connected": false,
    "sta_connecting": false,
    "wifi_off": false,
    "mdns": "dog-collar",
    "sta_ip": "0.0.0.0",
    "ap_ip": "192.168.4.1"
  },
  "gps": {
    "fix": true,
    "raw_fix": true,
    "quality_ok": true,
    "speed_usable": true,
    "speed_kph": 4.2,
    "sats": 9,
    "fix_quality": 1,
    "hdop": 1.1
  },
  "home": {"set": true, "source": "manual", "distance_m": 37.4},
  "day_mode": {
    "enabled": false,
    "active": false,
    "state": "disabled",
    "time_available": true,
    "local_min": 343
  }
}
```

`hdop`, `distance_m`, and `local_min` use `-1` when unavailable. `/api/dev` is intentionally larger and intended for humans/test tooling rather than frequent polling.

### LED power diagnostics

`/api/dev` includes the current model under `led.power`:

```json
{
  "enabled": true,
  "budget_ma": 1000,
  "base_current_ma": 200,
  "rgb_channel_ma": 20,
  "white_channel_ma": 20,
  "requested_ma": 1160,
  "estimated_ma": 999,
  "peak_requested_ma": 1160,
  "scale": 212,
  "scale_percent": 83,
  "frames_limited": 42,
  "estimate_only": true
}
```

`requested_ma` is the pre-limit estimate and `estimated_ma` is the post-limit estimate for the last transported frame. `peak_requested_ma` and `frames_limited` accumulate since boot. These values are not sensor readings.

## Route exports

All three endpoints accept:

- `session=current` or omitted: current/open route;
- `session=0`, `1`, or `2`: a retained completed-session slot;
- `max_points=1..2000`: optional output cap; invalid/zero/omitted values mean the available snapshot limit.

Example:

```text
GET /api/track?session=current&max_points=500
```

JSON response shape:

```json
{
  "count": 64,
  "open": true,
  "sample_ms": 5000,
  "start_date": 20260812,
  "start_min": 602,
  "end_date": 20260812,
  "end_min": 643,
  "bbox": {
    "min_lat": 18.4849,
    "max_lat": 18.4882,
    "min_lon": -69.9331,
    "max_lon": -69.9293
  },
  "points": [[18.4851, -69.9328], [18.4854, -69.9324]]
}
```

No route returns `{"count":0,"status":"no_data"}` for JSON, a header-only CSV, or an empty GeoJSON `FeatureCollection`. Streams use bounded 768-byte chunks and service GNSS around socket writes.

## Runtime configuration

`GET /api/config` and `POST /api/config` are documented in [Runtime configuration](portal_config.md). The current schema version is `6`. Password values are never returned; boolean `has_ap_pass` and `has_sta_pass` fields report their presence.

## Home

`GET /api/home`:

```json
{
  "home_set": true,
  "home_source": "manual",
  "home_lat": 4.711,
  "home_lon": -74.0721,
  "gps_fix": true,
  "current_lat": 4.7112,
  "current_lon": -74.0718,
  "distance_m": 37.4
}
```

`POST /api/home/set` has no body and uses the current trusted coordinate. It returns `400 no_gps` when unavailable. Set and clear operations are transactional A/B-record updates.

## Wi-Fi

`POST /api/wifi` uses URL-encoded form fields:

```text
ssid=HomeNetwork&pass=secret
```

Station SSIDs are 1–32 bytes. Passwords may be empty for an open network; the validator also accommodates legacy short keys and future raw 64-byte PSKs. A successful save persists SSID/password as one CRC-protected A/B record before starting station mode.

`POST /api/wifi/ap` uses JSON:

```json
{
  "ap_ssid": "DogRGB",
  "ap_pass": "Dog12345",
  "ap_open": false,
  "mdns": "dog-collar"
}
```

Omit an empty `ap_pass` to retain the existing protected password. Set `ap_open: true` to clear it deliberately. A response with `"wifi_restart":true` means the client must reconnect.

Scan flow:

1. `POST /api/wifi/scan` returns `{"status":"scanning"}` or `503 radio`.
2. Poll `GET /api/wifi/scan` for `idle`, `scanning`, `failed`, or `ready`.
3. A ready result includes up to 20 unique, non-hidden networks as `{ssid, rssi, open}` and the driver's original `total` count.
4. Reading a ready result releases the driver's scan buffer; start another scan for fresh results.

## Optional write lock

`GET /api/lock` returns `{"enabled":true|false}`.

Enable or change it:

```json
{"enabled": true, "pin": "1234"}
```

Disable it:

```json
{"enabled": false, "pin": ""}
```

The PIN must contain 4–8 ASCII digits. When already enabled, include the current value in `X-Dog-Pin` to authorize either operation.

## Error model

Most JSON endpoints use:

```json
{"status":"error","reason":"machine_readable_reason"}
```

Common status codes:

| Code | Meaning |
| ---: | --- |
| 200 | Successful read/write, including explicit no-data route responses |
| 400 | Invalid JSON, field, range, mode, coordinate state, or request argument |
| 401 | Optional write lock rejected the PIN |
| 403 | Required `X-Dog-Portal` intent header is missing |
| 404 | Unknown API path |
| 405 | Known API path with unsupported method |
| 500 | Persistent storage write/verification failed; in-memory previous state is restored |
| 503 | Wi-Fi scan could not start because the radio was unavailable |

Do not parse human portal copy as an API. Prefer documented fields and tolerate new diagnostic fields.
