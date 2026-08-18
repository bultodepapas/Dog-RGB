# ADR-0007: Durable telemetry outbox on a raw flash ring

**Status:** Accepted as design direction; remediated host evidence awaiting independent acceptance

**Date:** 2026-08-13

**Implementation evidence:** Review/open. The corrected 664-slot byte-addressed candidate has regressions for all seven reproduced fallback/loss/corruption failures and passes 51/51 with regenerated deterministic metrics. Independent acceptance is still required. Physical ESP32 flash/power-cut evidence remains a separate mandatory gate.

**Scope:** Cloud telemetry staging, retry/ACK/reclaim semantics, pressure behavior, and legacy route preservation.

## Context

Wi-Fi and cloud availability are intermittent. The device may reset at any byte boundary, the server may commit a request whose response is lost, and a dog may remain away from known Wi-Fi longer than the existing two-hour route window. An in-memory queue or “delete after HTTP 200” file cannot provide the required evidence.

The current partition table has a `0x150000`-byte data partition labelled `spiffs` that the firmware does not use. The existing `tracknvs` partition remains the local two-hour v2 route store. Phase 0 compared two cloud-outbox candidates on the larger partition:

- a fixed-record log built directly on `esp_partition` erase/write/read operations;
- a segmented append log modeled on LittleFS behavior.

The model is useful design evidence, but it is not a trace from a physical ESP32, the real LittleFS implementation, or random power removal. Flash timing, wear distribution, brownout behavior, and interaction with GNSS/LED work remain unproved.

## Decision

### Select a fixed raw-partition ring

Use a dedicated raw `esp_partition` log for the cloud outbox. Rename/retype the currently unused partition when firmware implementation begins; do not mount it simultaneously as SPIFFS/LittleFS. The local `tracknvs` route ring remains independent.

The provisional fixed layout is:

- 336 erase sectors of 4 KiB in `0x150000` bytes;
- two alternating metadata/superblock sectors;
- two independently erasable emergency/loss-journal sectors;
- 332 data sectors, two 2,048-byte immutable chunk slots per sector;
- 664 chunk slots total;
- up to 96 fixed v3 observations per chunk, or 63,744 point slots before metadata/codec revisions.

Layout values are a versioned on-flash contract. Firmware must refuse unknown future layout versions rather than reformatting them.

### Immutable seal, at-least-once delivery

Each data slot moves monotonically through erased -> writing -> sealed/valid. A sealed chunk is immutable and contains enough independent validation to recover by scanning: magic/layout version, telemetry schema, stable collar/boot/chunk/point identities, sequence/time bounds, point count, payload length, flags, payload CRC-32, and a content hash used by the server contract.

The delivery invariant is:

> A sealed chunk remains eligible for upload until the collar has durably recorded the server's post-commit ACK. Duplicate delivery is expected and must resolve to one logical server result.

Required order:

1. encode and write payload/header without publishing it as sealed;
2. verify the complete slot by readback/CRC;
3. durably publish the sealed slot in recoverable metadata;
4. upload it one or more times with stable IDs and body hash;
5. server commits telemetry and idempotency receipt atomically, then returns a chunk-specific ACK;
6. collar durably advances ACK/reclaim metadata;
7. only then may a sector containing no unacknowledged slot be erased/reused.

A reset after server commit but before step 6 replays the same chunk; database uniqueness and request receipts return the same result. ACK is never inferred from connection close, request write completion, HTTP `200`, or an uncommitted server response. Firmware accepts an ACK only when `boot_sequence`, `chunk_sequence`, `accepted_point_count`, `through_point_sequence`, and canonical `content_sha256` match the sealed chunk. Later/out-of-order acceptance cannot bridge a missing or rejected chunk; holes remain unreclaimable until their own exact ACK arrives.

### Metadata and recovery

- Maintain two CRC-protected, generation-numbered superblocks with wrap-safe selection and readback verification.
- Metadata describes committed boundaries and exact ACK evidence; it is not the sole evidence that a chunk exists. On ambiguity, scan and validate data slots, recover fully written orphan chunks conservatively, and never convert uncertain data into ACKed data.
- Journal format v2 binds each reclaim intent to exact slot ordinals and carries a CRC-excluded, one-way consumed marker. Program and verify that marker only after the sector is fully erased, and never refill before it is durable; a fallback journal can therefore never resurrect old erase authority over newer data.
- Retain the global ordinal from a corrupt payload when its header remains valid and quarantine it until an acknowledged loss makes it reclaimable. If a committed header cannot be decoded, make mutation/reclaim read-only rather than risk ordinal reuse.
- Reclaim only complete sectors so an erase cannot destroy another unacknowledged slot.
- Persist counters for recoveries, corrupt slots, orphan salvage, failed reads/writes/erases, pressure level, dropped observations, and explicit data-loss intervals. Do not log coordinates.
- A future/oversized layout is read-only. Automatic “repair” must not erase it.

### Full/pressure policy

- Reclaim the oldest sector containing only acknowledged chunks.
- Never overwrite an unacknowledged movement/observation chunk to make room.
- Expose warning/critical/full pressure before capacity exhaustion and reduce optional sampling according to a documented adaptive profile.
- At full capacity with no reclaimable sector, stop adding ordinary cloud observations, keep local collar functions running, and coalesce the omitted sequence/time interval into the independently erasable A/B loss journal. Upload an explicit gap/loss marker when service resumes; never draw or count the missing interval as route, stationary time, or inactivity.
- The emergency journal is not permission to erase unacknowledged data. Its own saturation behavior must be bounded and preserve aggregate loss counters.

### Legacy v2 coexistence

The existing current/completed route records stay readable/exportable throughout migration. Cloud outbox initialization must not format `tracknvs` or erase legacy data. Firmware reads v2 and v3 independently; any v2 cloud import is labelled `legacy_v2` and preserves its actual minute-level/coordinate-only limitations. Destructive migration requires either a verified server ACK for converted data or explicit user reset with warning.

## Phase 0 model evidence

The checked [storage feasibility report](../cloud/phase0-storage-feasibility.md) now describes two different evidence generations and keeps both non-authoritative for safety:

- The superseded RAM-only model's 20/20 run is invalid historical evidence. It accepted `acknowledge_through(999)` after only chunks `0..2` existed, reclaimed all three, and could not construct recovery from a persisted flash image.
- The corrected candidate models NOR 1→0 programming, whole-sector erase, fresh-image mounts, globally monotonic outbox ordinals, exact per-slot ACK evidence, contiguous-prefix reclaim, A/B metadata journals, and two independently erasable emergency sectors. Its provisional geometry is 664 chunks/63,744 points, or 15.624 days at the four-hours-moving/five-second plus twenty-hours-stationary/sixty-second profile.
- The remediated suite binds reclaim intent to the exact sector slot ordinals, irreversibly consumes it before refill, derives the next ordinal from retained loss tombstones and quarantined corrupt headers, fails read-only on unreadable committed headers, processes loss intervals without range-sized allocation/iteration, durably coalesces a second pending loss while the first ACK transitions, automatically finalizes acknowledged sparse loss when the contiguous prefix closes, and distinguishes ACKed corrupt payloads from unsynchronized loss. Those seven regressions and the deterministic 10,000-cycle workload pass 51/51.

The host recovery/reclaim gate is therefore **review/open**, not accepted. The raw ring remains the accepted design direction because its fixed format makes the required invariants inspectable and its capacity difference from the idealized LittleFS model is small; that decision does not authorize firmware implementation. Candidate amplification, salvage, cut, recovery-scan, and wear figures remain provisional until an independent review accepts the complete host matrix.

No host model proves physical safety. Flash-driver timing, brownout behavior, cache behavior, actual LittleFS write amplification, metadata wear, and interaction with GNSS/LED work require target measurements. The LittleFS comparison remains an idealized model, not a measured library trace.

## Consequences

### Positive

- Stable chunk identities and post-commit ACKs provide at-least-once transport without logical duplication.
- A fixed layout is small, bounded, versionable, and recoverable without a filesystem allocator.
- Cloud backlog has far more retention than the local preview ring under the modeled adaptive profile.
- Explicit gaps keep missing evidence from becoming invented route/inactivity.

### Costs and limits

- Raw flash management, wear handling, recovery, and migration become project-owned code.
- Worst-case full scan is much slower than the modeled filesystem mount and must not block GNSS/watchdog constraints.
- Fixed 2 KiB slots can waste space and require a versioned layout change if the codec grows.
- The current capacity estimate excludes measured device power cost and real flash behavior.

## Rejected alternatives

- **RAM queue:** loses backlog on reset and cannot span offline periods.
- **Reuse the current two-hour route ring:** its overwrite semantics and v2 fields cannot satisfy ACK/idempotency/history requirements.
- **One mutable JSON file:** rewriting makes torn-write recovery and bounded memory difficult.
- **LittleFS as the initial choice:** attractive mount/recovery behavior, but its modeled capacity advantage was negligible and actual allocation/write behavior remains unmeasured.
- **Overwrite oldest unacknowledged data:** silently converts an outage into plausible but false history.

## Mandatory review/acceptance gates

### Host recovery/reclaim gate

This gate is review/open. Before any firmware outbox work is authorized (Phase 1 local-cloud work is already proceeding under the explicit exception recorded in the parent plan):

1. keep every destructive fallback, sequence-reuse, bounded-loss, ACK-transition, and corruption-classification reproduction in the byte-image suite, including fresh-instance recovery after every cut;
2. prove exact sent-manifest ACK matching, durable holes, sector-safe reclaim, bounded/idempotent loss reporting, and fail-closed recovery from corrupt metadata/loss copies;
3. regenerate every numerical claim and stable artifact hash after each storage change; and
4. obtain an independent review that explicitly accepts the matrix rather than relying on a green test count.

### Physical ESP32-S3 gate

After host acceptance, rerun equivalent tests against the production codec and actual ESP32 flash API:

1. at least 10,000 seal/ACK/reclaim cycles and randomized reset/power removal at every write/erase/metadata boundary;
2. full-ring pressure, emergency loss journal, delayed/lost ACK, replay, corrupted/torn header/payload, bad sector/read/write/erase failures;
3. measured mount/recovery latency, maximum cooperative-loop/LED/GNSS gap, heap, current/energy per sampling profile, and watchdog margin;
4. erase distribution/endurance analysis, including a design change if metadata concentration is unsafe;
5. byte-golden codec/layout tests and refusal of future/corrupt versions;
6. legacy-v2 dual-read/export and proof that initialization never erases existing routes.

If raw flash fails any gate or actual LittleFS traces materially outperform it without losing exact recovery guarantees, revisit this ADR before field deployment. Phase 0 remains open and Phase 2 firmware/cloud integration remains unauthorized until the host gate, physical gate, and separate credentialed provider/origin-control/human-review map gate all close through the parent plan's exit review.

## References

- [Espressif partition APIs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/storage/partition.html)
- [Espressif wear levelling component](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/storage/wear-levelling.html)
- [LittleFS design specification](https://github.com/littlefs-project/littlefs/blob/master/DESIGN.md)
- [AWS IoT Device Shadow data flow and retry-oriented state](https://docs.aws.amazon.com/iot/latest/developerguide/device-shadow-data-flow.html)
