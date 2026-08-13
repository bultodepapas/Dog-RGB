# Proposed Companion App Flow and Wireframe

**Document status:** Proposed. This describes an optional BLE companion app; no mobile application exists in this repository, and BLE is disabled in normal firmware builds.

## Data flow concept

```text
[Collar GNSS]
      |
      v
[Validated daily metrics]
      |
      +------> [Local Wi-Fi portal — implemented]
      |
      +------> [16-byte BLE summary — implemented, disabled]
                          |
                          v
                  [Companion app — proposed]
```

The companion must decode the documented [BLE payload](ble_spec.md); it must not duplicate GNSS calculation logic.

## One-screen wireframe

```text
+----------------------------------+
| Dog-RGB                          |
| Collar: connected / disconnected|
| GNSS: current / stale / no data  |
|                                  |
| [ Sync from collar ]             |
|                                  |
| Distance today       12.4 km     |
| Average active speed  4.8 km/h   |
| Maximum speed         9.2 km/h   |
|                                  |
| Last update: 10:32 GPS time      |
+----------------------------------+
```

## Required states

- Bluetooth unavailable or permission denied;
- scanning, connecting, reading, and disconnected;
- checksum failure or unsupported payload;
- valid payload with no date/data;
- valid payload with/without current GNSS fix;
- last successful reading with a clear timestamp.

## Accessibility and privacy

- Metrics need text labels; color alone cannot indicate GNSS/connection state.
- The sync action needs progress and a retry path.
- Store only the last reading for the MVP and explain that it may contain activity/location-derived data.
- No account, analytics, cloud upload, or background tracking is required.

Implementation should wait until BLE has a supported radio operating mode and a phone compatibility matrix. See [Companion app MVP](app_mvp_spec.md).
