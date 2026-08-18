from __future__ import annotations

from datetime import datetime, timezone
from dataclasses import replace
import hashlib
import json
from pathlib import Path
import unittest
import uuid

from legacy_v2 import (
    LEGACY_BOOT_SEQUENCE,
    LegacyPointV2,
    convert_v2_points,
    legacy_export_row,
)
from reference_fixtures import FIXTURE_DEVICE_ID, fixture_manifest, reference_fixtures
from storage_model import (
    AckReceipt,
    CHUNK_BYTES,
    ChunkIdentity,
    DEFAULT_DEVICE_ID,
    ERASE_BLOCK_BYTES,
    LITTLEFS_CAPACITY_CHUNKS,
    NorFlash,
    PARTITION_BYTES,
    RAW_CAPACITY_CHUNKS,
    RAW_DATA_BLOCKS,
    RAW_EMERGENCY_BLOCKS,
    RAW_METADATA_RECORD_BYTES,
    RAW_METADATA_RECORDS_PER_BLOCK,
    RAW_SLOT_BYTES,
    RAW_SUPERBLOCKS,
    TOTAL_BLOCKS,
    UINT64_MAX,
    EmergencyRecord,
    LittleFsModel,
    RawRingModel,
    retention_rows,
    run_fill_reclaim,
    run_random_power_cut_workload,
)
from track_v3 import (
    CHUNK_HEADER_SIZE,
    MAX_POINTS_PER_CHUNK,
    POINT_SIZE,
    PointFlag,
    SPEED_UNAVAILABLE,
    TimeQuality,
    TrackChunkV3,
    TrackPointV3,
    decode_chunk,
    decode_point,
    encode_chunk,
    encode_point,
)


ROOT = Path(__file__).resolve().parent


class TrackV3CodecTests(unittest.TestCase):
    def test_frozen_sizes_and_golden_point_vector(self):
        point = TrackPointV3(
            46_500_000,
            -740_600_000,
            1_787_000_000,
            140,
            11,
            int(PointFlag.FIX_VALID | PointFlag.MOVEMENT_EVIDENCE | PointFlag.TIME_TRUSTED),
        )
        self.assertEqual(POINT_SIZE, 16)
        self.assertEqual(CHUNK_HEADER_SIZE, 92)
        self.assertEqual(CHUNK_BYTES, 1_628)
        self.assertEqual(encode_point(point).hex(), "a088c5024057dbd3c074836a8c000b07")
        self.assertEqual(decode_point(encode_point(point)), point)

    def test_frozen_time_quality_values_and_presence_invariant(self):
        self.assertEqual(
            {quality.name: int(quality) for quality in TimeQuality},
            {
                "UNKNOWN": 0,
                "APPROXIMATE_PERSISTED": 1,
                "SERVER_ANCHORED": 2,
                "SNTP_SYNCED": 3,
                "GNSS_TRUSTED": 4,
                "LEGACY_MINUTE": 5,
            },
        )

        fixture = reference_fixtures()[1].chunk
        for quality in (
            TimeQuality.APPROXIMATE_PERSISTED,
            TimeQuality.SERVER_ANCHORED,
            TimeQuality.SNTP_SYNCED,
            TimeQuality.GNSS_TRUSTED,
        ):
            with self.subTest(quality=quality):
                candidate = TrackChunkV3(
                    fixture.device_id,
                    fixture.boot_sequence,
                    fixture.chunk_sequence,
                    fixture.first_point_sequence,
                    quality,
                    fixture.final_for_recording,
                    fixture.points,
                )
                self.assertEqual(decode_chunk(encode_chunk(candidate)).chunk, candidate)

        unknown_point = TrackPointV3(
            0,
            0,
            0,
            SPEED_UNAVAILABLE,
            0,
            int(PointFlag.GAP),
        )
        unknown_chunk = TrackChunkV3(
            fixture.device_id,
            fixture.boot_sequence,
            fixture.chunk_sequence,
            fixture.first_point_sequence,
            TimeQuality.UNKNOWN,
            True,
            (unknown_point,),
        )
        self.assertEqual(decode_chunk(encode_chunk(unknown_chunk)).chunk, unknown_chunk)

        invalid_quality = TrackChunkV3(
            fixture.device_id,
            fixture.boot_sequence,
            fixture.chunk_sequence,
            fixture.first_point_sequence,
            TimeQuality.UNKNOWN,
            fixture.final_for_recording,
            fixture.points,
        )
        with self.assertRaisesRegex(ValueError, "UNKNOWN"):
            encode_chunk(invalid_quality)

    def test_reference_chunks_round_trip_and_match_checked_in_manifest(self):
        manifest = json.loads((ROOT / "fixtures/reference_manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(fixture_manifest(), manifest)
        for fixture in reference_fixtures():
            encoded = encode_chunk(fixture.chunk)
            decoded = decode_chunk(encoded)
            self.assertEqual(decoded.chunk, fixture.chunk)
            self.assertEqual(decoded.payload_sha256, hashlib.sha256(encoded[CHUNK_HEADER_SIZE:]).digest())

    def test_header_payload_truncation_and_trailing_corruption_are_rejected(self):
        encoded = bytearray(encode_chunk(reference_fixtures()[1].chunk))
        cases = []
        header_flip = encoded.copy()
        header_flip[20] ^= 0x01
        cases.append(header_flip)
        payload_flip = encoded.copy()
        payload_flip[-1] ^= 0x01
        cases.append(payload_flip)
        cases.extend((encoded[:-1], encoded + b"\x00"))
        for corrupt in cases:
            with self.subTest(length=len(corrupt)):
                with self.assertRaises(ValueError):
                    decode_chunk(bytes(corrupt))

    def test_semantically_invalid_points_are_rejected(self):
        invalid = (
            TrackPointV3(1, 1, 0, 0, 3, int(PointFlag.FIX_VALID | PointFlag.TIME_TRUSTED)),
            TrackPointV3(1, 1, 1, 0, 3, int(PointFlag.FIX_VALID | PointFlag.GAP | PointFlag.TIME_TRUSTED)),
            TrackPointV3(1, 1, 1, 0, 3, int(PointFlag.FIX_VALID | PointFlag.MOVEMENT_EVIDENCE | PointFlag.STATIONARY_HEARTBEAT | PointFlag.TIME_TRUSTED)),
            TrackPointV3(0, 0, 1, 0, 0, int(PointFlag.GAP | PointFlag.TIME_TRUSTED)),
            TrackPointV3(0, 0, 0, SPEED_UNAVAILABLE, 0, 0x80),
        )
        for point in invalid:
            with self.subTest(point=point):
                with self.assertRaises(ValueError):
                    encode_point(point)

    def test_chunk_identity_cannot_be_reused_with_different_content(self):
        for model in (RawRingModel(data_blocks=2), LittleFsModel(capacity_chunks=4)):
            self.assertTrue(model.seal(7, b"a" * 32))
            with self.assertRaises(ValueError):
                model.seal(7, b"b" * 32)

    def test_chunk_rejects_nil_device_wrong_legacy_namespace_and_backwards_time(self):
        fixture = reference_fixtures()[1].chunk
        invalid = (
            TrackChunkV3(
                uuid.UUID(int=0), fixture.boot_sequence, fixture.chunk_sequence,
                fixture.first_point_sequence, fixture.time_quality,
                fixture.final_for_recording, fixture.points,
            ),
            TrackChunkV3(
                fixture.device_id, 0, fixture.chunk_sequence,
                fixture.first_point_sequence, fixture.time_quality,
                fixture.final_for_recording, fixture.points,
            ),
            TrackChunkV3(
                fixture.device_id, fixture.boot_sequence, fixture.chunk_sequence,
                fixture.first_point_sequence, fixture.time_quality,
                fixture.final_for_recording, tuple(reversed(fixture.points)),
            ),
        )
        for chunk in invalid:
            with self.subTest(chunk=chunk):
                with self.assertRaises(ValueError):
                    encode_chunk(chunk)


class LegacyV2ContractTests(unittest.TestCase):
    def test_conversion_is_stable_minute_precision_and_does_not_invent_metrics(self):
        source = (
            LegacyPointV2(46_500_000, -740_600_000, 1439),
            LegacyPointV2(46_500_010, -740_599_990, 0),
            LegacyPointV2(46_500_020, -740_599_980, 0),
        )
        chunks = convert_v2_points(
            device_id=FIXTURE_DEVICE_ID,
            slot=2,
            start_date_yyyymmdd=20260813,
            points=source,
        )
        self.assertEqual(len(chunks), 1)
        chunk = chunks[0]
        self.assertEqual(chunk.boot_sequence, LEGACY_BOOT_SEQUENCE)
        self.assertEqual(chunk.chunk_sequence, 64)
        self.assertEqual(chunk.first_point_sequence, 4096)
        self.assertEqual(chunk.time_quality, TimeQuality.LEGACY_MINUTE)
        self.assertEqual(
            chunk.points[1].utc_s,
            int(datetime(2026, 8, 14, tzinfo=timezone.utc).timestamp()),
        )
        row = legacy_export_row(chunk.points[0])
        self.assertIsNone(row["speed_cmps"])
        self.assertEqual(row["movement_state"], "unknown")
        self.assertEqual(row["stationary_state"], "unknown")

    def test_small_backwards_minute_jump_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "backwards"):
            convert_v2_points(
                device_id=uuid.uuid4(),
                slot=0,
                start_date_yyyymmdd=20260813,
                points=(LegacyPointV2(1, 1, 100), LegacyPointV2(2, 2, 99)),
            )

    def test_large_legacy_snapshot_splits_into_bounded_chunks(self):
        source = tuple(LegacyPointV2(1 + i, 2 + i, 100 + i // 12) for i in range(193))
        chunks = convert_v2_points(
            device_id=FIXTURE_DEVICE_ID,
            slot=0,
            start_date_yyyymmdd=20260813,
            points=source,
        )
        self.assertEqual([len(chunk.points) for chunk in chunks], [96, 96, 1])
        self.assertEqual([chunk.chunk_sequence for chunk in chunks], [0, 1, 2])
        self.assertFalse(chunks[0].final_for_recording)
        self.assertTrue(chunks[-1].final_for_recording)


def digest(index: int) -> bytes:
    return hashlib.sha256(f"test-chunk:{index}".encode("ascii")).digest()


def clear_one_set_bit(model: RawRingModel, offset: int, length: int = 1) -> None:
    raw = model.flash.read(offset, length)
    for relative, value in enumerate(raw):
        for bit in range(8):
            mask = 1 << bit
            if value & mask:
                model.flash.program(offset + relative, bytes([value & ~mask]))
                return
    raise AssertionError("fixture range contains no bit that can be cleared")


def exact_ack(model: RawRingModel, outbox_sequence: int) -> bool:
    selected = model.prepare_upload([outbox_sequence])
    if not selected:
        raise AssertionError(f"outbox {outbox_sequence} is not uploadable")
    return model.acknowledge_exact(selected[0].receipt())


def loss_ack_kwargs(loss) -> dict[str, object]:
    return {
        "loss_id": loss.loss_id,
        "generation": loss.generation,
        "record_sha256": loss.record_sha256,
        "first_missing_outbox_sequence": loss.first_missing_outbox_sequence,
        "last_missing_outbox_sequence": loss.last_missing_outbox_sequence,
        "dropped_chunks": loss.dropped_chunks,
    }


class StorageGeometryAndNorTests(unittest.TestCase):
    def test_geometry_reserves_two_independently_erasable_emergency_sectors(self):
        self.assertEqual(PARTITION_BYTES, 0x150000)
        self.assertEqual(ERASE_BLOCK_BYTES, 4096)
        self.assertEqual(TOTAL_BLOCKS, 336)
        self.assertEqual(RAW_SUPERBLOCKS, 2)
        self.assertEqual(RAW_EMERGENCY_BLOCKS, 2)
        self.assertEqual(RAW_DATA_BLOCKS, 332)
        self.assertEqual(RAW_CAPACITY_CHUNKS, 664)
        partitions = (
            ROOT.parent.parent / "Platformio/Dog-RGB/partitions_dog_rgb.csv"
        ).read_text(encoding="utf-8")
        self.assertIn("spiffs,   data, spiffs,  0x670000,0x150000", partitions)

    def test_retention_uses_the_corrected_664_chunk_geometry(self):
        raw = {row["profile"]: row for row in retention_rows(RAW_CAPACITY_CHUNKS)}
        littlefs = {row["profile"]: row for row in retention_rows(LITTLEFS_CAPACITY_CHUNKS)}
        self.assertEqual(raw["adaptive_4h_moving_20h_stationary"]["points_per_day"], 4_080)
        self.assertEqual(raw["adaptive_4h_moving_20h_stationary"]["retention_days"], 15.624)
        self.assertEqual(raw["1_second"]["retention_hours"], 17.707)
        self.assertEqual(raw["60_second"]["retention_days"], 44.267)
        self.assertEqual(littlefs["adaptive_4h_moving_20h_stationary"]["retention_days"], 15.812)

    def test_nor_program_only_clears_bits_and_erase_is_one_aligned_sector(self):
        flash = NorFlash(ERASE_BLOCK_BYTES * 2)
        flash.program(5, b"\xf0\x0f")
        before = flash.snapshot()
        with self.assertRaisesRegex(ValueError, "0-to-1"):
            flash.program(5, b"\xff\x0f")
        self.assertEqual(flash.snapshot(), before)
        with self.assertRaisesRegex(ValueError, "aligned"):
            flash.erase(1, ERASE_BLOCK_BYTES)
        with self.assertRaisesRegex(ValueError, "aligned"):
            flash.erase(0, ERASE_BLOCK_BYTES * 2)
        flash.erase(0, ERASE_BLOCK_BYTES)
        self.assertEqual(flash.read(0, ERASE_BLOCK_BYTES), b"\xff" * ERASE_BLOCK_BYTES)
        self.assertEqual(flash.read(ERASE_BLOCK_BYTES, ERASE_BLOCK_BYTES), before[ERASE_BLOCK_BYTES:])

    def test_blank_and_fresh_mount_state_comes_only_from_flash_bytes(self):
        model = RawRingModel(data_blocks=2)
        self.assertEqual(model.next_outbox_sequence, 0)
        self.assertTrue(model.seal(0, digest(0), boot_sequence=7))
        image = model.flash_bytes
        model.next_outbox_sequence = 999
        model.reclaim_through = 998
        model.slots = {}
        fresh = RawRingModel.from_flash(image, data_blocks=2)
        self.assertEqual(fresh.next_outbox_sequence, 1)
        self.assertEqual(fresh.reclaim_through, -1)
        self.assertTrue(fresh.contains(0, digest(0), boot_sequence=7))


class RawIdentityAndAckTests(unittest.TestCase):
    def test_outbox_sequence_never_reuses_after_reboot_loss_and_reclaim(self):
        model = RawRingModel(data_blocks=1)
        self.assertTrue(model.seal(0, digest(0), boot_sequence=10))
        model = model.restart()
        self.assertTrue(model.seal(0, digest(1), boot_sequence=11))
        self.assertFalse(model.seal(1, digest(2), boot_sequence=11))
        self.assertEqual(model.next_outbox_sequence, 3)

        for sequence in (0, 1):
            self.assertTrue(exact_ack(model, sequence))
        self.assertEqual(model.reclaim_acknowledged(), 2)
        model = model.restart()
        self.assertTrue(model.seal(2, digest(3), boot_sequence=11))
        self.assertIn(3, model.slots)
        self.assertEqual(model.next_outbox_sequence, 4)

    def test_outbox_uint64_exhaustion_fails_before_flash_mutation(self):
        model = RawRingModel(data_blocks=1)
        model.next_outbox_sequence = (1 << 64) - 1
        model.outbox_exhausted = True
        before = model.flash_bytes
        with self.assertRaisesRegex(OverflowError, "exhausted"):
            model.seal(0, digest(0))
        with self.assertRaises(OverflowError):
            model.record_loss(
                missing_outbox_sequence=(1 << 64) - 1,
                dropped_points=1,
                reason_mask=1,
            )
        self.assertEqual(model.flash_bytes, before)

    def test_loss_event_latest_retry_is_exact_and_older_retry_is_noop(self):
        model = RawRingModel(data_blocks=2)
        event_a = uuid.uuid4()
        event_b = uuid.uuid4()
        self.assertTrue(model.record_loss(missing_outbox_sequence=0, dropped_points=4, reason_mask=1, event_id=event_a))
        self.assertTrue(model.seal(0, digest(0)))
        self.assertTrue(model.record_loss(missing_outbox_sequence=2, dropped_points=5, reason_mask=2, event_id=event_b))
        image = model.flash_bytes
        self.assertFalse(model.record_loss(missing_outbox_sequence=0, dropped_points=4, reason_mask=1, event_id=event_a))
        self.assertEqual(model.flash_bytes, image)
        self.assertTrue(model.record_loss(missing_outbox_sequence=2, dropped_points=5, reason_mask=2, event_id=event_b))
        self.assertEqual(model.flash_bytes, image)
        with self.assertRaisesRegex(ValueError, "reused"):
            model.record_loss(missing_outbox_sequence=2, dropped_points=6, reason_mask=2, event_id=event_b)
        self.assertFalse(model.record_loss(missing_outbox_sequence=4, dropped_points=1, reason_mask=1, event_id=uuid.uuid4()))
        self.assertEqual(model.flash_bytes, image)

    def test_chunk_sequence_reset_after_boot_has_distinct_outbox_identity(self):
        model = RawRingModel(data_blocks=2)
        self.assertTrue(model.seal(0, digest(10), boot_sequence=10))
        self.assertTrue(model.seal(0, digest(11), boot_sequence=11))
        self.assertEqual(sorted(model.slots), [0, 1])
        self.assertTrue(exact_ack(model, 0))
        fresh = model.restart()
        self.assertEqual(fresh.reclaim_through, 0)
        self.assertTrue(fresh.slots[0].acknowledged)
        self.assertFalse(fresh.slots[1].acknowledged)
        self.assertTrue(fresh.contains(0, digest(11), boot_sequence=11))

    def test_same_identity_is_idempotent_and_hash_reuse_is_rejected(self):
        model = RawRingModel(data_blocks=2)
        self.assertTrue(model.seal(4, digest(1), boot_sequence=3))
        image = model.flash_bytes
        self.assertTrue(model.seal(4, digest(1), boot_sequence=3))
        self.assertEqual(model.flash_bytes, image)
        with self.assertRaisesRegex(ValueError, "different content"):
            model.seal(4, digest(2), boot_sequence=3)
        self.assertEqual(model.flash_bytes, image)

    def test_ack_requires_an_exact_member_of_the_sent_manifest(self):
        model = RawRingModel(data_blocks=2)
        self.assertTrue(model.seal(0, digest(0), first_point_sequence=50, point_count=3))
        slot = model.slots[0]
        image = model.flash_bytes
        self.assertFalse(model.acknowledge_exact(slot.receipt()))
        self.assertEqual(model.flash_bytes, image)
        model.prepare_upload([0])
        invalid = (
            replace(slot.receipt(), device_id=uuid.uuid4()),
            replace(slot.receipt(), boot_sequence=99),
            replace(slot.receipt(), chunk_sequence=99),
            replace(slot.receipt(), content_sha256=digest(99)),
            replace(slot.receipt(), accepted_point_count=2),
            replace(slot.receipt(), through_point_sequence=999),
        )
        for receipt in invalid:
            with self.subTest(receipt=receipt):
                before = model.flash_bytes
                self.assertFalse(model.acknowledge_exact(receipt))
                self.assertEqual(model.flash_bytes, before)
        unknown = AckReceipt(DEFAULT_DEVICE_ID, 1, 999, 3, 52, digest(0))
        self.assertFalse(model.acknowledge_exact(unknown))
        self.assertTrue(model.acknowledge_exact(slot.receipt()))
        committed = model.flash_bytes
        self.assertTrue(model.acknowledge_exact(slot.receipt()))
        self.assertEqual(model.flash_bytes, committed)

    def test_out_of_order_ack_hole_survives_fresh_mount_and_blocks_reclaim(self):
        model = RawRingModel(data_blocks=2)
        for sequence in range(4):
            self.assertTrue(model.seal(sequence, digest(sequence)))
        model.prepare_upload()
        self.assertTrue(model.acknowledge_exact(model.slots[2].receipt()))
        model = model.restart()
        self.assertEqual(model.reclaim_through, -1)
        self.assertTrue(model.slots[2].acknowledged)
        self.assertEqual(model.reclaim_acknowledged(), 0)
        model.prepare_upload([0])
        self.assertTrue(model.acknowledge_exact(model.slots[0].receipt()))
        self.assertEqual(model.reclaim_through, 0)
        model.prepare_upload([1])
        self.assertTrue(model.acknowledge_exact(model.slots[1].receipt()))
        self.assertEqual(model.reclaim_through, 2)
        self.assertEqual(model.reclaim_acknowledged(), 2)
        fresh = model.restart()
        self.assertNotIn(0, fresh.slots)
        self.assertNotIn(1, fresh.slots)
        self.assertIn(2, fresh.slots)
        self.assertIn(3, fresh.unacknowledged())


class RawPowerCutAndJournalTests(unittest.TestCase):
    def test_slot_commit_cut_matrix_mounts_only_committed_crc_valid_slots(self):
        rejected = ("during_data", "during_slot_commit")
        recovered = ("after_slot_commit", "during_metadata", "during_journal_record", "during_journal_commit")
        for stage in rejected + recovered:
            with self.subTest(stage=stage):
                model = RawRingModel(data_blocks=2)
                result = model.seal(0, digest(0), cut_at=stage)
                fresh = model.restart()
                if stage in rejected:
                    self.assertFalse(result)
                    self.assertFalse(fresh.contains(0))
                    self.assertIn(0, fresh.incomplete_slot_indexes)
                    self.assertTrue(fresh.seal(0, digest(0)))
                else:
                    self.assertTrue(result)
                    self.assertTrue(fresh.contains(0, digest(0)))
                    self.assertEqual(fresh.next_outbox_sequence, 1)

    def test_ack_cut_matrix_never_invents_reclaimable_progress(self):
        for stage in ("during_ack_marker", "during_ack_metadata", "during_journal_record", "during_journal_commit"):
            with self.subTest(stage=stage):
                model = RawRingModel(data_blocks=1)
                self.assertTrue(model.seal(0, digest(0)))
                slot = model.prepare_upload([0])[0]
                self.assertFalse(model.acknowledge_exact(slot.receipt(), cut_at=stage))
                fresh = model.restart()
                self.assertEqual(fresh.reclaim_acknowledged(), 0)
                if stage == "during_ack_marker":
                    self.assertFalse(fresh.slots[0].acknowledged)
                    retry = fresh.prepare_upload([0])[0]
                    self.assertTrue(fresh.acknowledge_exact(retry.receipt()))
                else:
                    self.assertTrue(fresh.slots[0].acknowledged)
                    self.assertEqual(fresh.reclaim_through, -1)
                    self.assertTrue(fresh._persist_ack_prefix())
                self.assertEqual(fresh.reclaim_through, 0)

    @classmethod
    def setUpClass(cls):
        base = RawRingModel(data_blocks=34)
        # The factory-format record occupies journal slot zero.  Sixty-three
        # seals fill the active/alternate sectors and make the next append a
        # rollover into a non-erased target.
        for sequence in range(63):
            if not base.seal(sequence, digest(sequence)):
                raise AssertionError(sequence)
        cls.full_journal_image = base.flash_bytes
        cls.rollover_sequence = 63

    def test_journal_rollover_cuts_keep_an_old_or_new_valid_generation(self):
        for stage in ("during_journal_erase", "during_journal_record", "during_journal_commit"):
            with self.subTest(stage=stage):
                model = RawRingModel.from_flash(self.full_journal_image, data_blocks=34)
                prior_generation = model.journal_generation
                sequence = self.rollover_sequence
                self.assertTrue(model.seal(sequence, digest(sequence), cut_at=stage))
                fresh = model.restart()
                self.assertTrue(fresh.contains(sequence, digest(sequence)))
                self.assertGreaterEqual(fresh.next_outbox_sequence, sequence + 1)
                self.assertEqual(fresh.journal_generation, prior_generation)
                self.assertTrue(fresh._append_journal())
                self.assertTrue(
                    fresh.journal_generation == (prior_generation + 1) & ((1 << 64) - 1)
                )

    def test_reclaim_cut_matrix_uses_durable_intent_and_preserves_tail(self):
        base = RawRingModel(data_blocks=2)
        for sequence in range(4):
            self.assertTrue(base.seal(sequence, digest(sequence)))
        self.assertTrue(exact_ack(base, 0))
        self.assertTrue(exact_ack(base, 1))
        tail_sector = base.flash.read(
            (base.data_start_sector + 1) * ERASE_BLOCK_BYTES,
            ERASE_BLOCK_BYTES,
        )
        image = base.flash_bytes
        for stage in (
            "during_intent_metadata",
            "during_erase",
            "after_erase",
            "during_intent_consumed",
            "during_clear_metadata",
        ):
            with self.subTest(stage=stage):
                model = RawRingModel.from_flash(image, data_blocks=2)
                model.reclaim_acknowledged(cut_at=stage)
                fresh = model.restart()
                self.assertEqual(
                    fresh.flash.read(
                        (fresh.data_start_sector + 1) * ERASE_BLOCK_BYTES,
                        ERASE_BLOCK_BYTES,
                    ),
                    tail_sector,
                )
                self.assertIn(2, fresh.unacknowledged())
                self.assertIn(3, fresh.unacknowledged())
                fresh.reclaim_acknowledged()
                self.assertIsNone(fresh.erase_intent_block)
                self.assertEqual(
                    fresh.flash.read(
                        fresh.data_start_sector * ERASE_BLOCK_BYTES,
                        ERASE_BLOCK_BYTES,
                    ),
                    b"\xff" * ERASE_BLOCK_BYTES,
                )

    def test_stale_reclaim_intent_cannot_erase_a_refilled_sector(self):
        model = RawRingModel(data_blocks=1)
        for sequence in range(2):
            self.assertTrue(model.seal(sequence, digest(sequence)))
            self.assertTrue(exact_ack(model, sequence))
        self.assertEqual(model.reclaim_acknowledged(), 2)
        for sequence in range(2, 4):
            self.assertTrue(model.seal(sequence, digest(sequence)))

        records = model._journal_records()
        intent = max(
            (record for record in records if record.erase_intent_block is not None),
            key=lambda record: record.generation,
        )
        self.assertEqual(set(intent.erase_intent_sequences), {0, 1})
        self.assertTrue(intent.erase_intent_consumed)
        for record in records:
            if record.generation > intent.generation:
                clear_one_set_bit(
                    model,
                    record.sector * ERASE_BLOCK_BYTES
                    + record.record_index * RAW_METADATA_RECORD_BYTES
                    + 116,
                    4,
                )

        fresh = model.restart()
        before = fresh.flash_bytes
        self.assertFalse(fresh.metadata_degraded)
        self.assertFalse(fresh.sequence_state_unknown)
        self.assertIsNone(fresh.erase_intent_block)
        self.assertEqual(fresh.reclaim_acknowledged(), 0)
        self.assertEqual(fresh.flash_bytes, before)
        self.assertTrue(fresh.contains(2, digest(2)))
        self.assertTrue(fresh.contains(3, digest(3)))

    def test_consumed_stale_intent_cannot_erase_a_corrupt_refilled_slot(self):
        model = RawRingModel(data_blocks=1)
        for sequence in range(2):
            self.assertTrue(model.seal(sequence, digest(sequence)))
            self.assertTrue(exact_ack(model, sequence))
        self.assertEqual(model.reclaim_acknowledged(), 2)
        self.assertTrue(model.seal(2, digest(2)))

        intent = max(
            (
                record
                for record in model._journal_records()
                if record.erase_intent_block is not None
            ),
            key=lambda record: record.generation,
        )
        self.assertTrue(intent.erase_intent_consumed)
        for record in model._journal_records():
            if record.generation > intent.generation:
                clear_one_set_bit(
                    model,
                    record.sector * ERASE_BLOCK_BYTES
                    + record.record_index * RAW_METADATA_RECORD_BYTES
                    + 116,
                    4,
                )
        slot = model.slots[2]
        clear_one_set_bit(model, model._slot_offset(slot.slot_index) + 128)

        fresh = model.restart()
        before = fresh.flash_bytes
        self.assertIsNone(fresh.erase_intent_block)
        self.assertFalse(fresh.sequence_state_unknown)
        self.assertEqual(fresh.next_outbox_sequence, 3)
        self.assertEqual(set(fresh.quarantined_slots), {2})
        self.assertEqual(fresh.missing_outbox_sequences, {2})
        self.assertEqual(fresh.reclaim_acknowledged(), 0)
        self.assertEqual(fresh.flash_bytes, before)

        self.assertTrue(fresh.persist_missing_as_loss())
        loss = fresh.prepare_loss_upload()
        self.assertIsNotNone(loss)
        self.assertTrue(fresh.acknowledge_loss(**loss_ack_kwargs(loss)))
        self.assertEqual(fresh.reclaim_through, 2)
        self.assertEqual(fresh.emergency.state, "empty")
        self.assertEqual(fresh.reclaim_acknowledged(), 1)


class RawEmergencyAndCorruptionTests(unittest.TestCase):
    def test_first_loss_update_cut_always_keeps_the_empty_baseline_retriable(self):
        event = uuid.uuid4()
        for stage in (
            "during_emergency_record",
            "during_emergency_commit",
        ):
            with self.subTest(stage=stage):
                model = RawRingModel(data_blocks=1)
                self.assertFalse(model.record_loss(
                    missing_outbox_sequence=0,
                    dropped_points=5,
                    reason_mask=1,
                    event_id=event,
                    cut_at=stage,
                ))
                fresh = model.restart()
                self.assertFalse(fresh.loss_state_unknown)
                self.assertEqual(fresh.emergency.state, "empty")
                self.assertTrue(fresh.record_loss(
                    missing_outbox_sequence=0,
                    dropped_points=5,
                    reason_mask=1,
                    event_id=event,
                ))
                self.assertEqual(fresh.restart().emergency.state, "pending")

    def test_full_ring_preserves_unacked_slots_and_durably_records_one_loss(self):
        model = RawRingModel(data_blocks=1)
        self.assertTrue(model.seal(0, digest(0)))
        self.assertTrue(model.seal(1, digest(1)))
        original = {sequence: slot.digest for sequence, slot in model.slots.items()}
        self.assertFalse(model.seal(2, digest(2)))
        self.assertFalse(model.seal(2, digest(2)))
        fresh = model.restart()
        self.assertEqual({sequence: slot.digest for sequence, slot in fresh.slots.items()}, original)
        self.assertEqual(fresh.next_outbox_sequence, 3)
        self.assertIsNotNone(fresh.emergency)
        self.assertEqual(fresh.emergency.state, "pending")
        self.assertEqual(fresh.emergency.first_missing_outbox_sequence, 2)
        self.assertEqual(fresh.emergency.last_missing_outbox_sequence, 2)
        self.assertEqual(fresh.emergency.dropped_chunks, 1)

    def test_emergency_ab_cut_matrix_is_atomic_and_retry_is_idempotent(self):
        base = RawRingModel(data_blocks=1)
        first = uuid.uuid4()
        second = uuid.uuid4()
        self.assertTrue(base.record_loss(missing_outbox_sequence=0, dropped_points=5, reason_mask=1, event_id=first))
        self.assertTrue(base.record_loss(missing_outbox_sequence=1, dropped_points=7, reason_mask=2, event_id=second))
        self.assertEqual(base.emergency.dropped_chunks, 2)
        image = base.flash_bytes
        third = uuid.uuid4()
        for stage in ("during_emergency_erase", "during_emergency_record", "during_emergency_commit", "after_emergency_commit"):
            with self.subTest(stage=stage):
                model = RawRingModel.from_flash(image, data_blocks=1)
                model.record_loss(missing_outbox_sequence=2, dropped_points=11, reason_mask=4, event_id=third, cut_at=stage)
                fresh = model.restart()
                self.assertIn(fresh.emergency.dropped_chunks, (2, 3))
                self.assertTrue(fresh.record_loss(missing_outbox_sequence=2, dropped_points=11, reason_mask=4, event_id=third))
                self.assertEqual(fresh.emergency.dropped_chunks, 3)
                self.assertEqual(fresh.emergency.dropped_points, 23)
                self.assertEqual(fresh.emergency.reason_mask, 7)

    def test_loss_coalescing_tracks_range_count_total_and_latest_event_retry(self):
        model = RawRingModel(data_blocks=2)
        event_a = uuid.uuid4()
        event_b = uuid.uuid4()
        self.assertTrue(model.record_loss(missing_outbox_sequence=0, dropped_points=4, reason_mask=1, event_id=event_a))
        self.assertTrue(model.seal(0, digest(0)))
        self.assertTrue(model.record_loss(missing_outbox_sequence=2, dropped_points=6, reason_mask=2, event_id=event_b))
        image = model.flash_bytes
        self.assertTrue(model.record_loss(missing_outbox_sequence=2, dropped_points=6, reason_mask=2, event_id=event_b))
        self.assertEqual(model.flash_bytes, image)
        loss = model.restart().emergency
        self.assertEqual((loss.first_missing_outbox_sequence, loss.last_missing_outbox_sequence), (0, 2))
        self.assertEqual(loss.dropped_chunks, 2)
        self.assertEqual(loss.dropped_points, 10)
        self.assertEqual(loss.total_dropped_points, 10)
        self.assertLess(loss.dropped_chunks, loss.last_missing_outbox_sequence - loss.first_missing_outbox_sequence + 1)

    def test_acknowledged_sparse_loss_cannot_bridge_a_live_unacked_chunk(self):
        model = RawRingModel(data_blocks=2)
        first_event = uuid.uuid4()
        second_event = uuid.uuid4()
        self.assertTrue(model.record_loss(missing_outbox_sequence=0, dropped_points=4, reason_mask=1, event_id=first_event))
        self.assertTrue(model.seal(0, digest(0)))
        self.assertTrue(model.record_loss(missing_outbox_sequence=2, dropped_points=6, reason_mask=2, event_id=second_event))
        loss = model.prepare_loss_upload()
        self.assertIsNotNone(loss)
        self.assertFalse(model.acknowledge_loss(
            **loss_ack_kwargs(loss),
            cut_at="after_emergency_commit",
        ))
        fresh = model.restart()
        self.assertEqual(fresh.emergency.state, "acknowledged")
        self.assertTrue(fresh.acknowledge_loss(
            **loss_ack_kwargs(loss),
        ))
        self.assertEqual(fresh.reclaim_through, 0)
        self.assertEqual(fresh.emergency.state, "acknowledged")
        self.assertIn(1, fresh.unacknowledged())
        self.assertEqual(fresh.reclaim_acknowledged(), 0)
        self.assertTrue(exact_ack(fresh, 1))
        self.assertEqual(fresh.reclaim_through, 2)
        self.assertEqual(fresh.emergency.state, "empty")
        # The retained empty tombstone still makes an exact duplicate server
        # ACK idempotent; no second ACK is needed to finish the local transition.
        self.assertTrue(fresh.acknowledge_loss(
            **loss_ack_kwargs(loss),
        ))
        self.assertEqual(fresh.restart().emergency.state, "empty")

    def test_loss_clear_requires_exact_sent_ack_and_persists_empty_tombstone(self):
        model = RawRingModel(data_blocks=1)
        self.assertTrue(model.record_loss(missing_outbox_sequence=0, dropped_points=9, reason_mask=1, event_id=uuid.uuid4()))
        loss = model.emergency
        self.assertFalse(model.acknowledge_loss(
            **loss_ack_kwargs(loss),
        ))
        model.prepare_loss_upload()
        self.assertFalse(model.acknowledge_loss(
            **{
                **loss_ack_kwargs(loss),
                "loss_id": uuid.uuid4(),
            },
        ))
        self.assertTrue(model.acknowledge_loss(
            **loss_ack_kwargs(loss),
        ))
        fresh = model.restart()
        self.assertEqual(fresh.emergency.state, "empty")
        self.assertEqual(fresh.dropped_points_total, 9)
        self.assertFalse(fresh.acknowledge_loss(
            **{
                **loss_ack_kwargs(loss),
                "record_sha256": digest(999),
            }
        ))
        self.assertTrue(fresh.acknowledge_loss(**loss_ack_kwargs(loss)))

    def test_new_loss_cut_preserves_prior_ack_and_is_retriable(self):
        model = RawRingModel(data_blocks=1)
        self.assertTrue(model.record_loss(missing_outbox_sequence=0, dropped_points=3, reason_mask=1, event_id=uuid.uuid4()))
        loss = model.prepare_loss_upload()
        self.assertFalse(model.acknowledge_loss(
            **loss_ack_kwargs(loss),
            cut_at="after_emergency_commit",
        ))
        before = model.flash_bytes
        self.assertFalse(model.record_loss(
            missing_outbox_sequence=1,
            dropped_points=4,
            reason_mask=2,
            event_id=uuid.uuid4(),
            cut_at="during_emergency_record",
        ))
        recovered = model.restart()
        self.assertEqual(recovered.emergency.state, "acknowledged")
        self.assertIsNone(recovered.emergency.deferred_loss)
        self.assertNotEqual(recovered.flash_bytes, before)
        self.assertTrue(recovered.record_loss(
            missing_outbox_sequence=1,
            dropped_points=4,
            reason_mask=2,
            event_id=uuid.uuid4(),
        ))

    def test_new_loss_is_durable_during_prior_loss_ack_transition(self):
        model = RawRingModel(data_blocks=1)
        self.assertTrue(model.record_loss(
            missing_outbox_sequence=0,
            dropped_points=3,
            reason_mask=1,
            event_id=uuid.uuid4(),
        ))
        first = model.prepare_loss_upload()
        self.assertFalse(model.acknowledge_loss(
            **loss_ack_kwargs(first),
            cut_at="after_emergency_commit",
        ))
        second_event = uuid.uuid4()
        self.assertTrue(model.record_loss(
            missing_outbox_sequence=1,
            dropped_points=4,
            reason_mask=2,
            event_id=second_event,
        ))

        fresh = model.restart()
        self.assertEqual(fresh.next_outbox_sequence, 2)
        self.assertEqual(fresh.dropped_points_total, 7)
        self.assertIsNotNone(fresh.emergency.deferred_loss)
        self.assertTrue(fresh.acknowledge_loss(**loss_ack_kwargs(first)))
        self.assertEqual(fresh.emergency.state, "pending")
        self.assertEqual(
            (
                fresh.emergency.first_missing_outbox_sequence,
                fresh.emergency.last_missing_outbox_sequence,
                fresh.emergency.dropped_points,
            ),
            (1, 1, 4),
        )

    def test_empty_loss_tombstone_prevents_sequence_reuse_after_journal_fallback(self):
        model = RawRingModel(data_blocks=1)
        self.assertTrue(model.record_loss(
            missing_outbox_sequence=0,
            dropped_points=3,
            reason_mask=1,
            event_id=uuid.uuid4(),
        ))
        loss = model.prepare_loss_upload()
        self.assertTrue(model.acknowledge_loss(**loss_ack_kwargs(loss)))
        self.assertEqual(model.emergency.state, "empty")

        for record in model._journal_records():
            if record.generation > 0:
                clear_one_set_bit(
                    model,
                    record.sector * ERASE_BLOCK_BYTES
                    + record.record_index * RAW_METADATA_RECORD_BYTES
                    + 116,
                    4,
                )
        fresh = model.restart()
        self.assertEqual(fresh.next_outbox_sequence, 1)
        self.assertEqual(fresh.reclaim_through, -1)
        self.assertTrue(fresh.seal(0, digest(99)))
        self.assertIn(1, fresh.slots)

    def test_recovery_of_maximum_loss_interval_is_bounded(self):
        model = RawRingModel(data_blocks=1)
        event_id = uuid.uuid4()
        record = EmergencyRecord(
            model.emergency_generation + 1,
            "pending",
            uuid.uuid4(),
            event_id,
            0,
            UINT64_MAX - 1,
            1,
            1,
            1,
            1,
            -1,
            hashlib.sha256(b"maximum-loss-range").digest(),
        )
        self.assertTrue(model._write_emergency(record))
        fresh = model.restart()
        self.assertTrue(fresh.outbox_exhausted)
        self.assertEqual(fresh.next_outbox_sequence, UINT64_MAX)
        self.assertEqual(fresh.missing_outbox_sequences, set())
        loss = fresh.prepare_loss_upload()
        self.assertTrue(fresh.acknowledge_loss(**loss_ack_kwargs(loss)))
        self.assertEqual(fresh.reclaim_through, UINT64_MAX - 1)
        self.assertEqual(fresh.emergency.state, "empty")

    def test_loss_clear_cut_never_silently_discards_the_alert(self):
        model = RawRingModel(data_blocks=1)
        self.assertTrue(model.record_loss(missing_outbox_sequence=0, dropped_points=9, reason_mask=1, event_id=uuid.uuid4()))
        loss = model.prepare_loss_upload()
        self.assertFalse(model.acknowledge_loss(
            **loss_ack_kwargs(loss),
            cut_at="after_emergency_commit",
        ))
        fresh = model.restart()
        self.assertEqual(fresh.emergency.state, "acknowledged")
        for stage in ("during_emergency_erase", "during_emergency_record", "during_emergency_commit"):
            with self.subTest(stage=stage):
                attempt = RawRingModel.from_flash(fresh.flash_bytes, data_blocks=1)
                self.assertFalse(attempt.acknowledge_loss(
                    **loss_ack_kwargs(loss),
                    cut_at=stage,
                ))
                recovered = attempt.restart()
                self.assertEqual(recovered.emergency.state, "acknowledged")
                self.assertTrue(recovered.acknowledge_loss(
                    **loss_ack_kwargs(loss),
                ))
                self.assertEqual(recovered.restart().emergency.state, "empty")

    def test_corrupt_slot_is_quarantined_neighbors_survive_and_gap_is_durable_once(self):
        model = RawRingModel(data_blocks=2)
        for sequence in range(3):
            self.assertTrue(model.seal(sequence, digest(sequence)))
        model.corrupt(1)
        fresh = model.restart()
        self.assertTrue(fresh.contains(0, digest(0)))
        self.assertFalse(fresh.contains(1))
        self.assertTrue(fresh.contains(2, digest(2)))
        self.assertIn(1, fresh.quarantined_slots)
        before_retry = fresh.flash_bytes
        with self.assertRaisesRegex(RuntimeError, "quarantined"):
            fresh.seal(1, digest(1))
        self.assertEqual(fresh.flash_bytes, before_retry)
        self.assertEqual(fresh.emergency.state, "pending")
        self.assertEqual(fresh.emergency.dropped_chunks, 1)
        again = fresh.restart()
        self.assertEqual(again.emergency.generation, fresh.emergency.generation)
        self.assertEqual(again.emergency.dropped_chunks, 1)

    def test_acked_corrupt_payload_is_not_misclassified_as_unsynchronized_loss(self):
        model = RawRingModel(data_blocks=1)
        self.assertTrue(model.seal(0, digest(0)))
        self.assertTrue(exact_ack(model, 0))
        model.corrupt(0)
        fresh = model.restart()
        self.assertFalse(fresh.contains(0, digest(0)))
        self.assertIn(0, fresh.slots)
        self.assertTrue(fresh.slots[0].acknowledged)
        self.assertFalse(fresh.slots[0].payload_valid)
        self.assertEqual(fresh.emergency.state, "empty")
        self.assertTrue(fresh.seal(1, digest(1)))
        self.assertTrue(exact_ack(fresh, 1))
        self.assertEqual(fresh.reclaim_acknowledged(), 2)


class RawFailClosedRecoveryTests(unittest.TestCase):
    def _corrupt_all_journals(self, model: RawRingModel) -> None:
        for sector in range(RAW_SUPERBLOCKS):
            for index in range(RAW_METADATA_RECORDS_PER_BLOCK):
                if model._decode_journal(sector, index) is not None:
                    clear_one_set_bit(
                        model,
                        sector * ERASE_BLOCK_BYTES + index * RAW_METADATA_RECORD_BYTES + 116,
                        4,
                    )

    def test_corrupt_latest_journal_falls_back_conservatively(self):
        model = RawRingModel(data_blocks=2)
        for sequence in range(3):
            self.assertTrue(model.seal(sequence, digest(sequence)))
            self.assertTrue(exact_ack(model, sequence))
        self.assertEqual(model.reclaim_through, 2)
        sector, index = model._journal_location
        clear_one_set_bit(
            model,
            sector * ERASE_BLOCK_BYTES + index * RAW_METADATA_RECORD_BYTES + 116,
            4,
        )
        fresh = model.restart()
        self.assertLess(fresh.reclaim_through, 2)
        # The conservative prior journal still proves q0..q1 reclaimable.  It
        # must not erase q2 until its durable ACK marker is folded into a new
        # contiguous-prefix journal record.
        self.assertEqual(fresh.reclaim_acknowledged(), 2)
        self.assertIn(2, fresh.slots)
        self.assertTrue(fresh._persist_ack_prefix())
        self.assertEqual(fresh.reclaim_through, 2)

    def test_all_metadata_records_corrupt_fail_closed_without_sequence_reuse(self):
        model = RawRingModel(data_blocks=1)
        for sequence in range(2):
            self.assertTrue(model.seal(sequence, digest(sequence)))
            self.assertTrue(exact_ack(model, sequence))
        self.assertEqual(model.reclaim_acknowledged(), 2)
        self._corrupt_all_journals(model)
        fresh = model.restart()
        self.assertTrue(fresh.metadata_degraded)
        self.assertTrue(fresh.sequence_state_unknown)
        self.assertEqual(fresh.reclaim_acknowledged(), 0)
        with self.assertRaisesRegex(RuntimeError, "read-only"):
            fresh.seal(2, digest(2))

    def test_all_metadata_corrupt_with_live_slot_is_read_only(self):
        model = RawRingModel(data_blocks=1)
        self.assertTrue(model.seal(0, digest(0)))
        self._corrupt_all_journals(model)
        fresh = model.restart()
        self.assertTrue(fresh.contains(0, digest(0)))
        self.assertTrue(fresh.metadata_degraded)
        self.assertTrue(fresh.sequence_state_unknown)
        with self.assertRaisesRegex(RuntimeError, "read-only"):
            fresh.seal(1, digest(1))
        with self.assertRaisesRegex(RuntimeError, "read-only"):
            fresh.record_loss(
                missing_outbox_sequence=1,
                dropped_points=1,
                reason_mask=1,
            )

    def test_committed_slot_with_unreadable_header_blocks_sequence_reuse(self):
        model = RawRingModel(data_blocks=1)
        self.assertTrue(model.seal(0, digest(0)))
        clear_one_set_bit(model, model._slot_offset(model.slots[0].slot_index))

        fresh = model.restart()
        before = fresh.flash_bytes
        self.assertTrue(fresh.sequence_state_unknown)
        self.assertEqual(fresh.unknown_corrupt_slot_indexes, {0})
        self.assertEqual(fresh.reclaim_acknowledged(), 0)
        self.assertEqual(fresh.flash_bytes, before)
        with self.assertRaisesRegex(RuntimeError, "read-only"):
            fresh.seal(0, digest(1))

    def test_both_emergency_copies_corrupt_fail_closed(self):
        model = RawRingModel(data_blocks=1)
        self.assertTrue(model.record_loss(missing_outbox_sequence=0, dropped_points=3, reason_mask=1, event_id=uuid.uuid4()))
        self.assertTrue(model.record_loss(missing_outbox_sequence=1, dropped_points=4, reason_mask=2, event_id=uuid.uuid4()))
        for sector in range(RAW_SUPERBLOCKS, RAW_SUPERBLOCKS + RAW_EMERGENCY_BLOCKS):
            clear_one_set_bit(model, sector * ERASE_BLOCK_BYTES + 244, 4)
        fresh = model.restart()
        self.assertTrue(fresh.loss_state_unknown)
        self.assertEqual(fresh.reclaim_acknowledged(), 0)
        with self.assertRaisesRegex(RuntimeError, "read-only"):
            fresh.seal(0, digest(9))
        with self.assertRaisesRegex(RuntimeError, "read-only"):
            fresh.record_loss(
                missing_outbox_sequence=2,
                dropped_points=5,
                reason_mask=4,
            )

    def test_latest_emergency_corruption_cross_checks_journal_generation(self):
        model = RawRingModel(data_blocks=1)
        self.assertTrue(model.record_loss(missing_outbox_sequence=0, dropped_points=3, reason_mask=1, event_id=uuid.uuid4()))
        self.assertTrue(model.record_loss(missing_outbox_sequence=1, dropped_points=4, reason_mask=2, event_id=uuid.uuid4()))
        latest_sector = model.emergency.sector
        clear_one_set_bit(model, latest_sector * ERASE_BLOCK_BYTES + 244, 4)
        fresh = model.restart()
        self.assertTrue(fresh.loss_state_unknown)
        with self.assertRaisesRegex(RuntimeError, "read-only"):
            fresh.record_loss(
                missing_outbox_sequence=2,
                dropped_points=5,
                reason_mask=4,
            )

    def test_duplicate_outbox_or_logical_identity_mount_fails_closed(self):
        for duplicate_outbox in (True, False):
            with self.subTest(duplicate_outbox=duplicate_outbox):
                model = RawRingModel(data_blocks=2)
                self.assertTrue(model.seal(0, digest(0)))
                first = model.slots[0]
                encoded = model._encode_slot(
                    outbox_sequence=0 if duplicate_outbox else 1,
                    identity=(
                        ChunkIdentity(DEFAULT_DEVICE_ID, 2, 0)
                        if duplicate_outbox
                        else first.identity
                    ),
                    first_point_sequence=96,
                    point_count=1,
                    digest=digest(99),
                )
                offset = model._slot_offset(1)
                model.flash.program(offset, encoded)
                model.flash.program(offset + 120, b"\x00" * 8)
                with self.assertRaises(RuntimeError):
                    model.restart()

    def test_exact_duplicate_logical_identity_with_same_digest_fails_closed(self):
        model = RawRingModel(data_blocks=2)
        self.assertTrue(model.seal(0, digest(0)))
        first = model.slots[0]
        encoded = model._encode_slot(
            outbox_sequence=1,
            identity=first.identity,
            first_point_sequence=first.first_point_sequence,
            point_count=first.point_count,
            digest=first.digest,
        )
        offset = model._slot_offset(1)
        model.flash.program(offset, encoded)
        model.flash.program(offset + 120, b"\x00" * 8)
        with self.assertRaisesRegex(RuntimeError, "multiple committed slots"):
            model.restart()

    def test_half_range_journal_generations_are_rejected_in_any_scan_order(self):
        base = RawRingModel(data_blocks=1)._journal_records()[0]
        ambiguous = replace(base, generation=1 << 63, record_index=1)
        with self.assertRaisesRegex(RuntimeError, "half-range ambiguous"):
            RawRingModel._select_newest([base, replace(base, generation=1), ambiguous])
        with self.assertRaisesRegex(RuntimeError, "half-range ambiguous"):
            RawRingModel._select_newest([ambiguous, replace(base, generation=1), base])


class StorageWorkloadEvidenceTests(unittest.TestCase):
    def test_fill_reclaim_refuses_overwrite_and_preserves_unacknowledged_tail(self):
        for factory in (RawRingModel, LittleFsModel):
            with self.subTest(factory=factory.__name__):
                result = run_fill_reclaim(factory)
                self.assertTrue(result["rejected_when_full"])
                self.assertGreater(result["reclaimed_chunks"], 0)
                self.assertEqual(result["reclaimed_chunks"], result["refilled_chunks"])
                self.assertTrue(result["preexisting_unacknowledged_preserved"])

    def test_deterministic_10k_workloads_finish_without_unacknowledged_tail(self):
        raw = run_random_power_cut_workload(RawRingModel)
        littlefs = run_random_power_cut_workload(LittleFsModel)
        for model in (raw, littlefs):
            metrics = model.metrics()
            self.assertEqual(metrics["successful_new_seals"], 10_000)
            self.assertGreaterEqual(metrics["power_cuts"], 190)
            self.assertEqual(metrics["unacknowledged_after_run"], 0)
        raw_metrics = raw.metrics()
        self.assertFalse(raw_metrics["metadata_degraded"])
        self.assertFalse(raw_metrics["sequence_state_unknown"])
        self.assertFalse(raw_metrics["loss_state_unknown"])
        self.assertLessEqual(raw_metrics["data_sector_erase_wear"]["spread"], 1)
        # One sector can be two erases ahead because power may fail during a
        # rollover erase and the retry must erase that target again.  The
        # deterministic workload freezes this bounded retry cost; it is not a
        # claim of perfect A/B erase-count equality.
        self.assertLessEqual(raw_metrics["metadata_sector_erase_wear"]["spread"], 2)


if __name__ == "__main__":
    unittest.main()
