# Runtime Configuration Reference

**Status:** Current schema and validation, verified against firmware on 2026-08-12.

The configuration UI is available at `/config`. The corresponding API is `GET/POST /api/config`; `POST /api/config/reset` restores compile-time runtime defaults. All writes require `X-Dog-Portal` and, when enabled, `X-Dog-Pin` as described in the [HTTP API reference](api-reference.md#write-guards).

## Read schema

`GET /api/config` never returns a password. Presence flags distinguish “stored but hidden” from “not configured.” The current schema version is `5`.

```json
{
  "version": 5,
  "mode": "speed",
  "fence_max_m": 300,
  "led": {"brightness": 77},
  "day_mode": {
    "enabled": false,
    "start_min": 360,
    "end_min": 960,
    "tz_offset_min": -300
  },
  "gps": {
    "min_fix_quality": 1,
    "min_sats": 6,
    "max_hdop": 2.5,
    "max_gga_age_ms": 2000,
    "min_segment_m": 3.0,
    "hdop_factor": 2.0,
    "max_min_segment_m": 10.0
  },
  "speed_ranges_kph": [2, 4, 6, 8, 10, 12, 14, 16, 18],
  "effects": {
    "range1": {"a": 7, "b": 7, "speed": 40, "intensity": 80},
    "range2": {"a": 7, "b": 7, "speed": 58, "intensity": 95},
    "range3": {"a": 7, "b": 7, "speed": 76, "intensity": 110},
    "range4": {"a": 7, "b": 7, "speed": 94, "intensity": 125},
    "range5": {"a": 7, "b": 7, "speed": 112, "intensity": 140},
    "range6": {"a": 7, "b": 7, "speed": 130, "intensity": 155},
    "range7": {"a": 7, "b": 7, "speed": 148, "intensity": 170},
    "range8": {"a": 7, "b": 7, "speed": 166, "intensity": 180},
    "range9": {"a": 7, "b": 7, "speed": 184, "intensity": 190},
    "range10": {"a": 7, "b": 7, "speed": 200, "intensity": 200}
  },
  "single": {
    "effect": 0,
    "speed": 80,
    "intensity": 140,
    "rgb": {"r": 0, "g": 60, "b": 60}
  },
  "wifi": {
    "ap_ssid": "DogRGB",
    "has_ap_pass": true,
    "sta_ssid": "",
    "has_sta_pass": false,
    "mdns": "dog-collar"
  }
}
```

`start_min`, `end_min`, and `tz_offset_min` are informational compile-time values. Only `day_mode.enabled` is persisted through this schema.

## Update semantics

`POST /api/config` accepts a JSON object and starts from the current configuration. Top-level sections are optional; omitted fields keep their current values.

Important nested rules:

- If `speed_ranges_kph` is present, it must contain all nine thresholds.
- If `effects` is present, it must contain `range1` through `range10`; fields inside each range may use current values as fallbacks.
- `single` and `single.rgb` may be partial.
- If `day_mode` is present, `enabled` must be an actual JSON boolean.
- `gps` may be partial, but the resulting group must pass all cross-field rules.
- `wifi.ap_pass` is write-only. Omit/leave it empty to retain an existing protected password; set `ap_open: true` to clear it explicitly.
- `version` is returned for clients but ignored as an update selector; compatibility is controlled by the firmware's stored schema.

Minimal example:

```http
POST /api/config
Content-Type: application/json
X-Dog-Portal: 1

{"mode":"show","led":{"brightness":64}}
```

Full writable shape:

```json
{
  "mode": "speed",
  "fence_max_m": 300,
  "led": {"brightness": 77},
  "day_mode": {"enabled": false},
  "gps": {
    "min_fix_quality": 1,
    "min_sats": 6,
    "max_hdop": 2.5,
    "max_gga_age_ms": 2000,
    "min_segment_m": 3.0,
    "hdop_factor": 2.0,
    "max_min_segment_m": 10.0
  },
  "speed_ranges_kph": [2, 4, 6, 8, 10, 12, 14, 16, 18],
  "effects": {
    "range1": {"a": 7, "b": 7, "speed": 40, "intensity": 80},
    "range2": {"a": 7, "b": 7, "speed": 58, "intensity": 95},
    "range3": {"a": 7, "b": 7, "speed": 76, "intensity": 110},
    "range4": {"a": 7, "b": 7, "speed": 94, "intensity": 125},
    "range5": {"a": 7, "b": 7, "speed": 112, "intensity": 140},
    "range6": {"a": 7, "b": 7, "speed": 130, "intensity": 155},
    "range7": {"a": 7, "b": 7, "speed": 148, "intensity": 170},
    "range8": {"a": 7, "b": 7, "speed": 166, "intensity": 180},
    "range9": {"a": 7, "b": 7, "speed": 184, "intensity": 190},
    "range10": {"a": 7, "b": 7, "speed": 200, "intensity": 200}
  },
  "single": {
    "effect": 0,
    "speed": 80,
    "intensity": 140,
    "rgb": {"r": 0, "g": 60, "b": 60}
  },
  "wifi": {
    "ap_ssid": "DogRGB",
    "ap_pass": "Dog12345",
    "ap_open": false,
    "mdns": "dog-collar"
  }
}
```

## Validation

| Field | Rule |
| --- | --- |
| `mode` | `speed`, `geofence`, `show`, or `simple` |
| `led.brightness` | Integer `1..255` |
| `day_mode.enabled` | JSON boolean |
| `fence_max_m` | Integer `50..5000` |
| `speed_ranges_kph` | Exactly nine positive, strictly increasing numbers |
| Effect `a` / `b` | Integer ID `0..11` |
| Effect `speed` / `intensity` | Integer `0..255` |
| `single.effect` | Integer ID `0..11` |
| `single.speed`, `single.intensity`, RGB channels | Integer `0..255` |
| `gps.min_fix_quality` | `0..8` |
| `gps.min_sats` | `3..12` |
| `gps.max_hdop` | `0.5..20.0` |
| `gps.max_gga_age_ms` | `500..10000` |
| `gps.min_segment_m` | `0.5..20.0` |
| `gps.hdop_factor` | `0.0..5.0` |
| `gps.max_min_segment_m` | `1.0..50.0` and not below `min_segment_m` |
| `wifi.ap_ssid` | 1–32 bytes |
| `wifi.ap_pass` | Empty only for explicit open AP; otherwise 8–63 characters |
| `wifi.mdns` | 1–32 ASCII letters, digits, or hyphens |

Common `400` reasons include `no body`, `bad json`, `brightness`, `mode`, `fence_max`, `day_mode`, `gps`, `ranges`, `ranges value`, `ranges order`, `effects`, `effect values`, `effect id`, `single`, `single values`, `ssid`, `pass required`, `pass`, and `mdns`.

## Apply and persistence

On a valid update, firmware:

1. builds a candidate from current state plus supplied fields;
2. validates the complete candidate;
3. writes it to the inactive A/B slot with magic/version/size/generation and CRC-32;
4. reads/commits the new generation as the active runtime state;
5. restores the previous in-memory state and returns `500 storage` if persistence fails;
6. applies brightness and mDNS changes;
7. schedules an AP restart only if AP SSID/password changed.

Success is `{"status":"ok","wifi_restart":true|false}`.

The Home coordinate, station credentials, portal PIN, metrics, sessions, and route history are separate records. They do not become part of the config blob.

## Restore defaults

`POST /api/config/reset` takes no body. It persists the compile-time runtime defaults through the same transactional path and returns `{"status":"ok"}`.

It resets LED/GNSS/mode/AP/mDNS fields only. It does **not** erase:

- station credentials;
- Home;
- portal PIN;
- metrics or completed sessions;
- route history.

If the restored AP credentials differ, the AP restart is scheduled after the response.

## Related endpoints

- Home: `GET /api/home`, `POST /api/home/set`, `POST /api/home/clear`.
- AP-only settings may also be updated through `POST /api/wifi/ap`.
- Station credentials and scan lifecycle live under `/api/wifi` and `/api/wifi/scan`.
- Portal write lock lives under `/api/lock`.

See [Local HTTP API](api-reference.md) for request formats and [Configuration parameters](config_params.md) for compile-time values.
