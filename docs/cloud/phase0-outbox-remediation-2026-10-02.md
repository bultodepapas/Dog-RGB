# Host outbox integrity remediation — 2026-10-02

**Status:** implemented; independent re-review pending. Host model only.

**Baseline:** `d58be9a0f4d9e9a31f5a302040b5575e27b1cfd0`. The [initial rejection](phase0-outbox-independent-review.md) and its raw readiness JSON are immutable historical evidence.

## Contract and implementation

| Finding | Correction | Permanent evidence |
| --- | --- | --- |
| R2/R3: reclaimed logical identity can acquire a new ordinal; stale receipt can acknowledge it | Persist `(device UUID, boot, chunk)` high-water in journal v3 and emergency v2. Reject historical identities before reclaim/write. Resident exact retries remain idempotent. | `test_integrity.py`: original identity/stale-receipt reproductions; 40-cycle reclaim/rollover; orphan-slot recovery; full-loss ACK/reclaim and deferred promotion |
| R7: corrupt committed first loss falls back to empty/writable state | Any committed-invalid journal or emergency record makes mutation/reclaim read-only; valid slots remain readable. Torn uncommitted appends retain old/new recovery. | Original first-loss reproduction; corrupt journal after identity reclaim; stronger fallback assertions in `test_phase0.py` |
| R6/R8: loss operations omit journal cut injection | Forward cuts through first, coalesced and deferred loss plus loss ACK. Force journal rollover in tests and assert that a cut actually occurs. | Nine loss/journal combinations, four loss-ACK boundaries, three full-storage emergency boundaries |
| R2/R7 follow-up: marker corruption hides a confirmed record | Recover complete CRC/semantic-valid bodies even if the commit marker is partial or erased. Marked invalid metadata/header remains read-only; valid-header corrupt payload retains its ordinal in quarantine. | All 64 single-bit marker changes plus whole-marker erasure for slots, journal and loss; invalid-body cases |
| R10: aggregate overflow reaches packing after erase | Preflight encoded counters, reason/receipt generation, identity and envelope before destructive operations. Overflow leaves flash unchanged. | Pending/deferred `UINT64_MAX` tests and invalid envelope on an ACKed full ring |
| Evidence depends on platform newline translation | Write UTF-8 + LF + one terminal newline directly to binary stdout; hash exact bytes. | Windows-style text-wrapper test; reject CRLF, missing newline, changed schema or bytes |

One flash outbox belongs to one device UUID. New seals are serialized and increase `(boot_sequence, chunk_sequence)` lexicographically. Chunk sequence may reset at a strictly newer boot; neither component wraps. Delayed historical imports cannot be interleaved with newer native records: legacy boot-zero import must precede native boots or use a separately specified migration. Future firmware must durably allocate boot identity before generating telemetry.

A full-ring omission consumes both its global ordinal and logical identity in the same emergency commit. Its latest exact retry returns `False` (still omitted) without allocating or incrementing counters again. After loss ACK/tombstone or coalescing replaces that retry token, the high-water rejects the old identity. Emergency transitions retain the watermark across pending, acknowledged, deferred, promoted and empty states; recovery merges journal, emergency and validated slot-header watermarks.

Journal v3 uses the former 36-byte reserved tail for UUID16 + boot32 + chunk32 + zero12. Emergency v2 uses the same tail inside its 120-byte deferred region; deferred presence is determined only by its 84-byte payload. Both watermarks are CRC-covered. Geometry stays 336 sectors, 332 data sectors, 664 slots. Old journal v2/emergency v1 committed records are refused as writable; no automatic migration or format is provided by this host prototype.

A marked invalid record can hide erased history, so fallback sacrifices availability and preserves the image for explicit recovery. Partial erase with a surviving marker is conservatively read-only. A complete CRC/semantic-valid body, however, recovers as the new state even if the commit marker is partial or entirely erased: the writer finishes the body before starting the marker. This also covers the body-before-marker crash boundary. Slot recovery without a marker requires both valid header and payload; a partial body with erased commit/ACK markers remains incomplete. A partial ACK marker still means unacknowledged, never invented acceptance.

The fault model covers modeled interrupted writes/erases and detectable corruption, including each commit-marker bit and whole-marker erasure with an intact body. Erasure of every publication indicator combined with invalid body bytes is indistinguishable from an incomplete append; undetectable CRC collisions and arbitrary simultaneous media destruction are not proved safe. Physical power-loss/retention/error behavior still requires target tests. The changed partial-commit tests assert the new valid state only after the exact ACK/body precedes the marker; no unauthorized reclaim is permitted.

Loss totals are uint64; reason and retained ACK-generation fields are uint32. Exceeding an encoded width raises before flash mutation, with no silent saturation. Generation-selection arithmetic remains wrap-safe, but the retained receipt width bounds representable loss-ACK generations; widening that field requires a future format change.

## Reproduction and frozen evidence

```sh
python -m unittest discover -s tools/cloud_phase0 -p 'test_*.py' -v
python tools/cloud_phase0/review_integrity_test.py -v
python tools/cloud_phase0/review_readiness_test.py -v
python tools/cloud_phase0/verify_review_candidate.py
```

- Host matrix: **67/67** (original 51 plus 16 permanent integrity tests).
- Compatibility review suite: **3/3**, included in the host matrix through `test_integrity.py`; no expected-failure suppression.
- Focused verifier suite: **10/10**, separate from the host matrix.
- Canonical evidence: **9,197 bytes**, SHA-256 `98978d48429f446c9ac82ad91cbba46936d5aed788d9a836a1338c4490831e9c`.
- Storage source: **100,829 bytes**, SHA-256 `c4401942eabec830917998b8c3257b27fc8c740a7e12604288b95b37c07cffbb`.
- All nine source pins are explicit in `verify_review_candidate.py`; both committed and worktree bytes must match. Origin ancestry records provenance, not content equality to the rejected baseline. Dirty or `--allow-dirty` runs are ineligible.

The marker-recovery change alters workload accounting; the regenerated 10,000-cycle raw-ring figures supersede the August feasibility table. LittleFS figures, 664-slot raw capacity, scan geometry and total 216 raw cuts are unchanged. Host figures remain provisional.

| Raw workload | Rejected candidate | Corrected candidate |
| --- | ---: | ---: |
| Programmed bytes | 21,734,346 | 21,639,350 |
| Program bytes / successful seal | 2,173.435 | 2,163.935 |
| Erased bytes | 24,570,880 | 24,452,096 |
| Erase bytes / successful seal | 2,457.088 | 2,445.210 |
| Recovered orphan chunks | 48 | 95 |
| Rolled-back incomplete chunks | 100 | 53 |
| Metadata erase min / mean / max | 470 / 471 / 472 | 467 / 468 / 469 |
| Mean data-sector erases | 15.238 | 15.169 |
| Seal metadata / journal rollover / ACK metadata cuts | 44 / 4 / 11 | 45 / 1 / 13 |

The [review packet](phase0-outbox-review-packet.md) requires a separate reviewer to decide all 12 invariants on a clean committed candidate.

No portal, firmware, partition table, Track v3 codec, fixture or legacy converter changes are included. M2B remains blocked pending independent host acceptance; M2C requires real ESP32-S3 flash, power-cut, timing and wear evidence.
