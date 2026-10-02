"""Permanent regressions for the 2026-10-02 independent outbox review."""

from __future__ import annotations

import hashlib
import unittest
import uuid

from storage_model import (
    ERASE_BLOCK_BYTES, RAW_METADATA_RECORDS_PER_BLOCK, RAW_METADATA_RECORD_BYTES,
    RawRingModel, UINT64_MAX,
)


R7_EVENT_ID = uuid.UUID("c0a3a422-3a18-4ce7-b0fd-f13c52d43f91")
R7_RETRY_EVENT_ID = uuid.UUID("e81e3015-7ba0-4f51-a29f-84bb603c8f22")


def clear_one_set_bit(model: RawRingModel, offset: int, length: int) -> None:
    raw = model.flash.read(offset, length)
    for relative, value in enumerate(raw):
        for bit in range(8):
            mask = 1 << bit
            if value & mask:
                model.flash.program(offset + relative, bytes([value & ~mask]))
                return
    raise AssertionError("fixture range has no bit that can be cleared")


class OutboxIntegrityReviewRegression(unittest.TestCase):
    def test_acknowledged_reclaimed_chunk_identity_cannot_be_reused(self) -> None:
        old_digest = hashlib.sha256(b"previous chunk body").digest()
        new_digest = hashlib.sha256(b"different chunk body").digest()

        def post_reclaim_model() -> RawRingModel:
            model = RawRingModel(data_blocks=1)
            self.assertTrue(model.seal(0, old_digest, boot_sequence=7))
            sent = model.prepare_upload([0])
            self.assertEqual(len(sent), 1)
            self.assertTrue(model.acknowledge_exact(sent[0].receipt()))
            self.assertEqual(model.reclaim_acknowledged(), 1)
            return model

        for fresh_mount in (False, True):
            with self.subTest(fresh_mount=fresh_mount):
                candidate = post_reclaim_model()
                if fresh_mount:
                    candidate = RawRingModel.from_flash(candidate.flash_bytes, data_blocks=1)
                self.assertEqual(candidate.slots, {})
                self.assertEqual(candidate.next_outbox_sequence, 1)
                before = candidate.flash_bytes

                try:
                    accepted = candidate.seal(0, new_digest, boot_sequence=7)
                except (ValueError, RuntimeError, OverflowError):
                    accepted = False

                self.assertFalse(accepted, "candidate accepted a reclaimed logical identity again")
                self.assertEqual(candidate.flash_bytes, before)
                self.assertNotIn(1, candidate.slots)

    def test_stale_receipt_cannot_ack_resealed_logical_identity(self) -> None:
        digest = hashlib.sha256(b"same immutable logical chunk body").digest()
        model = RawRingModel(data_blocks=1)
        self.assertTrue(model.seal(0, digest, boot_sequence=7))
        old_manifest = model.prepare_upload([0])
        old_receipt = old_manifest[0].receipt()
        self.assertTrue(model.acknowledge_exact(old_receipt))
        self.assertEqual(model.reclaim_acknowledged(), 1)

        remounted = RawRingModel.from_flash(model.flash_bytes, data_blocks=1)
        before_reseal = remounted.flash_bytes
        try:
            resealed = remounted.seal(0, digest, boot_sequence=7)
        except (ValueError, RuntimeError, OverflowError):
            resealed = False

        if not resealed:
            self.assertEqual(remounted.flash_bytes, before_reseal)
            self.assertNotIn(1, remounted.slots)
            return
        if 1 not in remounted.slots:
            # A true return is safe if it is only an idempotent historical
            # no-op and no new global ordinal or slot was allocated.
            self.assertEqual(remounted.flash_bytes, before_reseal)
            self.assertEqual(remounted.next_outbox_sequence, 1)
            return

        new_manifest = remounted.prepare_upload([1])
        self.assertEqual(len(new_manifest), 1)
        before_ack = remounted.flash_bytes
        self.assertFalse(
            remounted.acknowledge_exact(old_receipt),
            "old receipt advanced a different outbox ordinal",
        )
        self.assertEqual(remounted.flash_bytes, before_ack)
        self.assertFalse(remounted.slots[1].acknowledged)

    def test_corrupt_committed_first_loss_fails_closed_before_journal_commit(self) -> None:
        model = RawRingModel(data_blocks=1)
        # This cut point commits the emergency record and remounts it, then
        # returns before record_loss appends the cross-reference journal entry.
        self.assertTrue(model.record_loss(
            missing_outbox_sequence=0,
            dropped_points=3,
            reason_mask=1,
            event_id=R7_EVENT_ID,
            cut_at="after_emergency_commit",
        ))
        self.assertEqual(model.emergency.state, "pending")
        self.assertEqual(model.journal_generation, 0)
        sector = model.emergency.sector
        # The emergency record CRC is at bytes 244..247; clearing a bit models
        # later corruption of the already committed record.
        clear_one_set_bit(model, sector * ERASE_BLOCK_BYTES + 244, 4)

        try:
            fresh = model.restart()
        except RuntimeError:
            return  # refusing to mount the ambiguous image is fail-closed

        self.assertTrue(fresh.loss_state_unknown or fresh.sequence_state_unknown)
        before = fresh.flash_bytes
        with self.assertRaises(RuntimeError):
            fresh.record_loss(
                missing_outbox_sequence=0,
                dropped_points=4,
                reason_mask=2,
                event_id=R7_RETRY_EVENT_ID,
            )
        self.assertEqual(fresh.flash_bytes, before)


def loss_ack(loss):
    return {
        "loss_id": loss.loss_id, "generation": loss.generation,
        "record_sha256": loss.record_sha256,
        "first_missing_outbox_sequence": loss.first_missing_outbox_sequence,
        "last_missing_outbox_sequence": loss.last_missing_outbox_sequence,
        "dropped_chunks": loss.dropped_chunks,
    }


class RemediatedStorageTests(unittest.TestCase):
    def test_commit_marker_damage_preserves_slot_identity(self):
        model = RawRingModel(data_blocks=1)
        model.seal(0, b"a" * 32, cut_at="after_slot_commit")
        for bit in range(-1, 64):
            with self.subTest(bit=bit):
                image = bytearray(model.flash_bytes)
                marker = model._slot_offset(0) + 120
                if bit == -1:
                    image[marker:marker + 8] = b"\xff" * 8
                else:
                    image[marker + bit // 8] |= 1 << (bit % 8)
                fresh = RawRingModel.from_flash(bytes(image), data_blocks=1)
                self.assertTrue(fresh.contains(0, b"a" * 32))
                self.assertEqual(fresh.next_outbox_sequence, 1)
                before = fresh.flash_bytes
                with self.assertRaises(ValueError):
                    fresh.seal(0, b"b" * 32)
                self.assertEqual(fresh.flash_bytes, before)

    def test_commit_marker_damage_preserves_loss_and_journal(self):
        model = RawRingModel(data_blocks=1)
        model.record_loss(missing_outbox_sequence=0, dropped_points=3, reason_mask=1,
                          event_id=R7_EVENT_ID, cut_at="after_emergency_commit")
        loss_offset = model.emergency.sector * ERASE_BLOCK_BYTES + 248
        for bit in range(-1, 64):
            with self.subTest(kind="loss", bit=bit):
                image = bytearray(model.flash_bytes)
                if bit == -1:
                    image[loss_offset:loss_offset + 8] = b"\xff" * 8
                else:
                    image[loss_offset + bit // 8] |= 1 << (bit % 8)
                fresh = RawRingModel.from_flash(bytes(image), data_blocks=1)
                self.assertEqual(fresh.next_outbox_sequence, 1)
                self.assertEqual(fresh.dropped_points_total, 3)
                self.assertEqual(fresh.emergency.state, "pending")
                self.assertTrue(fresh.record_loss(missing_outbox_sequence=0,
                    dropped_points=3, reason_mask=1, event_id=R7_EVENT_ID))
                self.assertEqual(fresh.dropped_points_total, 3)
        model._append_journal()
        sector, index = model._journal_location
        offset = sector * ERASE_BLOCK_BYTES + index * RAW_METADATA_RECORD_BYTES + 120
        for bit in range(-1, 64):
            with self.subTest(kind="journal", bit=bit):
                image = bytearray(model.flash_bytes)
                if bit == -1:
                    image[offset:offset + 8] = b"\xff" * 8
                else:
                    image[offset + bit // 8] |= 1 << (bit % 8)
                fresh = RawRingModel.from_flash(bytes(image), data_blocks=1)
                self.assertEqual(fresh.journal_generation, model.journal_generation)
                self.assertEqual(fresh.next_outbox_sequence, 1)
                self.assertEqual(fresh.dropped_points_total, 3)

    def test_damaged_commit_marker_with_invalid_body_is_read_only(self):
        for kind in ("slot", "acked_slot", "journal", "loss"):
            with self.subTest(kind=kind):
                model = RawRingModel(data_blocks=1)
                model.seal(0, b"a" * 32)
                if kind == "acked_slot":
                    model.acknowledge_exact(model.prepare_upload()[0].receipt())
                model.record_loss(missing_outbox_sequence=1, dropped_points=3,
                                  reason_mask=1, event_id=R7_EVENT_ID)
                if kind in ("slot", "acked_slot"):
                    offset = model._slot_offset(0)
                    commit = offset + 120
                elif kind == "journal":
                    sector, index = model._journal_location
                    offset = sector * ERASE_BLOCK_BYTES + index * RAW_METADATA_RECORD_BYTES
                    commit = offset + 120
                else:
                    offset = model.emergency.sector * ERASE_BLOCK_BYTES
                    commit = offset + 248
                image = bytearray(model.flash_bytes)
                if kind == "acked_slot":
                    image[commit:commit + 8] = b"\xff" * 8
                else:
                    image[commit + 7] |= 1
                image[offset] ^= 1  # invalidate the envelope, independently of marker damage
                fresh = RawRingModel.from_flash(bytes(image), data_blocks=1)
                self.assertTrue(fresh.sequence_state_unknown or fresh.loss_state_unknown)
                before = fresh.flash_bytes
                with self.assertRaises(RuntimeError):
                    fresh.seal(1, b"b" * 32)
                self.assertEqual(fresh.reclaim_acknowledged(), 0)
                self.assertEqual(fresh.flash_bytes, before)

    def test_full_storage_loss_consumes_identity_through_ack_and_reclaim(self):
        model = RawRingModel(data_blocks=1)
        for sequence in (0, 1):
            self.assertTrue(model.seal(sequence, b"a" * 32, boot_sequence=7))
        self.assertFalse(model.seal(2, b"b" * 32, boot_sequence=7))
        model = model.restart()
        before = model.flash_bytes
        self.assertFalse(model.seal(2, b"b" * 32, boot_sequence=7))
        self.assertEqual(model.flash_bytes, before)
        with self.assertRaises(ValueError):
            model.seal(2, b"c" * 32, boot_sequence=7)
        for slot in model.prepare_upload():
            self.assertTrue(model.acknowledge_exact(slot.receipt()))
        self.assertTrue(model.acknowledge_loss(**loss_ack(model.prepare_loss_upload())))
        self.assertEqual(model.reclaim_acknowledged(), 2)
        model = model.restart()
        before = model.flash_bytes
        for digest in (b"b" * 32, b"c" * 32):
            with self.assertRaises(ValueError):
                model.seal(2, digest, boot_sequence=7)
        self.assertEqual(model.flash_bytes, before)
        self.assertTrue(model.seal(3, b"d" * 32, boot_sequence=7))

    def test_full_storage_loss_identity_survives_emergency_cuts(self):
        for stage in ("during_emergency_record", "during_emergency_commit", "after_emergency_commit"):
            with self.subTest(stage=stage):
                model = RawRingModel(data_blocks=1)
                for sequence in (0, 1):
                    model.seal(sequence, b"a" * 32)
                cuts = model.counters.power_cuts
                self.assertFalse(model.seal(2, b"b" * 32, cut_at=stage))
                self.assertEqual(model.counters.power_cuts, cuts + 1)
                model = model.restart()
                self.assertFalse(model.seal(2, b"b" * 32))
                self.assertEqual(model.emergency.dropped_chunks, 1)
                with self.assertRaises(ValueError):
                    model.seal(2, b"c" * 32)

    def test_deferred_full_loss_retains_identity_when_promoted(self):
        model = RawRingModel(data_blocks=1)
        for sequence in (0, 1):
            model.seal(sequence, b"a" * 32)
        self.assertFalse(model.seal(2, b"b" * 32))
        receipt = loss_ack(model.prepare_loss_upload())
        self.assertFalse(model.acknowledge_loss(**receipt, cut_at="after_emergency_commit"))
        self.assertFalse(model.seal(3, b"c" * 32, cut_at="after_emergency_commit"))
        model = model.restart()
        self.assertEqual(model.emergency.identity_high_water.chunk_sequence, 3)
        before = model.flash_bytes
        self.assertFalse(model.seal(3, b"c" * 32))
        self.assertEqual(model.flash_bytes, before)
        for slot in model.prepare_upload():
            self.assertTrue(model.acknowledge_exact(slot.receipt()))
        # The final chunk ACK finalizes the old loss and promotes the deferred one.
        self.assertEqual(model.emergency.state, "pending")
        self.assertEqual(model.emergency.first_missing_outbox_sequence, 3)
        self.assertEqual(model.restart().identity_high_water.chunk_sequence, 3)
        with self.assertRaises(ValueError):
            model.seal(3, b"d" * 32)

    def test_identity_history_survives_reclaim_and_journal_rollover(self):
        model = RawRingModel(data_blocks=1)
        for sequence in range(40):
            self.assertTrue(model.seal(sequence, hashlib.sha256(bytes([sequence])).digest(), boot_sequence=7))
            slot = model.prepare_upload()[0]
            self.assertTrue(model.acknowledge_exact(slot.receipt()))
            self.assertEqual(model.reclaim_acknowledged(), 1)
            model = model.restart()
        before = model.flash_bytes
        for boot, chunk in ((6, 100), (7, 0), (7, 39)):
            with self.subTest(boot=boot, chunk=chunk), self.assertRaises(ValueError):
                model.seal(chunk, b"x" * 32, boot_sequence=boot)
        with self.assertRaises(ValueError):
            model.seal(40, b"x" * 32, boot_sequence=7, device_id=uuid.UUID(int=2))
        self.assertEqual(model.flash_bytes, before)
        self.assertTrue(model.seal(0, b"x" * 32, boot_sequence=8))

    def test_identity_recovers_from_orphan_slot_before_reclaim(self):
        for cut in ("after_slot_commit", "during_journal_record", "during_journal_commit"):
            with self.subTest(cut=cut):
                model = RawRingModel(data_blocks=1)
                self.assertTrue(model.seal(9, b"a" * 32, boot_sequence=7, cut_at=cut))
                model = model.restart()
                self.assertTrue(model.acknowledge_exact(model.prepare_upload()[0].receipt()))
                self.assertEqual(model.reclaim_acknowledged(), 1)
                model = model.restart()
                with self.assertRaises(ValueError):
                    model.seal(9, b"b" * 32, boot_sequence=7)

    def test_identity_cannot_be_forgotten_by_corrupt_committed_journal_fallback(self):
        model = RawRingModel(data_blocks=1)
        self.assertTrue(model.seal(4, b"a" * 32))
        self.assertTrue(model.acknowledge_exact(model.prepare_upload()[0].receipt()))
        self.assertEqual(model.reclaim_acknowledged(), 1)
        for record in model._journal_records():
            if record.identity_high_water is not None:
                clear_one_set_bit(model, record.sector * ERASE_BLOCK_BYTES +
                                  record.record_index * RAW_METADATA_RECORD_BYTES + 116, 4)
        fresh = model.restart()
        self.assertTrue(fresh.sequence_state_unknown)
        before = fresh.flash_bytes
        with self.assertRaises(RuntimeError):
            fresh.seal(4, b"b" * 32)
        self.assertEqual(fresh.reclaim_acknowledged(), 0)
        self.assertEqual(fresh.flash_bytes, before)

    def test_invalid_slot_envelope_does_not_reclaim_before_validation(self):
        for full in (False, True):
            model = RawRingModel(data_blocks=1)
            if full:
                for sequence in (0, 1):
                    self.assertTrue(model.seal(sequence, b"a" * 32))
                for slot in model.prepare_upload():
                    self.assertTrue(model.acknowledge_exact(slot.receipt()))
            before = model.flash_bytes
            invalid = (
                {"digest": b"bad digest"}, {"sequence": True},
                {"boot_sequence": True}, {"sequence": 2.0},
                {"first_point_sequence": True}, {"point_count": True},
            )
            for overrides in invalid:
                with self.subTest(full=full, invalid=overrides), self.assertRaises(ValueError):
                    model.seal(**({"sequence": 2, "digest": b"a" * 32} | overrides))
                self.assertEqual(model.flash_bytes, before)

    def test_counter_and_reason_overflow_fail_before_any_flash_mutation(self):
        for deferred in (False, True):
            with self.subTest(deferred=deferred):
                model = RawRingModel(data_blocks=1)
                self.assertTrue(model.record_loss(missing_outbox_sequence=0,
                    dropped_points=UINT64_MAX, reason_mask=1, event_id=R7_EVENT_ID))
                if deferred:
                    self.assertFalse(model.acknowledge_loss(**loss_ack(model.prepare_loss_upload()),
                        cut_at="after_emergency_commit"))
                before = model.flash_bytes
                for points, reason in ((1, 1), (UINT64_MAX + 1, 1), (1, 1 << 32)):
                    with self.subTest(points=points, reason=reason), self.assertRaises(OverflowError):
                        model.record_loss(missing_outbox_sequence=1, dropped_points=points,
                                          reason_mask=reason, event_id=R7_RETRY_EVENT_ID)
                    self.assertEqual(model.flash_bytes, before)
                self.assertEqual(model.restart().dropped_points_total, UINT64_MAX)

    def test_loss_journal_cuts_preserve_committed_loss_and_exact_retry(self):
        for mode in ("first", "coalesced", "deferred"):
            for stage in ("during_journal_record", "during_journal_commit", "during_journal_erase"):
                with self.subTest(mode=mode, stage=stage):
                    model = RawRingModel(data_blocks=1)
                    if mode != "first":
                        model.record_loss(missing_outbox_sequence=0, dropped_points=3,
                                          reason_mask=1, event_id=R7_EVENT_ID)
                    if mode == "deferred":
                        model.acknowledge_loss(**loss_ack(model.prepare_loss_upload()),
                                               cut_at="after_emergency_commit")
                    if stage == "during_journal_erase":
                        # Fill both journal sectors so the next append erases a
                        # nonempty target and actually exercises the named cut.
                        while model.journal_generation < RAW_METADATA_RECORDS_PER_BLOCK * 2 - 1:
                            model._append_journal()
                    ordinal = model.next_outbox_sequence
                    previous_cuts = model.counters.power_cuts
                    self.assertFalse(model.record_loss(missing_outbox_sequence=ordinal,
                        dropped_points=4, reason_mask=2, event_id=R7_RETRY_EVENT_ID, cut_at=stage))
                    self.assertEqual(model.counters.power_cuts, previous_cuts + 1)
                    fresh = model.restart()
                    self.assertEqual(fresh.next_outbox_sequence, ordinal + 1)
                    self.assertEqual(fresh.dropped_points_total, 4 if mode == "first" else 7)
                    self.assertTrue(fresh.record_loss(missing_outbox_sequence=ordinal,
                        dropped_points=4, reason_mask=2, event_id=R7_RETRY_EVENT_ID))
                    self.assertEqual(fresh.restart().dropped_points_total, 4 if mode == "first" else 7)

    def test_loss_ack_journal_cuts_never_reclaim_before_durable_prefix(self):
        for stage in ("during_ack_metadata", "during_journal_record", "during_journal_commit", "during_journal_erase"):
            with self.subTest(stage=stage):
                model = RawRingModel(data_blocks=1)
                model.record_loss(missing_outbox_sequence=0, dropped_points=3,
                                  reason_mask=1, event_id=R7_EVENT_ID)
                if stage == "during_journal_erase":
                    while model.journal_generation < RAW_METADATA_RECORDS_PER_BLOCK * 2 - 1:
                        model._append_journal()
                kwargs = loss_ack(model.prepare_loss_upload())
                cuts = model.counters.power_cuts
                self.assertFalse(model.acknowledge_loss(**kwargs, cut_at=stage))
                self.assertEqual(model.counters.power_cuts, cuts + 1)
                fresh = model.restart()
                self.assertEqual(fresh.reclaim_through, 0 if stage == "during_journal_commit" else -1)
                self.assertEqual(fresh.emergency.state, "acknowledged")
                self.assertTrue(fresh.acknowledge_loss(**kwargs))
                self.assertEqual(fresh.restart().reclaim_through, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
