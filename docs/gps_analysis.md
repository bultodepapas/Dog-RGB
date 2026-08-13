# GNSS, Metrics, and Route Processing

**Status:** Current implementation reference, verified against `src/gps/gps.cpp` on 2026-08-12.

The active firmware consumes NMEA from an EBYTE E108-GN02 on UART1 at 9,600 baud. The receiver can emit at higher rates, but Dog-RGB's normal metric sampling cadence is one second.

## Input pipeline

1. Drain all available UART bytes into a bounded NMEA line buffer.
2. Validate the NMEA XOR checksum before parsing.
3. Parse RMC for status, coordinate, speed in knots, UTC time, and date.
4. Parse GGA for fix quality, satellite count, and HDOP.
5. Combine fresh RMC/GGA evidence into raw, quality, trusted, current, and speed-usable states.

The 16 KiB UART receive buffer is deliberately large enough to cover long synchronous portal writes. Diagnostics expose received bytes/sentences, RMC/GGA counts, checksum/parse failures, overflow, and observation ages.

## Fix vocabulary

| State | Meaning |
| --- | --- |
| Raw fix | RMC status says the receiver has a valid navigation solution |
| Quality OK | GGA is recent and fix quality, satellites, and HDOP pass runtime thresholds |
| Trusted fix | Raw fix **and** quality OK |
| Current fix | A coordinate was present in the latest valid RMC, even if quality gates currently reject metrics |
| Speed usable | Trusted fix plus finite speed in `0..40 km/h` |

Only trusted, speed-usable, date-accepted observations update metrics. The portal exposes the layers separately so a user can distinguish “receiver reports a fix” from “firmware trusts it.”

## Runtime quality gates

| Setting | Default | Range |
| --- | ---: | ---: |
| Minimum fix quality | 1 | 0–8 |
| Minimum satellites | 6 | 3–12 |
| Maximum HDOP | 2.5 | 0.5–20.0 |
| Maximum GGA age | 2,000 ms | 500–10,000 ms |
| Base minimum segment | 3.0 m | 0.5–20.0 m |
| HDOP segment factor | 2.0 | 0.0–5.0 |
| Maximum adaptive minimum | 10.0 m | 1.0–50.0 m |

The effective minimum distance segment is `max(base_min, hdop_factor × HDOP)`, capped at `max_min_segment_m`.

## Speed and distance

- RMC speed is converted from knots to km/h.
- Values above 40 km/h are marked unusable and counted as speed spikes; metrics are not updated from them.
- Haversine distance uses a 6,371,000 m Earth radius.
- A segment is accumulated only while the observation is active (`speed > 0.7 km/h`), at least the adaptive minimum, and below 50 m.
- Rejected small/large segments and the last segment/reason are visible in diagnostics.
- A rejected/untrusted/date-pending observation resets the distance baseline so a later reacquisition cannot bridge an unobserved jump.

This is a practical GPS-only filter, not a precision survey or Kalman/IMU fusion system. Stationary jitter, urban multipath, and overly strict/loose gates must be characterized on the finished collar.

## Active time and average speed

Active time uses GNSS observation timestamps, not the speed of the main loop:

- both endpoints must contain active, trusted, accepted evidence;
- duplicate time contributes nothing;
- backward time rebaselines;
- intervals above 3 seconds are rejected rather than inventing activity during an outage;
- buffered one-Hz observations can still be counted individually after a loop stall;
- counters saturate instead of wrapping.

Average speed is:

```text
average_kph = total_distance_m / (active_time_ms / 1000) × 3.6
```

It is therefore average speed while active, not average over the entire day.

## Date rollover

GNSS dates are validated calendar dates. Rollover behavior is designed to protect accumulated data:

- the first trusted date initializes the current day without inventing a completed day;
- a continuous next-day midnight is accepted immediately, including month/year/leap boundaries;
- a non-contiguous forward date after a gap requires three trusted observations no more than three seconds apart;
- backward dates are never accepted automatically;
- stale/untrusted observations break pending confirmation;
- before reset, the completed day is written to a CRC-protected A/B journal;
- a failed journal write blocks the reset and retries later, preserving current metrics.

On boot, the completed-day journal also prevents an older live metric snapshot from resurrecting the previous day.

## Sessions and routes

Daily metrics and session summaries are related but separate:

- one current session is transactionally persisted;
- up to three complete sessions are retained;
- power loss around finalization cannot duplicate a recovered session;
- route points live in the dedicated `tracknvs` partition;
- the rolling route holds up to 1,440 points at a nominal five-second interval;
- partial route data is flushed every 15 seconds;
- chunks and metadata use CRC-32/IEEE and strict coordinate/time validation.

The API streams route snapshots as JSON, CSV, or GeoJSON. See [Local HTTP API](api-reference.md#route-exports).

## Persistence

Metrics, the completed-day journal, and the session store use independent structured records with magic/version/size/semantic validation, CRC, generation ordering, and A/B selection. Diagnostics expose active slot, generation, write failures, and recoveries.

The firmware may migrate older key-based state once. After migration, current structured records are authoritative.

## Known limitations and validation needs

- RMC/GGA pairing is based on freshness, not a receiver-specific epoch ID.
- The 50 m upper segment rule and adaptive minimum are heuristic.
- No IMU confirms physical movement.
- No antenna/EMI/enclosure characterization exists yet for the final build.
- GNSS time is UTC; Day Mode applies a fixed UTC-5 offset rather than daylight-saving/timezone rules.
- Route timestamps are date plus minute-of-day, not full per-point seconds.

Use `/dev`, Wokwi fault/rate scenarios, and physical reference-track comparisons to tune gates. Never loosen filters from a single anecdotal walk without preserving spike/outage protection.
