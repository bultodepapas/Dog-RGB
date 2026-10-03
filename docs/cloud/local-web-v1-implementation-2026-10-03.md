# Local web v1 — implementation and acceptance

**Owner:** Codex implementation/review; repository owner retains deployment and operational decisions.

**Scope:** optional Next.js portal, local Supabase and synthetic device simulator. Based on `8d93abb`; implementation is in the working tree. This record supplements, and does not rewrite, the dated M1.13–M1.16 and firmware evidence. The [master plan](../PLANS/2026-08-13_web-platform-bidirectional-sync-plan.md) remains the only backlog.

**Acceptance:** integration checks in progress. Source implementation is not hosted, physical or manual-accessibility acceptance. No external project, production schedule or physical device was activated.

## Delivered behavior

| Area | Implementation boundary |
| --- | --- |
| Entry and recovery | Zero memberships → creation; one → Today; several → selector. Returning login preserves identity and authorized deep links. Confirmation resend and password recovery use generic responses; callback origin comes from validated `PORTAL_SITE_ORIGIN`, with an exact loopback development fallback |
| Dog profile | Owners can correct the display name. IANA timezone remains visible/read-only; no general settings catalog |
| Collar lifecycle | At most one active collar per dog. A current owner can re-enroll the same revoked device UUID into the same dog with a fresh claim/credential. Previous credentials cannot synchronize or revoke the replacement. History and retention/deletion fences remain; cross-dog transfer is excluded |
| Analytics | Pure v1 rules and bounded SQL producers for daily/recording summaries, with explicit source/computation/window/algorithm provenance. Atomic queue consumption, late data invalidation, unknown/retained-data absence, DST windows and gaps. No behavior or health inference |
| Reading | Today links to latest detail; History filters at most 366 inclusive local dates and preserves keyset pagination/unknown-time recordings. Daily/detail metrics require matching supported provenance; raw receipt time is not observation freshness |
| Export | Owner-only private JSON snapshot and per-recording GeoJSON. Includes available retained data, units, timezone, quality/gaps and summaries; excludes device secrets/internal receipts. Snapshot is one SQL statement, not a sequence of live pages |
| Dog deletion | Exact phrase plus password reauthentication; access/ingest close immediately, existing worker purges records, durable status/retry survives logout/login |
| Account deletion | Explicit owned-dog/member inventory and impact on co-owners; creator references without authorized ownership block before mutation. Pending accounts retain authenticated status/retry but cannot read or mutate dogs. Auth is removed only after linked purges complete |
| Release harness | Existing owner/authorization/fault/privacy runner extended with recovery, re-enrollment, computation, downloads and lifecycle checks; automated accessibility/performance and mobile WebKit smoke; relevant-path/release CI wiring |

## Bounded contracts

- Export v1 refuses the entire download above **25,000 points, 50,000 rows or 16 MiB**; it never labels truncated data complete. Collection caps also apply: **100 collars; 10,000 each recordings, loss markers, recording summaries and config revisions; 5,000 daily summaries; 1,000 each config heads and reports**. Capability manifests, config bodies and phase-duration JSON have additional **8 MiB aggregate / 64 KiB per-field** guards. A recording above the same applicable limits also fails; larger asynchronous exports are outside this implementation. The authenticated DB role has an **8 s** statement timeout; the DAL aborts after **15 s**. A function-local timeout is not a substitute for the caller's statement deadline.
- Re-enrollment preserves pending old-outbox identities; the old credential remains invalid. Exact already accepted chunks can replay under the replacement credential; retention/deletion fences still prevent resurrection. Physical credential persistence and outbox recovery remain M3C.
- Summaries process at most **four dirty days per manual transaction** under a caller-enforced **10 s** timeout. A day can require several recording batches. [Worker instructions](../../tools/cloud_analytics/README.md) define resumption and finite batch exhaustion; [analytics rules](../../packages/analytics/README.md) define units and exclusions.
- Account deletion requires a verified password AMR no older than **five minutes**, plus a live Auth identity. Its confirmation covers all owned dogs, including other members' access. Viewer/editor memberships detach; unresolved creator references do not transfer automatically.
- Deletion inventory acquires the telemetry retention fence for every collar, including already-revoked collars, before taking a fresh count snapshot. Fresh requests and restore replay share this boundary. The two-session regression passed: retention deleted one point while uncommitted; deletion waited, captured zero remaining points, and completed in one batch with zero residual rows. This regression is now invoked by `phase1:local`.
- The existing deletion fixture drill is not a general queue-drain command. Browser harnesses explicitly run the bounded worker for their own synthetic jobs. Hosted retention/deletion/summary schedules remain a release gate.

## Reproduction and evidence

Run the reset suites sequentially against the disposable local project only:

```sh
npm run phase1:check
npm run portal:build
npm run phase1:local -- --clean
npm run phase1:capacity -- --clean
node tools/portal-e2e/run.mjs --clean
```

The runtime is Node **24.18.0**, npm **11.6.2**, Next.js **16.3.8**, Supabase CLI **2.113.0**, Playwright **1.62.1**, Darwin arm64. Install Chromium and WebKit. The harness excludes optional Studio, image proxy, logs, pooler, Realtime and Storage containers; database, Auth, REST, Edge and Mailpit remain exercised.

| Check | Current result |
| --- | --- |
| Source contracts, lint, types, unit tests, secret scan | PASS; **242 distinct tests**: contracts 48, portal 146, analytics 12, simulator 23, tooling 13. Contracts execute twice in the composed command |
| Clean local foundation | PASS: **27 pgTAP files / 699 assertions**, generated types, lint/advisors without errors, real summary/configuration/revoke races, **49 gateway boundary scenarios**, browser pairing, **41 simulator scenarios**, dual restore/tombstone replay and concurrent dog deletion in **5 batches**. SQL lint reports five unused local variables; no error-level finding |
| Owner/authorization/fault/privacy matrix | Focused owner journey PASS: **20 checkpoints**, including recovery/resend, real recomputation, downloads and re-enrollment; final twice-clean combined matrix pending |
| Migrated capacity, 1,000,000 points | PASS: exact detail pages **3.951 / 1.805 ms**, both use the primary key without Sort/spill/unrelated telemetry scans; non-member reads **0 points**. Local measurements, not hosted SLOs |
| Production build | PASS, Next.js 16.3.8 |
| Automated accessibility/performance/WebKit | PASS on the pre-receipt-recovery build: **56** axe/layout/44 px/CSS-zoom checks, **200** navigations, **10** WebKit mobile checks. Max per-group median LCP **684 ms**, TTFB **262 ms**, max CLS **0**, max initial JS gzip **147,031 bytes**. Chromium **151.0.7922.34**, WebKit **26.5**, Apple M1. Recheck the added receipt-recovery surface before final closure |
| Dog/account browser lifecycle | PASS: dog **5 checkpoints**, account **7 checkpoints**, zero residual dog rows/Auth identities and old-session denial; runtime-log privacy PASS. Lost-final-response receipt recovery is being added and needs its own acceptance |
| Hosted/physical/manual acceptance | Not claimed |

Retained browser artifacts contain fixed checkpoints, numeric counts and aggregate metrics only. Auth cookies, tokens, claim codes, passwords, exported routes, HTML, traces and screenshots are not CI artifacts. Test secrets are ephemeral and the runner cleans Mailpit and its temporary files.

## Remaining release boundaries

1. Finish local acceptance above and record every failed gate before closure; do not replace measured results by checkboxes.
2. M3A: isolated hosted preview with synthetic simulator data; actual origin/redirect/email setup, grants/RLS/Edge parity, preview performance and rollback.
3. M5B: named operator, intended users/collars, measured limits/cost ceiling, privacy/contact details, finite retention and backup lag, bounded schedules, alerts and demonstrated restore/deletion replay. The current restore drill replays dog tombstones only; post-backup account/Auth deletion replay still needs implementation and proof before restored traffic.
4. M2B/C + M3B/C: physical firmware capture/outbox/TLS/credential persistence and power/network/resource acceptance. This branch remains independent of finishing the portal.

Map tiles, invitations, additional settings, live tracking, rich charts and enterprise infrastructure remain outside core v1. The segmented route and accessible point table remain the default.
