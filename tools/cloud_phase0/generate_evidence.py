"""Print deterministic Phase 0B evidence as JSON or a compact Markdown table."""

from __future__ import annotations

import argparse
import json

from reference_fixtures import fixture_manifest
from storage_model import comparison_evidence
from track_v3 import CHUNK_HEADER_SIZE, MAX_POINTS_PER_CHUNK, POINT_SIZE


def build_evidence() -> dict[str, object]:
    return {
        "evidence_schema": "dog-rgb-cloud-phase0b/1",
        "codec": {
            "endianness": "little",
            "point_bytes": POINT_SIZE,
            "chunk_header_bytes": CHUNK_HEADER_SIZE,
            "maximum_points_per_chunk": MAX_POINTS_PER_CHUNK,
            "hashes": ["CRC-32/ISO-HDLC", "SHA-256"],
        },
        "fixtures": fixture_manifest(),
        "storage": comparison_evidence(),
    }


def render_markdown(evidence: dict[str, object]) -> str:
    storage = evidence["storage"]
    rows = []
    for key, label in (("raw_ring", "Raw ring"), ("littlefs_segment_log", "LittleFS segment log")):
        candidate = storage[key]
        adaptive = next(
            row for row in candidate["retention"]
            if row["profile"] == "adaptive_4h_moving_20h_stationary"
        )
        workload = candidate["workload"]
        rows.append(
            "| {label} | {capacity} | {days:.3f} | {program:.1f} | {erased:.1f} | {cuts} | {unacked} |".format(
                label=label,
                capacity=workload["capacity_chunks"],
                days=adaptive["retention_days"],
                program=workload["program_bytes_per_successful_seal"],
                erased=workload["erase_bytes_per_successful_seal"],
                cuts=workload["power_cuts"],
                unacked=workload["unacknowledged_after_run"],
            )
        )
    return "\n".join(
        [
            "| Candidate | Chunks | Adaptive days | Program B/seal | Erase B/seal | Power cuts | Unacked at end |",
            "| --- | ---: | ---: | ---: | ---: | ---: | ---: |",
            *rows,
        ]
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--format", choices=("json", "markdown"), default="json")
    args = parser.parse_args()
    evidence = build_evidence()
    if args.format == "markdown":
        print(render_markdown(evidence))
    else:
        print(json.dumps(evidence, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()

