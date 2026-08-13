# Proposed BLE Companion App MVP

**Document status:** Proposed and not implemented. The firmware's BLE summary exists but `BLE_ENABLED` is false in normal builds because SoftAP/BLE coexistence is unreliable on the shared ESP32-S3 antenna.

## Objective

Build a deliberately small companion that reads one validated 16-byte daily summary and presents three metrics without an account or backend.

## Scope

Included:

- discover/select one Dog-RGB collar;
- connect and read the documented characteristic;
- verify payload checksum and decode little-endian fields;
- show distance, average active speed, maximum speed, GNSS/data flags, and reading time;
- persist only the last successful reading locally;
- accessible loading, permission, error, stale, and retry states.

Excluded:

- cloud sync, login, analytics, maps, route transfer, background tracking, multi-collar fleet, configuration writes, OTA, and notifications.

## User flow

1. Open the app and grant the minimum Bluetooth permission required by the platform.
2. Tap **Sync from collar**.
3. Select/connect to `Dog-Collar` and discover the summary service.
4. Read exactly 16 bytes, validate XOR checksum, then decode.
5. Display the new result or retain the prior result with an explicit error/stale label.
6. Disconnect; the MVP does not need a persistent BLE session.

## Acceptance criteria

- Reject payloads with the wrong length/checksum.
- Decode fields and flags exactly as specified in [BLE summary](ble_spec.md).
- Never present `date=0` as a valid day.
- Convert centimeters per second to the displayed unit without changing stored wire values.
- State whether GNSS fix/data were current at read time.
- Work without an Internet permission or account.
- Expose connection/permission state in text, not color alone.

## Gate before implementation

Define a supported firmware radio mode (for example STA-only while advertising), then validate AP/STA/BLE transitions on the XIAO and representative phones. Until that gate passes, the local Wi-Fi portal remains the supported interface.
