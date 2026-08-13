# Wi-Fi/AP State and Policy Diagram

**Status:** Current simplified behavior. For timing and edge cases, see [Wi-Fi, Access Point, and Captive Portal](wifi_portal_spec.md).

```mermaid
flowchart TD
    BOOT[Boot: hard-reset radio] --> CREDS{Stored station credentials?}
    CREDS -- no --> AP[Start AP, up to 3 attempts]
    CREDS -- yes --> APSTA[Start AP+STA, up to 3 AP attempts]
    APSTA --> STAWAIT[Station connecting]
    STAWAIT -- got IP --> STAOK[Station connected + mDNS]
    STAWAIT -- 10 s timeout --> FALLBACK[Keep/start AP; schedule STA retry]
    FALLBACK --> RETRY{Retry deadline and no AP client?}
    RETRY -- yes --> STAWAIT
    RETRY -- no --> FALLBACK

    AP --> POLICY[AP policy]
    APSTA --> POLICY
    STAOK --> POLICY
    FALLBACK --> POLICY

    POLICY --> FIX{Trusted GNSS fix?}
    FIX -- no --> FORCE[Force AP on]
    FIX -- yes --> STILL{At/below 2.0 km/h for 2 min?}
    STILL -- yes --> REQUEST[Request AP on]
    STILL -- no --> HOLD{AP active and hold/client idle window expired?}
    HOLD -- no --> POLICY
    HOLD -- yes --> STOP[Stop SoftAP; keep STA/recovery capability]
    STOP --> POLICY
    FORCE --> POLICY
    REQUEST --> POLICY
```

Additional rules:

- A new/restarted AP has a 15-minute hold.
- Any portal request adds a five-minute hold.
- No-client idle timeout is ten minutes after holds expire.
- `>=2.5 km/h` clears stationary accumulation.
- AP/STA callbacks enqueue into a 16-entry fixed queue; the main loop owns transitions.
- Automatic idle shutdown stops SoftAP, not the entire Wi-Fi subsystem.
