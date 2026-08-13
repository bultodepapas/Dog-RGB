# BLE Daily Summary Wire Format

**Status:** Implemented, read-only, and **disabled by default** (`BLE_ENABLED = false`).

The characteristic provides a compact daily summary for a possible companion app. It is not a configuration or route-transfer interface.

## GATT identifiers

| Item | Value |
| --- | --- |
| Device name | `Dog-Collar` |
| Service UUID | `8b4c0001-6c1d-4f3c-a5b0-1e0c5a00a101` |
| Characteristic UUID | `8b4c0002-6c1d-4f3c-a5b0-1e0c5a00a101` |
| Properties | READ only |

## Payload

Exactly 16 bytes, little-endian:

| Bytes | Type | Field | Unit / meaning |
| --- | --- | --- | --- |
| 0–3 | `uint32` | `date_yyyymmdd` | UTC GNSS date as `YYYYMMDD`; `0` means unavailable |
| 4–7 | `uint32` | `distance_m` | Rounded accumulated distance in meters |
| 8–9 | `uint16` | `avg_speed_cmps` | Average active speed in centimeters/second |
| 10–11 | `uint16` | `max_speed_cmps` | Maximum valid speed in centimeters/second |
| 12–13 | `uint16` | `last_update_min` | UTC minutes since midnight |
| 14 | `uint8` | `flags` | Bit field below |
| 15 | `uint8` | `checksum` | XOR of bytes 0 through 14 |

Flags:

- bit 0 (`0x01`): current trusted GNSS fix;
- bit 1 (`0x02`): nonzero current date/data context;
- bits 2–7: reserved, currently zero.

All numeric conversions saturate to their wire type rather than wrapping.

## Decode example

```python
def decode_summary(payload: bytes) -> dict:
    if len(payload) != 16:
        raise ValueError("Dog-RGB summary must be 16 bytes")
    checksum = 0
    for value in payload[:15]:
        checksum ^= value
    if checksum != payload[15]:
        raise ValueError("Dog-RGB summary checksum mismatch")

    u16 = lambda offset: int.from_bytes(payload[offset:offset + 2], "little")
    u32 = lambda offset: int.from_bytes(payload[offset:offset + 4], "little")
    flags = payload[14]
    return {
        "date_yyyymmdd": u32(0),
        "distance_m": u32(4),
        "avg_speed_cmps": u16(8),
        "max_speed_cmps": u16(10),
        "last_update_min": u16(12),
        "gps_fix": bool(flags & 0x01),
        "has_data": bool(flags & 0x02),
    }
```

## Radio coexistence behavior

When compiled in, BLE initializes before Wi-Fi, advertises at a relaxed 500–1,000 ms interval, and pauses advertising whenever SoftAP is active. The characteristic value still refreshes from the current GNSS summary during the loop.

The Bluetooth controller and Wi-Fi share the XIAO ESP32-S3 antenna. Even this policy has not been promoted to the supported default: normal firmware keeps BLE fully disabled to protect AP visibility and recovery. Any release that enables it needs an explicit STA/AP/BLE transition matrix and physical-phone evidence.

See [Proposed companion app](app_mvp_spec.md) for the optional consumer and [User guide](user-guide.md#ble-behavior) for the supported user-facing state.
