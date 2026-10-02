# Cloud Phase 0B storage feasibility prototype

This directory is a standard-library-only, deterministic host prototype for the
storage/data portion of Phase 0 in the web-platform plan. It does **not** modify
the collar firmware and it does **not** claim to replace power-cut testing on a
physical ESP32-S3.

It freezes and exercises:

- the byte-level Track v3 point and chunk encoding;
- deterministic, non-behavior synthetic reference datasets;
- retention calculations against the repository's real `0x150000` partition;
- comparable raw-ring and LittleFS workload models;
- fill, ACK/reclaim, corruption, random power-cut, recovery, and wear scenarios;
- the legacy Track v2 conversion contract.

Run all checks from the repository root:

```powershell
python -m unittest discover -s tools/cloud_phase0 -p "test_*.py" -v
python tools/cloud_phase0/generate_evidence.py
```

The second command writes canonical UTF-8 JSON with LF and one trailing newline
to binary stdout, independent of the host's text newline convention. Use
`--format markdown` for a compact table. Fixed seeds and no timestamps make
repeated runs byte-identical. Python 3.12 is sufficient; no dependencies needed.

## Independent P0-R1 review

The candidate workstream cannot accept itself. A reviewer working from a clean
clone/worktree should run:

```powershell
python tools/cloud_phase0/review_readiness_test.py -v
python tools/cloud_phase0/verify_review_candidate.py
```

The focused verifier suite passes `10/10`; the host matrix passes `67/67`.
The verifier compares nine explicit source pins with both committed HEAD blobs
and worktree bytes, checks ancestry to the previous review baseline, runs the
host matrix, and regenerates canonical evidence. A clean tree is required;
`--allow-dirty` is always ineligible. The tool only reports readiness and cannot
accept its own candidate. See the [review packet](../../docs/cloud/phase0-outbox-review-packet.md).

The [2026-10-02 rejected review](../../docs/cloud/phase0-outbox-independent-review.md)
and its raw evidence remain historical records. Its three failure reproductions
now run in `test_integrity.py` as part of normal discovery, alongside thirteen new
boundary/cut regressions. The compatibility command still runs those original
three tests:

```sh
python tools/cloud_phase0/review_integrity_test.py -v
```

The [remediation report](../../docs/cloud/phase0-outbox-remediation-2026-10-02.md)
defines the new single-device identity contract, versioned metadata layout,
read-only recovery policy, and validation. Physical ESP32 acceptance remains open.
