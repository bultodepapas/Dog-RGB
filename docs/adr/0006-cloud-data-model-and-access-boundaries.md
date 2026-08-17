# ADR-0006: Cloud data model and access boundaries

**Status:** Accepted

**Date:** 2026-08-13

**Scope:** Supabase/Postgres ownership model, telemetry shape, temporal/spatial representation, access control, and initial scaling policy.

## Context

The cloud platform has four different kinds of state:

1. user-owned entities and collaboration (`users`, dogs, memberships, collars);
2. append-only observations and recording metadata uploaded by a collar;
3. derived/versioned summaries computed from those observations;
4. security/transport state that no browser or collar may query directly.

The current collar stores compact coordinates and metric summaries. Its existing route format does not carry point timestamps in seconds, point speed, quality, sequence identity, stationary observations, or explicit gaps. Treating it as a complete activity history would create false precision. The database must accept a future versioned telemetry envelope without changing the meaning of legacy data.

Supabase exposes selected schemas through PostgREST and relies on PostgreSQL grants plus Row Level Security (RLS). A service-role client bypasses RLS, so service access must be narrower than “all tables.” Location histories are sensitive and require membership checks on every user-facing path.

## Decision

### Schemas and ownership

Use three ownership boundaries:

| Schema | Contents | Access |
| --- | --- | --- |
| `auth` | Supabase-managed user identities | Supabase Auth; never modified through Dog-RGB tables. |
| `api` | User profiles, dogs, memberships, collars, recordings, accepted telemetry, summaries, configuration state/revisions/reports, and narrow RPC wrappers | Explicit grants and RLS for users; selected service-only functions for Edge gateway work. |
| `private` | Claim-code digests, device credential digests, pepper/key versions, request receipts, ingestion internals, security audit details | No Data API exposure and no `anon`/`authenticated` grants. Edge gateway only through narrow privileged code. |

Do not use `auth.users.raw_user_meta_data` for authorization: users can update it. Authorization comes from normalized ownership/membership rows.

### Core relational model

The Phase 1 schema must preserve these concepts even if exact column names evolve in migrations:

| Relation | Required identity and invariant |
| --- | --- |
| user profile | One row per `auth.users.id`; display/preferences only, never authorization truth. |
| dog | UUID primary key; owner-independent profile; IANA timezone such as `America/Bogota`. |
| dog membership | Unique `(dog_id, user_id)`; role `owner`, `editor`, or `viewer`; at least one owner enforced by a tested transactional path. |
| collar | Stable public UUID, dog association, model/firmware/capability state, pairing/revocation state, last contact/result. |
| recording | Stable device-generated identity; collar, boot/session boundaries, schema version, start/end, final/open status, data-quality metadata. “Recording” is not automatically a walk. |
| telemetry chunk receipt | Unique device/chunk identity, body hash, sequence bounds, commit/ACK facts, schema version. |
| telemetry point | Unique `(collar_id, boot_sequence, point_sequence)` plus recording/chunk references; immutable observation fields. |
| daily summary | Unique `(dog_id, local_date, algorithm_version)`; computed values, coverage, known/unknown durations, source watermark, computation timestamp. |
| configuration current | Unique `(collar_id, resource_key)`; canonical winner envelope, server version, HLC, actor, body hash. |
| configuration revision | Append-only accepted/rejected mutation audit with origin, HLC, prior/current versions and redacted reason. |
| configuration apply report | Desired server version plus device applied/rejected/pending outcome. Desired is not reported/applied state. |
| private claim/credential/request rows | Digests, lifecycle/revocation/tombstone state, rate-limit/audit fields, and idempotency response state; never plaintext credentials. Device revoke changes credential/link state and stores its replay result in one transaction. |

Foreign keys must express ownership and deletion behavior; uniqueness constraints, not application checks alone, enforce idempotency. Use UUIDs for public entity identity and integer/bigint device sequence values for ordering. Do not expose incrementing internal IDs as authorization.

### Exact telemetry representation

- Preserve wire integers (`lat_e7`, `lon_e7`, UTC seconds, `speed_cmps`, sequence counters, flags) as the canonical uploaded evidence. Reject latitude outside `[-900000000, 900000000]`, longitude outside `[-1800000000, 1800000000]`, impossible lengths, and unknown required flags at ingestion.
- After validating the canonical chunk hash, decode the bounded payload transactionally into point rows and retain only chunk identity/header/hash/receipt metadata. Do **not** duplicate successful raw sync request bodies or binary chunks into Postgres `bytea`, Supabase Storage, logs, or tracing. Exact point integers plus the frozen codec can reproduce the canonical point payload when needed.
- Enable PostGIS in a dedicated extension schema and derive/store a WGS84 `geography(Point, 4326)` value for map/spatial operations. The integer evidence remains available for deterministic reprocessing.
- Never create a line across an explicit gap, fix loss, recording boundary, untrusted-time discontinuity, or sequence discontinuity. A rendered route is a set of ordered segments, not one unconditional polyline.
- Uploaded observations are append-only after commit except for an authorized privacy deletion. Corrections happen through new algorithm versions or superseding metadata, not silent point edits.
- Preserve device-reported summaries and cloud-derived summaries as different fields/relations. Differences are diagnostic evidence; do not overwrite one with the other.

Supabase Storage is not part of initial telemetry ingestion. It may later hold short-lived private export artifacts or explicitly added profile media under separate bucket policies/retention tests; it is not a route database and must never become a second uncontrolled copy of location history.

### Time and calendar semantics

- Store event instants as UTC (`timestamptz` or integer UTC seconds on the wire) plus explicit time-quality evidence.
- Each dog has an IANA timezone. Local-day grouping uses that timezone and therefore supports 23-, 24-, and 25-hour civil days. Never divide by or synthesize a fixed 86,400-second local day.
- A timezone change applies forward by default. Historical regrouping is an explicit, audited recomputation because it can move observations between days.
- Current-day denominators end at the current trustworthy instant, not local midnight tomorrow.
- Unknown time remains unknown. Server receipt time may order ingestion but must not be presented as the observation time.

### RLS and grants

- Enable RLS on every user-facing `api` table, including apparently harmless reference rows that contain dog/collar associations.
- Policies derive access through dog membership. Owners administer members/delete; editors mutate dog/config/product state as explicitly allowed; viewers read only. Configuration rights are separate from location-history read rights if a later sharing model needs that distinction.
- Write policies use both `USING` and `WITH CHECK` where applicable so a user cannot move a row into another dog.
- Revoke broad schema/table/function privileges first, then grant only required operations. Set equivalent default privileges so later migrations do not reopen access.
- Keep policy helper functions minimal, stable, and tested for recursion. If `security definer` is necessary, fix `search_path = ''`, schema-qualify names, revoke public execution, and audit arguments inside the function.
- Service-only RPC wrappers exposed for Edge Functions are not user APIs: execution is granted solely to service role and tested from `anon` and ordinary authenticated sessions.

### Initial indexes and scale

Create only evidence-backed indexes initially:

- membership lookups by `(user_id, dog_id)` and `(dog_id, user_id)`;
- collar/device ownership and revocation lookups;
- telemetry uniqueness and ordered retrieval by `(collar_id, boot_sequence, point_sequence)` and/or `(recording_id, observed_at, point_sequence)`;
- chunk/request idempotency uniqueness;
- recording and daily-summary history by dog/time descending;
- configuration `(collar_id, resource_key)` and revisions by collar/time.

Do not add a GiST spatial index until a measured query needs radius/containment search; route playback primarily filters by recording/time and orders points. Do not partition telemetry on day one. The one-million-point fixture and real query/retention evidence must determine whether partitioning or cold storage is justified. Every RLS predicate column and foreign-key child used in hot deletes/joins must be reviewed with `EXPLAIN (ANALYZE, BUFFERS)` under representative ownership distributions.

## Consequences

### Positive

- Database constraints carry replay/idempotency guarantees even if an Edge Function retries.
- Precise raw evidence and versioned derivations allow algorithms to improve without rewriting history.
- Membership-based RLS supports one or more dogs/collars without treating URLs as authorization.
- PostGIS remains available without forcing every route query through a spatial index.
- Sensitive device secrets stay outside the browser-visible schema.

### Costs and limits

- Append-only raw points are storage-heavy and require an enforced retention job.
- RLS joins and service-only wrappers add migrations/tests and need query-plan monitoring.
- Current v2 routes can only be imported with explicit legacy limitations; they cannot populate evidence that was never recorded.
- A service-role bug can bypass RLS, so the gateway and privileged functions remain high-risk code.

## Rejected alternatives

- **One wide JSON document per dog/day:** weak constraints, expensive partial queries, and ambiguous schema/version history.
- **Only PostGIS geometry, discarding wire integers:** loses deterministic wire evidence and complicates exact replay checks.
- **Only one derived summary value:** hides provenance and makes algorithm changes silently rewrite user history.
- **Keep every raw request/chunk blob in Supabase Storage:** duplicates high-sensitivity location, complicates deletion, and adds no evidence beyond exact decoded integers plus chunk hash for the fixed codec.
- **Put credentials in public/API tables protected only by RLS:** unnecessary exposure to configuration mistakes.
- **Rely on application filters instead of RLS:** crafted URLs or clients could cross account boundaries.
- **Partition and spatial-index immediately:** adds operational complexity before query/capacity evidence.

## Implementation and acceptance gates

This ADR is an accepted target, not an implemented database. Phase 1 must provide migrations and automated evidence for:

1. anonymous, cross-user, viewer/editor/owner, deleted-member, and forged-ID access cases;
2. identical and conflicting replay under concurrency;
3. telemetry constraints, sequence/gap semantics, and immutable rows;
4. service-only function execution denial to `public`, `anon`, and `authenticated`;
5. local-day tests around DST zones as well as `America/Bogota`, leap days, timezone changes, and incomplete current days;
6. one-million-point storage/query/egress measurements and query plans before adding partitions/GiST;
7. cascading privacy deletion plus documented backup-retention behavior.

## References

- [Supabase Data API security and schema exposure](https://supabase.com/docs/guides/api/securing-your-api)
- [Supabase Row Level Security](https://supabase.com/docs/guides/database/postgres/row-level-security)
- [Supabase database functions and `security definer` guidance](https://supabase.com/docs/guides/database/functions)
- [Supabase PostGIS extension](https://supabase.com/docs/guides/database/extensions/postgis)
- [PostgreSQL row security policies](https://www.postgresql.org/docs/current/ddl-rowsecurity.html)
- [PostgreSQL multicolumn indexes](https://www.postgresql.org/docs/current/indexes-multicolumn.html)
- [PostgreSQL declarative partitioning](https://www.postgresql.org/docs/current/ddl-partitioning.html)
