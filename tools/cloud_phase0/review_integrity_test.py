"""Independent M2.1 regression probes for OUTBOX-R2/R3/R7 defects.

Run explicitly with `python tools/cloud_phase0/review_integrity_test.py -v`;
this filename is outside the frozen `test_*.py` 51-case matrix. Against the
frozen candidate at origin 255136d6, all three tests fail. Keep this file
separate from frozen candidate sources until a deliberate correction and
rebaseline.
"""

from __future__ import annotations

import hashlib
import unittest
import uuid

from storage_model import ERASE_BLOCK_BYTES, RawRingModel


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


if __name__ == "__main__":
    unittest.main(verbosity=2)
