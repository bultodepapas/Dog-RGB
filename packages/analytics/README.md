# Cloud analytics v1

`computeSummaryV1` summarizes Track v3 telemetry evidence for a supplied window. It does not infer collar wear, walks, exercise, sleep, or animal behavior.

Inputs use Unix seconds, E7 coordinates, and reported speed in cm/s. A point is eligible for time classification only with schema 3, the trusted-time flag, and a trusted time quality (`server_anchored`, `sntp_synced`, or `gnss_trusted`). Adjacent points in one collar/boot stream classify an interval only when both show the same state, have no low-quality/gap marker, and are at most 65 seconds apart. Moving requires the movement flag and 20–1111 cm/s; stationary requires the stationary-heartbeat flag and a missing speed or speed below 20 cm/s. Same-state overlaps are unioned; conflicting states remain unknown.

Durations are seconds: `observed_s = moving_s + inactive_s`, and `unknown_s` is the rest of the valid window. `coverage_ratio` is observed time divided by window time. Invalid windows or absent trusted time return `insufficient_time_evidence` with metrics absent, not zero activity. A valid time window may still have unknown intervals.

Distance and mean speeds require accepted moving GPS segments. Both endpoints need a valid fix and coordinates, the interval must be 1–65 seconds, displacement at least 3 m, and implied speed at most 1111 cm/s. Distance is rounded to meters; mean speeds and filtered maximum use cm/s. The mean moving speed is filtered distance divided by moving seconds. Filtered maximum uses accepted moving point speeds and GPS-segment speeds. With no accepted GPS segment, distance and both means are `null`, not zero.

Daily inputs cap at 100,000 points and loss markers; recording inputs cap at 250,000 of each. Exceeding a cap returns `source_limit_exceeded` without partial metrics. Retention gaps return `insufficient_retained_data` in the producer. Other output fields include valid/warning point and gap counts, dropped-point evidence, source schema range, algorithm version, receipt watermark, computation time, and covered window.

The SQL producer writes algorithm version 1. Fixtures cover v1 computation, late arrivals, retries, time windows, and retention. The upgrade fixture seeds pre-M4 daily and recording rows with missing provenance, dirties both through source telemetry, verifies replacement with v1 provenance, and replays identical input to check deterministic result fields. It does not simulate a numerically distinct old algorithm or a future v2 migration. Physical accuracy remains unvalidated.

Focused checks:

```sh
npm run test --workspace @dog-rgb/analytics
npm run typecheck --workspace @dog-rgb/analytics
supabase test db supabase/tests/database/23_m4_truthful_summaries.test.sql --local
```
