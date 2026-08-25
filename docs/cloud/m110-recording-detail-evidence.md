# M1.10 Recording-detail evidence

**Captured:** 2026-08-25 (America/Bogota)

**Implementation:** `0494fb29de8c1962b63ea65fe099dee5e69cb649`

**Scope:** local recording-detail, RLS, query-plan, browser, and accessibility evidence; not a hosted latency SLO or physical-collar route acceptance

## Frozen product decisions

- The plain preview is a server-generated orientation aid for the current 100-row page. The table is authoritative; no external map, tile, geocoding, analytics, client fetch, polling, or Realtime path exists.
- The local continuity threshold is exactly 65 seconds: `delta <= 65 s` may connect and `delta > 65 s` starts a new segment. The 60-second stationary cadence plus one 5-second moving cadence supplies bounded simulator jitter headroom. M2 must revalidate this value against physical adaptive-cadence jitter.
- Explicit `GAP`, an invalid fix, a nonconsecutive sequence, a trusted/untrusted time transition, the greater-than-65-second threshold, and every page boundary break geometry. Equal known timestamps and consecutive unknown timestamps may remain in one segment. Malformed, future, or regressing known timestamps fail the page closed.
- `point_count` and first/last sequence bounds are historical recording metadata, not a promise that raw points remain after retention. A nonzero count with both bounds null is accepted and performs no point query. Sparse or empty pages do not manufacture loss, activity, coverage, or duration.
- Each keyset page is a fresh RLS read, not a frozen whole-recording snapshot. The fixed fixture proves no duplicates or skipped fixture rows; a point backfilled at or before an already-consumed cursor is outside that guarantee and must not be described as snapshot-consistent.

## Authorization and bounded-query result

The page uses one request-scoped user client for fresh Auth verification, dog membership, recording/collar identity, and the point page. The recording query filters through the collar's exact dog and does not filter collar state, so active, pending, retired, and revoked history remains available. The point projection contains only:

```text
point_sequence, recorded_at, lat_e7, lon_e7,
reported_speed_cmps, satellites, flags, time_quality
```

Point reads match one recording's `collar_id`, `boot_sequence`, and inclusive validated first/last sequence bounds; they order by sequence and stop at the 100 visible rows plus one lookahead. A malformed or out-of-range `after` renders bounded recovery metadata and performs no point query.

The focused pgTAP fixture passed 25/25 assertions. The raw local REST probe returned:

| Identity | Recording rows | First point rows | Deep point rows | Result |
| --- | ---: | ---: | ---: | --- |
| Owner | 1 | 101 | 5 | PASS |
| Viewer | 1 | 101 | 5 | PASS |
| Non-member | 0 | 0 | 0 | PASS |
| Anonymous | denied (`401`) | denied | denied | PASS |

## One-million-point capacity result

The existing primary key is `(collar_id, boot_sequence, point_sequence)`. No migration, RPC, view, or dependency was needed. Under the authenticated owner/RLS fixture, the exact first and deep M1.10 projections passed the automatic gate:

| Page | Execution | Access path | Sort/spill/unrelated point scan | Result |
| --- | ---: | --- | --- | --- |
| First 101 rows | 0.465 ms | `telemetry_points_pkey` index scan | None | PASS |
| Deep 101 rows after sequence 40,000 | 0.426 ms | `telemetry_points_pkey` index scan | None | PASS |

The complete migrated fixture measured 323.79 bytes per point and remained below the existing Phase 1 capacity ceiling. These are local acceptance measurements on the repository's disposable stack, not predictions for a future hosted region.

## Portal and browser result

- 90/90 portal tests passed, including exact authorization order, strict cursor recovery, 100+1 lookahead, retained/sparse metadata, all continuity boundaries, malformed evidence, and adapter/view scope assertions.
- The clean `npm run phase1:local -- --clean` gate passed 394 pgTAP assertions, 90 portal tests, 49 adversarial Edge scenarios, schema lint/advisors, type drift, restore, deletion, and capacity checks with Node `24.18.0`, npm `11.6.2`, Supabase CLI `2.113.0`, and PostgreSQL `17.6`.
- The production Next.js build passed; the leaf remained dynamic and returned `Cache-Control: private, no-store`. Next.js compilation, runtime-error, and metadata inspection found no issue.
- Browser traversal rendered 100 rows on page one and five on the tail page, recovered from noncanonical `after=01` without a point read, emitted exactly one descriptive non-prefetched History detail link per row, and made no external route request or idle polling request.
- At 320, 428, 768, and 1280 CSS pixels the document had no horizontal overflow; all nine table columns remained in the labelled keyboard-scroll region. Keyboard horizontal scrolling worked.
- Desktop and mobile Lighthouse snapshots scored 100 for Accessibility, Best Practices, SEO, and Agentic checks. Semantic table/SVG inspection, focus visibility, language/title, target size, and reduced-motion review passed.

## Reproduction

```powershell
$env:Path = '<checksum-verified Node 24.18.0 directory>;' + $env:Path

npm run phase1:check
npm run phase1:local -- --clean
npm run portal:build

# Optional disposable raw REST/browser fixture after a clean reset:
Get-Content -Raw tools/cloud_capacity/m110_recording_detail_fixture.sql |
  docker exec -i supabase_db_Dog-RGB-1 psql -X -v ON_ERROR_STOP=1 -U postgres -d postgres

$status = npx --yes supabase@2.113.0 status -o json 2>$null | ConvertFrom-Json
$env:SUPABASE_URL = $status.API_URL
$env:SUPABASE_ANON_KEY = $status.ANON_KEY
node tools/cloud_capacity/m110_recording_detail_rest_probe.mjs

npx --yes supabase@2.113.0 db reset
```

The fixture and probe reject or avoid hosted use; reset the disposable local database after the optional proof. Do not commit raw route payloads, credentials, or generated capacity output.
