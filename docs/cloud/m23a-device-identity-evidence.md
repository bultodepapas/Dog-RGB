# M2.3a — Durable device identity and sequence allocation

**Date:** 2026-10-02 (America/Bogota). **Baseline:** clean `d244221`.
**Implementation commit:** `f148bec`; results below describe its original worktree.
**Scope:** isolated C++ identity service and ESP-IDF NVS adapter. No caller in
startup/GNSS, automatic provisioning, credential storage or cloud transport.
M2.3 remains open for provisioning policy, credentials and observation startup.

## Storage and ownership

`include/track/device_identity.h` and `src/track/device_identity.cpp` implement
the portable core. `device_identity_nvs.h/.cpp` bind it to the existing `nvs`
partition, namespace `dogrgb_v3`, keys `id_a`/`id_b`. Opening is explicit and
requires initialized NVS. Construction does no I/O. No erase, repair, partition
initialization or migration is performed; `tracknvs`, v2 and the Display contact
identity store are separate.

One serialized service owns both banks for its lifetime. Copies of a live store
are forbidden. Multiple concurrent owners, snapshots rolled back externally and
external bank mutation during an active session are outside this contract.
Mount caches its initial result, including read errors; retry inspection through
a fresh service instance after resolving the storage error.

Each record is 40 explicit little-endian bytes; no struct serialization:

| Offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 4 | `D3ID` magic |
| 4 | 2 | Version 1 |
| 6 | 2 | Record size 40 |
| 8 | 4 | Generation |
| 12 | 4 | Last reserved boot |
| 16 | 16 | Public UUID, canonical byte order, non-nil |
| 32 | 4 | Reserved zero |
| 36 | 4 | CRC-32/ISO-HDLC over bytes 0–35 |

Generation equals last reserved boot: only boot allocation updates this format.
Credential lifecycle must not reuse these records for unrelated mutations.

## State and failure rules

- Two missing keys mean **empty**, not permission to emit. Explicit provisioning
  accepts an externally supplied non-nil UUID only on an empty store and writes
  both genesis records with boot/generation zero. It never resets existing data.
  Both keys are rechecked immediately before writing; a stale empty snapshot
  locks the instance without writing.
  The caller must supply the random public UUID required by ADR-0005; RNG and
  enrollment policy are deferred, not inferred from a MAC or chip identifier.
- Both banks must validate, match UUID, and have adjacent generations; equality
  is allowed only for the two identical genesis records. A missing single bank,
  corrupt/unknown-format record, oversized blob, I/O error or conflicting pair
  blocks a new boot grant. No fallback to an older bank and no automatic repair.
- Allocate from the freshest validated pair into the older bank. A successful
  backend write/commit, exact target readback and revalidation of both banks
  precede publication of the new boot. Any drift from the mounted pair blocks
  allocation before the write; it is not silently adopted as a new baseline.
  Boot zero is reserved for legacy data; uint32 exhaustion does not wrap.
- A failed/ambiguous mutation locks that instance. A fresh instance must inspect
  the bytes. A committed but unreturned reservation is consumed and skipped.
  If a failed write left the entire previous image intact, a fresh instance may
  retry its unissued next number: no record was authorized under that number.
  This is not a promise to remember attempts that left no persistent evidence.
- `alloc_boot_sequence` cannot be repeated and `mount` cannot reset counters in
  an active session. Point and chunk sequences start at zero under the new boot.
  `reserve_chunk` allocates one chunk and 1–96 contiguous points atomically;
  invalid/exhausted requests leave counters and output untouched. A caller must
  retain the exact reservation for retry rather than allocate another identity.
- The NVS adapter distinguishes missing keys from read/type/length failures.
  `nvs_set_blob` requires `nvs_commit`; an SDK write/commit failure closes the
  handle without claiming rollback. Reopening does not authorize reuse of the
  failed service instance.

Incomplete provisioning blocks further allocation; recovery policy/UI is deferred.
If both banks are erased, the image is indistinguishable from factory blank.
Re-enrollment therefore requires a fresh UUID and an explicit policy for retained
outbox/cloud history. This increment provides no reset/re-enrollment operation.
CRC collisions, valid-image rollback and physical flash behavior are unproved.
The two keys are logical A/B records within one NVS partition, not independently
isolated physical flash sectors.

## Verification

From the repository root, with Python standard library and a C++ compiler:

```sh
python3 -m unittest discover -s Platformio/Dog-RGB/test -p 'test_device_identity*.py' -v
python3 -m unittest discover -s Platformio/Dog-RGB/test -p 'test_*.py'
python3 -m unittest discover -s tools/cloud_phase0 -p 'test_*.py'
node --test contracts/device-v1/test-contracts.mjs
pio run -d Platformio/Dog-RGB -e seeed_xiao_esp32s3 -e waveshare_lcd169
```

The focused tests honor `CXX` and `CXXFLAGS`. For ASan/UBSan, set
`CXXFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'`.
On this Mac, host tests also require
`CPLUS_INCLUDE_PATH=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1`;
the path is not embedded in the test runner or CI.
Environment: macOS arm64, Python 3.12.15, Apple clang 17.0.0, PlatformIO Core
6.1.19 and the repository-pinned target SDKs. Existing firmware CI discovery
includes both new native wrappers; no CI run is claimed here.

Core tests use byte images and injected reads/writes; adapter tests compile the
real adapter against a fake ESP-IDF API with missing/type/size/read/commit faults.
They do not establish NVS power-cut behavior. Target execution, wear, recovery
time and the integrated observation gate remain M2B/M2C work.

## Validation results

Original implementation worktree based on `d244221`; no deployment or board run.

| Check | Result |
| --- | --- |
| Identity core and NVS adapter | 2/2 native wrappers; strict C++11 with warnings as errors; core executes 987 checks |
| ASan/UBSan, same focused suite | 2/2; 987 core checks, no sanitizer report |
| Full firmware host suite | 156/156 |
| Accepted outbox host suite | 67/67; model and frozen source pins unchanged |
| Device-v1 protocol | 48/48 |
| Classic build | PASS; RAM 57,636 B; flash 1,154,203 B |
| Display build | PASS; RAM 119,724 B; flash 1,443,359 B |

The core matrix includes an independently generated 40-byte/CRC golden,
both-bank corruption/missing/future/oversize/I/O cases, no fallback from a corrupt
latest bank, all 40 incomplete write prefixes, full-write response loss,
target readback and both post-write pair-read failures, A/B provisioning failures
before/after persistence, stale-empty provisioning, active-remount counter
preservation and uint32 exhaustion. Durable but unreturned boot 2 recovers at
boot 3; intact genesis after a lost provisioning response starts at boot 1.

Both target builds compile the core and actual NVS adapter. With no runtime
caller, linked sizes do not measure active service memory, timing or flash wear.

Independent AI review: Luna (`gpt-6-luna`, max;
`/root/identity_design_review`) inspected core, adapter, tests and scope and
independently ran the final focused suite: 2/2, 987 core checks. No open defects
within M2.3a after corrections. Review closed stale-empty overwrite and active
remount/reset risks and required latest-bank corruption and boot exhaustion
regressions. Whitespace and 158 local documentation links were checked across
all 16 changed/new files. This review does not close remaining M2.3 integration,
M2.4 observations or M2C physical acceptance.
