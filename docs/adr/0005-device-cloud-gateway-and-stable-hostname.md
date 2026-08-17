# ADR-0005: Device cloud gateway and stable API hostname

**Status:** Accepted

**Date:** 2026-08-13

**Scope:** Optional device-to-cloud transport, pairing, credentials, deployment boundary, and firmware endpoint stability.

## Context

Dog-RGB must remain fully useful without an account or Internet connection. When a user explicitly opts in, a collar connected to known Wi-Fi needs a small, retry-safe HTTPS surface for uploading telemetry and exchanging configuration. The website will run on Vercel and user/application data will live in Supabase.

Putting a human email/password, a Supabase publishable key, or a Supabase secret/service-role key in firmware would couple device identity to an interactive account credential and make one extracted collar credential useful against unrelated APIs. Sending devices through Vercel would also make Vercel an unnecessary ingestion hop and bind firmware to a frontend deployment contract.

The field endpoint needs to survive a future Supabase project migration. Supabase project hostnames are provider-owned. Supabase supports custom domains through a paid add-on and a DNS CNAME, while the default project hostname is suitable for development evidence only.

## Decision

### Local-first and explicit opt-in

- Cloud is disabled by default and is never required for LEDs, GNSS capture, metrics, route storage, AP configuration, recovery, or local export.
- No location leaves the collar until an authorized user completes an explicit cloud pairing flow.
- Losing Supabase, Vercel, DNS, the map provider, or Internet access must only delay synchronization. It must not block boot or local operation.
- The AP remains the recovery/configuration path. A future `/cloud` page may expose pairing, last result, retry, and unlink; it must not absorb Wi-Fi or ordinary local configuration.

### One narrow device gateway

Collars call versioned Supabase Edge Functions directly:

| Operation | Authentication | Purpose |
| --- | --- | --- |
| `user-v1-issue-claim` | Validated Supabase user session | Authorize a dog/collar relationship and mint a short-lived, one-use claim code. |
| `device-v1-claim` | Claim code plus pre-persisted candidate device identity/credential | Atomically consume the code and register the collar-generated credential digest. |
| `device-v1-sync` | Unique device credential | Submit a bounded batch and receive ACKs, server time/anchors, and desired configuration in one retry-safe exchange. |
| `device-v1-revoke` | Unique device credential | Idempotently revoke that credential and collar cloud link during normal device-initiated unlink. |

These names are the accepted boundary, not evidence of implementation. There is no cloud route in the current firmware or deployment.

The device gateway, not the collar, owns privileged database access. Device functions use custom credential verification and therefore must not mistake a Supabase `anon`/publishable key for device authentication. Any function configured without platform JWT verification must authenticate and authorize the device before parsing or acting on the privileged body. User endpoints validate the user session and ownership separately.

### Device identity, claim, and revocation

- A collar has a random public UUID and a random 256-bit device credential. Generate credentials with the ESP32 cryptographic RNG only while a documented entropy source is active; do not derive them from MAC address, chip ID, email, PIN, or claim code.
- The website account password is never entered into or stored on the collar. Pairing uses 80 random bits encoded as 16 unambiguous Crockford Base32 characters, a 900-second maximum lifetime, at most five failed consume attempts, and atomic one-use semantics. The server stores only `HMAC-SHA-256(versioned_claim_pepper, canonical_code)`.
- The plaintext device credential persists only on the collar. It is necessarily visible transiently to the Edge gateway in the verified-TLS claim body and later sync/revoke Authorization headers, solely to authenticate and compute/verify its digest; it is never stored or logged server-side. The server stores `HMAC-SHA-256(versioned_device_pepper, credential)`. Claim and device peppers are independent, live in Edge Function secret storage, and support versioned rotation.
- Each authenticated request carries a unique request ID. The gateway rejects a reused ID with a different body hash and returns the stored result for an identical replay.
- Credentials are independently revocable and rotatable. Account password resets and website session expiry do not brick an already paired collar.
- Normal AP unlink enters `REVOKE_PENDING`: stop telemetry/config exchange other than `device-v1-revoke`, retain the credential plus a stable request ID/reason, and resend the exact bounded revocation request. One transaction revokes that credential/collar cloud link and stores its idempotent receipt before responding. A schema-valid `200` returns `disposition: newly_revoked|already_revoked`; exact replay returns the persisted original logical response, while a prior website/different-request revoke authenticates through the revoke-only tombstone and returns a new receipt with `already_revoked`. Only either valid disposition for the pending request authorizes local erasure. Generic `401`/`403`, timeout, malformed, or lost responses do not. A forced offline clear must warn the user to revoke the device from the website because a copied credential may still work. Website-side revocation remains a separately authenticated user/RLS/service operation.

### Database privilege boundary

- `private` contains device credential digests, claim codes, and internal request receipts and is not an exposed Data API schema.
- Browser-accessible application tables use an explicitly exposed `api` schema, narrow grants, membership-based Row Level Security (RLS), and authenticated user sessions.
- Device Edge Functions call only narrow, service-only database functions. When a function must be callable through the Data API, its wrapper may live in `api`, but execution is revoked from `public`, `anon`, and `authenticated` and granted only to the service role. A `security definer` function is used only when required and fixes `search_path = ''` while schema-qualifying every object.
- Supabase secret/service-role keys stay in server-side function secrets. The browser receives only a publishable key and remains constrained by grants and RLS. The collar receives neither.

This explicit-grant rule is important for new Supabase projects: tables are no longer assumed to be automatically exposed, and the implementation must test the actual hosted project rather than relying on an old default.

### Stable hostname policy

- Lab phases may call the default development Supabase hostname, recorded as non-field firmware configuration.
- Before any field/production firmware is distributed, the project must own and validate a stable hostname such as `api.<owned-domain>` and point it directly to the Supabase custom domain. The canonical device path is `https://api.<owned-domain>/functions/v1/<operation>`.
- Vercel hosts the human web application on a separate hostname. It is not a reverse proxy for routine collar traffic.
- Firmware treats scheme/host and versioned paths as separate, reviewable configuration. It verifies hostname and certificate chain against an maintained CA bundle; certificate verification can never be skipped in a release build.
- Changing providers keeps the public hostname and changes DNS/backend routing after a staged compatibility test. Endpoint/path or protocol incompatibility still requires a versioned migration; DNS is not a wire-contract substitute.

## Consequences

### Positive

- A stolen website password does not become a fleet device secret, and extracting one collar does not reveal a global key.
- Device ingestion has one bounded, auditable choke point instead of broad PostgREST access.
- The website and device can evolve/deploy independently.
- An owned hostname reduces permanent coupling to one Supabase project.
- Offline behavior remains the mandatory product baseline.

### Costs and limits

- A Supabase custom domain is a paid operational dependency and requires owned DNS, certificate/DNS validation, and renewal monitoring.
- The custom-auth Edge Functions are Internet-facing and require rate limits, body limits, replay protection, redacted logs, and revocation runbooks.
- ESP32 TLS, entropy, clock bootstrap, retry, and credential persistence require physical failure testing.
- Stable DNS does not provide offline service and does not remove the need for versioned protocols.

## Rejected alternatives

- **Store account username/password on the collar:** excessive privilege, difficult rotation, and unsafe physical extraction semantics.
- **Embed a Supabase publishable/anon key as device identity:** public keys identify the project, not a unique device.
- **Embed a Supabase secret/service-role key:** compromise of one collar would bypass RLS across the project.
- **Let the collar call PostgREST tables/RPC broadly:** expands the device attack surface and distributes authorization logic.
- **Proxy all device traffic through Vercel:** adds a second runtime dependency and makes frontend deployment behavior part of device ingestion.
- **Flash the provider-owned project hostname for field use:** makes project/provider migration require firmware replacement.
- **Disable TLS verification to tolerate bad clocks:** makes location, credentials, ACKs, and configuration vulnerable to interception and tampering.

## Implementation and acceptance gates

This ADR accepts the architecture; it does **not** mark cloud support implemented. Before Phase 3 field use, evidence must show:

1. claim code expiry, one-use consumption, brute-force throttling, and concurrent-claim behavior;
2. identical replay returns one result, while same request ID/different hash is rejected;
3. revoked, wrong-collar, malformed, oversized, and expired credentials fail without secret/location logging;
4. cross-user RLS and service-function execution attacks fail in the hosted project;
5. TLS succeeds only for the intended hostname/chain and fails for bad time, hostname, chain, and interception;
6. a DNS/custom-domain migration rehearsal preserves a previously flashed field URL;
7. cloud-disabled, DNS failure, and prolonged outage do not regress local operation.

## References

- [Supabase Edge Functions architecture](https://supabase.com/docs/guides/functions/architecture)
- [Supabase Edge Function secrets](https://supabase.com/docs/guides/functions/secrets)
- [Supabase Data API hardening and exposed schemas](https://supabase.com/docs/guides/api/securing-your-api)
- [Supabase Row Level Security](https://supabase.com/docs/guides/database/postgres/row-level-security)
- [Supabase custom domains](https://supabase.com/docs/guides/platform/custom-domains)
- [Espressif ESP-TLS server verification](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_tls.html)
- [Espressif random-number generation and entropy conditions](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/random.html)
- [NISTIR 8259A, IoT Device Cybersecurity Capability Core Baseline](https://csrc.nist.gov/pubs/ir/8259/a/final)
