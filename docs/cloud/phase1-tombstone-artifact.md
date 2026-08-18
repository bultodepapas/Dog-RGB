# Phase 1 signed deletion-tombstone artifact

**Status:** The local artifact format and restore verification are implemented
and tested as of 2026-08-18. No production signing key, KMS, off-site object
store, export scheduler, or hosted recovery trust bundle is configured.

## Purpose

A database backup can predate a completed deletion. The restore process must
therefore obtain deletion tombstones from a different failure domain and verify
their origin before replaying them. The database's `replay_sha256` detects
accidental corruption but is unkeyed; a party that can rewrite an artifact can
also recompute that digest.

[`tombstone_artifact.mjs`](../../tools/cloud_restore/tombstone_artifact.mjs)
adds the external authentication layer. It signs each bounded database export
page with Ed25519 and verifies the complete chain before returning any item to
the SQL replay function.

## Signed format

Each artifact has exactly four top-level fields:

- a versioned payload;
- the lowercase SHA-256 of its canonical payload bytes;
- `Ed25519` as the signature algorithm;
- one canonical base64url signature.

The payload binds the trusted `key_id`, required project/environment
`context_id`, zero-based batch sequence, prior payload digest, input cursor,
creation instant, and exact database export page. The verifier requires the
expected context separately, preventing a valid artifact from another deployment
from being accepted under a shared or misconfigured key. JSON is
canonicalized recursively with lexicographically sorted object keys. Undefined,
non-finite, cyclic, class-instance, symbol-bearing, and unexpected-field values
fail closed instead of being silently normalized.

Chain verification requires:

- a bounded non-empty batch list;
- valid signature under a separately configured public key for every `key_id`;
- contiguous sequence numbers and matching previous-payload SHA-256 values;
- exact cursor continuation and strictly increasing item order;
- no repeated deletion request ID across pages;
- a terminal page with `has_more = false` before the chain is considered
  complete.

The embedded tombstone remains subject to the database's independent strict
field, UTC/base64url, request-hash, tombstone-hash, and replay-hash checks.
Neither layer includes coordinates, dog names, email addresses, request bodies,
credentials, or deleted row content.

## Local drill

The [isolated restore runner](../../tools/cloud_restore/phase1_restore.mjs)
generates one ephemeral Ed25519 keypair in memory, signs the post-snapshot
export, proves a modified signed payload is rejected, verifies the complete
chain, and only then passes the verified tombstone to SQL. It separately proves
that tampering after signature verification is rejected by the database hash
boundary. The private key, public key, artifact payload, signature, and logical
backup are never written to the evidence file; CI retains only the payload
SHA-256, non-secret local key ID, algorithm, counts, booleans, and timings.

Unit tests cover deterministic canonicalization, unsupported JSON values,
trusted/untrusted keys, payload modification with a recomputed SHA-256, valid
two-page chains, missing terminal pages, duplicate requests, broken cursors,
and non-canonical ordering.

## Production gate still open

Before a hosted project can claim recoverable deletion semantics:

1. create the signing key in a managed KMS/HSM or equivalent operator-controlled
   signer; never store its private material in Git, database backups, browser,
   firmware, or ordinary CI artifacts;
2. configure a reviewed public-key trust bundle in the isolated restore
   environment, with explicit key activation, overlap, revocation, and expiry;
3. export every page to immutable/versioned storage outside the database backup
   failure domain and durably record the completed terminal chain;
4. alert on export lag, incomplete chains, unknown keys, signature failures,
   cursor gaps, and tombstones newer than the latest protected artifact;
5. replay and verify the complete chain in a disposable hosted restore before
   any Edge, Auth, Data API, or portal traffic is enabled;
6. document retention and deletion for the artifacts themselves.

For a private DIY deployment, this hardened cloud-recovery profile remains
optional because cloud synchronization itself is optional. Once persistent
external-user cloud data is enabled, authenticated tombstone custody is a
release requirement rather than an optional hardening toggle.
