# Phase 0 PostgreSQL telemetry capacity evidence

**Status:** Measured locally; accepted as Phase 0 sizing evidence, not a hosted
Supabase service-level benchmark.  
**Measured:** 2026-08-13.  
**Fixture:** one collar, 1,000,000 semantically valid Track v3-shaped observations.  
**Runner:** [`tools/cloud_capacity/run.ps1`](../../tools/cloud_capacity/run.ps1).

## Decision

The initial database can use an ordinary, unpartitioned telemetry table with a
composite primary key and one collar/time B-tree. Do **not** create the PostGIS
GiST index until a measured product query needs spatial filtering. Route pages
are collar/time/sequence queries and do not need it.

At the measured 341.50 bytes per point with all three indexes, a continuously
running five-second profile is too large for a 500 MiB development database.
The adaptive profile nearly consumes that capacity in one collar-year before
Auth, configuration, summaries, receipts, dead tuples, and safety margin. Phase
1 may use the free tier for short synthetic development, but field history needs
a paid/monitored capacity decision and an explicit retention setting.

Partitioning is rejected for Phase 1. One million rows perform comfortably on
the representative access paths, and the project is far below the scale where
partition operations repay their complexity. Revisit after real growth and
query evidence, not by calendar date.

## Reproduction environment

| Component | Value |
| --- | --- |
| Host | Windows 11 Pro 10.0.26100 |
| CPU | AMD Ryzen 9 5900X, 12 cores / 24 logical processors |
| Host memory | 63.9 GiB |
| Docker engine | 29.6.1 |
| Image | `public.ecr.aws/supabase/postgres:17.6.1.158` |
| PostgreSQL | 17.6 |
| PostGIS | 3.3.7 |
| Container CPU/RAM cap | none; Docker Desktop defaults apply |
| `shared_buffers` | 128 MiB |
| `effective_cache_size` | 128 MiB |
| `work_mem` | 4 MiB |
| Parallel workers/gather | 2 |

These numbers are comparative evidence on one development machine. Hosted
latency, concurrent RLS evaluation, network transit, autovacuum, and service
contention must be measured separately in Phase 1.

## Fixture shape

The fixture uses the proposed logical columns:

- collar UUID, boot and point sequences;
- recorded/received UTC timestamps;
- exact latitude/longitude E7 integers and PostGIS `geography(Point,4326)`;
- integer speed, satellite count, flags, time quality;
- telemetry/firmware versions and chunk sequence;
- primary key `(collar_id, boot_sequence, point_sequence)`;
- B-tree `(collar_id, recorded_at, point_sequence)`;
- partial GiST on non-null position for comparison.

The generated route oscillates around Bogotá and deliberately alternates
stationary/moving values. It is synthetic load data, not a behavior model.

## Measured storage

| Item | Exact bytes | Approximate size |
| --- | ---: | ---: |
| Table heap | 174,301,184 | 166 MiB |
| Composite primary key | 49,741,824 | 47 MiB |
| Collar/time B-tree | 49,676,288 | 47 MiB |
| Partial position GiST | 67,698,688 | 65 MiB |
| Representative accepted-run total | 341,499,904 | 326 MiB |
| Total per point across clean loads | 341.39–341.76 | — |

Clean runs varied by about 0.11% in total size because GiST/index page layout
is not byte-deterministic across loads. Without the unused GiST index, this
fixture would occupy approximately 273.80 bytes per point, saving about 19.8% of the measured total. This does not
mean the `position` column itself is free; it means its additional index should
be justified by a real spatial access path.

### One-collar annual extrapolation

| Profile | Points/day | Points/year | Measured total/year with all indexes |
| --- | ---: | ---: | ---: |
| Continuous 1 s | 86,400 | 31,536,000 | 10.03 GiB |
| Continuous 5 s | 17,280 | 6,307,200 | 2.01 GiB |
| Continuous 15 s | 5,760 | 2,102,400 | 684.7 MiB |
| Continuous 60 s | 1,440 | 525,600 | 171.2 MiB |
| Adaptive: 4 h at 5 s + 20 h at 60 s | 4,080 | 1,489,200 | 485.0 MiB |

### Multi-collar annual extrapolation

The following uses the measured 341.39–341.76 bytes/point range and therefore
includes the deliberately optional GiST index. Values are capacity-planning
estimates, not a billing quote or proof of linear growth.

| Cadence | 1 collar | 5 collars | 10 collars |
| --- | ---: | ---: | ---: |
| Continuous 1 s | 10.03–10.04 GiB | 50.15–50.19 GiB | 100.30–100.38 GiB |
| Continuous 5 s | 2.01 GiB | 10.03–10.04 GiB | 20.06–20.08 GiB |
| Continuous 15 s | 684.7–685.2 MiB | 3.34–3.35 GiB | 6.69–6.70 GiB |
| Continuous 60 s | 171.2–171.3 MiB | 856.0–856.7 MiB | 1.67 GiB |

The extrapolation is deliberately conservative: it uses the one-million-row
average and does not claim linearity forever. It also excludes every non-point
table, database overhead, churn, backups, and headroom.

## Measured representative queries

| Query | Returned/scanned shape | Execution time | Observed plan |
| --- | --- | ---: | --- |
| One local day aggregate | 17,280 points | 5.96–11.40 ms | collar/time index scan or bitmap scan |
| Thirty-day aggregate | 518,400 points | 62.92–119.92 ms | parallel sequential scan selected by planner |
| Recording/keyset page | 2,000 points | 0.71–1.65 ms | composite PK index scan |
| Bogotá bounding box | 91,338 matches | 27.53–50.63 ms | partial GiST bitmap scan |
| Exact duplicate ingest | 384 conflicts | 0.77–2.01 ms | primary-key conflict arbiter; zero inserts |

These ranges are the observed snapshots from four clean, single-user container
loads. They are not latency SLOs. Docker restart resets PostgreSQL shared
buffers, but the Windows/Docker host page cache was neither measurable nor
safely controllable, and `VACUUM (ANALYZE)` itself warms pages. The runs are
therefore intentionally **not** labelled cold/warm evidence. They do show that
the proposed keys support the intended day/month/recording/bbox paths and
idempotency at this scale. Phase 1 must add controlled repeated runs, RLS,
concurrency, and hosted-network measurements before any SLO or paid tier choice.

## Serialization and egress evidence

PostgreSQL sampling produced averages of 53.76 bytes for the proposed compact
tuple and 435.72 bytes for a fully named database-row JSON representation. The
separate deterministic Node measurement includes a realistic response envelope:

| Profile | Representation | JSON | gzip level 6 | gzip/point |
| --- | --- | ---: | ---: | ---: |
| 17,280-point five-second day | compact tuples | 850,751 B | 217,438 B | 12.58 B |
| 17,280-point five-second day | named objects | 2,060,351 B | 262,281 B | 15.18 B |
| 4,080-point adaptive day | compact tuples | 200,030 B | 54,299 B | 13.31 B |
| 4,080-point adaptive day | named objects | 485,630 B | 64,804 B | 15.88 B |

Compact tuples materially reduce uncompressed transfer and browser parsing even
when compression narrows the byte difference. Device uploads remain bounded to
the protocol's 384-point/128 KiB ceiling; web route responses use pagination or
bounded time windows and compression.

## Phase 1 acceptance consequences

1. Keep the composite PK and collar/time B-tree from the logical plan.
2. Index every foreign key introduced by the final schema.
3. Omit GiST initially unless the accepted route endpoint contains an actual
   bounding-box/spatial predicate.
4. Keep keyset pagination; never introduce deep `OFFSET` history pagination.
5. Batch ingest inside the single short sync transaction and use unique
   constraints/`ON CONFLICT` for retry safety.
6. Keep daily summaries so common history pages do not scan raw month ranges.
7. Load-test with RLS, multiple collars, concurrent ingest/read, and hosted
   network latency in Phase 1 before setting SLOs.
8. Re-run this fixture after the Phase 1 migration is written; fail the capacity
   gate if total bytes/point grows more than 20% without an accepted reason.
