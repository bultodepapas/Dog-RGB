# Phase 0 host outbox remediation independent review

**Reviewer:** Luna AI (Codex independent review agent, `gpt-6-luna` / max), separate from the remediation author. I made no candidate implementation edits and authored only this review ledger. I reported a commit-marker recovery defect and a boolean-input preflight defect during review; the remediation author implemented both. This is an AI review, not human signoff.

**Completed:** 2026-10-02 18:10 UTC

**Reviewed commit:** `fb6dbef1bc9443f7045563ac770ca4a36de95686`

**Candidate origin:** `d58be9a0f4d9e9a31f5a302040b5575e27b1cfd0`

## Reproduction and artifact identity

I ran the clean-room verifier on the reviewed commit:

```text
python tools/cloud_phase0/review_readiness_test.py -v
python tools/cloud_phase0/verify_review_candidate.py > /tmp/dog-rgb-independent-readiness.json
```

The focused verifier suite passed **10/10**. The full verifier exited 0 and recorded `review_eligible: true`, `worktree_clean: true`, zero dirty entries, the required origin as an ancestor, all nine candidate source artifacts matching their static manifest, **67/67** host tests, all **13/13** required regressions, matching canonical evidence, and no failures. The verifier correctly left the decision at `awaiting_independent_review` and states that it cannot decide acceptance.

The readiness JSON is [`phase0-outbox-remediation-readiness-2026-10-02.json`](phase0-outbox-remediation-readiness-2026-10-02.json), SHA-256 `10820702b766bb349d5ebb01a0fcd8fa60544e09b3e7422039876fddb47bca98` (8,299 bytes). At verifier capture, the worktree was clean. The readiness artifact and this independent ledger were added afterward and are not part of the reviewed candidate source.

The pinned storage artifact is 101,136 bytes with SHA-256 `0106a89c140d26439839a2c7ad80950d72b707049d52fe4c35476656050c85b7`. Canonical evidence is 9,197 UTF-8/LF bytes with one trailing newline and SHA-256 `98978d48429f446c9ac82ad91cbba46936d5aed788d9a836a1338c4490831e9c`.

The review also inspected the implementation and assertions for all 13 required regressions. A targeted run of the three preserved R2/R3/R7 reproductions and three marker-damage tests passed **6/6** on the preceding candidate commit; the final clean-room 67-test run covers those tests plus the final input-preflight change.

## Invariant decisions

| ID | Decision | Evidence and assessment |
| --- | --- | --- |
| OUTBOX-R1 | **accepted** | `RawRingModel` reconstructs storage state from flash bytes on construction, `restart`, and power-cut recovery. Network-send provenance resets on mount. Fresh-instance and blank/factory tests confirm runtime fields do not provide recovery state. |
| OUTBOX-R2 | **accepted** | Journal v3 and emergency v2 retain a device/boot/chunk high-water. Recovery merges it with validated slot headers; full-ring loss records consume the identity with the ordinal. New seals reject historical identities before reclaim or write. Reclaim, rollover, orphan, loss, and marker-damage regressions pass. |
| OUTBOX-R3 | **accepted** | ACK receipts must match the current slot's device, boot, chunk, digest, point count, and through-sequence, and that slot's outbox ordinal must be in the current runtime's sent set. The receipt omits an ordinal field, but durable logical-identity uniqueness prevents an old receipt from mapping to a different ordinal. |
| OUTBOX-R4 | **accepted** | The contiguous prefix stops at live unacknowledged slots and does not jump a sparse acknowledged loss over a live hole. The permanent sparse-loss regression covers finalization after the live chunk is ACKed. |
| OUTBOX-R5 | **accepted** | Reclaim intent records the exact sector ordinals, verifies them again before erase, and uses a CRC-excluded consumed marker before a sector can refill. Stale-intent, corrupt-refill, and reclaim-cut regressions preserve later data. |
| OUTBOX-R6 | **accepted** | First, coalesced, and deferred losses, loss ACK, and tombstone transitions use the two-sector A/B emergency store. Journal cut injection now reaches loss commits and loss ACKs; the regressions cover record, commit, and rollover cuts plus exact retry behavior. |
| OUTBOX-R7 | **accepted within the documented host fault model** | Invalid committed journal/emergency record bodies and unreadable slot headers with programmed markers fail closed. A complete CRC/semantic-valid body is recovered through partial or erased commit markers. A valid-header slot with corrupt payload is quarantined with its identity and ordinal retained; the ACKed-corrupt case remains distinct. Tests cover every single-bit marker change, whole-marker erasure with intact bodies, and marked-invalid metadata/headers. Old journal/emergency formats also mount read-only. The report explicitly leaves simultaneous loss of every publication marker plus invalid body bytes, CRC collisions, and arbitrary multi-fault media destruction unproved; this does not establish physical flash behavior. |
| OUTBOX-R8 | **accepted within the modeled cut points** | Slot, ACK, journal, reclaim, and emergency operations remount from fresh byte images across named partial program/erase/commit points. Tests assert a prior valid or fully validated new state, including loss-to-journal and loss-ACK journal cuts. The model does not sweep every physical byte timing or brownout behavior. |
| OUTBOX-R9 | **accepted** | `NorFlash` rejects 0-to-1 programming and permits only aligned single-sector erases. Layout assertions reserve independent journal/emergency erase sectors; geometry and raw-byte tests pass. |
| OUTBOX-R10 | **accepted** | Loss totals and ordinals are bounded to their encoded widths and preflighted before emergency-sector mutation. New slot identity/count inputs require exact integers before writes; overflow and invalid-envelope tests assert unchanged flash. Recovery bounds work by ring capacity rather than loss-range magnitude. The uint32 retained loss-ACK generation limits future representable generations and is documented as a format bound with fail-closed overflow. |
| OUTBOX-R11 | **accepted** | The final candidate does not change Track v3, reference fixtures, or the legacy-v2 converter. Codec vectors, fixture hashes, and converter boundaries remain in the 67-test matrix. The remediation report states that boot-zero legacy imports must precede native boots or use a separately specified migration. |
| OUTBOX-R12 | **accepted** | Capacity, wear, and recovery figures are identified as host estimates. The ADR and plan continue to block firmware acceptance on physical ESP32-S3 flash, power-cut, timing, wear, and energy evidence. No physical or field-safety evidence is claimed. |

## Findings and disposition

- **High, resolved — R2/R3 identity reuse and stale ACK mapping.** The independent repros are retained as permanent tests. Durable identity high-water now rejects the post-reclaim reuse that let an old receipt ACK a new ordinal.
- **High, resolved — R7 corrupt first-loss fallback.** A corrupt committed emergency record can no longer fall back to an older empty record as writable state. Marker-damage review extended this protection to partial and erased markers when the record body remains valid, and to invalid metadata bodies or unreadable slot headers with any programmed marker. Valid-header payload corruption remains quarantined with its identity and ordinal preserved.
- **Medium, resolved — R6/R8 missing loss journal cuts.** Cut labels now reach the journal append for loss recording and loss ACK; the tests force rollover and check that a cut was injected.
- **Medium, resolved — R10 counter overflow and input preflight.** Encoded counter overflow is rejected before emergency erase. Exact-integer checks reject boolean and floating-point identity inputs before slot mutation.
- **Medium, resolved — evidence newline portability.** Canonical JSON is emitted and hashed as explicit UTF-8/LF bytes independent of host text newline conversion.

No unresolved high-integrity finding remains within the reviewed host-model scope. Acceptance does not replace the mandatory target-device gate, and the documented multi-fault and CRC-collision limits remain outside this result.

**Reviewer signature:** Luna AI (Codex independent review agent, `/root/remediation_independent_review`)

**UTC:** 2026-10-02 18:10

**Reviewed commit:** `fb6dbef1bc9443f7045563ac770ca4a36de95686`

## Final decision

accepted
