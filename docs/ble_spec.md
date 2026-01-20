# BLE Spec - Daily Summary (Phase 1)

This document defines the BLE service and payload for the daily GPS summary.

---

## Service

- Device name: Dog-Collar
- Service UUID: 8b4c0001-6c1d-4f3c-a5b0-1e0c5a00a101
- Characteristic UUID: 8b4c0002-6c1d-4f3c-a5b0-1e0c5a00a101
- Properties: READ

---

## Payload (16 bytes)

Fixed-size payload, little-endian.

Byte layout:

- 0-3: date_yyyymmdd (uint32)
- 4-7: distance_m (uint32)
- 8-9: avg_speed_cmps (uint16)
- 10-11: max_speed_cmps (uint16)
- 12-13: last_update_min (uint16)  // minutes since midnight
- 14: flags (uint8)
- 15: checksum (uint8)             // XOR of bytes 0-14

Flags (byte 14):
- bit0: gps_fix
- bit1: has_data

Units:
- distance_m: meters
- avg_speed_cmps: centimeters per second
- max_speed_cmps: centimeters per second

Notes:
- last_update_min uses GPS time in minutes since 00:00.
- checksum is XOR of bytes 0..14 inclusive.

---

## Example Decode

1) Read 16 bytes from the characteristic.
2) Verify checksum XOR.
3) Decode fields (little-endian).
4) Convert speeds: cm/s -> km/h = cmps * 0.036.
