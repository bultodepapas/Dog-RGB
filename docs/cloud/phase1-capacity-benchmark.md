# Phase 1 migrated-schema capacity evidence

**Status:** Local migrated-schema gate passed; disposable hosted-project and
concurrent network evidence remain open.  
**Measured:** 2026-08-17.  
**Fixture:** 1,000,000 semantically valid Track v3-shaped observations across
two collars.  
**Runner:** [`tools/cloud_capacity/phase1_run.mjs`](../../tools/cloud_capacity/phase1_run.mjs).

## Outcome

The actual Phase 1 `api.telemetry_points` table remains below the accepted
Phase 0 no-GiST storage baseline plus 20%. It occupies **323.78 bytes/point**,
an 18.25% increase over 273.80 bytes/point. The increase includes the real
generated PostGIS geography and the ingest/reconciliation chunk index.

The first capacity run also found a serious RLS plan defect: the original
policy resolved collar ownership once per candidate point. A 17,280-point day
took 2.35 seconds and a 500,000-point month exceeded the explicit 60-second
statement timeout. Migration `20260818001742_optimize_telemetry_rls.sql`
preserves the collar membership policy but materializes the caller-visible
collar set once as a hashed subplan. With the same fixture, the day query fell
to 11.97 ms and the month query to 328.70 ms.

This is acceptance evidence for the local schema and access paths. It is not a
hosted latency SLO, a billing quote, or authorization to enable raw-data purge.

## Reproduction boundary

The runner requires the explicit `--clean` flag, resets only this repository's
disposable local Supabase database before loading, writes evidence under the
ignored `test-results/capacity/` directory, and resets the database again in a
`finally` path. CI uploads that evidence as a short-lived artifact.

```powershell
npm run phase1:capacity -- --clean
```

Measured database components:

| Component | Value |
| --- | --- |
| PostgreSQL | 17.6, x86-64 Linux |
| PostGIS | 3.3.7 |
| `shared_buffers` | 128 MiB |
| `effective_cache_size` | 128 MiB |
| `work_mem` | 4 MiB |
| Parallel workers/gather | 2 |

The host and Docker allocation are the same local development environment used
by the Phase 0 benchmark. `VACUUM (ANALYZE)` warms data, so the measurements
must not be labelled controlled cold-cache results.

## Storage

| Item | Exact bytes | Approximate size |
| --- | ---: | ---: |
| Table heap | 174,301,184 | 166 MiB |
| Composite primary key | 69,877,760 | 67 MiB |
| Collar/time B-tree | 69,754,880 | 67 MiB |
| Collar/boot/chunk B-tree | 9,764,864 | 9.3 MiB |
| Total | 323,780,608 | 309 MiB |
| Total per point | 323.78 | — |

No GiST index exists. The Phase 0 comparison showed that an unproven spatial
index adds roughly 64–68 bytes/point and would exceed this gate. Ordinary route
retrieval is collar/time/sequence ordered; add GiST only when a product spatial
query and a revised capacity decision justify it.

### One-collar annual raw-point projection

| Profile | Points/year | Estimated raw point table + current indexes |
| --- | ---: | ---: |
| Continuous 1 s | 31,536,000 | 9.51 GiB |
| Continuous 5 s | 6,307,200 | 1.90 GiB |
| Continuous 15 s | 2,102,400 | 0.63 GiB |
| Continuous 60 s | 525,600 | 0.16 GiB |
| Adaptive: 4 h at 5 s + 20 h at 60 s | 1,489,200 | 0.45 GiB |

These are raw table/index projections only. Auth, configuration, recordings,
receipts, summaries, dead tuples, backups, logs, and operational headroom are
additional. Consequently, even one adaptive collar-year does not fit safely in
a 500 MiB development database.

## Query evidence under authenticated RLS

| Query | Rows/result shape | Before RLS fix | After RLS fix | Resulting access path |
| --- | ---: | ---: | ---: | --- |
| Local-day aggregate | 17,280 | 2,349.56 ms | 11.97 ms | collar/time bitmap scan + one hashed visible-collar subplan |
| Thirty-day aggregate | 500,000 available | >60,000 ms, timed out | 328.70 ms | bounded collar scan; one hashed visible-collar subplan |
| Recording keyset page | 2,000 | not accepted | 1.36 ms | composite PK index scan |
| Bogotá bbox, no GiST | 45,667 matches of 500,000 | not accepted | 433.94 ms | collar index then geography filter |
| Non-member exact guessed point | zero visible | not accepted | 0.33 ms | PK index-only scan; RLS removes the row |

The bbox result is diagnostic, not a reason to add GiST: product route pages do
not currently issue bbox SQL. The non-member check uses the exact primary-key
shape available to an attacker through a bounded API. The broader pgTAP matrix
continues to prove anonymous and cross-user isolation independently.

## Retention consequences

- Keep the proposed 12-month raw default as policy input, not as an active
  deletion job. Pairing consent, export/delete propagation, backup-lag copy,
  and destructive-flow tests remain mandatory before purge activation.
- Keep summaries until dog/account deletion as proposed; they are not included
  in the raw-point projection.
- Keep the 30-day sync-receipt proposal inactive until the eventual firmware's
  maximum exact-replay/backlog horizon is measured. Premature cleanup could
  turn a delayed exact retry into a second logical transaction.
- Configure 50/70/85% database and egress alerts before field deployment. The
  local fixture proves query shape, not hosted capacity or network egress.
- Re-run this gate after any telemetry column/index/policy change. A bytes/point
  result above 328.56 fails until an explicit capacity ADR accepts the growth.

## Remaining Phase 1 capacity evidence

The plan also asks for a disposable hosted-project run and concurrent
ingest/read/network measurements. Those require a deliberately disposable
project and credentials and therefore remain open; this runner never follows a
linked project and cannot affect hosted data.
