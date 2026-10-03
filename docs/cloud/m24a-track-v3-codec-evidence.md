# M2.4a — Native Track v3 codec

**Date:** 2026-10-02 (America/Bogota). **Scope:** codec-only increment of M2.4.
Implemented in `d244221`, based on `0230227`; validation below records the
original implementation worktree.
M2.4 remains open for observations.

## Implementation boundary

- `Platformio/Dog-RGB/include/track/track_v3.h` and `src/track/track_v3.cpp`:
  C++11-compatible, no heap, Arduino, clock, storage or network dependency.
  Caller-owned buffers; explicit little-endian serialization; no struct dumps.
- Point: 16 bytes. Chunk: 92-byte header plus 1–96 points, at most 1,628 bytes.
  UUID uses canonical 16-byte order. Payload CRC-32/ISO-HDLC and SHA-256;
  header CRC includes a zeroed final CRC field.
- SHA-256 is a mandatory callback. The ESP32 adapter uses the existing SDK's
  `mbedtls_sha256`; the core has no cryptographic implementation or bypass.
- Chunk encoding stages a complete frame before publishing it. Decoding checks
  the complete frame before publishing points/metadata. Bounds, reserved fields,
  flags, time quality, legacy rules, monotonic timestamps, exact time bounds,
  checksums and point-sequence overflow are validated.
- No caller in `main.cpp` or GNSS: current v2 capture/read/export and partition
  layout remain active. Native v3 emission requires M2.3 durable identity/boot
  allocation and the remaining M2.4 observation path. No outbox or cloud runtime
  is introduced by this increment.

## Compatibility and tests

The native harness is compiled by `test_track_v3_native.py` and talks to Python
through pipes. Its SHA callback submits the exact payload to `hashlib.sha256`;
it cannot substitute a digest without submitting those bytes. Frames are
compared with the frozen `tools/cloud_phase0/track_v3.py`, including the four
reference fixtures. Host hashing verifies codec integration; the target adapter
still needs execution on physical hardware.

Coverage: 576 valid size/time-quality combinations, four manifest-pinned
fixtures, coordinate/sequence limits, eleven buffer-alias cases, failed/missing
SHA callbacks, malformed lengths/headers/checksums and semantic corruption with
recalculated hashes. Sentinel outputs prove rejection does not publish partial
points, metadata or frames. The harness bounds protocol waits and closes pipes.

One intentional stricter check: C++ rejects a decoded frame whose
`first_point_sequence + point_count - 1` exceeds uint32, even with valid hashes.
The frozen Python encoder rejects this input; its decoder currently omits that
check. The accepted Python source pins and historical review artifacts are
unchanged. Such a frame is invalid and is never an interoperability fixture.

Reproduce from the repository root:

```sh
python3 -m unittest discover -s Platformio/Dog-RGB/test -p 'test_track_v3_native.py' -v
python3 -m unittest discover -s Platformio/Dog-RGB/test -p 'test_*.py'
python3 -m unittest discover -s tools/cloud_phase0 -p 'test_*.py'
python3 tools/cloud_phase0/review_readiness_test.py -v
node --test contracts/device-v1/test-contracts.mjs
pio run -d Platformio/Dog-RGB -e seeed_xiao_esp32s3 -e waveshare_lcd169
```

Repeat the focused native command with
`CXXFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'` for ASan/UBSan.
`CXX` may select another host compiler. The existing firmware CI discovery
includes these tests automatically; no additional service or dependency is needed.

Environment: macOS arm64, Python 3.12.15, Apple clang 17.0.0, PlatformIO Core
6.1.19 and repository-pinned targets. This Mac's compiler installation needed
`CPLUS_INCLUDE_PATH=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1`
on host test commands; without it, existing tests failed to find standard C++
headers. No compiler path is embedded in source or CI.

## Validation

| Check | Result |
| --- | --- |
| Native codec | 7/7; strict C++11 compilation; ASan/UBSan also 7/7 |
| Full firmware host suite | 154/154 |
| Classic build (`seeed_xiao_esp32s3`) | Passed; 57,636 B static RAM, 1,154,203 B flash |
| Display build (`waveshare_lcd169`) | Passed; 119,724 B static RAM, 1,443,359 B flash |
| Frozen host outbox matrix | 67/67; nine source pins and canonical 9,197-byte evidence unchanged |
| Review verifier tests | 10/10 |
| Device-v1 contracts | 48/48 |

Both target builds compile the codec and SDK adapter. No runtime caller means
these linked image sizes do not measure active codec stack, timing or energy.
The original dirty-tree readiness invocation correctly reported ineligible; it is
not a replacement for the archived clean M2A review.

Independent AI review: Luna (`gpt-6-luna`, max; `/root/next_task_review`) inspected
the final core, SDK adapter and harness without candidate edits, then reran the
focused suite after the alias-fixture correction: 7/7. No unresolved defect was
reported for this codec-only increment. The review also caught stale gate text;
roadmap, architecture, testing, field inventory and Phase 0 handoff now agree
with the accepted M2A ledger. Relative documentation targets: 223 checked, none
missing; tracked and new-file whitespace checks pass.

Physical power-cut, timing, energy, wear and integrated observation acceptance
remain M2B/M2C work.
