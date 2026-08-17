# Device v1 compatibility matrix

This file is normative together with `compatibility-matrix.json`. The JSON file
is the machine-readable source for tests; this table is the review surface.

| Protocol | Telemetry | Runtime config | Capability manifest | Result |
| --- | ---: | ---: | ---: | --- |
| 1 | 3 | 7 | 1 | Supported |
| 1 | 2 | 6 | none | Current baseline; firmware upgrade required before cloud use |
| 1 | 2 | 7 | 1 | Reject with `unsupported_schema` |
| 1 | 3 | 6 | 1 | Reject with `unsupported_schema` |
| 2 | 3 | 7 | 1 | Reject with `unsupported_protocol` until protocol 2 exists |

Device v1 does not send a Track v2 wire representation. It does allow the
frozen Phase 0B converter to upload legacy snapshots inside schema 3 using boot
sequence zero, `legacy_minute`, `LEGACY_V2`, unavailable speed, and no invented
movement/stationary evidence. Existing v2 data remains locally recoverable and
must not be erased until every converted chunk is explicitly acknowledged.

## Configuration resources

| Resource key | Schema | Direction | Device-v1 status |
| --- | ---: | --- | --- |
| `brightness` | 1 | Bidirectional | Supported; first vertical slice |
| `visual_mode` | 1 | Bidirectional | Supported after the brightness proof |
| `speed_profile` | 1 | Bidirectional | Supported after the brightness proof |
| `simple_effect` | 1 | Bidirectional | Supported after the brightness proof |
| `gps_quality` | 1 | Bidirectional | Supported; expert UI and strict validation |
| `geofence_policy` | 1 | Bidirectional | Supported; distance only |
| `home_location` | — | None | Local-only/deferred; coordinates never enter this contract |
| `power_model` | — | None | Local-only/deferred; safety-sensitive |
| `scene:<slot>` | — | None | Deferred; requires its own resource schema/tombstone contract |
| Wi-Fi, AP, mDNS, PIN, cloud secret/API host | — | None | Permanently excluded from device-v1 sync |

## Evolution rules

- A changed top-level claim/sync envelope requires a new protocol version.
- A changed Track tuple or flag meaning requires a new telemetry schema.
- A changed resource body or validation meaning requires that resource's schema
  to increase; unrelated resources do not increase.
- A changed capability shape requires a new manifest schema.
- Servers may add problem codes only with a catalog-version increase. A v1
  device treats an unknown code conservatively using the HTTP status class and
  never deletes queued data because it does not recognize an error.
- During a migration, the server must support the old and new versions for a
  measured overlap. There is no silent coercion between versions.
