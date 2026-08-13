# Dog-RGB Local HTTP API

**Status:** Current firmware contract, verified on 2026-08-13.

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
| GET | `/api/v1/led/state` | Read-only | Current selected LED intent, priority, effects, color, and limiter snapshot |
| GET | `/api/v1/led/capabilities` | Read-only | Versioned effect metadata, layout, limits, and supported LED API features |
| GET | `/api/v1/led/scenes` | Read-only | Four built-ins, four user slots, active scene, store health, and generation |
| POST | `/api/v1/led/scenes/apply` | CSRF + optional PIN | Queue a volatile scene override by stable ID |
| POST | `/api/v1/led/scenes/cancel` | CSRF + optional PIN | Cancel the volatile override and return to configured policy/mode |
| POST | `/api/v1/led/scenes/save` | CSRF + optional PIN | Create or replace one user slot with optimistic concurrency |
| POST | `/api/v1/led/scenes/delete` | CSRF + optional PIN | Clear one user slot with optimistic concurrency |
| GET | `/api/v1/led/scenes/export` | Read-only | Download the canonical user-scene document |
| POST | `/api/v1/led/scenes/import` | CSRF + optional PIN | Validate or atomically replace all user scenes |
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

## LED API v1

The v1 state/capability endpoints remain additive read contracts and there is deliberately no `PATCH /api/v1/led/state`. Runtime configuration writes continue through schema-6 `/api/config`; scene recipes use their own bounded routes and persistence generation. Phase 4 adds scene metadata without removing the Phase 2/3 shape.

### Current state

`GET /api/v1/led/state` reports the state selected by the policy engine, rather than recomputing a second interpretation in the HTTP layer:

```json
{
  "schema_version": 1,
  "mode": "speed",
  "intent": "range",
  "priority": 30,
  "body_enabled": true,
  "status_enabled": true,
  "homogeneous": false,
  "mirror": false,
  "alert": "none",
  "critical_alert": false,
  "range": 3,
  "brightness": 180,
  "body_level": 255,
  "transition_ms": 500,
  "scene": {
    "id": 0,
    "key": "none",
    "name": "",
    "origin": "none",
    "playback": "none",
    "pending_id": 0,
    "stale": false,
    "applied_generation": 0,
    "activation_revision": 0,
    "store_generation": 3
  },
  "effect_a": {
    "id": 1,
    "key": "pulse",
    "name": "PULSE",
    "speed": 128,
    "intensity": 200,
    "palette_id": -1,
    "palette_key": "none",
    "palette_name": "None"
  },
  "effect_b": {
    "id": 2,
    "key": "breath",
    "name": "BREATH",
    "speed": 128,
    "intensity": 200,
    "palette_id": 7,
    "palette_key": "custom_ab",
    "palette_name": "Custom A-B"
  },
  "base_rgb": {"r": 0, "g": 60, "b": 0},
  "accent_rgb": {"r": 0, "g": 60, "b": 0},
  "transition": {
    "active": false,
    "duration_ms": 500,
    "progress": 255,
    "started": 4,
    "completed": 3,
    "interrupted": 1
  },
  "power": {
    "budget_ma": 1000,
    "requested_ma": 640,
    "estimated_ma": 640,
    "scale": 255,
    "estimate_only": true
  }
}
```

`intent` is one of `welcome`, `day_status`, `idle`, `home_missing`, `range`, `show`, `simple`, `scene_manual`, or `critical_alert`; current composition represents an overlay through `alert` (`none`, `system`, or `geofence`) and `critical_alert:true` alongside the underlying body intent. `range` is `-1` when no Speed/Geofence range is active. Palette ID `-1` means that renderer has no selected registry palette.

`body_level` is the active recipe's relative body scale, not the global brightness. It never scales status or alert pixels. `scene.id` and `pending_id` are `0` when absent; `playback` is `none`, `manual`, or `show`. A user scene becomes `stale:true` when its saved slot no longer matches the active snapshot; it stays visually stable until reapplied or cancelled.

Priority values are part of schema 1: welcome `100`, System/Geofence overlay `90`, Day Mode `80`, active Range/Show/Simple/manual scenes `30`, and idle/Home guidance `20`. Higher values win policy selection; consumers should still use the named fields instead of inferring the full scene from a number alone.

### Capabilities and effect metadata

`GET /api/v1/led/capabilities` returns exactly one descriptor for every valid persisted effect ID. A shortened response is:

```json
{
  "schema_version": 1,
  "effect_registry_version": 2,
  "palette_registry_version": 1,
  "scene_registry_version": 1,
  "scene_schema_version": 1,
  "effect_count": 12,
  "palette_count": 8,
  "persistent_effect_ids": true,
  "layout": {
    "buses": 2,
    "pixels_per_bus": 24,
    "status_pixels_per_bus": 2,
    "body_pixels_per_bus": 22,
    "bus_a_orientation": "forward",
    "bus_b_orientation": "reverse",
    "mirror_supported": true,
    "mirror_default": true,
    "regions": ["status", "body_left", "body_right", "body_all", "alert"],
    "physical_format": "RGBW",
    "logical_format": "RGB"
  },
  "limits": {
    "brightness_min": 1,
    "brightness_max": 255,
    "speed_min": 0,
    "speed_max": 255,
    "intensity_min": 0,
    "intensity_max": 255,
    "transition_default_ms": 500,
    "scene_transition_max_ms": 5000,
    "scene_name_bytes": 24,
    "scene_user_slots": 4,
    "scene_import_max_bytes": 4096,
    "scene_json_nesting": 6,
    "scene_record_bytes": 196,
    "current_budget_min_ma": 250,
    "current_budget_max_ma": 5000
  },
  "features": {
    "state_get": true,
    "state_patch": false,
    "transitions": true,
    "palettes": true,
    "scenes": true,
    "scene_import": true,
    "scene_export": true
  },
  "effects": [
    {
      "id": 0,
      "key": "solid",
      "name": "SOLID",
      "controls": {"speed": false, "intensity": false, "color": true},
      "defaults": {"speed": 80, "intensity": 140},
      "useful_range": {
        "speed_min": 0,
        "speed_max": 255,
        "intensity_min": 0,
        "intensity_max": 255
      },
      "color_mode": "base",
      "palette_mode": "none",
      "default_palette_id": -1,
      "safety": "calm"
    }
  ],
  "palettes": [
    {
      "id": 0,
      "key": "safety_amber",
      "name": "Safety Amber",
      "cyclic": true,
      "dynamic": false,
      "stops_rgbw": [
        {"r": 180, "g": 45, "b": 0, "w": 0},
        {"r": 200, "g": 70, "b": 0, "w": 20}
      ]
    }
  ]
}
```

The real response contains all 12 effects and all eight palettes with their complete RGBW stop arrays; the shortened example shows only part of each. `controls` says which stored parameters visibly affect a renderer, so clients should disable irrelevant inputs without deleting persisted values. `color_mode` is `base` or `generated`; `palette_mode` is `none`, `internal`, or `selectable`. `default_palette_id` is `-1` when no palette applies.

`limits.scene_name_bytes` is the 24-byte wire field, including its terminating NUL. The maximum user-visible UTF-8 name is therefore 23 bytes; clients must count encoded bytes, not Unicode characters.

## LED scenes API v1

Scenes are bounded visual recipes, not serialized device state. They cannot set mode, status, alerts, Day Mode, global brightness, power limits, GPS/Home, Wi-Fi, PIN, or other product state.

Stable IDs are `1..4` for the built-ins `high_visibility`, `calm`, `active`, and `party`; user slots 1–4 use IDs `128..131`. A user scene may be manually applied even when `show_eligible:false`. A scene marked eligible is rejected if either effect has `advanced` safety metadata.

Inside scene documents, a branch without a registry palette uses `{"id":255,"key":"none"}`. This is the scene wire sentinel; `/api/v1/led/state` continues to report palette ID `-1` for “none” for backward compatibility with its existing contract.

### Request envelope and limits

Every scene `POST` requires the write headers described above plus:

```http
Content-Type: application/json; charset=utf-8
Content-Length: <2..4096>
```

The charset suffix is optional. Missing length returns `411`; more than 4096 bytes returns `413`; form or multipart media types return `415`. JSON nesting is limited to 6, enough for the canonical import path `document.scenes[].branch_a.effect.id`. Unknown or missing fields, wrong JSON types, fractional/negative/stringified integers, duplicate slots, and ID/key mismatches fail closed.

The body is bounded before allocation/parsing. Clients must not use chunked form uploads for these routes.

### List and active state

`GET /api/v1/led/scenes` returns all eight stable positions. Empty user slots remain present with `occupied:false`:

```json
{
  "schema_version": 1,
  "store": {"health": "healthy", "generation": 3, "read_only": false},
  "active": {
    "id": 1,
    "key": "high_visibility",
    "name": "Alta visibilidad",
    "origin": "builtin",
    "playback": "show",
    "pending_id": 0,
    "stale": false,
    "applied_generation": 3,
    "activation_revision": 7
  },
  "scenes": [
    {
      "id": 1,
      "key": "high_visibility",
      "origin": "builtin",
      "editable": false,
      "occupied": true,
      "name": "Alta visibilidad",
      "mirror": true,
      "show_eligible": true,
      "speed": 120,
      "intensity": 180,
      "body_level": 255,
      "transition_ms": 400,
      "base_rgb": {"r": 255, "g": 80, "b": 0},
      "accent_rgb": {"r": 255, "g": 220, "b": 160},
      "branch_a": {
        "effect": {"id": 3, "key": "chase"},
        "palette": {"id": 0, "key": "safety_amber"}
      },
      "branch_b": {
        "effect": {"id": 3, "key": "chase"},
        "palette": {"id": 0, "key": "safety_amber"}
      }
    },
    {"id": 128, "slot": 1, "key": "user_1", "origin": "user", "editable": true, "occupied": false}
  ]
}
```

The example shortens the `scenes` array; the real response contains four built-ins followed by four user slots.

### Apply and cancel

Queue a volatile override:

```json
{"id": 128}
```

`POST /api/v1/led/scenes/apply` returns `202` with `{"ok":true,"code":"pending","pending_id":128,...}`. An unoccupied user ID returns `404 scene_not_found`. The request does not persist the active scene or change the configured mode.

Cancel with the exact empty object:

```json
{}
```

`POST /api/v1/led/scenes/cancel` returns `202` with `pending_id:0`. Commands are consumed by the LED tick; if requests race before that tick, the latest command wins.

### Save and delete

Save one complete recipe. `slot` is 1-based and `scene` deliberately omits `id`/`slot`:

```json
{
  "expected_generation": 3,
  "slot": 1,
  "scene": {
    "name": "Paseo azul",
    "mirror": true,
    "show_eligible": true,
    "speed": 140,
    "intensity": 170,
    "body_level": 180,
    "transition_ms": 600,
    "base_rgb": {"r": 0, "g": 40, "b": 80},
    "accent_rgb": {"r": 0, "g": 180, "b": 220},
    "branch_a": {
      "effect": {"id": 4, "key": "comet"},
      "palette": {"id": 2, "key": "ocean"}
    },
    "branch_b": {
      "effect": {"id": 4, "key": "comet"},
      "palette": {"id": 2, "key": "ocean"}
    }
  }
}
```

Creating an empty slot returns `201`; replacing returns `200`. An identical save returns `no_change:true` without a flash write or generation increment.

Delete with:

```json
{"expected_generation": 4, "slot": 1}
```

Deleting an already empty slot is also an observable no-op. A stale generation returns `409 generation_conflict`; reread the catalog before retrying instead of blindly overwriting another client.

### Export and import

`GET /api/v1/led/scenes/export` downloads `dog-rgb-scenes.json`. It contains only occupied user recipes:

```json
{
  "format": "dog-rgb-scenes",
  "schema_version": 1,
  "store_generation": 4,
  "registry": {"effects": 2, "palettes": 1},
  "scenes": []
}
```

Import wraps that exact document:

```json
{
  "expected_generation": 4,
  "dry_run": false,
  "recover_corrupt": false,
  "document": {
    "format": "dog-rgb-scenes",
    "schema_version": 1,
    "store_generation": 4,
    "registry": {"effects": 2, "palettes": 1},
    "scenes": []
  }
}
```

Import is atomic replace-all, not merge. `dry_run:true` may omit `expected_generation` and executes the same parse/validation without writing. Registry-version differences produce warnings only when every current ID/key and semantic rule still validates. `recover_corrupt:true` is accepted only with generation 0 for explicit corrupt/ambiguous recovery; it cannot overwrite a future/oversized read-only record.

Successful scene mutations use `{ok, code, no_change, store_generation, store_health}` plus operation-specific fields. Parse errors also include `field` and may include `detail`.

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

Scene routes use a stricter shape:

```json
{"ok":false,"code":"invalid_scene","field":"scene","detail":"invalid_palette_a","store_generation":4,"store_health":"healthy"}
```

Their additional statuses are `411 length_required`, `413 payload_too_large`, `415 unsupported_media_type`, `422 invalid_scene`/schema or field errors, `409 generation_conflict`/`recovery_required`/`store_read_only`, `500 storage_write_failed`/`storage_verify_failed`/`storage_uncertain`, `503 storage_unavailable`, and `507 storage_full`.

Do not parse human portal copy as an API. Prefer documented fields and tolerate new diagnostic fields.
