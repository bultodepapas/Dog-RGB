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

The second command prints canonical JSON to stdout. Use `--format markdown` for
a compact table. Results use fixed seeds and contain no wall-clock timestamps,
so two runs on the same source must be byte-for-byte identical.

The checked-in decision and interpretation are in
[`docs/cloud/phase0-storage-feasibility.md`](../../docs/cloud/phase0-storage-feasibility.md).

