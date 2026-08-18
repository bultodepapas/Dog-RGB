# Phase 0 host outbox independent-review packet

**Status:** ready for an independent reviewer; **not an acceptance ledger**.

This packet reduces P0-R1 to a reproducible review. It was prepared by the
candidate workstream and cannot accept its own implementation. The required
final file, `phase0-outbox-independent-review.md`, must remain absent until a
reviewer other than the implementation author completes every item and records
an explicit `accepted` or `rejected` decision.

## 1. Candidate identity

| Item | Frozen value |
| --- | --- |
| Hardened candidate origin commit | `255136d6ca9b5bcc128af338c444476ebea64a26` |
| `storage_model.py` bytes | `93,767` |
| `storage_model.py` SHA-256 | `9d7f0c059399708b4a3162d231a18d00c8378e85c4837f30b5ccd1369574b3d8` |
| Canonical evidence schema | `dog-rgb-cloud-phase0b/1` |
| Canonical evidence bytes | `9,505` |
| Canonical evidence SHA-256 | `be28dcad59cb034a1a9aa28a8ee82d03b7bd343f8225f5a63a50efd8aed13475` |
| Frozen host matrix | exactly `51/51` tests |
| Decision before independent review | `awaiting_independent_review` |

The reviewer may use a later repository commit only when the readiness verifier
proves that the seven candidate source artifacts are unchanged from the origin
commit. Any candidate-source difference is a new candidate and invalidates this
packet until its hashes, regressions, and documentation are deliberately
rebaselined.

## 2. Clean-room reproduction

Use a fresh clone or clean worktree. Do not edit the ledger before running the
verifier because a dirty tree is intentionally not review-eligible.

```powershell
git status --short
python tools/cloud_phase0/review_readiness_test.py -v
python tools/cloud_phase0/verify_review_candidate.py
```

Expected results:

- the focused verifier suite passes `4/4`;
- the readiness JSON reports `automated_checks_passed: true`;
- `review_eligible` is `true` only in a clean tree without `--allow-dirty`;
- `candidate_unchanged_from_origin`, `storage_artifact.matches`,
  `host_matrix.passed`, and `canonical_evidence.matches` are all `true`;
- all seven named historical regressions are present;
- the tool still reports `decision: awaiting_independent_review` and
  `acceptance_may_be_decided_by_this_tool: false`.

`--allow-dirty` exists only so the implementation author can test changes to the
verifier. Its output always has `review_eligible: false` and is not review
evidence. The full command normally takes several minutes because it executes
the 51-test byte-image matrix and regenerates the deterministic 10,000-cycle
evidence rather than trusting copied report values.

## 3. Seven mandatory historical regressions

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
`docs/cloud/phase0-outbox-independent-review.md` with:

1. reviewer name or stable identity and independence statement;
2. UTC completion timestamp and exact 40-character reviewed commit;
3. the complete readiness JSON results or its recorded source/evidence hashes;
4. commands and `4/4`, `51/51`, artifact, and evidence outcomes;
5. one explicit decision for each `OUTBOX-R1` through `OUTBOX-R12`;
6. findings with severity and disposition;
7. final lowercase decision exactly `accepted` or `rejected`;
8. reviewer signature.

For this DIY project, a cryptographic Git signature is recommended but optional.
The minimum signature is a stable reviewer identity, UTC timestamp, reviewed
commit, explicit decision, and normal Git authorship. The implementation author
must not author or sign the independent decision.
