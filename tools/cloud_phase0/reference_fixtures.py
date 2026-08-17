"""Deterministic synthetic Track v3 reference datasets.

Names such as ``walking_kinematic`` describe only generated speed/cadence
profiles. They are not labels of dog behavior, health, intent, or wear state.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import uuid

from track_v3 import (
    PointFlag,
    SPEED_UNAVAILABLE,
    TimeQuality,
    TrackChunkV3,
    TrackPointV3,
    encode_chunk,
)


FIXTURE_DEVICE_ID = uuid.UUID("6b2f8f64-7dd8-4d42-a9a4-2452e8240003")
BASE_UTC_S = 1_787_000_000
BASE_LAT_E7 = 46_500_000
BASE_LON_E7 = -740_600_000


@dataclass(frozen=True)
class ReferenceFixture:
    fixture_id: str
    display_profile: str
    truth_label: str
    caveat: str
    chunk: TrackChunkV3


def _point(
    lat_e7: int,
    lon_e7: int,
    utc_s: int,
    speed_cmps: int,
    satellites: int,
    flags: PointFlag,
) -> TrackPointV3:
    return TrackPointV3(lat_e7, lon_e7, utc_s, speed_cmps, satellites, int(flags))


def _stationary_fixture() -> ReferenceFixture:
    # Bounded +/-0.55 m coordinate perturbations imitate accepted GNSS jitter.
    offsets = ((0, 0), (2, -3), (-4, 1), (3, 4), (-2, -5), (1, 3), (0, -2), (-3, 2))
    flags = PointFlag.FIX_VALID | PointFlag.TIME_TRUSTED | PointFlag.STATIONARY_HEARTBEAT
    points = tuple(
        _point(BASE_LAT_E7 + dlat, BASE_LON_E7 + dlon, BASE_UTC_S + i * 60, 0, 10, flags)
        for i, (dlat, dlon) in enumerate(offsets)
    )
    return ReferenceFixture(
        fixture_id="stationary_observation",
        display_profile="stationary",
        truth_label="synthetic_trusted_stationary_observations",
        caveat="Stationary is an observation-state fixture, not proof of rest, sleep, or collar wear.",
        chunk=TrackChunkV3(FIXTURE_DEVICE_ID, 11, 1, 1, TimeQuality.GNSS_TRUSTED, True, points),
    )


def _movement_fixture(*, running_profile: bool) -> ReferenceFixture:
    # Eastward kinematics: 5 s samples at nominal 1.4 or 4.5 m/s.
    speed_cmps = 450 if running_profile else 140
    lon_step_e7 = 2025 if running_profile else 630
    count = 12
    flags = PointFlag.FIX_VALID | PointFlag.TIME_TRUSTED | PointFlag.MOVEMENT_EVIDENCE
    points = tuple(
        _point(
            BASE_LAT_E7 + (i % 3) - 1,
            BASE_LON_E7 + i * lon_step_e7,
            BASE_UTC_S + i * 5,
            speed_cmps,
            11,
            flags,
        )
        for i in range(count)
    )
    name = "running_kinematic" if running_profile else "walking_kinematic"
    truth = "synthetic_higher_speed_movement" if running_profile else "synthetic_lower_speed_movement"
    return ReferenceFixture(
        fixture_id=name,
        display_profile="running" if running_profile else "walking",
        truth_label=truth,
        caveat="The familiar profile name is test shorthand; speed alone cannot identify dog behavior.",
        chunk=TrackChunkV3(
            FIXTURE_DEVICE_ID,
            11,
            3 if running_profile else 2,
            21 if running_profile else 9,
            TimeQuality.GNSS_TRUSTED,
            True,
            points,
        ),
    )


def _poor_fix_fixture() -> ReferenceFixture:
    points = (
        _point(
            BASE_LAT_E7,
            BASE_LON_E7,
            BASE_UTC_S,
            SPEED_UNAVAILABLE,
            4,
            PointFlag.FIX_VALID | PointFlag.TIME_TRUSTED | PointFlag.LOW_QUALITY,
        ),
        _point(
            BASE_LAT_E7 + 140,
            BASE_LON_E7 - 180,
            BASE_UTC_S + 5,
            SPEED_UNAVAILABLE,
            3,
            PointFlag.FIX_VALID | PointFlag.TIME_TRUSTED | PointFlag.LOW_QUALITY,
        ),
        _point(
            0,
            0,
            BASE_UTC_S + 10,
            SPEED_UNAVAILABLE,
            0,
            PointFlag.TIME_TRUSTED | PointFlag.LOW_QUALITY | PointFlag.GAP,
        ),
        _point(
            0,
            0,
            BASE_UTC_S + 25,
            SPEED_UNAVAILABLE,
            0,
            PointFlag.TIME_TRUSTED | PointFlag.LOW_QUALITY | PointFlag.GAP,
        ),
        _point(
            BASE_LAT_E7 - 90,
            BASE_LON_E7 + 110,
            BASE_UTC_S + 30,
            SPEED_UNAVAILABLE,
            4,
            PointFlag.FIX_VALID | PointFlag.TIME_TRUSTED | PointFlag.LOW_QUALITY,
        ),
    )
    return ReferenceFixture(
        fixture_id="poor_fix_and_gap",
        display_profile="poor-fix",
        truth_label="synthetic_low_quality_and_coverage_gap",
        caveat="Coordinates with warnings and explicit gaps must not be converted into movement or inactivity.",
        chunk=TrackChunkV3(FIXTURE_DEVICE_ID, 11, 4, 33, TimeQuality.GNSS_TRUSTED, True, points),
    )


def reference_fixtures() -> tuple[ReferenceFixture, ...]:
    return (
        _stationary_fixture(),
        _movement_fixture(running_profile=False),
        _movement_fixture(running_profile=True),
        _poor_fix_fixture(),
    )


def fixture_manifest() -> dict[str, object]:
    fixtures = []
    for fixture in reference_fixtures():
        encoded = encode_chunk(fixture.chunk)
        fixtures.append(
            {
                "fixture_id": fixture.fixture_id,
                "display_profile": fixture.display_profile,
                "truth_label": fixture.truth_label,
                "caveat": fixture.caveat,
                "point_count": len(fixture.chunk.points),
                "cadence_s": [
                    fixture.chunk.points[i].utc_s - fixture.chunk.points[i - 1].utc_s
                    for i in range(1, len(fixture.chunk.points))
                ],
                "encoded_chunk_bytes": len(encoded),
                "encoded_chunk_sha256": hashlib.sha256(encoded).hexdigest(),
            }
        )
    return {
        "fixture_schema": "dog-rgb-track-v3-synthetic-fixtures/1",
        "generator": "tools/cloud_phase0/reference_fixtures.py",
        "deterministic_seed": None,
        "behavior_truth_available": False,
        "fixtures": fixtures,
    }


def fixture_manifest_json() -> str:
    return json.dumps(fixture_manifest(), indent=2, sort_keys=True) + "\n"
