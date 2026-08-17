# Dog RGB device-v1 contract

Status: **Phase 0 contract freeze**  
Contract version: `device-v1`  
Protocol integer: `1`  
Telemetry schema: `3`  
Runtime-config envelope: `7`  
Capability-manifest schema: `1`

This directory is the normative collar/cloud boundary. It describes a future
implementation; its presence does not mean cloud synchronization exists in the
firmware. Keywords MUST, MUST NOT, SHOULD, and MAY use the meanings in
[RFC 2119](https://www.rfc-editor.org/rfc/rfc2119) and
[RFC 8174](https://www.rfc-editor.org/rfc/rfc8174).

If prose, a schema, and a golden fixture disagree, implementation stops and the
contract is amended with a reviewed decision. Code must not guess. Before the
first production release, changing an already frozen meaning requires updating
the schemas, fixtures, compatibility matrix, HLC vectors, and consumers in one
change. After release, use the version rules below.

## Normative wire names

JSON member names, enum strings, resource keys, problem codes, and numeric
constants are normative exactly as written. Implementations may use different
internal identifiers but must serialize these values unchanged. Human-facing
copy and labels are non-normative and localizable.

## Contract inventory

| Artifact | Purpose |
| --- | --- |
| `schemas/user-v1-issue-claim-*.schema.json` | Authenticated browser claim issuance |
| `schemas/device-v1-claim-*.schema.json` | One-time collar pairing |
| `schemas/device-v1-sync-*.schema.json` | Routine transactional synchronization |
| `schemas/device-v1-revoke-*.schema.json` | Device-initiated credential/link revocation |
| `schemas/telemetry.schema.json` | Track v3 chunks, summaries, and loss markers |
| `schemas/config-resource.schema.json` | Six coherent LWW resources and desired/reported messages |
| `schemas/capabilities.schema.json` | Device-owned effects, palettes, limits, and schema support |
| `schemas/problem-details.schema.json` | Safe RFC 9457 failure body |
| `problem-catalog.json` | Stable HTTP problem/status/retry behavior |
| `compatibility-matrix.*` | Exact supported version combinations and evolution policy |
| `hlc.md` / `fixtures/hlc-vectors.json` | Exact LWW clock algorithm and executable cases |
| `fixtures/manifest.json` | Expected-valid and expected-invalid golden corpus |
| `test-contracts.mjs` | Dependency-free schema/semantic/HLC verifier |

Schema identifiers are stable logical URNs. They are not network locations.
Every schema uses JSON Schema Draft 2020-12 and rejects unknown properties.

## Endpoints and authentication

```text
POST /functions/v1/user-v1-issue-claim
POST /functions/v1/device-v1-claim
POST /functions/v1/device-v1-sync
POST /functions/v1/device-v1-revoke
```

All endpoints require verified HTTPS, hostname validation, and
`Content-Type: application/json`. Bodies are UTF-8 without a BOM. Device
requests send `Accept-Encoding: identity`; v1 firmware need not implement
compressed response decoding.

`user-v1-issue-claim` requires a valid Supabase user access token. The server
must validate the user, verified-email state, and owner/editor membership in
the same trusted flow that inserts the claim. The collar never receives that
token or a human password.

`device-v1-claim` uses the one-time code plus the already-persisted candidate
credential in its body. Routine synchronization uses exactly:

```http
Authorization: Bearer drgb_v1_<credential-uuid>.<base64url-32-byte-secret>
Content-Type: application/json
Accept-Encoding: identity
User-Agent: DogRGB/<firmware-version> (<hardware-revision>)
```

The UUID enables indexed lookup. The server HMACs the 32-byte secret with the
device-credential pepper and compares the stored digest in constant time. It
never stores or logs the raw secret. This bearer authorizes only the associated
collar's narrow sync/revoke operations; it grants no route-history read access.
The collar contains no Supabase publishable, secret, or service-role key and
never calls PostgREST directly.

## Structural limits

The gateway enforces byte/depth limits before general JSON allocation or a
database call.

| Boundary | Limit |
| --- | ---: |
| Issue-claim request/success | 4 KiB / 4 KiB |
| Device-claim request/success | 32 KiB / 8 KiB |
| Sync request/success | 128 KiB / 64 KiB |
| Device-revoke request/success | 4 KiB / 4 KiB |
| Problem response | 16 KiB |
| JSON nesting depth | 12 |
| Chunks per sync | 8 |
| Points per sealed chunk | 96 |
| Points across all chunks in one sync | 384 |
| Summaries per sync | 16 |
| Loss markers per sync | 16 |
| Config mutations/reports/response outcomes | 16 of each |
| Device request deadline | 30 seconds overall, with bounded DNS/connect/TLS/send/receive phases |

`Content-Length` is required for device endpoints and must be rejected before
body parsing when over the endpoint limit. Chunk and array limits are schema
constraints where local. The cross-chunk 384-point sum, encoded byte count, and
depth are semantic gateway checks because JSON Schema cannot express them.

Machine strings are ASCII-constrained by schemas, so their `maxLength` is also
a byte bound. Capability labels allow Unicode: enforce both the schema's
48-code-point limit and a 96-byte UTF-8 limit at the gateway/database boundary.
Problem `detail` is server-authored, safe, and never includes a token, claim
code, config body, coordinate, SQL fragment, or stack trace.

## Request identity and replay

Every request UUID is generated once and persisted with the exact selected
batch. `request_sha256` is SHA-256 over the exact UTF-8 HTTP body bytes, before
parsing; the Authorization header is not part of it.

For the authenticated collar identity and `request_id`:

- unseen ID: process one transaction;
- same ID and same digest: return the previously committed logical result,
  including accepted HLCs and ACK identities, without re-running effects;
- same ID and different digest: return `409 request_id_reused` and do not
  generate a replacement ID automatically.

Whitespace/key-order changes therefore make a different digest. Firmware MUST
retain and resend the exact serialized bytes until it durably processes the
response. This deliberate rule makes uncertain-response retry behavior
testable and keeps idempotency independent from non-final HTTP drafts.

The same identity rule applies to device-initiated revocation. Its body binds
`request_id`, public `device_id`, `credential_id`, and one bounded reason
(`local_unlink` or `factory_reset`). It does not repeat the secret in JSON; the
device Authorization header authenticates it.

## Pairing

The website returns 80 random bits as 16 uppercase unambiguous Crockford Base32
characters matching `[0-9A-HJKMNP-TV-Z]{16}`. It may display groups as
`XXXX-XXXX-XXXX-XXXX`; UI/AP input must remove hyphens and uppercase locally,
then send the canonical 16 characters. Codes expire after 900 seconds and five
failed consume attempts. Only
`HMAC-SHA-256(claim_pepper, canonical_code)` is stored.

Before its first claim request, the collar persists a random UUIDv4 device ID,
UUIDv4 credential ID, and 256-bit random secret in `PAIR_PENDING` A/B storage.
It sends the same values on retry. One database transaction locks/consumes the
claim, links the dog/collar, verifies the capability hash, stores only a
peppered credential digest, and stores the replay receipt. A response lost
after commit is resolved by the identical replay. Success returns the accepted
capability hash but never returns the raw secret.

## Device-initiated revoke and local unlink

Normal AP unlink is a two-phase operation. Firmware first persists
`REVOKE_PENDING`, including the exact serialized revoke request, and stops
ordinary sync. It retries those same bytes with the same bearer credential
until it durably validates a matching revoke success. It MUST NOT erase local
credential/link metadata after a timeout, malformed response, generic 401/403,
or any response whose request, device, or credential identity differs.

The revoke transaction authenticates the UUID/secret against either the active
credential digest or a retained non-usable tombstone digest. It then locks the
credential/link, verifies both body identities, stores the request digest and
compact result, and commits before replying. An active credential returns
`disposition: newly_revoked`. If a website or another committed request already
revoked the same matching credential, the revoke-only tombstone path stores a
receipt and returns `disposition: already_revoked`. An exact replay always
returns its previously stored logical result, including the original
`revoked_at` and disposition; it does not re-run effects.

Both schema-valid dispositions with matching identities prove that this
credential can no longer sync and permit local erasure after the result itself
is durably processed. Tombstone authentication is allowed only on this revoke
endpoint and never restores sync authority. A wrong secret, unknown credential,
identity mismatch, reused request ID with different bytes, or expired tombstone
fails closed and never authorizes local erasure. Forced offline clear remains
an explicit DIY recovery option, but its UI warns that website-side revocation
is still required because a copied credential may remain active.

## Capability manifest

The manifest is device-owned data. Compute `capability_hash` as unpadded
base64url SHA-256 of its UTF-8
[RFC 8785 JSON Canonicalization Scheme](https://www.rfc-editor.org/rfc/rfc8785)
representation. Claim requires the manifest. Routine sync sends `capabilities:
null` only when the server previously acknowledged the exact current hash; it
sends the complete manifest after any hash change. A success repeats
`accepted_capability_hash`, which firmware persists.

The web application renders supported resource forms, effect IDs, palette IDs,
control ranges, and safety labels from the stored validated manifest. It must
not maintain an independent JavaScript catalog. The gateway verifies unique
resource/effect/palette IDs and keys, min/max coherence, referenced default
palettes, hardware-revision equality, and declared support for protocol 1,
telemetry 3, and config 7.

## Track v3 tuple and chunk hash

`chunks[].points[]` is exactly the existing Phase 0B storage payload decoded to
JSON; the cloud contract does not introduce a second serialization:

```text
[lat_e7, lon_e7, utc_s, speed_cmps, satellites, flags]
```

Point sequence is `first_point_sequence + array_offset`. The normative hash
input is the concatenation of the exact 16-byte little-endian point records
already frozen in `tools/cloud_phase0/track_v3.py` (`<iiIHBB`):

| Offset | Width | Encoding |
| ---: | ---: | --- |
| 0 | 4 | signed `lat_e7`, two's-complement little-endian |
| 4 | 4 | signed `lon_e7`, two's-complement little-endian |
| 8 | 4 | unsigned Unix `utc_s`, little-endian |
| 12 | 2 | unsigned `speed_cmps`, little-endian |
| 14 | 1 | unsigned saturated satellite count |
| 15 | 1 | flags |

`content_sha256` is unpadded base64url SHA-256 of that stored point payload. It
is not a hash of the JSON or of the chunk header. The flags are frozen as:

| Mask | Name | Meaning |
| ---: | --- | --- |
| `0x01` | `FIX_VALID` | Coordinates are an accepted position observation |
| `0x02` | `MOVEMENT_EVIDENCE` | Kinematics meet the configured movement classifier; not a behavior label |
| `0x04` | `TIME_TRUSTED` | Historical codec name: `utc_s` is present; trust/precision comes from chunk `time_quality` |
| `0x08` | `STATIONARY_HEARTBEAT` | Trusted stationary observation; not proof of rest, sleep, or wear |
| `0x10` | `LOW_QUALITY` | Observation is retained but below preferred position quality |
| `0x20` | `GAP` | Explicit unknown-coverage marker; it is not a fix or activity evidence |
| `0x40` | `LEGACY_V2` | Deterministic minute-precision Track v2 conversion |
| `0x80` | reserved | MUST be zero in telemetry schema 3 |

`TIME_TRUSTED` MUST be set if and only if `utc_s != 0`; its meaning is that UTC
is present and usable for ordering/analytics, not that the source is
cryptographically authenticated. Every chunk carries one exact `time_quality`
with this frozen uint8 header representation:

| Header byte | JSON value | Meaning |
| ---: | --- | --- |
| `0` | `unknown` | No usable UTC |
| `1` | `approximate_persisted` | Restored anchor plus monotonic uptime |
| `2` | `server_anchored` | Bounded by a verified HTTPS server response |
| `3` | `sntp_synced` | Bounded SNTP result |
| `4` | `gnss_trusted` | Trusted GNSS UTC |
| `5` | `legacy_minute` | Deterministic Track v2 minute conversion |

There is no lossy quality mapping between the runtime service and Track v3.
Configuration HLC trust remains a separate decision: only
`server_anchored`, `sntp_synced`, and `gnss_trusted` qualify for authored-time
ordering within the skew window; `approximate_persisted`, `unknown`, and
`legacy_minute` rebase at server acceptance.

An `unknown` chunk has zero UTC and no `TIME_TRUSTED` on every point. Every
other chunk has nonzero monotonic UTC and `TIME_TRUSTED` on every point. Thus,
for every point, `(utc_s != 0) == TIME_TRUSTED == (chunk quality != unknown)`.
`MOVEMENT_EVIDENCE` and `STATIONARY_HEARTBEAT` are mutually exclusive. `GAP`
cannot coexist with `FIX_VALID` or either evidence flag. When `FIX_VALID` is
clear, coordinates MUST be zero and speed MUST be `0xFFFF`. `LEGACY_V2`
requires reserved boot sequence zero and `legacy_minute`; non-legacy chunks
must not use either. Invalid fixes, `GAP`, and intervals beyond the analytics
gap threshold break route geometry; they never create a straight line across
missing coverage.

Chunk identity is `(authenticated_collar_id, boot_sequence, chunk_sequence)`.
Point identity adds `point_sequence`. The server accepts out-of-order chunks,
but records holes. A known chunk identity with another content hash is an
integrity rejection even under a new request ID. `point_count` must equal array
length, derived last sequence must fit uint32, ranges in one request cannot
overlap, and at most one final chunk can exist per boot. A final chunk prevents
later sequences unless an explicit future recovery contract says otherwise.

Daily summaries are advisory inputs and are recomputed from raw points in the
cloud. Their semantic checks include `moving_s + inactive_s == observed_s`,
window end after start, valid IANA timezone, and plausible duration/distance.
Missing/off-collar time remains unknown; it does not become inactivity. A loss
marker's inclusive range length must equal `lost_points`.

## Configuration resources and hashes

Device-v1 defines six independent LWW registers:

- `brightness`
- `visual_mode`
- `speed_profile`
- `simple_effect`
- `gps_quality`
- `geofence_policy`

Home coordinates, power calibration, scenes, Wi-Fi/AP credentials, mDNS, portal
PIN, cloud credentials/API host, and developer controls are absent by design.
Unknown fields/resources are rejected; they are never silently retained or
echoed. Exact bodies/ranges are in `config-resource.schema.json`.

`body_sha256` is unpadded base64url SHA-256 of the RFC 8785 canonical resource
`body` only—not its envelope. In addition to schema validation:

- speed thresholds must be finite and strictly increasing;
- effect IDs must exist in the accepted capability manifest;
- `gps_quality.min_segment_m <= max_min_segment_m`;
- mutation actor must equal the authenticated public device ID;
- mutation IDs and local sequences are unique in a request;
- the same mutation ID with another body hash is an integrity rejection;
- reports are idempotent on resource/version/hash/status;
- `applied` reports carry no error, while rejection reports carry a bounded
  machine-safe error code.

The HLC and unknown-time fallback algorithm is fully specified in `hlc.md`.
Resource heads are locked/compared in the sync transaction. Only winning
revisions receive a new monotonically increasing `server_version`. A
configuration rejection or losing mutation is an item outcome in HTTP 200 when
telemetry committed successfully; it must not make a usable telemetry ACK
unreachable behind a non-2xx response.

## Transaction and ACK rules

The Edge Function performs method/media/length, authentication format, bounded
parse, schema, and cheap semantic validation. It then invokes one service-only
database transaction. That transaction must:

1. derive collar identity from the credential and lock its active state;
2. check quotas and request receipt;
3. return the stored result for an exact replay;
4. enforce chunk/point identity and hash consistency and insert missing data;
5. upsert summaries/loss markers idempotently;
6. resolve configuration mutations and reports under locks;
7. select compatible desired resource heads;
8. persist capability/diagnostic/last-sync state and the compact response;
9. commit before the gateway returns success.

An HTTP 200 is not itself an ACK. Firmware reclaims a chunk only after matching
`boot_sequence`, `chunk_sequence`, `accepted_point_count`,
`through_point_sequence`, and `content_sha256`, and after that ACK is persisted
in verified local metadata. Summary/loss records use their explicit accepted-ID
lists. Rejected identities are not accepted watermarks. A truncated,
unrecognized, non-2xx, or lost response deletes nothing.

## Required semantic and database checks

JSON Schema cannot prove authentication/ownership, hashes, aggregate sums,
sequence history, clock plausibility, timezone database membership, stored
capability compatibility, or transaction races. Edge and database tests must
therefore cover all of the following:

| Layer | Required checks |
| --- | --- |
| Pre-parse Edge | POST, media type, required length, endpoint byte limit, UTF-8, depth, bearer grammar |
| Parsed Edge | JSON Schema, total 384 points, UTF-8 label bytes, UUID uniqueness in request, raw request digest |
| Claim transaction | verified user/membership, one active claim, expiry/attempt lock, claim HMAC, atomic consume, device/credential replay consistency |
| Credential transaction | active/not expired/revoked, constant-time HMAC, body device ID equals credential collar |
| Revoke transaction | active-or-tombstoned credential match, body device/credential match, exact request-digest replay, one locked revoke/link transition, persisted bounded result before response |
| Telemetry transaction | canonical chunk hash, point/chunk identities, duplicate hash equality, sequence/range/final rules, flag cross-rules, timestamp policy and quotas |
| Summary/loss transaction | duration and inclusive range invariants, IANA timezone, idempotent identities, sane windows |
| Configuration transaction | canonical body hash, capability/resource/effect compatibility, HLC skew/rebase/order, mutation replay hash, locked head/version update, reported version/hash |
| Commit/response | bounded persisted replay response, no ACK before commit, response IDs/hashes correspond only to committed rows |

Database constraints duplicate critical integer/range/enumeration checks even
when the Edge Function already checked them. The exposed `api` RPC wrapper is
executable only by `service_role`, uses an empty safe `search_path`, and calls
private tables/functions. Neither `anon` nor `authenticated` receives direct
device-sync execution or writes to telemetry/config heads.

## Problems and retry behavior

Non-2xx bodies use `application/problem+json` and the stable entries in
`problem-catalog.json`, following
[RFC 9457](https://www.rfc-editor.org/rfc/rfc9457). `type` is always
`urn:dog-rgb:problem:<code>`, and catalog status/title must match the body.
When present, JSON `retry_after_seconds` and HTTP `Retry-After` must agree.
`device_action` is the default behavior; when an entry contains
`device_action_overrides`, the endpoint-specific action wins. In particular,
credential failures and an oversized fixed revoke envelope preserve
`REVOKE_PENDING`; they do not split the body, re-enter pairing, or clear the
credential.

An unknown problem code never authorizes deletion. Firmware falls back to the
HTTP class: 4xx stops hot-looping and retains/quarantines identified data; 429
honors `Retry-After`; 5xx/timeouts retain the exact batch and retry with full
jitter. `401` enters a long pairing/credential recovery cooldown. `403
device_revoked` marks the device revoked and stops routine uploads while local
collar operation continues.

For `device-v1-revoke`, a generic problem response never proves revocation and
never permits local credential deletion. Only a schema-valid HTTP 200 revoke
result whose request/device/credential identities match the pending operation
and whose disposition is `newly_revoked` or `already_revoked` has that meaning.

## Version and privacy rules

The compatibility matrix is closed, not aspirational.

Track v2/config 6 is the current local baseline and cannot impersonate native
Track v3/config 7. The frozen converter may upload a v2 snapshot only as Track
v3 records with boot sequence zero, `legacy_minute`, `LEGACY_V2`, unavailable
speed, and unknown movement/stationary state. Original v2 data remains locally
recoverable until its converted chunks are explicitly acknowledged.

Precise coordinates, claim codes, bearer secrets, request bodies, dog names,
emails, and config bodies must not appear in routine Edge/Vercel/firmware logs.
Raw telemetry is append-only except explicit quarantine metadata and the
documented privacy/account-deletion workflow. Device sync never returns history.

## Run the contract suite

No dependency installation or package-script change is required:

```powershell
node --test contracts/device-v1/test-contracts.mjs
```

The suite parses every schema, resolves every `$ref`, validates the golden
corpus, runs the semantic hash/sequence/config/problem checks, executes all HLC
vectors (including overflow and unknown-time rebase), and asserts the
compatibility matrix. Phase 1 implementations should import or generate from
these artifacts rather than copy their constants by hand.
