"""Legacy Track v2 conversion/export contract for the Phase 0B prototype."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import date, datetime, timedelta, timezone
import uuid

from track_v3 import (
    MAX_POINTS_PER_CHUNK,
    PointFlag,
    SPEED_UNAVAILABLE,
    TimeQuality,
    TrackChunkV3,
    TrackPointV3,
)


LEGACY_BOOT_SEQUENCE = 0
LEGACY_SLOT_COUNT = 4
LEGACY_SLOT_POINT_SEQUENCE_SPAN = 2048
LEGACY_SLOT_CHUNK_SEQUENCE_SPAN = 32


@dataclass(frozen=True)
class LegacyPointV2:
    lat_e7: int
    lon_e7: int
    t_min: int


def _parse_yyyymmdd(value: int) -> date:
    text = f"{value:08d}"
    try:
        return date(int(text[0:4]), int(text[4:6]), int(text[6:8]))
    except ValueError as exc:
        raise ValueError("invalid legacy start date") from exc


def convert_v2_points(
    *,
    device_id: uuid.UUID,
    slot: int,
    start_date_yyyymmdd: int,
    points: tuple[LegacyPointV2, ...],
) -> tuple[TrackChunkV3, ...]:
    """Convert a frozen v2 slot snapshot without inventing speed or behavior.

    Native v3 boot sequences start at 1. Sequence 0 is reserved for this one
    upgrade snapshot, making retry identities stable for all four legacy slots.
    A large backwards minute jump is treated as UTC midnight rollover; a small
    backwards jump is rejected as corrupt ordering.
    """

    if not 0 <= slot < LEGACY_SLOT_COUNT:
        raise ValueError("legacy slot must be in [0, 3]")
    if len(points) > LEGACY_SLOT_POINT_SEQUENCE_SPAN:
        raise ValueError("legacy slot exceeds reserved point sequence span")
    current_date = _parse_yyyymmdd(start_date_yyyymmdd)
    last_minute: int | None = None
    converted: list[TrackPointV3] = []
    flags = int(PointFlag.FIX_VALID | PointFlag.TIME_TRUSTED | PointFlag.LEGACY_V2)

    for point in points:
        if not -900_000_000 <= point.lat_e7 <= 900_000_000:
            raise ValueError("legacy latitude is invalid")
        if not -1_800_000_000 <= point.lon_e7 <= 1_800_000_000:
            raise ValueError("legacy longitude is invalid")
        if not 0 <= point.t_min < 1440:
            raise ValueError("legacy minute is invalid")
        if last_minute is not None and point.t_min < last_minute:
            if last_minute - point.t_min > 720:
                current_date += timedelta(days=1)
            else:
                raise ValueError("legacy minutes moved backwards without midnight rollover")
        instant = datetime.combine(current_date, datetime.min.time(), tzinfo=timezone.utc)
        utc_s = int(instant.timestamp()) + point.t_min * 60
        converted.append(
            TrackPointV3(
                lat_e7=point.lat_e7,
                lon_e7=point.lon_e7,
                utc_s=utc_s,
                speed_cmps=SPEED_UNAVAILABLE,
                satellites=0,
                flags=flags,
            )
        )
        last_minute = point.t_min

    chunks: list[TrackChunkV3] = []
    for group_index, offset in enumerate(range(0, len(converted), MAX_POINTS_PER_CHUNK)):
        group = tuple(converted[offset : offset + MAX_POINTS_PER_CHUNK])
        chunks.append(
            TrackChunkV3(
                device_id=device_id,
                boot_sequence=LEGACY_BOOT_SEQUENCE,
                chunk_sequence=slot * LEGACY_SLOT_CHUNK_SEQUENCE_SPAN + group_index,
                first_point_sequence=slot * LEGACY_SLOT_POINT_SEQUENCE_SPAN + offset,
                time_quality=TimeQuality.LEGACY_MINUTE,
                final_for_recording=(offset + len(group) == len(converted)),
                points=group,
            )
        )
    return tuple(chunks)


def legacy_export_row(point: TrackPointV3) -> dict[str, object]:
    if not int(point.flags) & PointFlag.LEGACY_V2:
        raise ValueError("point is not a legacy conversion")
    return {
        "lat_e7": point.lat_e7,
        "lon_e7": point.lon_e7,
        "utc_s": point.utc_s,
        "time_precision": "minute",
        "speed_cmps": None,
        "satellites": None,
        "movement_state": "unknown",
        "stationary_state": "unknown",
        "legacy_v2": True,
    }

