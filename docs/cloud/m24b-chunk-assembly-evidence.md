# M2.4b — Native chunk assembly and local handoff

**Date:** 2026-10-02 (America/Bogota). **Baseline:** clean `f148bec`.
**Scope:** bounded C++ assembler connecting M2.3a identity allocation and M2.4a
Track v3 encoding. No startup/GNSS caller, sampling scheduler, outbox driver,
cloud transport, partition change or v2 migration.

## Contract

`include/track/chunk_assembler.h` and `src/track/chunk_assembler.cpp` own one
pending batch of at most 96 native observations. The caller supplies already
classified observations and time quality; the assembler does not infer movement,
stationary state, GNSS trust, elapsed time or missing intervals.

- An active session from the durable identity service is required. Construction
  performs no I/O and cannot provision or allocate a boot.
- Append validates the point and native time-quality rules before mutation.
  Legacy flags/quality are rejected; a point without a fix must be an explicit
  gap with zero coordinates and unavailable speed. Known time quality requires
  nonzero UTC; unknown time requires zero UTC. Equal timestamps are valid.
  The nonfix/gap requirement is the M2.4 native observation policy and is stricter
  than the frozen codec, which also permits nonfix records without a gap flag.
  Wire validation and historical frames are unchanged.
- Capacity, changed time quality or backward UTC requires sealing the current
  batch first. The incoming point is not consumed; the caller must retain it.
- Sealing reserves one chunk identity and the batch's contiguous point range
  exactly once. Points and the final-recording flag then remain frozen, including
  after encoding/hash failure. Retrying uses the same reservation. A changed
  final-recording flag is rejected. A missing hash callback is rejected before
  reservation; it cannot consume identity with an unusable encoder configuration.
- Successful encoding retains the exact frame. Repeated sealing does not hash
  or allocate again; new points are blocked while the batch is pending.
- A synchronous sink receives the immutable frame. Success means the sink has
  verified a **durable local commit**. Failure or an ambiguous result must return
  false; the assembler retains the exact frame for retry. Only success releases
  a non-final batch for further appends. Acceptance of a final batch permanently
  closes the assembler for that boot; later append/seal/handoff calls are rejected
  without another sink call or sequence reservation. There is no overwrite/drop/
  reset operation.
  The sink must accept exact retries idempotently by frame identity/content;
  false may follow a completed commit whose confirmation was lost.

Local handoff is not a server ACK, upload receipt or flash reclaim authorization.
No durable sink is implemented here. The pending batch lives in RAM and is lost
on reset; a fresh durable boot prevents identity reuse but does not recover those
points. Durable sealing, full-storage loss accounting and reboot replay remain
M2.5. This assembler must not enable native runtime capture before that path exists.

## Ownership and integration

Use exactly one serialized assembler throughout the identity service's granted
boot session, and keep the service and hash context alive longer than the
assembler. Do not reserve chunks separately, create competing assemblers or
reconstruct one to bypass pending/final state. Both service and assembler are
noncopyable. Callbacks must not retain or modify borrowed buffers; recursive
mutating calls are rejected while hashing or handing off. Exceptions are outside
the embedded callback contract.

Memory is fixed: 96 point records plus complete staging and sealed-frame buffers;
no dynamic allocation in the assembler. Use a long-lived owner with an explicit
RAM budget, rather than a cooperative-loop stack temporary. Target compilation
does not establish active stack use, execution latency, watchdog margin or energy.

The caller must seal and hand off before retrying a point rejected at a batch
boundary. A batch boundary alone is not a generated gap or a time anchor; the
future observation/time service must supply that evidence. A recording's final
flag must be chosen before its final batch is frozen; empty final chunks are not
supported. The [device-v1 contract](../../contracts/device-v1/README.md) permits
one final chunk per boot, so reopening capture requires a newly allocated durable
boot session, not another recording under the closed boot. Cadence (moving 5 s,
stationary 60 s), gap generation, time anchors and GNSS integration remain separate work.
The existing v2 capture, retention, read/export and `tracknvs` path remain active.

## Verification

From the repository root:

```sh
python3 -m unittest discover -s Platformio/Dog-RGB/test -p 'test_chunk_assembler_native.py' -v
python3 -m unittest discover -s Platformio/Dog-RGB/test -p 'test_*.py'
node --test contracts/device-v1/test-contracts.mjs
pio run -d Platformio/Dog-RGB -e seeed_xiao_esp32s3 -e waveshare_lcd169
```

The native wrapper honors `CXX` and `CXXFLAGS`; repeat the focused suite with
`CXXFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'` for ASan/UBSan.
This Mac requires
`CPLUS_INCLUDE_PATH=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1`;
that host-specific path is not embedded in the test runner or CI.
Environment: macOS arm64, Python 3.12.15, Apple clang 17.0.0, PlatformIO Core
6.1.19 and repository-pinned targets. The existing firmware CI discovery includes
the native wrapper; no new package or remote service is required. No CI run is
claimed here.

## Validation results

Local implementation worktree based on `f148bec`; no deployment or board run.

| Check | Result |
| --- | --- |
| Assembler native suite | 1/1; 376 C++ checks and 7 exact Python-oracle frames |
| Same suite under ASan/UBSan | 1/1; same checks/frames, no sanitizer report |
| Complete firmware host suite | 157/157, including existing codec and identity regressions |
| Device-v1 protocol | 48/48 |
| Classic final build | PASS; RAM 57,636 B; flash 1,154,203 B |
| Display final build | PASS; RAM 119,724 B; flash 1,443,359 B |

The wrapper compiles the actual assembler, identity store and codec as strict
C++11 with `-Wall -Wextra -Werror -Wshadow -pedantic`. SHA requests are answered
with Python `hashlib.sha256`; seven independently specified batches are compared
byte-for-byte with the frozen Python encoder and decoded back to their expected
fields. The driver bounds execution and captures sanitizer stderr independently
of the hash-response pipe. Native checks remain active under `NDEBUG`.

Coverage includes no constructor I/O, no pre-grant capture, 1/96-point batches,
quality/backward-time/capacity boundaries without input consumption, native gap
policy, invalid point/time rejection, stable reservation through SHA failure,
absent SHA before reservation, callback reentry, exact replay after ambiguous
local commit, sequential next-batch identities and terminal final acceptance.
The native object is bounded below 6 KiB by a test; no active instance is added
to firmware. Final build sizes therefore do not measure runtime memory/cost.

Independent AI review: Luna (`gpt-6-luna`, max;
`/root/next_increment_review`) reviewed the core, sink/ownership contracts and
tests, and independently ran the final focused suite: 1/1, 376 checks, 7 oracle
frames. Review found and closed the missing terminal-final latch and null-SHA
preflight; no remaining functional defect was identified within this scope.
The accepted host outbox, its source pins and wire codec are unchanged. M2.4
remains open for observation cadence/gaps and runtime integration; M2.5 durable
storage and M2C physical acceptance remain open.
