"""Deterministic Phase 0B flash/storage feasibility models.

The raw-ring model is deliberately byte-addressed.  Every reboot reconstructs
runtime state from NOR-flash bytes, committed slot markers, CRCs, two metadata
journals, and two emergency-loss sectors.  It is still a host model rather
than physical ESP32 evidence; timing, brownout and real-device wear remain a
separate gate.

The LittleFS candidate remains an explicitly idealized capacity/write-cost
model.  It is not presented as a trace of the ESP-IDF LittleFS port.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import hashlib
import math
import random
import statistics
import struct
from typing import Callable, Iterable
import uuid
import zlib

from track_v3 import CHUNK_HEADER_SIZE, MAX_POINTS_PER_CHUNK, POINT_SIZE


PARTITION_BYTES = 0x150000
ERASE_BLOCK_BYTES = 4096
PROGRAM_PAGE_BYTES = 256
TOTAL_BLOCKS = PARTITION_BYTES // ERASE_BLOCK_BYTES
CHUNK_BYTES = CHUNK_HEADER_SIZE + MAX_POINTS_PER_CHUNK * POINT_SIZE
ASSUMED_SEQUENTIAL_READ_MIB_S = 20.0

RAW_SUPERBLOCKS = 2
# Two erase sectors are required for a power-safe A/B loss snapshot.  Two
# records in one sector would both be destroyed by the same sector erase.
RAW_EMERGENCY_BLOCKS = 2
RAW_DATA_BLOCKS = TOTAL_BLOCKS - RAW_SUPERBLOCKS - RAW_EMERGENCY_BLOCKS
RAW_SLOT_BYTES = 2048
RAW_SLOTS_PER_BLOCK = ERASE_BLOCK_BYTES // RAW_SLOT_BYTES
RAW_CAPACITY_CHUNKS = RAW_DATA_BLOCKS * RAW_SLOTS_PER_BLOCK
RAW_METADATA_RECORD_BYTES = 128
RAW_METADATA_RECORDS_PER_BLOCK = ERASE_BLOCK_BYTES // RAW_METADATA_RECORD_BYTES
RAW_EMERGENCY_RECORD_BYTES = 256
RAW_SLOT_HEADER_BYTES = 128

LITTLEFS_EMERGENCY_BLOCKS = 2
LITTLEFS_METADATA_BLOCKS = 2
LITTLEFS_OPERATIONAL_RESERVE_BLOCKS = math.ceil(TOTAL_BLOCKS * 0.15)
LITTLEFS_SEGMENT_CHUNKS = 32
LITTLEFS_SEGMENT_BLOCKS = math.ceil(LITTLEFS_SEGMENT_CHUNKS * CHUNK_BYTES / ERASE_BLOCK_BYTES)
LITTLEFS_DATA_BLOCKS = (
    TOTAL_BLOCKS
    - LITTLEFS_EMERGENCY_BLOCKS
    - LITTLEFS_METADATA_BLOCKS
    - LITTLEFS_OPERATIONAL_RESERVE_BLOCKS
)
LITTLEFS_CAPACITY_CHUNKS = (
    LITTLEFS_DATA_BLOCKS // LITTLEFS_SEGMENT_BLOCKS
) * LITTLEFS_SEGMENT_CHUNKS
LITTLEFS_METADATA_COMMIT_BYTES = 512

DEFAULT_DEVICE_ID = uuid.UUID("11111111-1111-4111-8111-111111111111")
UINT64_MAX = (1 << 64) - 1
COMMIT_MARKER = b"\x00" * 8
ERASED_MARKER = b"\xff" * 8

_SLOT_PREFIX = struct.Struct("<4sBBHQ16sIIIHH32sII24s")  # 112 bytes
_JOURNAL_PREFIX = struct.Struct("<4sBBHQQQIIQQ60sI")  # 120 bytes
_EMERGENCY_PREFIX = struct.Struct("<4sBBHQ16s16sQQQQQI152sI")  # 248 bytes
_JOURNAL_RESERVED = struct.Struct("<QQ44s")
_DEFERRED_LOSS = struct.Struct("<16sQQQQI32s36s")


def _digest(sequence: int) -> bytes:
    return hashlib.sha256(f"dog-rgb-phase0-chunk:{sequence}".encode("ascii")).digest()


def _opaque_chunk(digest: bytes, point_count: int) -> bytes:
    """Make deterministic opaque bytes of the right encoded-chunk length.

    Track-v3 byte/hash correctness is tested by ``track_v3.py``.  The flash
    model treats that already-validated chunk as opaque and stores its
    canonical content digest in the slot envelope.
    """

    length = CHUNK_HEADER_SIZE + point_count * POINT_SIZE
    seed = hashlib.sha256(b"raw-slot-body" + digest).digest()
    return (seed * math.ceil(length / len(seed)))[:length]


def _loss_event_fingerprint(
    event_id: uuid.UUID,
    outbox_sequence: int,
    dropped_points: int,
    reason_mask: int,
) -> bytes:
    return hashlib.sha256(
        b"dog-rgb-loss-event-v1"
        + event_id.bytes
        + struct.pack("<QQI", outbox_sequence, dropped_points, reason_mask)
    ).digest()


def _wear_summary(counts: list[int]) -> dict[str, float | int]:
    if not counts:
        return {"min": 0, "mean": 0.0, "max": 0, "spread": 0}
    return {
        "min": min(counts),
        "mean": round(statistics.fmean(counts), 3),
        "max": max(counts),
        "spread": max(counts) - min(counts),
    }


def _serial_newer(left: int, right: int) -> bool:
    """RFC-1982-style comparison for uint64 generations."""

    delta = (left - right) & UINT64_MAX
    return 0 < delta < (1 << 63)


@dataclass
class Counters:
    programmed_bytes: int = 0
    erased_bytes: int = 0
    read_bytes: int = 0
    program_operations: int = 0
    erase_operations: int = 0
    recoveries: int = 0
    power_cuts: int = 0
    power_cuts_during_data: int = 0
    power_cuts_during_slot_commit: int = 0
    power_cuts_after_data: int = 0
    power_cuts_during_seal_metadata: int = 0
    power_cuts_during_ack_marker: int = 0
    power_cuts_during_ack_metadata: int = 0
    power_cuts_during_journal_rollover: int = 0
    power_cuts_during_reclaim: int = 0
    power_cuts_during_emergency: int = 0
    recovered_orphan_chunks: int = 0
    rolled_back_incomplete_chunks: int = 0
    corruption_events: int = 0
    coverage_gap_events: int = 0
    successful_new_seals: int = 0
    idempotent_seals: int = 0


class NorFlash:
    """Small NOR model: program may only clear bits; erase is sector-wide."""

    def __init__(self, size: int, image: bytes | bytearray | None = None) -> None:
        if size <= 0 or size % ERASE_BLOCK_BYTES:
            raise ValueError("flash size must be a positive whole number of sectors")
        self.data = bytearray(b"\xff" * size if image is None else image)
        if len(self.data) != size:
            raise ValueError("flash image has the wrong size")

    def snapshot(self) -> bytes:
        return bytes(self.data)

    def read(self, offset: int, length: int) -> bytes:
        if offset < 0 or length < 0 or offset + length > len(self.data):
            raise ValueError("flash read out of bounds")
        return bytes(self.data[offset : offset + length])

    def program(self, offset: int, value: bytes, *, cut_after: int | None = None) -> int:
        if offset < 0 or offset + len(value) > len(self.data):
            raise ValueError("flash program out of bounds")
        count = len(value) if cut_after is None else max(0, min(cut_after, len(value)))
        for index, new in enumerate(value[:count]):
            old = self.data[offset + index]
            if (old | new) != old:
                raise ValueError("NOR program attempted a 0-to-1 transition")
        for index, new in enumerate(value[:count]):
            self.data[offset + index] &= new
        return count

    def erase_sector(self, sector: int, *, cut_after: int | None = None) -> int:
        offset = sector * ERASE_BLOCK_BYTES
        if sector < 0 or offset + ERASE_BLOCK_BYTES > len(self.data):
            raise ValueError("flash erase sector out of bounds")
        count = ERASE_BLOCK_BYTES if cut_after is None else max(0, min(cut_after, ERASE_BLOCK_BYTES))
        self.data[offset : offset + count] = b"\xff" * count
        return count

    def erase(self, offset: int, length: int, *, cut_after: int | None = None) -> int:
        if offset % ERASE_BLOCK_BYTES or length != ERASE_BLOCK_BYTES:
            raise ValueError("NOR erase must target one aligned erase sector")
        return self.erase_sector(offset // ERASE_BLOCK_BYTES, cut_after=cut_after)


@dataclass(frozen=True)
class ChunkIdentity:
    device_id: uuid.UUID
    boot_sequence: int
    chunk_sequence: int


@dataclass(frozen=True)
class AckReceipt:
    device_id: uuid.UUID
    boot_sequence: int
    chunk_sequence: int
    accepted_point_count: int
    through_point_sequence: int
    content_sha256: bytes


@dataclass(frozen=True)
class RawSlot:
    slot_index: int
    outbox_sequence: int
    identity: ChunkIdentity
    first_point_sequence: int
    point_count: int
    digest: bytes
    acknowledged: bool
    ack_marker_torn: bool = False
    payload_valid: bool = True

    @property
    def through_point_sequence(self) -> int:
        return self.first_point_sequence + self.point_count - 1

    def receipt(self) -> AckReceipt:
        return AckReceipt(
            self.identity.device_id,
            self.identity.boot_sequence,
            self.identity.chunk_sequence,
            self.point_count,
            self.through_point_sequence,
            self.digest,
        )


@dataclass(frozen=True)
class JournalRecord:
    generation: int
    next_outbox_sequence: int
    reclaim_through: int
    write_cursor: int
    erase_intent_block: int | None
    erase_intent_sequences: tuple[int, ...]
    emergency_generation: int
    dropped_points_total: int
    sector: int
    record_index: int


@dataclass(frozen=True)
class EmergencyRecord:
    generation: int
    state: str
    loss_id: uuid.UUID
    last_event_id: uuid.UUID
    first_missing_outbox_sequence: int
    last_missing_outbox_sequence: int
    dropped_chunks: int
    dropped_points: int
    total_dropped_points: int
    reason_mask: int
    sector: int
    last_event_fingerprint: bytes = b"\x00" * 32
    deferred_loss: "DeferredLoss | None" = None

    @property
    def record_sha256(self) -> bytes:
        return hashlib.sha256(
            b"dog-rgb-loss-receipt-v1"
            + struct.pack("<Q", self.generation)
            + self.loss_id.bytes
            + struct.pack(
                "<QQQQQI",
                self.first_missing_outbox_sequence,
                self.last_missing_outbox_sequence,
                self.dropped_chunks,
                self.dropped_points,
                self.total_dropped_points,
                self.reason_mask,
            )
            + self.last_event_fingerprint
        ).digest()


@dataclass(frozen=True)
class DeferredLoss:
    last_event_id: uuid.UUID
    first_missing_outbox_sequence: int
    last_missing_outbox_sequence: int
    dropped_chunks: int
    dropped_points: int
    reason_mask: int
    last_event_fingerprint: bytes


class RawRingModel:
    """Power-cut model for the selected raw layout.

    Correctness state lives only in ``flash``.  Counters and erase-count arrays
    are external instrumentation and are never consulted for recovery or
    reclaim decisions.
    """

    def __init__(
        self,
        data_blocks: int = RAW_DATA_BLOCKS,
        *,
        flash_image: bytes | None = None,
        counters: Counters | None = None,
        data_erase_counts: list[int] | None = None,
        metadata_erase_counts: list[int] | None = None,
        emergency_erase_counts: list[int] | None = None,
    ) -> None:
        if data_blocks <= 0:
            raise ValueError("data_blocks must be positive")
        self.data_blocks = data_blocks
        self.total_blocks = RAW_SUPERBLOCKS + RAW_EMERGENCY_BLOCKS + data_blocks
        self.flash = NorFlash(self.total_blocks * ERASE_BLOCK_BYTES, flash_image)
        self.counters = counters or Counters()
        self.data_erase_counts = data_erase_counts or [0] * data_blocks
        self.metadata_erase_counts = metadata_erase_counts or [0] * RAW_SUPERBLOCKS
        self.emergency_erase_counts = emergency_erase_counts or [0] * RAW_EMERGENCY_BLOCKS
        self.max_recovery_read_bytes = 0
        self.sent_outbox_sequences: set[int] = set()
        self.sent_loss_id: uuid.UUID | None = None
        self.sent_loss_generation: int | None = None
        self.sent_loss_sha256: bytes | None = None
        self._load_from_flash()
        if flash_image is None:
            # Factory format establishes one committed empty record in each
            # independently erasable A/B domain.  Without these baselines, a
            # cut during the first journal or loss update would leave only a
            # torn record and make an otherwise blank device ambiguous.
            empty = EmergencyRecord(
                0,
                "empty",
                uuid.UUID(int=0),
                uuid.UUID(int=0),
                0,
                0,
                0,
                0,
                0,
                0,
                -1,
            )
            if not self._write_emergency(empty):
                raise AssertionError("initial emergency baseline failed")
            if not self._append_journal():
                raise AssertionError("initial metadata baseline failed")

    @classmethod
    def from_flash(cls, image: bytes, *, data_blocks: int | None = None) -> "RawRingModel":
        inferred = len(image) // ERASE_BLOCK_BYTES - RAW_SUPERBLOCKS - RAW_EMERGENCY_BLOCKS
        return cls(inferred if data_blocks is None else data_blocks, flash_image=bytes(image))

    @property
    def capacity_chunks(self) -> int:
        return self.data_blocks * RAW_SLOTS_PER_BLOCK

    @property
    def flash_bytes(self) -> bytes:
        return self.flash.snapshot()

    @property
    def data_start_sector(self) -> int:
        return RAW_SUPERBLOCKS + RAW_EMERGENCY_BLOCKS

    def restart(self) -> "RawRingModel":
        """Return a genuinely fresh runtime reconstructed only from bytes."""

        return RawRingModel.from_flash(self.flash.snapshot(), data_blocks=self.data_blocks)

    def _program(self, offset: int, value: bytes, *, cut_after: int | None = None) -> int:
        count = self.flash.program(offset, value, cut_after=cut_after)
        self.counters.programmed_bytes += count
        self.counters.program_operations += 1
        return count

    def _erase(self, sector: int, *, cut_after: int | None = None) -> int:
        count = self.flash.erase_sector(sector, cut_after=cut_after)
        self.counters.erased_bytes += count
        self.counters.erase_operations += 1
        if sector < RAW_SUPERBLOCKS:
            self.metadata_erase_counts[sector] += 1
        elif sector < self.data_start_sector:
            self.emergency_erase_counts[sector - RAW_SUPERBLOCKS] += 1
        else:
            self.data_erase_counts[sector - self.data_start_sector] += 1
        return count

    def _power_cut(self, stage: str) -> None:
        self.counters.power_cuts += 1
        field_name = {
            "data": "power_cuts_during_data",
            "slot_commit": "power_cuts_during_slot_commit",
            "after_slot_commit": "power_cuts_after_data",
            "seal_metadata": "power_cuts_during_seal_metadata",
            "ack_marker": "power_cuts_during_ack_marker",
            "ack_metadata": "power_cuts_during_ack_metadata",
            "journal_rollover": "power_cuts_during_journal_rollover",
            "reclaim": "power_cuts_during_reclaim",
            "emergency": "power_cuts_during_emergency",
        }[stage]
        setattr(self.counters, field_name, getattr(self.counters, field_name) + 1)
        snapshot = self.flash.snapshot()
        fresh = RawRingModel(
            self.data_blocks,
            flash_image=snapshot,
            counters=self.counters,
            data_erase_counts=self.data_erase_counts,
            metadata_erase_counts=self.metadata_erase_counts,
            emergency_erase_counts=self.emergency_erase_counts,
        )
        self.flash = fresh.flash
        self._copy_runtime_from(fresh)
        self.counters.recoveries += 1
        scan_bytes = len(snapshot)
        self.counters.read_bytes += scan_bytes
        self.max_recovery_read_bytes = max(self.max_recovery_read_bytes, scan_bytes)

    def _copy_runtime_from(self, other: "RawRingModel") -> None:
        self.slots = other.slots
        self.incomplete_slot_indexes = other.incomplete_slot_indexes
        self.corrupt_slot_indexes = other.corrupt_slot_indexes
        self.missing_outbox_sequences = other.missing_outbox_sequences
        self.write_cursor = other.write_cursor
        self.next_outbox_sequence = other.next_outbox_sequence
        self.reclaim_through = other.reclaim_through
        self.journal_generation = other.journal_generation
        self._journal_location = other._journal_location
        self.emergency = other.emergency
        self.emergency_generation = other.emergency_generation
        self.dropped_points_total = other.dropped_points_total
        self.erase_intent_block = other.erase_intent_block
        self.erase_intent_sequences = other.erase_intent_sequences
        self.metadata_degraded = other.metadata_degraded
        self.sequence_state_unknown = other.sequence_state_unknown
        self.loss_state_unknown = other.loss_state_unknown
        self.outbox_exhausted = other.outbox_exhausted
        # Network-response provenance is intentionally volatile across reboot.
        self.sent_outbox_sequences = set()
        self.sent_loss_id = None
        self.sent_loss_generation = None
        self.sent_loss_sha256 = None

    def _slot_offset(self, slot_index: int) -> int:
        if not 0 <= slot_index < self.capacity_chunks:
            raise IndexError(slot_index)
        block, within = divmod(slot_index, RAW_SLOTS_PER_BLOCK)
        return (self.data_start_sector + block) * ERASE_BLOCK_BYTES + within * RAW_SLOT_BYTES

    def _encode_slot(
        self,
        *,
        outbox_sequence: int,
        identity: ChunkIdentity,
        first_point_sequence: int,
        point_count: int,
        digest: bytes,
    ) -> bytes:
        if not 0 <= outbox_sequence <= UINT64_MAX:
            raise ValueError("outbox sequence out of uint64 range")
        if identity.device_id.int == 0:
            raise ValueError("nil device UUID")
        if not 0 <= identity.boot_sequence <= 0xFFFFFFFF:
            raise ValueError("boot sequence out of uint32 range")
        if not 0 <= identity.chunk_sequence <= 0xFFFFFFFF:
            raise ValueError("chunk sequence out of uint32 range")
        if not 0 <= first_point_sequence <= 0xFFFFFFFF:
            raise ValueError("first point sequence out of uint32 range")
        if not 1 <= point_count <= MAX_POINTS_PER_CHUNK:
            raise ValueError("point count out of range")
        if first_point_sequence + point_count - 1 > 0xFFFFFFFF:
            raise ValueError("through point sequence overflows uint32")
        if len(digest) != 32:
            raise ValueError("content digest must be 32 bytes")
        payload = _opaque_chunk(digest, point_count)
        payload_crc = zlib.crc32(payload) & 0xFFFFFFFF
        reserved = b"\x00" * 24
        prefix_zero_crc = _SLOT_PREFIX.pack(
            b"DROS", 1, 0, RAW_SLOT_HEADER_BYTES, outbox_sequence,
            identity.device_id.bytes, identity.boot_sequence, identity.chunk_sequence,
            first_point_sequence, point_count, len(payload), digest, payload_crc, 0, reserved,
        )
        header_crc = zlib.crc32(prefix_zero_crc) & 0xFFFFFFFF
        prefix = _SLOT_PREFIX.pack(
            b"DROS", 1, 0, RAW_SLOT_HEADER_BYTES, outbox_sequence,
            identity.device_id.bytes, identity.boot_sequence, identity.chunk_sequence,
            first_point_sequence, point_count, len(payload), digest, payload_crc,
            header_crc, reserved,
        )
        encoded = prefix + ERASED_MARKER + ERASED_MARKER + payload
        if len(encoded) > RAW_SLOT_BYTES:
            raise AssertionError("slot encoding exceeds fixed slot")
        return encoded

    def _decode_slot(self, slot_index: int) -> tuple[str, RawSlot | None]:
        offset = self._slot_offset(slot_index)
        raw = self.flash.read(offset, RAW_SLOT_BYTES)
        if raw == b"\xff" * RAW_SLOT_BYTES:
            return "erased", None
        ack_marker = raw[112:120]
        commit_marker = raw[120:128]
        if commit_marker != COMMIT_MARKER:
            return "incomplete", None
        try:
            values = _SLOT_PREFIX.unpack(raw[:112])
            (
                magic, version, flags, header_bytes, outbox_sequence, device_bytes,
                boot_sequence, chunk_sequence, first_point_sequence, point_count,
                payload_length, digest, payload_crc, header_crc, reserved,
            ) = values
            if magic != b"DROS" or version != 1 or flags != 0 or header_bytes != RAW_SLOT_HEADER_BYTES:
                raise ValueError("slot envelope version")
            if reserved != b"\x00" * 24:
                raise ValueError("slot reserved bytes")
            if not 1 <= point_count <= MAX_POINTS_PER_CHUNK:
                raise ValueError("slot point count")
            if payload_length != CHUNK_HEADER_SIZE + point_count * POINT_SIZE:
                raise ValueError("slot payload length")
            if RAW_SLOT_HEADER_BYTES + payload_length > RAW_SLOT_BYTES:
                raise ValueError("slot payload bounds")
            zero_crc = _SLOT_PREFIX.pack(
                magic, version, flags, header_bytes, outbox_sequence, device_bytes,
                boot_sequence, chunk_sequence, first_point_sequence, point_count,
                payload_length, digest, payload_crc, 0, reserved,
            )
            if zlib.crc32(zero_crc) & 0xFFFFFFFF != header_crc:
                raise ValueError("slot header CRC")
            identity = ChunkIdentity(uuid.UUID(bytes=device_bytes), boot_sequence, chunk_sequence)
            if identity.device_id.int == 0 or first_point_sequence + point_count - 1 > 0xFFFFFFFF:
                raise ValueError("slot semantic bounds")
        except (ValueError, struct.error):
            return "corrupt", None
        acknowledged = ack_marker == COMMIT_MARKER
        payload = raw[RAW_SLOT_HEADER_BYTES : RAW_SLOT_HEADER_BYTES + payload_length]
        payload_valid = zlib.crc32(payload) & 0xFFFFFFFF == payload_crc
        if not payload_valid and not acknowledged:
            return "corrupt", None
        return ("valid" if payload_valid else "acked_corrupt"), RawSlot(
            slot_index, outbox_sequence, identity, first_point_sequence,
            point_count, digest, acknowledged,
            ack_marker_torn=ack_marker not in (ERASED_MARKER, COMMIT_MARKER),
            payload_valid=payload_valid,
        )

    def _encode_journal(self, generation: int) -> bytes:
        reclaim_encoded = UINT64_MAX if self.reclaim_through < 0 else self.reclaim_through
        emergency_generation = UINT64_MAX if self.emergency_generation < 0 else self.emergency_generation
        intent_sequences = list(self.erase_intent_sequences)
        if len(intent_sequences) > RAW_SLOTS_PER_BLOCK:
            raise AssertionError("erase intent exceeds one data sector")
        intent_sequences.extend([UINT64_MAX] * (RAW_SLOTS_PER_BLOCK - len(intent_sequences)))
        reserved = _JOURNAL_RESERVED.pack(
            intent_sequences[0], intent_sequences[1], b"\x00" * 44
        )
        erase_intent = 0 if self.erase_intent_block is None else self.erase_intent_block + 1
        zero = _JOURNAL_PREFIX.pack(
            b"DRMJ", 1, 0, RAW_METADATA_RECORD_BYTES, generation,
            self.next_outbox_sequence, reclaim_encoded, self.write_cursor, erase_intent,
            emergency_generation, self.dropped_points_total, reserved, 0,
        )
        crc = zlib.crc32(zero) & 0xFFFFFFFF
        return _JOURNAL_PREFIX.pack(
            b"DRMJ", 1, 0, RAW_METADATA_RECORD_BYTES, generation,
            self.next_outbox_sequence, reclaim_encoded, self.write_cursor, erase_intent,
            emergency_generation, self.dropped_points_total, reserved, crc,
        ) + ERASED_MARKER

    def _decode_journal(self, sector: int, record_index: int) -> JournalRecord | None:
        offset = sector * ERASE_BLOCK_BYTES + record_index * RAW_METADATA_RECORD_BYTES
        raw = self.flash.read(offset, RAW_METADATA_RECORD_BYTES)
        if raw[120:128] != COMMIT_MARKER:
            return None
        try:
            (
                magic, version, flags, record_bytes, generation, next_outbox,
                reclaim_encoded, write_cursor, erase_intent, emergency_generation,
                dropped_points_total, reserved, crc,
            ) = _JOURNAL_PREFIX.unpack(raw[:120])
        except struct.error:
            return None
        if (
            magic != b"DRMJ" or version != 1 or flags != 0
            or record_bytes != RAW_METADATA_RECORD_BYTES
        ):
            return None
        zero = _JOURNAL_PREFIX.pack(
            magic, version, flags, record_bytes, generation, next_outbox,
            reclaim_encoded, write_cursor, erase_intent, emergency_generation,
            dropped_points_total, reserved, 0,
        )
        if zlib.crc32(zero) & 0xFFFFFFFF != crc:
            return None
        reclaim = -1 if reclaim_encoded == UINT64_MAX else reclaim_encoded
        if (
            write_cursor >= self.capacity_chunks
            or (reclaim >= 0 and reclaim >= next_outbox)
            or erase_intent > self.data_blocks
        ):
            return None
        try:
            intent_first, intent_second, intent_reserved = _JOURNAL_RESERVED.unpack(reserved)
        except struct.error:
            return None
        if intent_reserved != b"\x00" * 44:
            return None
        intent_sequences = tuple(
            value for value in (intent_first, intent_second) if value != UINT64_MAX
        )
        if erase_intent == 0 and intent_sequences:
            return None
        if len(set(intent_sequences)) != len(intent_sequences):
            return None
        emergency_gen = -1 if emergency_generation == UINT64_MAX else emergency_generation
        return JournalRecord(
            generation, next_outbox, reclaim, write_cursor,
            None if erase_intent == 0 else erase_intent - 1,
            intent_sequences, emergency_gen, dropped_points_total, sector, record_index,
        )

    def _journal_records(self) -> list[JournalRecord]:
        return [
            record
            for sector in range(RAW_SUPERBLOCKS)
            for index in range(RAW_METADATA_RECORDS_PER_BLOCK)
            if (record := self._decode_journal(sector, index)) is not None
        ]

    @staticmethod
    def _select_newest(records: Iterable[JournalRecord]) -> JournalRecord | None:
        records = list(records)
        for left_index, left in enumerate(records):
            for right in records[left_index + 1 :]:
                if ((left.generation - right.generation) & UINT64_MAX) == (1 << 63):
                    raise RuntimeError("metadata generations are half-range ambiguous")
        selected: JournalRecord | None = None
        for record in records:
            if selected is None or _serial_newer(record.generation, selected.generation):
                selected = record
            elif record.generation == selected.generation and record != selected:
                raise RuntimeError("conflicting metadata records share a generation")
        return selected

    def _encode_emergency(self, record: EmergencyRecord) -> bytes:
        state_code = {"empty": 0, "pending": 1, "acknowledged": 2}[record.state]
        if len(record.last_event_fingerprint) != 32:
            raise ValueError("loss-event fingerprint must be 32 bytes")
        if record.deferred_loss is None:
            deferred = b"\x00" * 120
        else:
            pending = record.deferred_loss
            if len(pending.last_event_fingerprint) != 32:
                raise ValueError("deferred loss-event fingerprint must be 32 bytes")
            deferred = _DEFERRED_LOSS.pack(
                pending.last_event_id.bytes,
                pending.first_missing_outbox_sequence,
                pending.last_missing_outbox_sequence,
                pending.dropped_chunks,
                pending.dropped_points,
                pending.reason_mask,
                pending.last_event_fingerprint,
                b"\x00" * 36,
            )
        reserved = record.last_event_fingerprint + deferred
        zero = _EMERGENCY_PREFIX.pack(
            b"DREL", 1, state_code, RAW_EMERGENCY_RECORD_BYTES, record.generation,
            record.loss_id.bytes, record.last_event_id.bytes,
            record.first_missing_outbox_sequence,
            record.last_missing_outbox_sequence, record.dropped_chunks,
            record.dropped_points, record.total_dropped_points,
            record.reason_mask, reserved, 0,
        )
        crc = zlib.crc32(zero) & 0xFFFFFFFF
        return _EMERGENCY_PREFIX.pack(
            b"DREL", 1, state_code, RAW_EMERGENCY_RECORD_BYTES, record.generation,
            record.loss_id.bytes, record.last_event_id.bytes,
            record.first_missing_outbox_sequence,
            record.last_missing_outbox_sequence, record.dropped_chunks,
            record.dropped_points, record.total_dropped_points,
            record.reason_mask, reserved, crc,
        ) + ERASED_MARKER + b"\xff" * (ERASE_BLOCK_BYTES - RAW_EMERGENCY_RECORD_BYTES)

    def _decode_emergency(self, sector: int) -> EmergencyRecord | None:
        offset = sector * ERASE_BLOCK_BYTES
        raw = self.flash.read(offset, RAW_EMERGENCY_RECORD_BYTES)
        if raw[248:256] != COMMIT_MARKER:
            return None
        try:
            (
                magic, version, state_code, record_bytes, generation, loss_id_bytes,
                last_event_id_bytes, first, last, dropped_chunks, dropped_points,
                total_dropped_points, reason_mask, reserved, crc,
            ) = _EMERGENCY_PREFIX.unpack(raw[:248])
        except struct.error:
            return None
        if (
            magic != b"DREL" or version != 1 or state_code not in (0, 1, 2)
            or record_bytes != RAW_EMERGENCY_RECORD_BYTES
        ):
            return None
        zero = _EMERGENCY_PREFIX.pack(
            magic, version, state_code, record_bytes, generation, loss_id_bytes,
            last_event_id_bytes, first, last, dropped_chunks, dropped_points,
            total_dropped_points, reason_mask, reserved, 0,
        )
        if zlib.crc32(zero) & 0xFFFFFFFF != crc:
            return None
        deferred_raw = reserved[32:]
        deferred_loss: DeferredLoss | None = None
        if deferred_raw != b"\x00" * 120:
            try:
                (
                    deferred_event_id, deferred_first, deferred_last,
                    deferred_chunks, deferred_points, deferred_reason,
                    deferred_fingerprint, deferred_reserved,
                ) = _DEFERRED_LOSS.unpack(deferred_raw)
            except struct.error:
                return None
            if (
                state_code != 2
                or deferred_reserved != b"\x00" * 36
                or uuid.UUID(bytes=deferred_event_id).int == 0
                or deferred_first > deferred_last
                or deferred_chunks == 0
                or deferred_points == 0
                or deferred_reason == 0
                or deferred_fingerprint == b"\x00" * 32
            ):
                return None
            deferred_loss = DeferredLoss(
                uuid.UUID(bytes=deferred_event_id), deferred_first, deferred_last,
                deferred_chunks, deferred_points, deferred_reason,
                deferred_fingerprint,
            )
        state = {0: "empty", 1: "pending", 2: "acknowledged"}[state_code]
        if state == "empty":
            has_retained_receipt = uuid.UUID(bytes=loss_id_bytes).int != 0
            if not has_retained_receipt and (
                any((first, last, dropped_chunks, dropped_points, reason_mask))
                or uuid.UUID(bytes=last_event_id_bytes).int
                or reserved[:32] != b"\x00" * 32
            ):
                return None
            if has_retained_receipt and (
                first > last or dropped_chunks == 0 or dropped_points == 0
                or reason_mask == 0 or uuid.UUID(bytes=last_event_id_bytes).int == 0
                or reserved[:32] == b"\x00" * 32
            ):
                return None
        elif (
            first > last or dropped_chunks == 0 or dropped_points == 0
            or uuid.UUID(bytes=loss_id_bytes).int == 0
            or uuid.UUID(bytes=last_event_id_bytes).int == 0
            or reserved[:32] == b"\x00" * 32
        ):
            return None
        return EmergencyRecord(
            generation, state, uuid.UUID(bytes=loss_id_bytes),
            uuid.UUID(bytes=last_event_id_bytes), first, last,
            dropped_chunks, dropped_points, total_dropped_points,
            reason_mask, sector, reserved[:32], deferred_loss,
        )

    def _select_emergency(self) -> EmergencyRecord | None:
        records = [
            record
            for sector in range(RAW_SUPERBLOCKS, self.data_start_sector)
            if (record := self._decode_emergency(sector)) is not None
        ]
        selected: EmergencyRecord | None = None
        for record in records:
            if selected is not None and (
                (record.generation - selected.generation) & UINT64_MAX
            ) == (1 << 63):
                raise RuntimeError("emergency generations are half-range ambiguous")
            if selected is None or _serial_newer(record.generation, selected.generation):
                selected = record
            elif record.generation == selected.generation and record != selected:
                raise RuntimeError("conflicting emergency records share a generation")
        return selected

    def _load_from_flash(self) -> None:
        journal_records = self._journal_records()
        journal = self._select_newest(journal_records)
        journal_bytes_present = any(
            self.flash.read(sector * ERASE_BLOCK_BYTES, ERASE_BLOCK_BYTES)
            != b"\xff" * ERASE_BLOCK_BYTES
            for sector in range(RAW_SUPERBLOCKS)
        )
        self.journal_generation = -1 if journal is None else journal.generation
        self._journal_location = None if journal is None else (journal.sector, journal.record_index)
        self.reclaim_through = -1 if journal is None else journal.reclaim_through
        journal_next = 0 if journal is None else journal.next_outbox_sequence
        self.write_cursor = 0 if journal is None else journal.write_cursor
        self.erase_intent_block = None if journal is None else journal.erase_intent_block
        self.erase_intent_sequences = () if journal is None else journal.erase_intent_sequences
        self.dropped_points_total = 0 if journal is None else journal.dropped_points_total

        emergency = self._select_emergency()
        emergency_bytes_present = any(
            self.flash.read(sector * ERASE_BLOCK_BYTES, ERASE_BLOCK_BYTES)
            != b"\xff" * ERASE_BLOCK_BYTES
            for sector in range(RAW_SUPERBLOCKS, self.data_start_sector)
        )
        self.emergency = emergency
        self.emergency_generation = -1 if emergency is None else emergency.generation
        if emergency is not None:
            self.dropped_points_total = max(
                self.dropped_points_total, emergency.total_dropped_points
            )

        self.slots: dict[int, RawSlot] = {}
        identities: dict[ChunkIdentity, RawSlot] = {}
        self.incomplete_slot_indexes: set[int] = set()
        self.corrupt_slot_indexes: set[int] = set()
        self.acked_corrupt_slot_indexes: set[int] = set()
        max_slot: RawSlot | None = None
        for index in range(self.capacity_chunks):
            state, slot = self._decode_slot(index)
            if state == "incomplete":
                self.incomplete_slot_indexes.add(index)
            elif state == "corrupt":
                self.corrupt_slot_indexes.add(index)
            elif slot is not None:
                if state == "acked_corrupt":
                    self.acked_corrupt_slot_indexes.add(index)
                prior = self.slots.get(slot.outbox_sequence)
                if prior is not None and prior != slot:
                    raise RuntimeError("duplicate outbox sequence with different slot data")
                identity_prior = identities.get(slot.identity)
                if identity_prior is not None:
                    if identity_prior.digest != slot.digest:
                        raise RuntimeError("logical chunk identity reused with different content")
                    raise RuntimeError("logical chunk identity appears in multiple committed slots")
                self.slots[slot.outbox_sequence] = slot
                identities[slot.identity] = slot
                if max_slot is None or slot.outbox_sequence > max_slot.outbox_sequence:
                    max_slot = slot

        candidates = [journal_next]
        if max_slot is not None:
            candidates.append(max_slot.outbox_sequence + 1)
            if max_slot.outbox_sequence >= journal_next:
                self.write_cursor = (max_slot.slot_index + 1) % self.capacity_chunks
        if emergency is not None and (
            emergency.state != "empty" or emergency.loss_id.int != 0
        ):
            candidates.append(emergency.last_missing_outbox_sequence + 1)
        if emergency is not None and emergency.deferred_loss is not None:
            candidates.append(emergency.deferred_loss.last_missing_outbox_sequence + 1)
        next_candidate = max(candidates)
        self.outbox_exhausted = next_candidate >= UINT64_MAX
        self.next_outbox_sequence = min(next_candidate, UINT64_MAX)

        self.metadata_degraded = journal is None and journal_bytes_present
        # If every metadata record is unreadable, slot scanning can salvage
        # upload content but cannot prove historical ACK, loss, or reclaim
        # state.  Keep the image read-only until an explicit repair workflow.
        self.sequence_state_unknown = bool(
            self.metadata_degraded
            or (journal is None and bool(self.corrupt_slot_indexes))
        )
        self.loss_state_unknown = emergency is None and emergency_bytes_present
        if journal is not None:
            selected_emergency_generation = -1 if emergency is None else emergency.generation
            if (
                journal.emergency_generation >= 0
                and (
                    selected_emergency_generation < 0
                    or _serial_newer(
                        journal.emergency_generation,
                        selected_emergency_generation,
                    )
                )
            ):
                self.loss_state_unknown = True
            if (
                emergency is None
                and journal.dropped_points_total > 0
            ) or (
                emergency is not None
                and journal.dropped_points_total > emergency.total_dropped_points
            ):
                self.loss_state_unknown = True

        self.missing_outbox_sequences = set()
        loss_intervals: list[tuple[int, int]] = []
        if emergency is not None and (
            emergency.state != "empty" or emergency.loss_id.int != 0
        ):
            loss_intervals.append((
                emergency.first_missing_outbox_sequence,
                emergency.last_missing_outbox_sequence,
            ))
        if emergency is not None and emergency.deferred_loss is not None:
            loss_intervals.append((
                emergency.deferred_loss.first_missing_outbox_sequence,
                emergency.deferred_loss.last_missing_outbox_sequence,
            ))
        loss_intervals.sort()

        def add_uncovered(first: int, last: int) -> None:
            if first > last or self.sequence_state_unknown:
                return
            cursor = first
            for covered_first, covered_last in loss_intervals:
                if covered_last < cursor:
                    continue
                if covered_first > last:
                    break
                if covered_first > cursor:
                    count = covered_first - cursor
                    if len(self.missing_outbox_sequences) + count > self.capacity_chunks:
                        self.sequence_state_unknown = True
                        return
                    self.missing_outbox_sequences.update(range(cursor, covered_first))
                cursor = max(cursor, covered_last + 1)
                if cursor > last:
                    return
            if cursor <= last:
                count = last - cursor + 1
                if len(self.missing_outbox_sequences) + count > self.capacity_chunks:
                    self.sequence_state_unknown = True
                    return
                self.missing_outbox_sequences.update(range(cursor, last + 1))

        cursor = self.reclaim_through + 1
        for sequence in sorted(
            value for value in self.slots if self.reclaim_through < value < self.next_outbox_sequence
        ):
            add_uncovered(cursor, sequence - 1)
            cursor = sequence + 1
        add_uncovered(cursor, self.next_outbox_sequence - 1)

        if self.erase_intent_block is not None:
            decoded = [
                self._decode_slot(index)
                for index in range(
                    self.erase_intent_block * RAW_SLOTS_PER_BLOCK,
                    (self.erase_intent_block + 1) * RAW_SLOTS_PER_BLOCK,
                )
            ]
            valid_sequences = {
                slot.outbox_sequence for _, slot in decoded if slot is not None
            }
            if not valid_sequences.issubset(set(self.erase_intent_sequences)):
                # A valid record newer than the intent proves that journal
                # fallback resurrected a stale erase authorization.  Preserve
                # the refilled sector and require an explicit repair.
                self.metadata_degraded = True
                self.sequence_state_unknown = True

    def _record_is_erased(self, sector: int, index: int) -> bool:
        offset = sector * ERASE_BLOCK_BYTES + index * RAW_METADATA_RECORD_BYTES
        return self.flash.read(offset, RAW_METADATA_RECORD_BYTES) == b"\xff" * RAW_METADATA_RECORD_BYTES

    def _append_journal(self, *, cut_at: str | None = None, stage: str = "seal_metadata") -> bool:
        generation = 0 if self.journal_generation < 0 else (self.journal_generation + 1) & UINT64_MAX
        active_sector = None if self._journal_location is None else self._journal_location[0]
        target_sector = 0 if active_sector is None else active_sector
        target_index: int | None = None
        start = 0 if self._journal_location is None else self._journal_location[1] + 1
        for index in range(start, RAW_METADATA_RECORDS_PER_BLOCK):
            if self._record_is_erased(target_sector, index):
                target_index = index
                break
        rollover = target_index is None
        if rollover:
            target_sector = 1 - target_sector
            if self.flash.read(target_sector * ERASE_BLOCK_BYTES, ERASE_BLOCK_BYTES) != b"\xff" * ERASE_BLOCK_BYTES:
                if cut_at == "during_journal_erase":
                    self._erase(target_sector, cut_after=ERASE_BLOCK_BYTES // 2)
                    self._power_cut("journal_rollover")
                    return False
                self._erase(target_sector)
            target_index = 0

        record = self._encode_journal(generation)
        offset = target_sector * ERASE_BLOCK_BYTES + target_index * RAW_METADATA_RECORD_BYTES
        if cut_at in ("during_metadata", "during_journal_record"):
            self._program(offset, record[:120], cut_after=60)
            self._power_cut("journal_rollover" if rollover else stage)
            return False
        self._program(offset, record[:120])
        if cut_at == "during_journal_commit":
            self._program(offset + 120, COMMIT_MARKER, cut_after=4)
            self._power_cut("journal_rollover" if rollover else stage)
            return False
        self._program(offset + 120, COMMIT_MARKER)
        decoded = self._decode_journal(target_sector, target_index)
        if decoded is None or decoded.generation != generation:
            raise RuntimeError("journal readback failed")
        self._load_from_flash()
        return True

    def _write_emergency(self, record: EmergencyRecord, *, cut_at: str | None = None) -> bool:
        active_sector = None if self.emergency is None else self.emergency.sector
        target_sector = RAW_SUPERBLOCKS if active_sector is None else (
            RAW_SUPERBLOCKS + (1 - (active_sector - RAW_SUPERBLOCKS))
        )
        if self.flash.read(target_sector * ERASE_BLOCK_BYTES, ERASE_BLOCK_BYTES) != b"\xff" * ERASE_BLOCK_BYTES:
            if cut_at == "during_emergency_erase":
                self._erase(target_sector, cut_after=ERASE_BLOCK_BYTES // 2)
                self._power_cut("emergency")
                return False
            self._erase(target_sector)
        encoded = self._encode_emergency(record)
        offset = target_sector * ERASE_BLOCK_BYTES
        if cut_at == "during_emergency_record":
            self._program(offset, encoded[:248], cut_after=124)
            self._power_cut("emergency")
            return False
        self._program(offset, encoded[:248])
        if cut_at == "during_emergency_commit":
            self._program(offset + 248, COMMIT_MARKER, cut_after=4)
            self._power_cut("emergency")
            return False
        self._program(offset + 248, COMMIT_MARKER)
        self._load_from_flash()
        if cut_at == "after_emergency_commit":
            self._power_cut("emergency")
        return True

    def _find_identity(self, identity: ChunkIdentity) -> RawSlot | None:
        for slot in self.slots.values():
            if slot.identity == identity:
                return slot
        return None

    def _find_erased_slot(self) -> int | None:
        for step in range(self.capacity_chunks):
            index = (self.write_cursor + step) % self.capacity_chunks
            if self.erase_intent_block is not None and index // RAW_SLOTS_PER_BLOCK == self.erase_intent_block:
                continue
            state, _ = self._decode_slot(index)
            if state == "erased":
                self.write_cursor = (index + 1) % self.capacity_chunks
                return index
        return None

    def contains(
        self,
        sequence: int,
        digest: bytes | None = None,
        *,
        device_id: uuid.UUID = DEFAULT_DEVICE_ID,
        boot_sequence: int = 1,
    ) -> bool:
        slot = self._find_identity(ChunkIdentity(device_id, boot_sequence, sequence))
        return bool(
            slot is not None
            and slot.payload_valid
            and (digest is None or slot.digest == digest)
        )

    def unacknowledged(self) -> dict[int, bytes]:
        return {
            sequence: slot.digest
            for sequence, slot in self.slots.items()
            if not slot.acknowledged
        }

    def seal(
        self,
        sequence: int,
        digest: bytes,
        cut_at: str | None = None,
        *,
        device_id: uuid.UUID = DEFAULT_DEVICE_ID,
        boot_sequence: int = 1,
        first_point_sequence: int | None = None,
        point_count: int = MAX_POINTS_PER_CHUNK,
    ) -> bool:
        if self.sequence_state_unknown or self.loss_state_unknown:
            raise RuntimeError("flash metadata/loss state is unknown; model is read-only")
        identity = ChunkIdentity(device_id, boot_sequence, sequence)
        existing = self._find_identity(identity)
        if existing is not None:
            if existing.digest != digest:
                raise ValueError("same logical chunk identity was reused with different content")
            self.counters.idempotent_seals += 1
            return True
        if self.outbox_exhausted:
            raise OverflowError("outbox sequence space is exhausted")

        full_event_id = uuid.uuid5(
            uuid.NAMESPACE_OID,
            f"dog-rgb-full:{device_id}:{boot_sequence}:{sequence}:{digest.hex()}",
        )
        if (
            self.emergency is not None
            and self.emergency.state == "pending"
            and self.emergency.last_event_id == full_event_id
        ):
            fingerprint = _loss_event_fingerprint(
                full_event_id,
                self.emergency.last_missing_outbox_sequence,
                point_count,
                1,
            )
            if self.emergency.last_event_fingerprint == fingerprint:
                self.counters.idempotent_seals += 1
                return False
            raise ValueError("full-storage loss retry changed immutable content")

        index = self._find_erased_slot()
        if index is None:
            self.reclaim_acknowledged()
            index = self._find_erased_slot()
        if index is None:
            self.counters.coverage_gap_events += 1
            self.record_loss(
                dropped_points=point_count,
                reason_mask=1,
                event_id=full_event_id,
                missing_outbox_sequence=self.next_outbox_sequence,
            )
            return False

        first = sequence * MAX_POINTS_PER_CHUNK if first_point_sequence is None else first_point_sequence
        outbox_sequence = self.next_outbox_sequence
        encoded = self._encode_slot(
            outbox_sequence=outbox_sequence,
            identity=identity,
            first_point_sequence=first,
            point_count=point_count,
            digest=digest,
        )
        offset = self._slot_offset(index)
        body_length = len(encoded)
        if cut_at == "during_data":
            self._program(offset, encoded, cut_after=body_length // 2)
            self._power_cut("data")
            self.counters.rolled_back_incomplete_chunks += 1
            return False
        self._program(offset, encoded)
        if cut_at == "during_slot_commit":
            self._program(offset + 120, COMMIT_MARKER, cut_after=4)
            self._power_cut("slot_commit")
            self.counters.rolled_back_incomplete_chunks += 1
            return False
        self._program(offset + 120, COMMIT_MARKER)
        state, slot = self._decode_slot(index)
        if state != "valid" or slot is None or slot.digest != digest:
            raise RuntimeError("slot readback failed")
        self.counters.successful_new_seals += 1
        self.next_outbox_sequence = outbox_sequence + 1
        if cut_at == "after_data" or cut_at == "after_slot_commit":
            self._power_cut("after_slot_commit")
            self.counters.recovered_orphan_chunks += 1
            return self.contains(sequence, digest, device_id=device_id, boot_sequence=boot_sequence)
        if cut_at in ("during_metadata", "during_journal_record", "during_journal_commit", "during_journal_erase"):
            self._append_journal(cut_at=cut_at, stage="seal_metadata")
            return self.contains(sequence, digest, device_id=device_id, boot_sequence=boot_sequence)
        if not self._append_journal(stage="seal_metadata"):
            raise AssertionError("unexpected journal failure")
        return True

    def prepare_upload(self, outbox_sequences: Iterable[int] | None = None) -> tuple[RawSlot, ...]:
        selected = set(self.unacknowledged()) if outbox_sequences is None else set(outbox_sequences)
        records = tuple(
            self.slots[sequence]
            for sequence in sorted(selected)
            if sequence in self.slots and not self.slots[sequence].acknowledged
        )
        self.sent_outbox_sequences.update(slot.outbox_sequence for slot in records)
        return records

    def _contiguous_ack_prefix(self) -> int:
        sequence = self.reclaim_through + 1
        while sequence < self.next_outbox_sequence:
            slot = self.slots.get(sequence)
            if slot is not None and slot.acknowledged:
                sequence += 1
                continue
            if (
                slot is None
                and
                self.emergency is not None
                and (
                    self.emergency.state == "acknowledged"
                    or (
                        self.emergency.state == "empty"
                        and self.emergency.loss_id.int != 0
                    )
                )
                and self.emergency.first_missing_outbox_sequence <= sequence <= self.emergency.last_missing_outbox_sequence
            ):
                next_live = min(
                    (
                        candidate for candidate in self.slots
                        if sequence < candidate <= self.emergency.last_missing_outbox_sequence
                    ),
                    default=self.emergency.last_missing_outbox_sequence + 1,
                )
                sequence = next_live
                continue
            break
        return sequence - 1

    def _persist_ack_prefix(self, *, cut_at: str | None = None) -> bool:
        candidate = self._contiguous_ack_prefix()
        if candidate <= self.reclaim_through:
            return True
        previous = self.reclaim_through
        self.reclaim_through = candidate
        if not self._append_journal(cut_at=cut_at, stage="ack_metadata"):
            # Recovery has already restored the preceding committed generation.
            return False
        if self.reclaim_through < previous:
            raise AssertionError("reclaim watermark moved backwards")
        return True

    def acknowledge_exact(self, receipt: AckReceipt, *, cut_at: str | None = None) -> bool:
        if self.sequence_state_unknown or self.loss_state_unknown:
            raise RuntimeError("flash metadata/loss state is unknown; model is read-only")
        identity = ChunkIdentity(receipt.device_id, receipt.boot_sequence, receipt.chunk_sequence)
        slot = self._find_identity(identity)
        if slot is None:
            return False
        if (
            slot.digest != receipt.content_sha256
            or slot.point_count != receipt.accepted_point_count
            or slot.through_point_sequence != receipt.through_point_sequence
        ):
            return False
        if slot.acknowledged:
            return self._persist_ack_prefix(cut_at=cut_at)
        if slot.outbox_sequence not in self.sent_outbox_sequences:
            return False
        offset = self._slot_offset(slot.slot_index) + 112
        if cut_at == "during_ack_marker":
            self._program(offset, COMMIT_MARKER, cut_after=4)
            self._power_cut("ack_marker")
            return False
        self._program(offset, COMMIT_MARKER)
        self._load_from_flash()
        if cut_at in ("during_ack_metadata", "during_journal_record", "during_journal_commit", "during_journal_erase"):
            mapped = "during_metadata" if cut_at == "during_ack_metadata" else cut_at
            return self._persist_ack_prefix(cut_at=mapped)
        return self._persist_ack_prefix()

    def reclaim_acknowledged(
        self,
        *,
        cut_during_reclaim: bool = False,
        cut_at: str | None = None,
    ) -> int:
        if self.metadata_degraded or self.sequence_state_unknown or self.loss_state_unknown:
            return 0
        reclaimed = 0
        if self.erase_intent_block is not None:
            block = self.erase_intent_block
            indexes = range(block * RAW_SLOTS_PER_BLOCK, (block + 1) * RAW_SLOTS_PER_BLOCK)
            decoded = [self._decode_slot(index) for index in indexes]
            valid_sequences = {slot.outbox_sequence for _, slot in decoded if slot is not None}
            if not valid_sequences.issubset(set(self.erase_intent_sequences)):
                self.metadata_degraded = True
                self.sequence_state_unknown = True
                return 0
            if cut_at == "during_erase":
                self._erase(
                    self.data_start_sector + block,
                    cut_after=ERASE_BLOCK_BYTES * 3 // 4,
                )
                self._power_cut("reclaim")
                return 0
            self._erase(self.data_start_sector + block)
            if cut_at == "after_erase":
                self._power_cut("reclaim")
                return 0
            self.erase_intent_block = None
            self.erase_intent_sequences = ()
            if not self._append_journal(
                cut_at="during_metadata" if cut_at == "during_clear_metadata" else None,
                stage="ack_metadata",
            ):
                return 0
        for block in range(self.data_blocks):
            indexes = range(block * RAW_SLOTS_PER_BLOCK, (block + 1) * RAW_SLOTS_PER_BLOCK)
            decoded = [(index, *self._decode_slot(index)) for index in indexes]
            if all(state == "erased" for _, state, _ in decoded):
                continue
            if any(state == "corrupt" for _, state, _ in decoded):
                continue
            valid = [
                slot for _, state, slot in decoded
                if state in ("valid", "acked_corrupt") and slot is not None
            ]
            if any(not slot.acknowledged or slot.outbox_sequence > self.reclaim_through for slot in valid):
                continue
            sector = self.data_start_sector + block
            self.erase_intent_block = block
            self.erase_intent_sequences = tuple(slot.outbox_sequence for slot in valid)
            if not self._append_journal(
                cut_at="during_metadata" if cut_at == "during_intent_metadata" else None,
                stage="ack_metadata",
            ):
                return reclaimed
            if cut_during_reclaim or cut_at == "during_erase":
                self._erase(sector, cut_after=ERASE_BLOCK_BYTES * 3 // 4)
                self._power_cut("reclaim")
                return reclaimed
            self._erase(sector)
            reclaimed += len(valid)
            if cut_at == "after_erase":
                self._power_cut("reclaim")
                return reclaimed
            self.erase_intent_block = None
            self.erase_intent_sequences = ()
            if not self._append_journal(
                cut_at="during_metadata" if cut_at == "during_clear_metadata" else None,
                stage="ack_metadata",
            ):
                return reclaimed
            if cut_at is not None:
                # A named cut is injected only once per call.
                cut_at = None
        self._load_from_flash()
        return reclaimed

    def record_loss(
        self,
        *,
        missing_outbox_sequence: int,
        dropped_points: int,
        reason_mask: int,
        event_id: uuid.UUID | None = None,
        cut_at: str | None = None,
    ) -> bool:
        if self.sequence_state_unknown or self.loss_state_unknown:
            raise RuntimeError("flash metadata/loss state is unknown; model is read-only")
        if dropped_points <= 0 or reason_mask <= 0:
            raise ValueError("loss counters/reason must be positive")
        current = self.emergency if self.emergency and self.emergency.state == "pending" else None
        if self.emergency is not None and self.emergency.state == "acknowledged":
            acknowledged = self.emergency
            deferred = acknowledged.deferred_loss
            missing = missing_outbox_sequence
            if not 0 <= missing < UINT64_MAX:
                raise OverflowError("missing outbox sequence is outside the usable uint64 range")
            event_id = event_id or uuid.uuid5(
                uuid.NAMESPACE_OID,
                f"dog-rgb-loss-event:{missing}:{dropped_points}:{reason_mask}",
            )
            fingerprint = _loss_event_fingerprint(event_id, missing, dropped_points, reason_mask)
            if deferred is not None:
                if missing == deferred.last_missing_outbox_sequence:
                    if deferred.last_event_id == event_id and deferred.last_event_fingerprint == fingerprint:
                        return True
                    raise ValueError("loss-event ordinal was reused with different content")
                if missing < deferred.last_missing_outbox_sequence:
                    return False
                if missing != self.next_outbox_sequence:
                    return False
                deferred = DeferredLoss(
                    event_id,
                    deferred.first_missing_outbox_sequence,
                    missing,
                    deferred.dropped_chunks + 1,
                    deferred.dropped_points + dropped_points,
                    deferred.reason_mask | reason_mask,
                    fingerprint,
                )
            else:
                if missing != self.next_outbox_sequence:
                    return False
                deferred = DeferredLoss(
                    event_id, missing, missing, 1, dropped_points,
                    reason_mask, fingerprint,
                )
            generation = (acknowledged.generation + 1) & UINT64_MAX
            updated = EmergencyRecord(
                generation,
                acknowledged.state,
                acknowledged.loss_id,
                acknowledged.last_event_id,
                acknowledged.first_missing_outbox_sequence,
                acknowledged.last_missing_outbox_sequence,
                acknowledged.dropped_chunks,
                acknowledged.dropped_points,
                acknowledged.total_dropped_points + dropped_points,
                acknowledged.reason_mask,
                -1,
                acknowledged.last_event_fingerprint,
                deferred,
            )
            if not self._write_emergency(updated, cut_at=cut_at):
                return False
            self.next_outbox_sequence = missing + 1
            self.dropped_points_total = updated.total_dropped_points
            if cut_at == "after_emergency_commit":
                return True
            return self._append_journal(stage="seal_metadata")
        missing = missing_outbox_sequence
        if not 0 <= missing < UINT64_MAX:
            raise OverflowError("missing outbox sequence is outside the usable uint64 range")
        event_id = event_id or uuid.uuid5(
            uuid.NAMESPACE_OID,
            f"dog-rgb-loss-event:{missing}:{dropped_points}:{reason_mask}",
        )
        fingerprint = _loss_event_fingerprint(event_id, missing, dropped_points, reason_mask)
        if current is not None:
            if missing == current.last_missing_outbox_sequence:
                if current.last_event_id == event_id and current.last_event_fingerprint == fingerprint:
                    return True
                raise ValueError("loss-event ordinal was reused with different content")
            if missing < current.last_missing_outbox_sequence:
                # The bounded aggregate proves only its latest event.  Older
                # retries are rejected without mutation; the caller must
                # serialize local loss commits so this is never ambiguous.
                return False
            if missing != self.next_outbox_sequence:
                return False
        elif missing != self.next_outbox_sequence:
            return False
        if self.outbox_exhausted:
            raise OverflowError("outbox sequence space is exhausted")
        first = missing if current is None else current.first_missing_outbox_sequence
        last = missing
        loss_id = uuid.uuid5(uuid.NAMESPACE_OID, f"dog-rgb-loss:{first}") if current is None else current.loss_id
        generation = 0 if self.emergency_generation < 0 else (self.emergency_generation + 1) & UINT64_MAX
        record = EmergencyRecord(
            generation,
            "pending",
            loss_id,
            event_id,
            first,
            last,
            1 if current is None else current.dropped_chunks + 1,
            dropped_points if current is None else current.dropped_points + dropped_points,
            self.dropped_points_total + dropped_points,
            reason_mask if current is None else current.reason_mask | reason_mask,
            -1,
            fingerprint,
        )
        if not self._write_emergency(record, cut_at=cut_at):
            return False
        # A committed emergency record itself proves this ordinal was consumed;
        # recovery derives next_outbox even if the following journal tears.
        self.next_outbox_sequence = max(self.next_outbox_sequence, last + 1)
        self.dropped_points_total = record.total_dropped_points
        if cut_at == "after_emergency_commit":
            return True
        return self._append_journal(stage="seal_metadata")

    def prepare_loss_upload(self) -> EmergencyRecord | None:
        if self.emergency is None or self.emergency.state != "pending":
            return None
        self.sent_loss_id = self.emergency.loss_id
        self.sent_loss_generation = self.emergency.generation
        self.sent_loss_sha256 = self.emergency.record_sha256
        return self.emergency

    def acknowledge_loss(
        self,
        *,
        loss_id: uuid.UUID,
        generation: int,
        record_sha256: bytes,
        first_missing_outbox_sequence: int,
        last_missing_outbox_sequence: int,
        dropped_chunks: int,
        cut_at: str | None = None,
    ) -> bool:
        if self.sequence_state_unknown or self.loss_state_unknown:
            raise RuntimeError("flash metadata/loss state is unknown; model is read-only")
        current = self.emergency
        if current is not None and current.state == "empty":
            return bool(
                current.loss_id.int != 0
                and current.loss_id == loss_id
                and current.first_missing_outbox_sequence == first_missing_outbox_sequence
                and current.last_missing_outbox_sequence == last_missing_outbox_sequence
                and current.dropped_chunks == dropped_chunks
                and current.last_event_fingerprint == record_sha256
                and current.reason_mask == generation
            )
        if current is None or current.state not in ("pending", "acknowledged"):
            return False
        if (
            current.loss_id != loss_id
            or current.first_missing_outbox_sequence != first_missing_outbox_sequence
            or current.last_missing_outbox_sequence != last_missing_outbox_sequence
            or current.dropped_chunks != dropped_chunks
            or (
                current.state == "pending"
                and (
                    current.generation != generation
                    or current.record_sha256 != record_sha256
                )
            )
            or (
                current.state == "acknowledged"
                and (
                    current.reason_mask != generation
                    or current.last_event_fingerprint != record_sha256
                )
            )
        ):
            return False
        if current.state == "pending" and (
            self.sent_loss_id != loss_id
            or self.sent_loss_generation != current.generation
            or self.sent_loss_sha256 != current.record_sha256
        ):
            return False
        if current.state == "pending":
            acknowledged = EmergencyRecord(
                (current.generation + 1) & UINT64_MAX,
                "acknowledged",
                current.loss_id,
                current.last_event_id,
                current.first_missing_outbox_sequence,
                current.last_missing_outbox_sequence,
                current.dropped_chunks,
                current.dropped_points,
                current.total_dropped_points,
                current.generation,
                -1,
                current.record_sha256,
                current.deferred_loss,
            )
            if not self._write_emergency(acknowledged, cut_at=cut_at):
                return False
            if cut_at == "after_emergency_commit":
                return False
        accepted_generation = (
            generation if self.emergency.state == "acknowledged" else current.reason_mask
        )
        accepted_sha256 = (
            record_sha256
            if self.emergency.state == "acknowledged"
            else current.last_event_fingerprint
        )
        if not self._persist_ack_prefix(
            cut_at="during_metadata" if cut_at == "during_ack_metadata" else None
        ):
            return False
        current = self.emergency
        if current is None:
            return True
        # A sparse coalesced loss envelope may contain still-live chunks.  The
        # cloud ACK authorizes gaps only; retain the acknowledged loss record
        # until every live chunk inside the envelope is also durably ACKed and
        # the contiguous reclaim prefix reaches the envelope's end.
        if self.reclaim_through < current.last_missing_outbox_sequence:
            return True
        if current.deferred_loss is not None:
            deferred = current.deferred_loss
            promoted = EmergencyRecord(
                (current.generation + 1) & UINT64_MAX,
                "pending",
                uuid.uuid5(
                    uuid.NAMESPACE_OID,
                    f"dog-rgb-loss:{deferred.first_missing_outbox_sequence}",
                ),
                deferred.last_event_id,
                deferred.first_missing_outbox_sequence,
                deferred.last_missing_outbox_sequence,
                deferred.dropped_chunks,
                deferred.dropped_points,
                current.total_dropped_points,
                deferred.reason_mask,
                -1,
                deferred.last_event_fingerprint,
            )
            return self._write_emergency(
                promoted,
                cut_at=cut_at if cut_at and cut_at.startswith("during_emergency") else None,
            )
        empty = EmergencyRecord(
            (current.generation + 1) & UINT64_MAX,
            "empty",
            current.loss_id,
            current.last_event_id,
            current.first_missing_outbox_sequence,
            current.last_missing_outbox_sequence,
            current.dropped_chunks,
            current.dropped_points,
            self.dropped_points_total,
            accepted_generation,
            -1,
            accepted_sha256,
        )
        return self._write_emergency(empty, cut_at=cut_at if cut_at and cut_at.startswith("during_emergency") else None)

    def persist_missing_as_loss(self) -> bool:
        if self.sequence_state_unknown or self.loss_state_unknown:
            raise RuntimeError("flash metadata/loss state is unknown; model is read-only")
        if not self.missing_outbox_sequences:
            return True
        first = min(self.missing_outbox_sequences)
        last = max(self.missing_outbox_sequences)
        if self.emergency is not None and self.emergency.state != "empty":
            return False
        generation = 0 if self.emergency_generation < 0 else (self.emergency_generation + 1) & UINT64_MAX
        record = EmergencyRecord(
            generation,
            "pending",
            uuid.uuid5(uuid.NAMESPACE_OID, f"dog-rgb-corrupt:{first}:{last}"),
            uuid.uuid5(uuid.NAMESPACE_OID, f"dog-rgb-corrupt-event:{first}:{last}"),
            first,
            last,
            len(self.missing_outbox_sequences),
            len(self.missing_outbox_sequences) * MAX_POINTS_PER_CHUNK,
            self.dropped_points_total + len(self.missing_outbox_sequences) * MAX_POINTS_PER_CHUNK,
            2,
            -1,
            _loss_event_fingerprint(
                uuid.uuid5(uuid.NAMESPACE_OID, f"dog-rgb-corrupt-event:{first}:{last}"),
                last,
                len(self.missing_outbox_sequences) * MAX_POINTS_PER_CHUNK,
                2,
            ),
        )
        self.counters.coverage_gap_events += 1
        return self._write_emergency(record) and self._append_journal(stage="seal_metadata")

    def corrupt(self, outbox_sequence: int) -> None:
        slot = self.slots.get(outbox_sequence)
        if slot is None:
            raise KeyError(outbox_sequence)
        payload_offset = self._slot_offset(slot.slot_index) + RAW_SLOT_HEADER_BYTES
        payload_length = CHUNK_HEADER_SIZE + slot.point_count * POINT_SIZE
        payload = self.flash.read(payload_offset, payload_length)
        for relative, original in enumerate(payload):
            bit = next(
                (1 << shift for shift in range(8) if original & (1 << shift)),
                None,
            )
            if bit is not None:
                self._program(payload_offset + relative, bytes([original & ~bit]))
                break
        else:
            raise RuntimeError("no programmable corruption bit")
        self.counters.corruption_events += 1
        fresh = RawRingModel.from_flash(self.flash.snapshot(), data_blocks=self.data_blocks)
        self.flash = fresh.flash
        self._copy_runtime_from(fresh)
        self.persist_missing_as_loss()

    def metrics(self) -> dict[str, object]:
        recovery_ms = self.max_recovery_read_bytes / (ASSUMED_SEQUENTIAL_READ_MIB_S * 1024 * 1024) * 1000
        return {
            "capacity_chunks": self.capacity_chunks,
            "successful_new_seals": self.counters.successful_new_seals,
            "programmed_bytes": self.counters.programmed_bytes,
            "erased_bytes": self.counters.erased_bytes,
            "program_bytes_per_successful_seal": round(
                self.counters.programmed_bytes / max(1, self.counters.successful_new_seals), 3
            ),
            "erase_bytes_per_successful_seal": round(
                self.counters.erased_bytes / max(1, self.counters.successful_new_seals), 3
            ),
            "power_cuts": self.counters.power_cuts,
            "power_cut_breakdown": {
                "during_data": self.counters.power_cuts_during_data,
                "during_slot_commit": self.counters.power_cuts_during_slot_commit,
                "after_slot_commit": self.counters.power_cuts_after_data,
                "during_seal_metadata": self.counters.power_cuts_during_seal_metadata,
                "during_ack_marker": self.counters.power_cuts_during_ack_marker,
                "during_ack_metadata": self.counters.power_cuts_during_ack_metadata,
                "during_journal_rollover": self.counters.power_cuts_during_journal_rollover,
                "during_reclaim": self.counters.power_cuts_during_reclaim,
                "during_emergency": self.counters.power_cuts_during_emergency,
            },
            "recoveries": self.counters.recoveries,
            "recovered_orphan_chunks": self.counters.recovered_orphan_chunks,
            "rolled_back_incomplete_chunks": self.counters.rolled_back_incomplete_chunks,
            "corruption_events": self.counters.corruption_events,
            "coverage_gap_events": self.counters.coverage_gap_events,
            "data_sector_erase_wear": _wear_summary(self.data_erase_counts),
            "metadata_sector_erase_wear": _wear_summary(self.metadata_erase_counts),
            "emergency_sector_erase_wear": _wear_summary(self.emergency_erase_counts),
            "max_recovery_scan_bytes": self.max_recovery_read_bytes,
            "modeled_recovery_read_ms_at_20_MiB_s": round(recovery_ms, 3),
            "unacknowledged_after_run": len(self.unacknowledged()),
            "reclaim_through": self.reclaim_through,
            "next_outbox_sequence": self.next_outbox_sequence,
            "missing_outbox_sequences": len(self.missing_outbox_sequences),
            "pending_loss_record": bool(self.emergency and self.emergency.state != "empty"),
            "metadata_degraded": self.metadata_degraded,
            "sequence_state_unknown": self.sequence_state_unknown,
            "loss_state_unknown": self.loss_state_unknown,
        }


@dataclass
class LittleFsModel:
    """Idealized filesystem comparator; not raw-flash recovery evidence."""

    capacity_chunks: int = LITTLEFS_CAPACITY_CHUNKS
    counters: Counters = field(default_factory=Counters)

    def __post_init__(self) -> None:
        self.live: dict[int, bytes] = {}
        self.durable_ack = -1
        self.dynamic_blocks = TOTAL_BLOCKS - LITTLEFS_EMERGENCY_BLOCKS
        self.erase_counts = [0] * self.dynamic_blocks
        self._allocated_program_blocks = 0
        self._program_remainder = 0
        self.max_recovery_read_bytes = 0

    def _record_program(self, size: int) -> None:
        self.counters.programmed_bytes += size
        self.counters.program_operations += 1
        self._program_remainder += size
        while self._program_remainder >= ERASE_BLOCK_BYTES:
            self._program_remainder -= ERASE_BLOCK_BYTES
            allocation = self._allocated_program_blocks
            self._allocated_program_blocks += 1
            if allocation >= self.dynamic_blocks:
                block = allocation % self.dynamic_blocks
                self.erase_counts[block] += 1
                self.counters.erased_bytes += ERASE_BLOCK_BYTES
                self.counters.erase_operations += 1

    def contains(self, sequence: int, digest: bytes | None = None) -> bool:
        return sequence in self.live and (digest is None or self.live[sequence] == digest)

    def unacknowledged(self) -> dict[int, bytes]:
        return {seq: digest for seq, digest in self.live.items() if seq > self.durable_ack}

    def seal(self, sequence: int, digest: bytes, cut_at: str | None = None) -> bool:
        if sequence in self.live:
            if self.live[sequence] != digest:
                raise ValueError("same chunk sequence was reused with different content")
            self.counters.idempotent_seals += 1
            return True
        if len(self.live) >= self.capacity_chunks:
            self.reclaim_acknowledged()
        if len(self.live) >= self.capacity_chunks:
            self.counters.coverage_gap_events += 1
            return False
        program_bytes = math.ceil(CHUNK_BYTES / PROGRAM_PAGE_BYTES) * PROGRAM_PAGE_BYTES
        if cut_at == "during_data":
            self._record_program(program_bytes // 2)
            self.counters.power_cuts += 1
            self.counters.power_cuts_during_data += 1
            self.counters.rolled_back_incomplete_chunks += 1
            self.recover()
            return False
        self._record_program(program_bytes)
        if cut_at in ("after_data", "during_metadata"):
            self.counters.power_cuts += 1
            if cut_at == "after_data":
                self.counters.power_cuts_after_data += 1
            else:
                self._record_program(LITTLEFS_METADATA_COMMIT_BYTES // 2)
                self.counters.power_cuts_during_seal_metadata += 1
            self.counters.rolled_back_incomplete_chunks += 1
            self.recover()
            return False
        self._record_program(LITTLEFS_METADATA_COMMIT_BYTES)
        self.live[sequence] = digest
        self.counters.successful_new_seals += 1
        return True

    def acknowledge_through(self, sequence: int, *, cut_during_metadata: bool = False) -> bool:
        if sequence not in self.live:
            return False
        if cut_during_metadata:
            self._record_program(LITTLEFS_METADATA_COMMIT_BYTES // 2)
            self.counters.power_cuts += 1
            self.counters.power_cuts_during_ack_metadata += 1
            self.recover()
            return False
        self._record_program(LITTLEFS_METADATA_COMMIT_BYTES)
        self.durable_ack = max(self.durable_ack, sequence)
        return True

    def reclaim_acknowledged(self, *, cut_during_reclaim: bool = False) -> int:
        groups: dict[int, list[int]] = {}
        for sequence in self.live:
            groups.setdefault(sequence // LITTLEFS_SEGMENT_CHUNKS, []).append(sequence)
        reclaimed = 0
        for sequences in groups.values():
            if max(sequences) <= self.durable_ack:
                if cut_during_reclaim:
                    self._record_program(LITTLEFS_METADATA_COMMIT_BYTES // 2)
                    self.counters.power_cuts += 1
                    self.counters.power_cuts_during_reclaim += 1
                    self.recover()
                    return reclaimed
                for sequence in sequences:
                    del self.live[sequence]
                    reclaimed += 1
                self._record_program(LITTLEFS_METADATA_COMMIT_BYTES)
        return reclaimed

    def corrupt(self, sequence: int) -> None:
        if sequence not in self.live:
            raise KeyError(sequence)
        del self.live[sequence]
        self.counters.corruption_events += 1
        self.counters.coverage_gap_events += 1

    def recover(self) -> None:
        self.counters.recoveries += 1
        segment_count = math.ceil(len(self.live) / LITTLEFS_SEGMENT_CHUNKS)
        scan_bytes = 2 * ERASE_BLOCK_BYTES + segment_count * PROGRAM_PAGE_BYTES + ERASE_BLOCK_BYTES
        self.counters.read_bytes += scan_bytes
        self.max_recovery_read_bytes = max(self.max_recovery_read_bytes, scan_bytes)

    def metrics(self) -> dict[str, object]:
        recovery_ms = self.max_recovery_read_bytes / (ASSUMED_SEQUENTIAL_READ_MIB_S * 1024 * 1024) * 1000
        return {
            "capacity_chunks": self.capacity_chunks,
            "successful_new_seals": self.counters.successful_new_seals,
            "programmed_bytes": self.counters.programmed_bytes,
            "erased_bytes": self.counters.erased_bytes,
            "program_bytes_per_successful_seal": round(
                self.counters.programmed_bytes / max(1, self.counters.successful_new_seals), 3
            ),
            "erase_bytes_per_successful_seal": round(
                self.counters.erased_bytes / max(1, self.counters.successful_new_seals), 3
            ),
            "power_cuts": self.counters.power_cuts,
            "recoveries": self.counters.recoveries,
            "recovered_orphan_chunks": self.counters.recovered_orphan_chunks,
            "rolled_back_incomplete_chunks": self.counters.rolled_back_incomplete_chunks,
            "corruption_events": self.counters.corruption_events,
            "coverage_gap_events": self.counters.coverage_gap_events,
            "dynamic_sector_erase_wear": _wear_summary(self.erase_counts),
            "max_recovery_scan_bytes": self.max_recovery_read_bytes,
            "modeled_recovery_read_ms_at_20_MiB_s": round(recovery_ms, 3),
            "unacknowledged_after_run": len(self.unacknowledged()),
        }


def retention_rows(capacity_chunks: int) -> list[dict[str, int | float | str]]:
    capacity_points = capacity_chunks * MAX_POINTS_PER_CHUNK
    profiles = (
        ("1_second", 86_400),
        ("5_second", 17_280),
        ("15_second", 5_760),
        ("60_second", 1_440),
        ("adaptive_4h_moving_20h_stationary", 4_080),
    )
    return [
        {
            "profile": name,
            "points_per_day": per_day,
            "capacity_points": capacity_points,
            "retention_days": round(capacity_points / per_day, 3),
            "retention_hours": round(capacity_points / per_day * 24, 3),
        }
        for name, per_day in profiles
    ]


def _ack_raw_through(model: RawRingModel, logical_chunk_sequence: int, *, cut: bool = False) -> None:
    selected = [
        slot.outbox_sequence
        for slot in model.slots.values()
        if slot.identity.boot_sequence == 1
        and slot.identity.chunk_sequence <= logical_chunk_sequence
        and not slot.acknowledged
    ]
    records = model.prepare_upload(selected)
    for index, slot in enumerate(records):
        inject = cut and index == len(records) - 1
        if not model.acknowledge_exact(slot.receipt(), cut_at="during_ack_metadata" if inject else None):
            retry = model.prepare_upload([slot.outbox_sequence])
            if retry and not model.acknowledge_exact(retry[0].receipt()):
                raise AssertionError("exact ACK retry failed")
            if not retry and slot.outbox_sequence in model.slots and model.slots[slot.outbox_sequence].acknowledged:
                if not model._persist_ack_prefix():
                    raise AssertionError("ACK-prefix repair failed")


def run_random_power_cut_workload(
    factory: Callable[[], RawRingModel | LittleFsModel],
    *,
    cycles: int = 10_000,
    seed: int = 0xD06,
) -> RawRingModel | LittleFsModel:
    model = factory()
    seal_rng = random.Random(seed)
    ack_rng = random.Random(seed ^ 0xA11)
    reclaim_rng = random.Random(seed ^ 0xEC1A)
    raw_cut_stages = ("during_data", "during_slot_commit", "after_slot_commit", "during_metadata")
    fs_cut_stages = ("during_data", "after_data", "during_metadata")

    for sequence in range(cycles):
        digest = _digest(sequence)
        stages = raw_cut_stages if isinstance(model, RawRingModel) else fs_cut_stages
        cut_at = stages[seal_rng.randrange(len(stages))] if seal_rng.randrange(50) == 0 else None
        committed = model.seal(sequence, digest, cut_at=cut_at)
        if not committed:
            committed = model.seal(sequence, digest)
        if not committed or not model.contains(sequence, digest):
            raise AssertionError(f"chunk {sequence} was not durable before ACK")

        if sequence >= 7 and sequence % 8 == 7:
            ack_target = sequence - 3
            ack_cut = ack_rng.randrange(100) == 0
            if isinstance(model, RawRingModel):
                _ack_raw_through(model, ack_target, cut=ack_cut)
            else:
                if not model.acknowledge_through(ack_target, cut_during_metadata=ack_cut):
                    model.acknowledge_through(ack_target)
            reclaim_cut = reclaim_rng.randrange(200) == 0
            model.reclaim_acknowledged(cut_during_reclaim=reclaim_cut)
            if reclaim_cut:
                model.reclaim_acknowledged()

    if isinstance(model, RawRingModel):
        _ack_raw_through(model, cycles - 1)
    elif not model.acknowledge_through(cycles - 1):
        raise AssertionError("final ACK failed")
    model.reclaim_acknowledged()
    if model.unacknowledged():
        raise AssertionError("workload ended with unexpected unacknowledged chunks")
    return model


def run_fill_reclaim(factory: Callable[[], RawRingModel | LittleFsModel]) -> dict[str, int | bool]:
    model = factory()
    capacity = model.capacity_chunks
    for sequence in range(capacity):
        if not model.seal(sequence, _digest(sequence)):
            raise AssertionError("model failed before its declared capacity")
    rejected_when_full = not model.seal(capacity, _digest(capacity))

    ack_target = capacity // 2 - 1
    if isinstance(model, RawRingModel):
        _ack_raw_through(model, ack_target)
    else:
        model.acknowledge_through(ack_target)
    reclaimed = model.reclaim_acknowledged()
    unacked_before_refill = set(model.unacknowledged())
    refill_count = 0
    next_sequence = capacity + 1
    while model.seal(next_sequence, _digest(next_sequence)):
        refill_count += 1
        next_sequence += 1
        if refill_count > capacity:
            raise AssertionError("refill exceeded bounded capacity")
    unacked_preserved = unacked_before_refill.issubset(model.unacknowledged())
    return {
        "declared_capacity_chunks": capacity,
        "rejected_when_full": rejected_when_full,
        "reclaimed_chunks": reclaimed,
        "refilled_chunks": refill_count,
        "preexisting_unacknowledged_preserved": unacked_preserved,
    }


def comparison_evidence() -> dict[str, object]:
    raw_workload = run_random_power_cut_workload(RawRingModel)
    littlefs_workload = run_random_power_cut_workload(LittleFsModel)
    return {
        "model_schema": "dog-rgb-phase0-storage-model/2",
        "determinism": {"seed": 0xD06, "seal_ack_reclaim_cycles": 10_000},
        "geometry": {
            "partition_bytes": PARTITION_BYTES,
            "partition_hex": "0x150000",
            "erase_block_bytes": ERASE_BLOCK_BYTES,
            "program_page_assumption_bytes": PROGRAM_PAGE_BYTES,
            "total_erase_blocks": TOTAL_BLOCKS,
            "track_v3_point_bytes": POINT_SIZE,
            "points_per_chunk": MAX_POINTS_PER_CHUNK,
            "encoded_full_chunk_bytes": CHUNK_BYTES,
            "raw_slot_envelope_bytes": RAW_SLOT_HEADER_BYTES,
        },
        "raw_ring": {
            "layout": {
                "metadata_superblocks": RAW_SUPERBLOCKS,
                "emergency_blocks": RAW_EMERGENCY_BLOCKS,
                "data_blocks": RAW_DATA_BLOCKS,
                "slot_bytes": RAW_SLOT_BYTES,
                "slots_per_block": RAW_SLOTS_PER_BLOCK,
            },
            "retention": retention_rows(RAW_CAPACITY_CHUNKS),
            "fill_reclaim": run_fill_reclaim(RawRingModel),
            "workload": raw_workload.metrics(),
        },
        "littlefs_segment_log": {
            "layout_assumptions": {
                "emergency_blocks": LITTLEFS_EMERGENCY_BLOCKS,
                "metadata_blocks": LITTLEFS_METADATA_BLOCKS,
                "operational_reserve_blocks": LITTLEFS_OPERATIONAL_RESERVE_BLOCKS,
                "segment_chunks": LITTLEFS_SEGMENT_CHUNKS,
                "segment_blocks": LITTLEFS_SEGMENT_BLOCKS,
                "metadata_commit_program_bytes": LITTLEFS_METADATA_COMMIT_BYTES,
                "wear_leveling": "idealized dynamic round-robin; not an implementation measurement",
            },
            "retention": retention_rows(LITTLEFS_CAPACITY_CHUNKS),
            "fill_reclaim": run_fill_reclaim(LittleFsModel),
            "workload": littlefs_workload.metrics(),
        },
        "interpretation_limits": [
            "The raw result is a byte/NOR invariant model, not an ESP32 timing benchmark.",
            "LittleFS program and metadata costs are declared assumptions, not traced library writes.",
            "The 20 MiB/s recovery figure excludes hashing and driver latency.",
            "Physical 10,000-cycle and randomized power-removal evidence remains a release gate.",
        ],
    }
