"""Frozen Track v3 codec used by the Phase 0B feasibility prototype.

The codec deliberately uses explicit little-endian ``struct`` formats. It does
not serialize Python or C/C++ object memory. Every reserved bit/byte is checked
so a future format change needs a new header/schema version.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum, IntFlag
import hashlib
import struct
import uuid
import zlib
from typing import Iterable, Sequence


POINT_SCHEMA_VERSION = 3
POINT_SIZE = 16
MAX_POINTS_PER_CHUNK = 96
SPEED_UNAVAILABLE = 0xFFFF

POINT_STRUCT = struct.Struct("<iiIHBB")
CHUNK_HEADER_STRUCT = struct.Struct("<4sBBH16sIIIHBBIIII32sI")
CHUNK_MAGIC = b"D3CK"
CHUNK_HEADER_VERSION = 1
CHUNK_HEADER_SIZE = CHUNK_HEADER_STRUCT.size


class PointFlag(IntFlag):
    FIX_VALID = 0x01
    MOVEMENT_EVIDENCE = 0x02
    TIME_TRUSTED = 0x04
    STATIONARY_HEARTBEAT = 0x08
    LOW_QUALITY = 0x10
    GAP = 0x20
    LEGACY_V2 = 0x40


KNOWN_POINT_FLAGS = int(
    PointFlag.FIX_VALID
    | PointFlag.MOVEMENT_EVIDENCE
    | PointFlag.TIME_TRUSTED
    | PointFlag.STATIONARY_HEARTBEAT
    | PointFlag.LOW_QUALITY
    | PointFlag.GAP
    | PointFlag.LEGACY_V2
)


class ChunkFlag(IntFlag):
    FINAL_FOR_RECORDING = 0x0001


KNOWN_CHUNK_FLAGS = int(ChunkFlag.FINAL_FOR_RECORDING)


class TimeQuality(IntEnum):
    UNKNOWN = 0
    APPROXIMATE_PERSISTED = 1
    SERVER_ANCHORED = 2
    SNTP_SYNCED = 3
    GNSS_TRUSTED = 4
    LEGACY_MINUTE = 5


@dataclass(frozen=True)
class TrackPointV3:
    lat_e7: int
    lon_e7: int
    utc_s: int
    speed_cmps: int
    satellites: int
    flags: int


@dataclass(frozen=True)
class TrackChunkV3:
    device_id: uuid.UUID
    boot_sequence: int
    chunk_sequence: int
    first_point_sequence: int
    time_quality: TimeQuality
    final_for_recording: bool
    points: tuple[TrackPointV3, ...]


@dataclass(frozen=True)
class DecodedChunk:
    chunk: TrackChunkV3
    payload_crc32: int
    payload_sha256: bytes


def _require_u32(name: str, value: int) -> None:
    if not 0 <= value <= 0xFFFFFFFF:
        raise ValueError(f"{name} must fit uint32")


def validate_point(point: TrackPointV3) -> None:
    flags = int(point.flags)
    if flags & ~KNOWN_POINT_FLAGS:
        raise ValueError("point has an unknown/reserved flag")
    if not 0 <= point.utc_s <= 0xFFFFFFFF:
        raise ValueError("utc_s must fit uint32")
    if not 0 <= point.speed_cmps <= 0xFFFF:
        raise ValueError("speed_cmps must fit uint16")
    if not 0 <= point.satellites <= 0xFF:
        raise ValueError("satellites must fit uint8")

    has_fix = bool(flags & PointFlag.FIX_VALID)
    is_gap = bool(flags & PointFlag.GAP)
    moving = bool(flags & PointFlag.MOVEMENT_EVIDENCE)
    stationary = bool(flags & PointFlag.STATIONARY_HEARTBEAT)
    trusted_time = bool(flags & PointFlag.TIME_TRUSTED)

    if moving and stationary:
        raise ValueError("movement and stationary flags are mutually exclusive")
    if is_gap and has_fix:
        raise ValueError("a gap marker cannot contain a valid fix")
    if is_gap and (moving or stationary):
        raise ValueError("a gap marker cannot claim movement or stationary evidence")
    if trusted_time != (point.utc_s != 0):
        raise ValueError("TIME_TRUSTED must exactly match a non-zero utc_s")

    if has_fix:
        if not -900_000_000 <= point.lat_e7 <= 900_000_000:
            raise ValueError("latitude is outside [-90, 90]")
        if not -1_800_000_000 <= point.lon_e7 <= 1_800_000_000:
            raise ValueError("longitude is outside [-180, 180]")
    elif point.lat_e7 != 0 or point.lon_e7 != 0:
        raise ValueError("coordinates must be zero without FIX_VALID")

    if not has_fix and point.speed_cmps != SPEED_UNAVAILABLE:
        raise ValueError("speed must be unavailable without a valid fix")


def encode_point(point: TrackPointV3) -> bytes:
    validate_point(point)
    encoded = POINT_STRUCT.pack(
        point.lat_e7,
        point.lon_e7,
        point.utc_s,
        point.speed_cmps,
        point.satellites,
        int(point.flags),
    )
    if len(encoded) != POINT_SIZE:  # defensive contract assertion
        raise AssertionError("Track v3 point encoding changed size")
    return encoded


def decode_point(data: bytes) -> TrackPointV3:
    if len(data) != POINT_SIZE:
        raise ValueError(f"point must be exactly {POINT_SIZE} bytes")
    point = TrackPointV3(*POINT_STRUCT.unpack(data))
    validate_point(point)
    return point


def _payload_for(points: Iterable[TrackPointV3]) -> bytes:
    return b"".join(encode_point(point) for point in points)


def _time_bounds(points: Sequence[TrackPointV3]) -> tuple[int, int]:
    timestamps = [point.utc_s for point in points if point.utc_s]
    if not timestamps:
        return 0, 0
    if timestamps != sorted(timestamps):
        raise ValueError("point timestamps must be monotonic within a chunk")
    return timestamps[0], timestamps[-1]


def _validate_chunk_semantics(
    *,
    device_id: uuid.UUID,
    boot_sequence: int,
    time_quality: TimeQuality,
    points: Sequence[TrackPointV3],
) -> None:
    if device_id.int == 0:
        raise ValueError("device_id cannot be the nil UUID")
    timestamped = [point.utc_s != 0 for point in points]
    if time_quality == TimeQuality.UNKNOWN:
        if any(timestamped):
            raise ValueError("UNKNOWN time quality cannot have timestamped points")
    elif not all(timestamped):
        raise ValueError("known time quality requires every point to have time")

    legacy = [bool(int(point.flags) & PointFlag.LEGACY_V2) for point in points]
    if boot_sequence == 0:
        if time_quality != TimeQuality.LEGACY_MINUTE or not all(legacy):
            raise ValueError("boot sequence 0 is reserved for legacy-v2 conversion")
    elif any(legacy):
        raise ValueError("legacy-v2 points require reserved boot sequence 0")
    if time_quality == TimeQuality.LEGACY_MINUTE and not all(legacy):
        raise ValueError("legacy-minute time quality requires legacy-v2 points")
    if time_quality != TimeQuality.LEGACY_MINUTE and any(legacy):
        raise ValueError("legacy-v2 points require legacy-minute time quality")
    _time_bounds(points)


def encode_chunk(chunk: TrackChunkV3) -> bytes:
    if not 1 <= len(chunk.points) <= MAX_POINTS_PER_CHUNK:
        raise ValueError(f"point count must be in [1, {MAX_POINTS_PER_CHUNK}]")
    _require_u32("boot_sequence", chunk.boot_sequence)
    _require_u32("chunk_sequence", chunk.chunk_sequence)
    _require_u32("first_point_sequence", chunk.first_point_sequence)
    if chunk.first_point_sequence + len(chunk.points) - 1 > 0xFFFFFFFF:
        raise ValueError("point sequence range exceeds uint32")
    try:
        time_quality = TimeQuality(chunk.time_quality)
    except ValueError as exc:
        raise ValueError("unknown time quality") from exc
    _validate_chunk_semantics(
        device_id=chunk.device_id,
        boot_sequence=chunk.boot_sequence,
        time_quality=time_quality,
        points=chunk.points,
    )

    payload = _payload_for(chunk.points)
    payload_crc32 = zlib.crc32(payload) & 0xFFFFFFFF
    payload_sha256 = hashlib.sha256(payload).digest()
    start_utc_s, end_utc_s = _time_bounds(chunk.points)
    flags = int(ChunkFlag.FINAL_FOR_RECORDING) if chunk.final_for_recording else 0

    values = (
        CHUNK_MAGIC,
        POINT_SCHEMA_VERSION,
        CHUNK_HEADER_VERSION,
        flags,
        chunk.device_id.bytes,
        chunk.boot_sequence,
        chunk.chunk_sequence,
        chunk.first_point_sequence,
        len(chunk.points),
        int(time_quality),
        0,  # reserved
        start_utc_s,
        end_utc_s,
        len(payload),
        payload_crc32,
        payload_sha256,
        0,  # header CRC while calculating it
    )
    header_without_crc = CHUNK_HEADER_STRUCT.pack(*values)
    header_crc32 = zlib.crc32(header_without_crc) & 0xFFFFFFFF
    header = CHUNK_HEADER_STRUCT.pack(*values[:-1], header_crc32)
    return header + payload


def decode_chunk(data: bytes) -> DecodedChunk:
    if len(data) < CHUNK_HEADER_SIZE + POINT_SIZE:
        raise ValueError("chunk is shorter than header plus one point")
    values = CHUNK_HEADER_STRUCT.unpack_from(data)
    (
        magic,
        schema_version,
        header_version,
        flags,
        device_bytes,
        boot_sequence,
        chunk_sequence,
        first_point_sequence,
        point_count,
        time_quality_raw,
        reserved,
        start_utc_s,
        end_utc_s,
        payload_length,
        stored_payload_crc32,
        stored_payload_sha256,
        stored_header_crc32,
    ) = values

    if magic != CHUNK_MAGIC:
        raise ValueError("wrong chunk magic")
    if schema_version != POINT_SCHEMA_VERSION or header_version != CHUNK_HEADER_VERSION:
        raise ValueError("unsupported chunk version")
    if flags & ~KNOWN_CHUNK_FLAGS:
        raise ValueError("chunk has an unknown/reserved flag")
    if reserved != 0:
        raise ValueError("chunk reserved byte must be zero")
    if not 1 <= point_count <= MAX_POINTS_PER_CHUNK:
        raise ValueError("invalid point count")
    if payload_length != point_count * POINT_SIZE:
        raise ValueError("payload length does not match point count")
    if len(data) != CHUNK_HEADER_SIZE + payload_length:
        raise ValueError("chunk has truncated or trailing bytes")

    header_zero_crc = CHUNK_HEADER_STRUCT.pack(*values[:-1], 0)
    if zlib.crc32(header_zero_crc) & 0xFFFFFFFF != stored_header_crc32:
        raise ValueError("header CRC mismatch")

    payload = data[CHUNK_HEADER_SIZE:]
    if zlib.crc32(payload) & 0xFFFFFFFF != stored_payload_crc32:
        raise ValueError("payload CRC mismatch")
    if hashlib.sha256(payload).digest() != stored_payload_sha256:
        raise ValueError("payload SHA-256 mismatch")

    points = tuple(
        decode_point(payload[offset : offset + POINT_SIZE])
        for offset in range(0, payload_length, POINT_SIZE)
    )
    calculated_start, calculated_end = _time_bounds(points)
    if (start_utc_s, end_utc_s) != (calculated_start, calculated_end):
        raise ValueError("chunk time bounds do not match payload")

    try:
        time_quality = TimeQuality(time_quality_raw)
    except ValueError as exc:
        raise ValueError("unknown time quality") from exc
    _validate_chunk_semantics(
        device_id=uuid.UUID(bytes=device_bytes),
        boot_sequence=boot_sequence,
        time_quality=time_quality,
        points=points,
    )

    chunk = TrackChunkV3(
        device_id=uuid.UUID(bytes=device_bytes),
        boot_sequence=boot_sequence,
        chunk_sequence=chunk_sequence,
        first_point_sequence=first_point_sequence,
        time_quality=time_quality,
        final_for_recording=bool(flags & ChunkFlag.FINAL_FOR_RECORDING),
        points=points,
    )
    return DecodedChunk(
        chunk=chunk,
        payload_crc32=stored_payload_crc32,
        payload_sha256=stored_payload_sha256,
    )


def chunk_identity(chunk: TrackChunkV3) -> tuple[uuid.UUID, int, int]:
    """Return the stable local/cloud identity, excluding content hashes."""

    return chunk.device_id, chunk.boot_sequence, chunk.chunk_sequence
