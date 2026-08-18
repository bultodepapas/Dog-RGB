# ADR-0010: Cloud retention and truthful activity vocabulary

**Status:** Accepted

**Date:** 2026-08-13

**Scope:** Meaning of user-visible statistics, missing-data treatment, algorithm provenance, timezone behavior, and initial cloud retention defaults.

## Context

The product vision is “Garmin-like” history for a dog, but the current collar has GNSS only. It does not know whether it is worn, charging, lying on a table, or attached to a moving vehicle. The current route record stores coordinates plus minute-of-day; it omits point seconds, point speed/quality, stationary heartbeats, and explicit gaps. Current “active time” is an implementation-specific GNSS metric, not an independently validated behavioral classifier.

If the web UI turns missing samples into inactivity, calls every boot a walk, or calls a speed threshold “running,” it will present false certainty. If raw location is retained forever merely because storage is inexpensive, it creates unnecessary privacy exposure.

## Decision

### Current firmware vocabulary

The website and APIs must preserve the firmware's actual meanings:

| Term | Exact meaning | Forbidden implication |
| --- | --- | --- |
| recording / session | Current firmware interval from boot/start of recording to reboot/close, summarized with start/end/date fields | “walk,” outing, exercise session, or continuous wearing |
| accepted distance | Sum of GNSS Haversine segments that passed current fix, quality, distance and speed gates | exact ground truth or total movement during gaps |
| active time (device-reported) | Sum of accepted adjacent observation intervals with activity evidence at both endpoints and no gap over 3 seconds; current movement threshold is 0.7 km/h | veterinary exercise, running time, or all time the dog was active |
| average active speed | accepted distance divided by device-reported active time | mean of every NMEA speed sample or all-day average |
| maximum valid speed | maximum speed admitted after the current filters; observations above 40 km/h are rejected | guaranteed biological maximum or proof the dog ran |
| route point v2 | `lat_e7`, `lon_e7`, and UTC minute-of-day sampled nominally every five seconds | exact second, per-point speed/quality, or stationary coverage |
| current-day metrics | firmware accumulator for its current trusted GNSS date | cloud-local day in the dog's chosen timezone |

These labels remain qualified as `device-reported` when displayed beside later cloud derivations. A mismatch is diagnostic and is not silently reconciled by replacing one value.

### Cloud v3 vocabulary

Richer statistics require a v3 observation stream with stable identities, UTC seconds plus time quality, speed, fix/quality flags, stationary heartbeats, and explicit gap/fix-loss evidence. After field validation and a versioned algorithm, the cloud may show:

| Term | Required definition |
| --- | --- |
| observed duration | union of intervals the algorithm considers covered by usable observations, capped by explicit maximum gap rules |
| moving duration | covered intervals whose endpoints/evidence meet the published versioned movement rule |
| observed stationary duration | covered intervals whose usable evidence meets the published stationary rule |
| unknown duration | selected civil-day/recording interval not covered by trusted observations, including power-off, fix loss, full outbox, or disallowed gaps |
| coverage | `(moving + observed stationary) / eligible interval`; eligible interval and current-day endpoint must be stated |
| estimated movement phase | contiguous moving evidence produced by a named algorithm version; not called walk/run without validated classifier evidence |

The conservation rule is `eligible interval = moving + observed stationary + unknown` after non-overlap normalization. Unknown is never converted to stationary/inactive. “Inactive” may be used only as clearly qualified **observed stationary/inactive while the collar was producing usable evidence**. The UI must explain that collar-on-table, charging, not worn, and sleep cannot be distinguished. “Sleep,” health, calories, stress, readiness, and medical/anomaly claims remain out of scope without new sensors and validation.

Speed-coloured routes use observed speed only where the selected algorithm has trustworthy time/position/speed evidence. Low quality is visually distinct; gaps break the line. Legacy v2 routes do not gain invented per-point speeds.

### Algorithm and provenance

- Every derived summary stores `algorithm_version`, computation time, input watermark/schema range, coverage, and quality/gap counts.
- Algorithm changes write/recompute a new version before promotion. Historical values are never silently redefined without recording provenance.
- Device-reported values, raw observations, and cloud-derived values have distinct source labels.
- User-facing rounding happens at presentation boundaries; canonical database/wire integer units remain unchanged.
- A dog has an IANA timezone. Local-day analytics support 23/24/25-hour civil days; current day ends at “now.” Timezone changes apply forward by default and historical regrouping requires explicit audited recomputation.

### Initial retention defaults

Use data minimization with explicit operational windows:

| Data class | Initial active-system retention |
| --- | --- |
| raw telemetry points plus chunk identity/header/hash receipt metadata | 12 months rolling from observation time; delete sooner on dog/account deletion; successful raw request/chunk bodies are not retained |
| recording metadata and versioned daily summaries | Until the dog/account is deleted, with user export/delete controls |
| current desired/reported configuration | Until collar unlink/dog deletion; retain only the current minimum necessary state |
| configuration revisions and apply/rejection outcomes | 12 months |
| one-use claim material | 900-second maximum validity; purge consumed/expired rows within 24 hours |
| device request/idempotency response receipts | 30 days; permanent telemetry uniqueness constraints remain |
| active device credential digest | Pairing lifetime; non-usable revoke-only digest/tombstone 90 days for exact-response replay, `already_revoked` receipt authentication, and investigation, then purge; a collar still pending beyond the tombstone window uses the explicit warned recovery path |
| security audit events | 12 months, redacted and coordinate-free |
| Edge/Vercel operational logs | 14 days target, redacted and coordinate-free; use the shortest provider-supported setting |
| account/profile/membership | Until account/dog deletion, subject to legal/backup constraints |

Deletion removes active route/summary/config data within 24 hours of a successful authorized request and records a non-location deletion receipt. Backups may retain encrypted remnants until their documented expiration; deleted data must not be restored into active service. Provider backup windows and restore procedures are operational policy and must be disclosed. Map-provider log retention is outside Dog-RGB's direct control, so the app sends no route coordinates and links the selected provider's current privacy terms.

Users can later choose a shorter raw-location retention. Longer retention is never enabled silently. Aggregate summaries may be retained after raw-point expiry only while still tied to an active dog/account and only under the stated meaning/provenance.

## Consequences

### Positive

- The first website can be useful without claiming behavior the hardware cannot observe.
- Gaps and coverage expose data quality instead of rewarding missing data.
- Versioned raw/derived separation enables reproducible improvements.
- A finite raw-location window reduces breach and misuse impact.

### Costs and limits

- Marketing/UI copy is less dramatic than consumer activity labels.
- Raw-point deletion limits future reprocessing after 12 months.
- Summary algorithms need interval normalization, timezone tests, and provenance storage.
- Backups and external map logs prevent an absolute instantaneous-erasure promise.

## Rejected alternatives

- **Call sessions walks and threshold bands running:** unsupported by the current sensor/evidence.
- **Compute inactivity as day length minus moving time:** converts outage, not-worn time, and poor fixes into false inactivity.
- **Join across gaps:** invents route/distance and visually hides missing evidence.
- **Overwrite device metrics with cloud metrics:** destroys diagnostic provenance.
- **Retain raw location indefinitely by default:** unnecessary privacy exposure for an unproven DIY service.
- **Delete summaries whenever raw points expire:** removes useful low-sensitivity history without a clear privacy gain while the account remains active.

## Implementation and acceptance gates

Current firmware metrics exist. A bounded, replay-safe raw-telemetry retention
primitive is locally implemented but deliberately unscheduled; the broader
retention classes, hosted operation, cloud vocabulary/analytics, and UI are not
complete. Acceptance requires:

1. golden stationary/moving/poor-fix/outage datasets with published expected interval partitions;
2. proof that no gap/offline interval becomes route or inactive time;
3. algorithm-version recomputation and device/cloud discrepancy tests;
4. DST, leap-day, incomplete-current-day, and timezone-change tests;
5. retention jobs tested at boundaries, retries, partial failure, legal hold if ever introduced, and deleted-account restore drills;
6. privacy export/deletion showing all data classes and disclosed backup lag;
7. copy review preventing walk/run/sleep/health claims without future validation.

## References

- [NIST Privacy Framework](https://www.nist.gov/privacy-framework)
- [Supabase database backups](https://supabase.com/docs/guides/platform/backups)
- [PostgreSQL date/time types and time zones](https://www.postgresql.org/docs/current/datatype-datetime.html)
- [RFC 3339 date and time on the Internet](https://www.rfc-editor.org/rfc/rfc3339)
