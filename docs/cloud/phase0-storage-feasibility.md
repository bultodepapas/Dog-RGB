# Phase 0B — Track v3 and outbox storage feasibility

**Status:** host recovery/reclaim remediation passes 51/51 and awaits independent acceptance; physical ESP32-S3 gate open

**Decision:** retain the raw partition ring as design direction; do not implement/ship it until both the host acceptance matrix and the physical gate in this document pass

**Evidence date:** 2026-08-18

**Scope:** Track v3 encoding, retention, outbox candidates, failure simulation, reference fixtures, and Track v2 migration

**Non-scope:** cloud transport, Supabase schema, product analytics, and firmware integration

> **Correction notice (2026-08-13):** the original RAM-only model accepted an
> unsafe numeric ACK watermark and its results remain invalid historical
> output. It has been replaced by a byte-addressed NOR model with fresh-image
> mounts, globally monotonic outbox identities, exact per-slot ACK evidence,
> contiguous-prefix reclaim, serialized A/B journals, and two independently
> erasable loss sectors. Adversarial review then found five
> destructive fallback/loss/corruption cases. The candidate now binds reclaim
> intent to exact slot ordinals, retains monotonic identity through tombstone
> fallback, handles maximum loss intervals with bounded work, durably coalesces
> loss during ACK transition, and classifies ACKed corruption separately.
>
> **Follow-up correction (2026-08-18):** two additional byte-image probes
> reproduced unsafe behavior. A consumed reclaim intent could be resurrected
> after newer journals were corrupted and erase a corrupt refilled slot; an
> acknowledged sparse loss could also remain stuck after the intervening live
> chunk was ACKed unless the server repeated an already durable loss ACK. Journal
> format v2 now programs an irreversible consumed-intent marker before refill,
> retains validated corrupt-slot ordinals in quarantine, fails read-only when a
> committed header is unreadable, and finalizes an acknowledged loss as soon as
> its contiguous prefix closes. All seven regressions and the deterministic
> workload pass 51/51. The regenerated
> metrics remain **provisional, not accepted evidence** until independent
> acceptance; the physical gate remains open alongside Section 12.

The remediated `storage_model.py` artifact is 93,767 bytes with SHA-256
`9d7f0c059399708b4a3162d231a18d00c8378e85c4837f30b5ccd1369574b3d8`.

## 1. Decision in one page

The existing `spiffs` partition is unused and is exactly `0x150000` bytes
(1,376,256 bytes, or 336 4-KiB erase sectors). A fixed 16-byte Track v3 point
and a 92-byte chunk header fit 96 points into 1,628 encoded bytes. The proposed
raw layout reserves two sectors for CRC-protected metadata journals and two
sectors for power-safe loss records, leaving 332 data sectors with two
2,048-byte immutable chunk slots each.

That raw layout retains 664 sealed chunks, or 63,744 points. It provides:

- 0.738 days at a continuous one-second cadence;
- 3.689 days at five seconds;
- 11.067 days at fifteen seconds;
- 44.267 days at sixty seconds; and
- 15.624 days for the planned four-hours-moving-at-five-seconds plus
  twenty-hours-stationary-at-sixty-seconds profile.

A deliberately competitive LittleFS segment-log model retains 672 chunks, or
15.812 adaptive-profile days. The approximately 1.2% capacity difference is immaterial.

A provisional deterministic 10,000 seal/ACK/reclaim run reported that both
candidates:

- recovered from every injected cut (216 raw and 210 LittleFS; reclaim
  eligibility differs because the raw ring reclaims two-chunk sectors while
  LittleFS reclaims 32-chunk segments);
- ended with zero unacknowledged chunks;
- refused to overwrite unacknowledged data when full; and
- preserved the unacknowledged tail through reclaim/refill.

The byte/NOR raw candidate reports 2,173 programmed bytes per successful seal versus 2,415 bytes in
the declared LittleFS model. It recovered 48 complete chunks written before a
metadata commit; LittleFS correctly rolls incomplete/uncommitted operations
back and requires 195 caller retries. Raw recovery scans 1,376,256 bytes; the
LittleFS mount model scans 12,800 bytes. At the report's purely illustrative
20 MiB/s sequential-read assumption those are 65.63 ms and 0.61 ms,
respectively, before hashing and driver latency.

The accepted design direction remains the raw ring because the payload is one
fixed binary record type, its on-flash identity/reclaim rules can be made
explicitly inspectable, and its calculated capacity is sufficient. The
host-model amplification remains an estimate rather than a page-level driver
trace. LittleFS remains the fallback if physical evidence overturns the
custom-ring choice.

This result is **not** the physical evidence required by the parent plan. No
desktop model can prove flash-driver timing, brownout behavior, cache behavior,
real LittleFS write amplification, or interaction with GNSS/LED loop latency.
The decision remains provisional until Section 12 passes on hardware.

## 2. Reproducibility

The prototype uses only the Python standard library and fixed random seed
`0xD06`. It reads no clock, network, environment variable, or cloud credential.

From the repository root:

```powershell
python -m unittest discover -s tools/cloud_phase0 -p "test_*.py" -v
python tools/cloud_phase0/generate_evidence.py
python tools/cloud_phase0/generate_evidence.py --format markdown
```

Canonical files:

| Artifact | Purpose |
| --- | --- |
| [`track_v3.py`](../../tools/cloud_phase0/track_v3.py) | frozen point/chunk byte codec and strict decoder |
| [`legacy_v2.py`](../../tools/cloud_phase0/legacy_v2.py) | deterministic v2 conversion and truthful export contract |
| [`reference_fixtures.py`](../../tools/cloud_phase0/reference_fixtures.py) | stationary/lower-speed/higher-speed/poor-fix synthetic generators |
| [`reference_manifest.json`](../../tools/cloud_phase0/fixtures/reference_manifest.json) | checked hashes, cadence, counts, and non-behavior labels |
| [`storage_model.py`](../../tools/cloud_phase0/storage_model.py) | geometry, retention, raw/LittleFS models, and failure workloads |
| [`test_phase0.py`](../../tools/cloud_phase0/test_phase0.py) | codec/migration plus byte-level NOR, exact-ACK, fresh-mount, cut, corruption, loss and deterministic-workload tests |
| [`generate_evidence.py`](../../tools/cloud_phase0/generate_evidence.py) | canonical JSON/Markdown evidence renderer |

The JSON renderer is intentionally stdout-only. This avoids stale generated
result files; the checked manifest freezes the external fixture hashes. The
corrected candidate keeps every reproduced adversarial failure as a regression,
freezes regenerated workload results, and must pass independent review before
this report can be accepted.

## 3. Repository baseline discovered before the prototype

### 3.1 Current Track v2

The current route store is not a cloud outbox:

- [`TrackPoint`](../../Platformio/Dog-RGB/include/gps/gps.h) is a packed
  10-byte tuple: signed E7 latitude, signed E7 longitude, and minute-of-day;
- [`gps.cpp`](../../Platformio/Dog-RGB/src/gps/gps.cpp) declares Track version
  2, four session slots, five-second sampling, a two-hour visible window,
  48 points per chunk, and a fifteen-second partial-chunk rewrite interval;
- each chunk has a nine-byte header and CRC-32, and strict reads reject wrong
  version, size, flags, CRC, time, or coordinate range;
- route admission is movement-filtered, so absence of v2 points is not evidence
  that the dog or collar was stationary;
- the visible two-hour maximum is 1,440 points; and
- the existing implementation exposes streamed JSON, CSV, and GeoJSON from the
  AP portal while servicing GNSS around bounded writes.

Consequences:

1. V2 has minute position time but no per-point seconds, speed, satellite
   count, fix-quality state, stationary heartbeat, or explicit coverage gap.
2. V2 can support a legacy route line, but not a trustworthy speed overlay or
   inactivity calculation.
3. Changing the existing `TRACK_VER` and clearing mismatched data would destroy
   retained routes and is prohibited.

### 3.2 Current flash layout

[`partitions_dog_rgb.csv`](../../Platformio/Dog-RGB/partitions_dog_rgb.csv)
contains:

| Partition | Size | Current use |
| --- | ---: | --- |
| default `nvs` | `0x5000` | config, metrics, sessions, Wi-Fi, Home, scenes namespaces |
| `spiffs` | `0x150000` | currently unused by the portal/firmware |
| `tracknvs` | `0x30000` | current Track v2 metadata/chunks |
| `coredump` | `0x10000` | ESP coredump |

The new outbox must not use the small default NVS partition and must not erase
`tracknvs` during upgrade. Phase 0 changes no partition table and no firmware.

## 4. Frozen Track v3 encoding

### 4.1 Point: exactly 16 bytes, little-endian

| Offset | Bytes | Type | Field | Rule |
| ---: | ---: | --- | --- | --- |
| 0 | 4 | signed int32 | `lat_e7` | valid only with `FIX_VALID`; otherwise zero |
| 4 | 4 | signed int32 | `lon_e7` | valid only with `FIX_VALID`; otherwise zero |
| 8 | 4 | uint32 | `utc_s` | Unix seconds; zero exactly when time is unknown |
| 12 | 2 | uint16 | `speed_cmps` | centimetres/second; `0xFFFF` means unavailable |
| 14 | 1 | uint8 | `satellites` | accepted count saturated at 255 |
| 15 | 1 | uint8 | `flags` | bits below; bit 7 is reserved and must be zero |

Point flags:

| Bit | Name | Meaning |
| ---: | --- | --- |
| `0x01` | `FIX_VALID` | coordinate passed the observation-path acceptance gate |
| `0x02` | `MOVEMENT_EVIDENCE` | accepted movement evidence; not a behavior label |
| `0x04` | `TIME_TRUSTED` | historical wire name: UTC is present and usable at the chunk's declared quality; corresponds exactly to non-zero `utc_s` |
| `0x08` | `STATIONARY_HEARTBEAT` | trusted stationary observation; not rest/sleep/wear proof |
| `0x10` | `LOW_QUALITY` | usable only with a visible quality warning |
| `0x20` | `GAP` | explicit no-coordinate coverage/fix-loss marker |
| `0x40` | `LEGACY_V2` | deterministic v2 conversion with reduced semantics |

Strict invariants include:

- movement and stationary flags are mutually exclusive;
- a gap cannot contain a fix, movement, or stationary claim;
- a point without a valid fix has zero coordinates and unavailable speed;
- coordinate ranges are checked even after CRC validation;
- `TIME_TRUSTED` and non-zero time must agree exactly; the bit is not a claim
  that SNTP is cryptographically authenticated;
- timestamps are monotonic within a chunk;
- the nil device UUID is invalid;
- boot sequence zero and legacy-minute quality are reserved for wholly
  `LEGACY_V2` chunks; and
- unknown flags are rejected, not ignored.

Every point in a chunk has one shared `time_quality` byte. A quality transition
seals the current chunk before the next observation. The frozen values are:

| Value | Name | Meaning |
| ---: | --- | --- |
| `0` | `UNKNOWN` | no usable UTC; every point has `utc_s=0` and clears `TIME_TRUSTED` |
| `1` | `APPROXIMATE_PERSISTED` | UTC projected from a persisted anchor, with bounded uncertainty |
| `2` | `SERVER_ANCHORED` | UTC bounded by the last authenticated HTTPS server anchor |
| `3` | `SNTP_SYNCED` | UTC established from bounded SNTP; this does not make SNTP cryptographically authenticated |
| `4` | `GNSS_TRUSTED` | UTC accepted from the GNSS trust path |
| `5` | `LEGACY_MINUTE` | deterministic v2 conversion with only minute precision |

All non-`UNKNOWN` qualities require non-zero UTC and `TIME_TRUSTED` on every
point. HLC mutation-authoring trust is a separate server policy: only
server-anchored, bounded-SNTP, and GNSS values inside the receipt skew window
are accepted without rebasing. Approximate and legacy timestamps remain useful
for ordered history but cannot author a globally winning configuration time.

Golden point:

```text
lat_e7=46500000
lon_e7=-740600000
utc_s=1787000000
speed_cmps=140
satellites=11
flags=FIX_VALID|MOVEMENT_EVIDENCE|TIME_TRUSTED

little-endian hex:
a088c5024057dbd3c074836a8c000b07
```

### 4.2 Chunk header: exactly 92 bytes, little-endian

| Offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 4 | magic `D3CK` |
| 4 | 1 | point schema version `3` |
| 5 | 1 | chunk header version `1` |
| 6 | 2 | chunk flags; bit 0 is `FINAL_FOR_RECORDING` |
| 8 | 16 | device UUID bytes |
| 24 | 4 | persisted boot sequence |
| 28 | 4 | chunk sequence within boot/migration namespace |
| 32 | 4 | first point sequence |
| 36 | 2 | point count, 1–96 |
| 38 | 1 | time quality enum |
| 39 | 1 | reserved zero |
| 40 | 4 | first non-zero `utc_s`, or zero |
| 44 | 4 | last non-zero `utc_s`, or zero |
| 48 | 4 | payload length |
| 52 | 4 | payload CRC-32/ISO-HDLC |
| 56 | 32 | payload SHA-256 |
| 88 | 4 | header CRC-32 with this field encoded as zero |

Time quality values are `0=unknown`, `1=approximate_persisted`,
`2=server_anchored`, `3=sntp_synced`, `4=gnss_trusted`, and
`5=legacy_minute`. A non-unknown time quality requires timestamped points. Header
and payload decoders reject truncation, trailing data, unknown versions/flags,
bad reserved bytes, inconsistent lengths/time bounds, CRC mismatch, SHA
mismatch, and any invalid decoded point.

The stable identities remain:

```text
point = (device_id, boot_sequence, point_sequence)
chunk = (device_id, boot_sequence, chunk_sequence)
```

Reusing a stable chunk sequence with a different digest is an integrity error,
never an idempotent retry.

### 4.3 Why 96 points

At 96 points:

```text
92-byte header + 96 × 16-byte points = 1,628 encoded bytes
```

The 1,628-byte encoded chunk spans at most seven 256-byte program pages and is
assigned one immutable 2,048-byte slot. Two slots share one 4-KiB erase sector.
This keeps RAM needed to seal a full chunk below 2 KiB while avoiding a whole
erase sector per chunk. The current host NOR abstraction counts bytes and
enforces one-way bit transitions and sector erases; it does not model page
buffering, page-boundary commands, or driver latency. Those remain explicit
physical-gate measurements.

The final choice must be confirmed against the ESP32-S3 build's actual flash
write API/alignment and RAM high-water mark. The ESP-IDF partition API requires
erased space before writing, and partition-table sectors are 4 KiB; see the
[ESP-IDF SPI flash API](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/peripherals/spi_flash/index.html)
and [ESP32-S3 partition-table guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/partition-tables.html).

## 5. Synthetic reference datasets

Fixtures are deterministic codec/failure references. Their friendly names do
not establish dog behavior truth.

| Fixture | Points/cadence | Truth-safe label | Encoded chunk SHA-256 |
| --- | --- | --- | --- |
| stationary | 8 at 60 s | `synthetic_trusted_stationary_observations` | `89601cb61de0246b611f2e4879a4433012a0c8a417b5ee3b47410a5b6d16a0aa` |
| walking | 12 at 5 s | `synthetic_lower_speed_movement` | `92e4a1b09b58c1088ecd8cb79ce620ae3dc3dfff7e53c633160732943562bbfa` |
| running | 12 at 5 s | `synthetic_higher_speed_movement` | `84e79ac893a65df53d55c263a583039e61ca5dfe34b3f9625aee5564e67f14f5` |
| poor-fix | 5 at 5/15 s gaps | `synthetic_low_quality_and_coverage_gap` | `00a00b0f2800500823fbb427f113abd3b69716a57ade4c1030ecabb5baada23b` |

Fixture interpretation rules:

- “walking” and “running” are human-friendly kinematic profile names only;
- low/higher speed alone cannot classify a dog's gait or intent;
- a stationary heartbeat is not proof of rest, sleep, health, or collar wear;
- a gap is unknown coverage, never inactivity; and
- poor-fix coordinates must remain visibly qualified and must not add distance
  unless the independent metric path accepts them.

## 6. Retention evidence

### 6.1 Daily observation counts

| Profile | Formula | Points/day |
| --- | --- | ---: |
| 1 second | `86,400 / 1` | 86,400 |
| 5 seconds | `86,400 / 5` | 17,280 |
| 15 seconds | `86,400 / 15` | 5,760 |
| 60 seconds | `86,400 / 60` | 1,440 |
| adaptive | `4 h × 3,600 / 5 + 20 h × 3,600 / 60` | 4,080 |

Adaptive is a capacity profile, not a promise that a given dog moves four
hours per day or that the collar is worn for twenty-four hours.

### 6.2 Candidate retention

| Profile | Raw points/days | LittleFS-model points/days |
| --- | ---: | ---: |
| 1 second | 63,744 / 0.738 | 64,512 / 0.747 |
| 5 seconds | 63,744 / 3.689 | 64,512 / 3.733 |
| 15 seconds | 63,744 / 11.067 | 64,512 / 11.200 |
| 60 seconds | 63,744 / 44.267 | 64,512 / 44.800 |
| adaptive | 63,744 / 15.624 | 64,512 / 15.812 |

These are sealed-point capacities, not guaranteed elapsed retention. Immediate
transition/gap markers, partial final chunks, corrupt sectors, unexpectedly
high movement, and pressure coalescing change elapsed coverage. Conversely,
time with the collar off or without trusted observations consumes no point
slots and must appear as unknown coverage.

## 7. Raw ring prototype

### 7.1 Layout

| Region | Sectors | Bytes | Purpose |
| --- | ---: | ---: | --- |
| metadata journal A | 1 | 4,096 | 32 append-only 128-byte generation/CRC records |
| metadata journal B | 1 | 4,096 | alternate while the other is valid |
| emergency A/B | 2 | 8,192 | independently erasable loss records/tombstones |
| chunk data | 332 | 1,359,872 | 664 immutable 2,048-byte slots |

The candidate and future on-device metadata must contain at least generation,
write head, oldest live slot/full identity, highest sealed identity, durable
proof of exactly ACKed full identities (and any contiguous reclaim frontier
derived from those proofs), dropped/coalesced counters, active emergency-record
generation, and CRC. A bare caller-supplied sequence watermark is forbidden.
The commit marker/CRC is programmed last. This report does not freeze that
internal superblock/ACK-journal byte layout; the corrected candidate and future
firmware implementation must add golden vectors before their respective gates
can close.

### 7.2 Commit, ACK, and reclaim order

Seal:

1. Choose a slot in an already erased sector; never erase an unacknowledged
   slot's sector.
2. Serialize the complete chunk in RAM and calculate payload CRC/SHA plus
   header CRC.
3. Program the immutable slot.
4. Read it back and strictly decode/verify it.
5. Append a metadata generation referencing the sealed sequence.
6. Only after the metadata commit is verified may the chunk become available
   to the networking reader.

Recovery deliberately scans slots as well as metadata. A fully valid slot
whose sequence follows the last valid metadata generation is salvaged as an
orphan and receives a new metadata generation. A torn/CRC-invalid slot is never
exposed.

ACK:

1. Authenticate and fully parse a successful server response.
2. Match `boot_sequence`, `chunk_sequence`, `accepted_point_count`,
   `through_point_sequence`, and `content_sha256` to one sealed chunk that was
   actually sent; reject any mismatch or unknown identity.
3. Durably append/verify that exact ACK identity. If a compact contiguous
   frontier is used, derive it only from verified per-chunk proofs and stop at
   every hole/rejection; never accept a numeric frontier from the caller.
4. Append and verify an exact sector reclaim intent that binds the current slot
   ordinals before erasing.
5. Erase and read back the complete sector, then irreversibly program the
   intent's CRC-excluded consumed marker. Any partial marker is treated as
   consumed; this can leak an already erased sector after a fault but cannot
   resurrect authority to erase newer data.
6. Append the clear-intent journal generation. The sector may not be refilled
   before the consumed marker is durable. An out-of-order ACK beyond a hole
   cannot make either side of the hole reclaimable unless every affected chunk
   has its own durable exact proof.

If an ACK metadata write tears, the prior committed exact-ACK state wins.
Retrying already stored chunks is safe; reclaiming data on an uncommitted ACK is
not.

### 7.3 Full-storage behavior

The fill test wrote all 664 slots without ACK, durably represented the 665th
distinct chunk as a loss record, ACKed the oldest half, erased only wholly
reclaimable sectors, and refilled 332 slots. That scheduled test preserved its
prior unacknowledged set. The seven later adversarial fallback/loss/corruption
probes are now part of the passing 51/51 suite; this remains provisional workload
output pending independent review, not a physical safety proof.

Production pressure policy remains:

1. reclaim verified ACKed sectors;
2. coalesce future stationary heartbeats and expose the pressure state;
3. retain movement/transition chunks and the reserved emergency record;
4. if no safe detailed slot remains, increment/persist an explicit coverage
   gap and continue local metrics; and
5. never silently overwrite unacknowledged movement data.

## 8. LittleFS segment-log prototype

The fallback is modeled as append-only segment files, not one filesystem file
per chunk. Each segment contains 32 immutable chunks. Declared assumptions:

- two 4-KiB emergency blocks remain outside detailed telemetry pressure;
- two blocks cover root/metadata-pair baseline;
- 51 blocks (15% of the partition) remain an operational/COW reserve;
- a 32-chunk segment consumes 13 4-KiB data blocks;
- each chunk/ACK/directory metadata commit programs a modeled 512 bytes; and
- dynamic block allocation is idealized round-robin for the wear calculation.

That yields 21 segments × 32 chunks = 672 chunks. The fill test rejected chunk
673, ACKed the oldest half, reclaimed ten complete 32-chunk segments, refilled
320 chunks, and preserved the old unacknowledged tail.

The LittleFS model intentionally gives the fallback a fair packing advantage,
but it is not a trace of `esp_littlefs`. LittleFS actually uses CRC-protected
metadata pairs, copy-on-write file structures, and dynamic wear leveling. Its
upstream design also explains that wear leveling is dynamic rather than static.
See the primary [littlefs design](https://github.com/littlefs-project/littlefs/blob/master/DESIGN.md)
and [littlefs repository](https://github.com/littlefs-project/littlefs).

Before selecting the fallback, compile the exact Arduino-ESP32/ESP-IDF port,
instrument its block-device read/program/erase callbacks, and replace every
modeled number in this section with traces from the same 10,000-cycle workload.

## 9. Random power-cut and wear workload

### 9.1 Deterministic scenario

For sequences 0–9,999:

- write one distinct full-size logical chunk;
- for one in fifty seal attempts, choose uniformly between a cut during data,
  after complete data/before metadata, or during metadata;
- retry only when recovery does not expose that exact sequence/digest;
- every eight sequences, prepare the eligible sent manifest through the
  scheduled target and persist each chunk's exact ACK evidence;
- for one in one hundred ACK attempts, tear the metadata update and retry it;
- reclaim only committed ACKed data; and
- finally ACK/reclaim all data and assert no unacknowledged tail remains.

Both candidates receive the same pseudorandom decisions from seed `0xD06`.
That scheduled workload checked content before each ACK it chose to issue, but
it never tested the public model API with an unknown/skipped sequence. The
adversarial `acknowledge_through(999)` reproduction exposes that omission and
invalidates the workload's recovery/reclaim conclusion.

### 9.2 Corrected host-model results

These provisional deterministic figures come from fresh-instance recovery over
the byte-addressed flash image. They do not pass the host acceptance matrix and
do not establish storage safety, ESP32 timing, page programming, flash
endurance, or energy.

| Measurement | Raw ring | LittleFS segment model |
| --- | ---: | ---: |
| successful new seals | 10,000 | 10,000 |
| simulated power cuts | 216 | 210 |
| recoveries | 216 | 210 |
| valid pre-metadata chunks salvaged | 48 | 0 |
| incomplete/uncommitted writes rolled back | 100 | 195 |
| final unacknowledged chunks | 0 | 0 |
| coverage-gap events | 0 | 0 |
| programmed bytes | 21,734,346 | 24,151,808 |
| programmed bytes/successful seal | 2,173.435 | 2,415.181 |
| erased bytes | 24,570,880 | 22,781,952 |
| erased bytes/successful seal | 2,457.088 | 2,278.195 |
| data/dynamic sector erase min–max | 15–16 | 16–17 |
| raw metadata sector erase min–max | 470–472 | n/a in abstract FS model |
| maximum modeled recovery read | 1,376,256 B | 12,800 B |
| recovery read at declared 20 MiB/s | 65.625 ms | 0.610 ms |

The raw run injects 53 data, 47 slot-commit, 48 post-commit, 44 seal-metadata,
11 ACK-metadata, four journal-rollover, and nine reclaim cuts. It rejects 100
torn slots and salvages 48 fully committed orphans. Every recovery constructs
a new runtime from flash bytes. The exact-ACK tests separately cover unknown,
unsent, wrong-identity/hash/count/bounds receipts and durable out-of-order
holes.

### 9.3 What the wear numbers mean

They are operation-distribution evidence, not flash-lifetime guarantees.

- The raw data ring is balanced to within one erase because its cursor walks
  sectors monotonically.
- The two raw metadata journal sectors are the hotspot at 470–472 erases for
  the modeled 10,000-cycle workload. The append-only 128-byte journal reduces
  what would otherwise be one sector erase per metadata change.
- The LittleFS result assumes idealized dynamic allocation. Upstream littlefs
  explicitly describes best-effort dynamic wear leveling; the actual port must
  be traced.
- No endurance rating is assumed because the exact installed flash component
  and vendor guarantee have not been established in this repository.

## 10. Corruption and recovery behavior

Focused scheduled tests inject a corrupt middle chunk after three valid chunks.
Those tests report that both models:

- reject the corrupt identity;
- keep both valid neighbors readable;
- increment corruption and explicit coverage-gap counters; and
- avoid treating metadata references as proof that bad payload bytes are valid.

Additional codec tests flip a header byte, flip a payload byte, truncate a
chunk, append trailing bytes, and create semantically invalid but structurally
packable points. Every codec case is rejected. These results are not host
storage acceptance by itself. The suite now separately treats a CRC-invalid
payload with a durable exact-ACK marker as already synchronized and reclaimable,
while an unacknowledged corrupt payload keeps its validated global ordinal in
quarantine and creates a durable loss record. An unreadable committed header
forces read-only recovery rather than sequence reuse. Journal v2's irreversible
consumed-intent marker prevents an older reclaim authorization from erasing a
later refill even when that refill's payload is corrupt, and sparse loss ACKs
finalize locally when the intervening live chunk closes the prefix. The seven
reproduced fallback/loss/corruption cases remain permanent regressions; the
complete matrix still requires independent acceptance.

The physical implementation must distinguish:

- **expected incomplete write:** old state or a fully verified orphan wins;
- **post-write corruption:** bad chunk is omitted and an explicit loss marker
  is persisted; valid neighbors remain uploadable; and
- **unreadable metadata journals:** expose strict-scan salvage read-only, mark
  metadata state unknown/degraded, and never append, ACK or reclaim on inference.

## 11. Legacy Track v2 migration/export

### 11.1 Storage coexistence

V2 stays in the dedicated `tracknvs` partition. V3/outbox data uses the
currently unused `spiffs` region. Therefore upgrade does not need in-place
conversion and downgrade cannot mistake V3 slot bytes for V2 NVS values.

During future firmware integration, support partition-label compatibility:

1. new full-flash images may rename the same region to `cloudout` with an
   appropriate data subtype;
2. upgraded devices may still have the old `spiffs` label because a firmware
   binary alone does not install a new partition table; and
3. lookup may accept `cloudout` first and the exact legacy `spiffs` label/offset
   second, but must verify offset and size before any erase.

Phase 0 does not rename or format the partition.

### 11.2 Stable conversion

The prototype reserves native-invalid `boot_sequence=0` for the one-time V2
snapshot. Native V3 boot sequences begin at 1. For four legacy slots:

```text
point_sequence = slot * 2048 + point_index
chunk_sequence = slot * 32 + converted_chunk_index
```

Those disjoint ranges make retries stable. Conversion:

- validates all V2 coordinate/minute fields first;
- reconstructs UTC at second `00` from the Track metadata date and
  minute-of-day;
- accepts a greater-than-twelve-hour backwards minute jump as UTC midnight;
- rejects smaller backwards jumps as corrupt ordering;
- packs at most 96 converted points per V3 chunk;
- uses time quality `LEGACY_MINUTE`;
- sets `FIX_VALID | TIME_TRUSTED | LEGACY_V2`;
- sets speed to unavailable and satellites to unknown; and
- exports movement and stationary states as `unknown`.

The conversion does **not** set movement/stationary/low-quality flags from
coordinate spacing. V2's admission filter and minute precision cannot establish
those facts.

### 11.3 Upgrade state machine

Required future firmware behavior:

1. Freeze a validated manifest of all readable V2 meta/chunk blobs, including
   per-slot digest and conversion version.
2. Keep existing AP V2 JSON/CSV/GeoJSON export working throughout migration.
3. Deterministically convert/export V2 through the schema above; do not need to
   duplicate every converted point into the local raw ring if the streaming
   uploader can retry from immutable V2 NVS plus manifest.
4. Persist cloud ACKs for every migration chunk in an A/B migration record.
5. Mark migration complete only when the complete manifest is ACKed and the
   state record is read-back verified.
6. Retain original V2 data by default. Reclaim it only after verified completion
   plus an explicit retention policy/user action; never as a side effect of
   seeing Track version 3.
7. On downgrade, old firmware sees its unchanged V2 records and ignores the
   unused/raw `spiffs` region. On re-upgrade, the manifest makes cloud retry
   idempotent.

## 12. Mandatory physical ESP32-S3 gate

Host recovery/reclaim acceptance is **review/open**. Even after that host gate
passes, the raw decision remains unshippable until this exact-target test is
recorded.

### 12.1 Harness

- one Seeed Studio XIAO ESP32-S3 with the same flash component/revision as the
  intended collar build;
- current production firmware services (GNSS ingestion and LED tick) running;
- a controllable MOSFET/relay power fixture independent of the target;
- serial or secondary persistent oracle for intended sequence/digest/ACK;
- flash wrapper counters around partition read/program/erase calls;
- randomized cut delays that cover data erase, data program, readback,
  metadata append, metadata rollover, ACK update, and reclaim erase; and
- the exact compiler/framework pins in `platformio.ini`.

Test both raw and instrumented LittleFS candidates on freshly erased partitions
with the same generated chunks and cut schedule.

### 12.2 Minimum executions

1. 10,000 successful seal/ACK/reclaim cycles per candidate.
2. At least 1,000 actual asynchronous power removals per candidate distributed
   across all write boundaries.
3. Full partition with zero ACKs, pressure coalescing, explicit gap, partial
   ACK, sector-aligned reclaim, and refill.
4. Corrupt/torn metadata A, metadata B, newest chunk, middle chunk, both slots
   in one sector, and emergency record.
5. Reboot after every injected cut and compare strict recovered identities and
   digests to the oracle.
6. Long route export/upload reads concurrent with capture, proving no slot is
   mutated or erased while a reader owns it.
7. Downgrade/re-upgrade with four populated V2 slots and interrupted migration.

### 12.3 Pass criteria

- zero false ACKs and zero reclamation of unacknowledged data;
- zero invalid/torn chunks exposed as valid;
- every completed pre-cut chunk either recovered exactly or represented by one
  explicit coverage gap; never silently absent;
- valid neighbors survive a corrupt chunk;
- full storage never overwrites the unacknowledged tail;
- recovery completes within a measured two-second boot budget at p99;
- no unbounded allocation and less than 2.5 KiB peak chunk-working RAM beyond
  the fixed caller/network buffers;
- maximum GNSS/LED service gaps stay within existing firmware test/physical
  budgets, with actual values recorded;
- raw data-sector erase spread no greater than two after the 10,000-cycle run;
- raw metadata-sector maximum must be frozen only after the host model passes
  and real driver traces are captured (the current rejected candidate observed
  472, which is not an acceptance baseline);
- program/erase byte totals and latency distributions are published for both
  candidates, replacing model estimates; and
- factory reset, old partition label, new partition label, and corrupt region
  each fail safely without erasing an unverified address range.

If raw fails any integrity criterion, select LittleFS only if it passes the same
criterion. If both fail, stop Phase 2 firmware integration and redesign the
outbox; capacity or schedule pressure is not a reason to weaken integrity.

## 13. Decision matrix

Capacity/format rows are design arithmetic. Program, salvage, recovery-scan,
and wear rows are provisional outputs from the host candidate awaiting independent acceptance; both a
passing host matrix and physical evidence are still required.

| Criterion | Raw ring | LittleFS segment log |
| --- | --- | --- |
| Capacity in declared layout | 664 chunks | 672 modeled chunks |
| Format fit | exact single fixed record | general filesystem abstraction |
| Program amplification | lower in current byte model; page/driver trace pending | declared idealized estimate; real trace pending |
| Pre-metadata full-write salvage | strict committed-slot scan covered by fresh-mount tests | modeled old/new state plus caller retry |
| Recovery scan | full fixed partition scan from bytes | smaller idealized traversal |
| Wear | explicit cursor; metadata hotspot visible | mature best-effort dynamic leveling |
| Implementation risk | custom flash state machine | larger dependency/configuration surface |
| Debug/audit surface | small but project-owned | upstream filesystem internals |
| Static/non-outbox files | not supported by design | naturally supported |
| Current decision | **accepted design direction; host and physical acceptance open** | fallback if the mandatory gates overturn the design |

The raw choice is specific to an immutable telemetry outbox. It is not a general
claim that custom storage is superior to LittleFS.

## 14. Phase handoff

Phase 0B codec artifacts are ready for frozen-protocol compatibility review,
and a corrected host storage candidate exists whose seven reproduced adversarial
failures now pass as regressions, but independent acceptance is still open. They
do not close Phase 0 or authorize Phase 1 schema
or firmware implementation while the host, physical, and map gates remain open:

1. Freeze the 16-byte point and 92-byte header in shared golden vectors before
   copying the codec into C++/TypeScript.
2. Cloud ingestion must use the stable device/boot/chunk and point identities,
   reject identity/content mismatch, and preserve legacy time/metric nulls.
3. Database/UI vocabulary must distinguish observed stationary, movement
   evidence, low quality, gap/unknown, and legacy V2.
4. Firmware integration must dual-read V2 and must not format either storage
   region automatically after a version mismatch.
5. The raw-outbox design direction is accepted by ADR-0007, while host
   implementation acceptance awaits independent review and HIL evidence is open. Section
   12 remains a mandatory review trigger and release gate after the host matrix
   passes; failure reopens the storage choice.
6. Phase 0B does not authorize networking code, account credentials, or any
   claim that cloud synchronization is implemented.
