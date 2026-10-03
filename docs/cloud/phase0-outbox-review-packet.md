# Phase 0 host outbox independent-review packet

**Status:** review procedure; **not an acceptance ledger**. The [independent remediation ledger](phase0-outbox-remediation-review-2026-10-02.md) accepts candidate `fb6dbef` on 2026-10-02.

This packet reduces P0-R1 to a reproducible review. It was prepared by the
candidate workstream and cannot accept its own implementation. The original
`phase0-outbox-independent-review.md` is a historical rejection and must remain
unchanged. A separate reviewer records the corrected decision in
`phase0-outbox-remediation-review-2026-10-02.md` after completing every invariant.

## 1. Candidate identity

| Item | Frozen value |
| --- | --- |
| Prior review baseline (required ancestor) | `d58be9a0f4d9e9a31f5a302040b5575e27b1cfd0` |
| Source identity | nine static path/size/SHA-256 pins in `verify_review_candidate.py` |
| `storage_model.py` bytes | `101,136` |
| `storage_model.py` SHA-256 | `0106a89c140d26439839a2c7ad80950d72b707049d52fe4c35476656050c85b7` |
| Canonical evidence schema | `dog-rgb-cloud-phase0b/1` |
| Canonical evidence bytes | `9,197` (UTF-8/LF, one trailing newline) |
| Canonical evidence SHA-256 | `98978d48429f446c9ac82ad91cbba46936d5aed788d9a836a1338c4490831e9c` |
| Frozen host matrix | exactly `67/67` tests |
| Decision before independent review | `awaiting_independent_review` |

The verifier compares every source artifact against both committed HEAD and
worktree bytes. Any source difference invalidates this candidate until explicitly
rebaselined. The reviewed 40-character HEAD is recorded in the readiness JSON;
the ancestor is provenance only, not a claim that corrected code equals old code.
See the [remediation contract](phase0-outbox-remediation-2026-10-02.md).

## 2. Clean-room reproduction

Use a fresh clone or clean worktree. Do not edit the ledger before running the
verifier because a dirty tree is intentionally not review-eligible.

```powershell
git status --short
python tools/cloud_phase0/review_readiness_test.py -v
python tools/cloud_phase0/verify_review_candidate.py
```

Expected results:

- the focused verifier suite passes `10/10`;
- the readiness JSON reports `automated_checks_passed: true`;
- `review_eligible` is `true` only in a clean tree without `--allow-dirty`;
- `candidate_matches_manifest`, `storage_artifact.matches`,
  `host_matrix.passed`, and `canonical_evidence.matches` are all `true`;
- all thirteen named mandatory regressions are present;
- the tool still reports `decision: awaiting_independent_review` and
  `acceptance_may_be_decided_by_this_tool: false`.

`--allow-dirty` exists only so the implementation author can test changes to the
verifier. Its output always has `review_eligible: false` and is not review
evidence. The full command normally takes several minutes because it executes
the 67-test byte-image matrix and regenerates the deterministic 10,000-cycle
evidence rather than trusting copied report values.

## 3. Thirteen mandatory regressions

The reviewer must inspect the implementation path and the assertion, not only
confirm that the method name exists.

| Failure that must remain impossible | Permanent regression |
| --- | --- |
| A stale reclaim intent erases a later refill | `test_stale_reclaim_intent_cannot_erase_a_refilled_sector` |
| Journal fallback permits outbox-sequence reuse after a cleared loss | `test_empty_loss_tombstone_prevents_sequence_reuse_after_journal_fallback` |
| Maximum loss-range recovery allocates or iterates across the range | `test_recovery_of_maximum_loss_interval_is_bounded` |
| A new loss disappears during the prior loss ACK transition | `test_new_loss_is_durable_during_prior_loss_ack_transition` |
| An ACKed corrupt payload is reclassified as unsynchronized loss | `test_acked_corrupt_payload_is_not_misclassified_as_unsynchronized_loss` |
| A consumed fallback intent erases a corrupt refilled slot | `test_consumed_stale_intent_cannot_erase_a_corrupt_refilled_slot` |
| A sparse acknowledged loss needs a duplicate server ACK after the live hole closes | `test_acknowledged_sparse_loss_cannot_bridge_a_live_unacked_chunk` |

The rejected review adds three mandatory permanent regressions in
`test_integrity.py`, included by normal test discovery:

| Failure that must remain impossible | Permanent regression |
| --- | --- |
| Reclaimed logical identity is reused | `test_acknowledged_reclaimed_chunk_identity_cannot_be_reused` |
| Old receipt acknowledges a new ordinal of a reused identity | `test_stale_receipt_cannot_ack_resealed_logical_identity` |
| Corrupt committed first loss permits writable empty fallback | `test_corrupt_committed_first_loss_fails_closed_before_journal_commit` |

The follow-up marker-corruption finding adds three further requirements:
`test_commit_marker_damage_preserves_slot_identity`,
`test_commit_marker_damage_preserves_loss_and_journal`, and
`test_damaged_commit_marker_with_invalid_body_is_read_only`. They cover every
single-bit marker change, whole-marker erasure, and marked-invalid bodies;
inspect the body-before-marker recovery contract in the remediation report.

Supporting corruption/identity tests must also convince the reviewer that a
valid-header/corrupt-payload slot retains its global ordinal, an unreadable
committed header forces read-only operation, and a quarantined identity cannot
be sealed into a second committed copy.

## 4. Manual invariant review

Every row needs an explicit `accepted` or `rejected` statement in the final
ledger. “Covered by tests” is not sufficient.

| ID | Reviewer question | Minimum evidence to inspect |
| --- | --- | --- |
| OUTBOX-R1 | Is the mounted state derived only from NOR bytes, never surviving Python/RAM state? | Fresh-instance recovery paths and blank/factory mount tests. |
| OUTBOX-R2 | Can a global outbox ordinal or logical chunk identity ever be reused after ACK, reclaim, loss, corruption, or fallback? | Slot headers, high-water marks, tombstones, quarantine, exhaustion tests. |
| OUTBOX-R3 | Can any ACK advance without the exact device/boot/chunk/outbox/digest member of the sent manifest? | Manifest construction, ACK comparison, replay/no-op tests. |
| OUTBOX-R4 | Can a hole, sparse loss, or unverified identity be bridged when computing the contiguous reclaim prefix? | ACK-marker scan, loss interval math, sparse-loss transition. |
| OUTBOX-R5 | Can sector reclaim erase an unauthorized tail or a slot refilled after the original intent? | Intent binding, erase boundary, consumed marker, all reclaim cut stages. |
| OUTBOX-R6 | Are first loss, coalesced loss, deferred loss, loss ACK, and empty tombstone atomic across both emergency sectors and every cut? | A/B generation selection, bounded record, cut matrices, exact loss ACK. |
| OUTBOX-R7 | Does every ambiguous/corrupt metadata, header, payload, duplicate, or half-range generation case fail closed without inventing progress? | Journal fallback, emergency cross-checks, quarantine/read-only behavior. |
| OUTBOX-R8 | After every modeled partial program/erase/commit, does a new model instance mount an old valid or new valid state only? | Slot, ACK, journal, reclaim, and emergency cut matrices. |
| OUTBOX-R9 | Does the flash model enforce one-way programming, aligned sector erase, and independent emergency-sector geometry? | `NorFlash`, layout assertions, raw byte images. |
| OUTBOX-R10 | Are interval recovery, RAM use, counters, and `uint64` boundaries bounded and fail-closed? | Maximum-range and exhaustion cases; absence of range-sized structures. |
| OUTBOX-R11 | Do Track v3 bytes, identities, time quality, reference fixtures, and legacy-v2 limitations remain frozen? | Codec vectors, fixture manifest, converter tests, canonical evidence. |
| OUTBOX-R12 | Are host workload/wear figures treated as provisional and the physical ESP32-S3 gate still mandatory? | Reports, ADR-0007, and absence of production firmware authorization. |

## 5. Findings and decision rules

Classify every finding:

- **high integrity:** could erase unacknowledged data, invent ACK/reclaim progress,
  reuse identity, hide loss, or mount ambiguous state as writable;
- **medium:** weakens determinism, boundedness, diagnostics, or reviewability
  without directly demonstrating data loss;
- **low:** clarity or maintainability issue with no integrity impact.

Acceptance requires no unresolved high-integrity finding and an explicit
acceptance for all 12 invariant rows. A failing image/seed must be preserved as
a permanent regression. If the reviewer changes candidate code, the decision is
`rejected` for the reviewed commit; land the correction, rebaseline the candidate
through the plan, and perform a new independent review.

## 6. Required final ledger

The independent reviewer creates
`docs/cloud/phase0-outbox-remediation-review-2026-10-02.md` with:

1. reviewer name or stable identity and independence statement;
2. UTC completion timestamp and exact 40-character reviewed commit;
3. the complete readiness JSON results or its recorded source/evidence hashes;
4. commands and `10/10`, `67/67`, artifact, and evidence outcomes;
5. one explicit decision for each `OUTBOX-R1` through `OUTBOX-R12`;
6. findings with severity and disposition;
7. final lowercase decision exactly `accepted` or `rejected`;
8. reviewer signature.

For this DIY project, a cryptographic Git signature is recommended but optional.
The minimum signature is a stable reviewer identity, UTC timestamp, reviewed
commit, explicit decision, and normal Git authorship. The implementation author
must not author or sign the independent decision.
