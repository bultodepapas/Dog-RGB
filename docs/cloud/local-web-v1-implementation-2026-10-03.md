# Local web v1 — implementation and acceptance

**Owner:** Codex implementation/review; repository owner retains deployment and operational decisions.

**Scope:** optional Next.js portal, local Supabase and synthetic device simulator. Initial work started from `8d93abb`; the previous handoff was based on `b038139`. Receipt changes were subsequently committed as `6444fda`; the Windows resumption started from `bda635f` and was committed as `b1ba45b`. The latest acceptance increment is based on `b1ba45b`. This record supplements, and does not rewrite, the dated M1.13–M1.16 and firmware evidence. The [master plan](../PLANS/2026-08-13_web-platform-bidirectional-sync-plan.md) remains the only backlog.

**Acceptance — 2026-10-03:** local implementation and automated acceptance are complete. The results below supersede the stop-point status only for their explicitly tested scope. M1.17 human accessibility acceptance remains pending. No external project, production schedule or physical device was activated.

## Resumption validation — Windows

Owner: Codex, with Luna/max harness review and implementation. Implementation: `b1ba45b` records the first resumption below; subsequent interaction changes remain in the working tree. Environment: Windows x64, Node **24.18.0** via `npm exec --yes --package=node@24.18.0 --`, npm **11.6.2**, Next.js **16.3.8**, Supabase CLI **2.113.0**, Playwright **1.62.1**; disposable local `Dog-RGB-1` and synthetic data only.

| Command after the Node wrapper | Result |
| --- | --- |
| `npm run phase1:check` | PASS: assets/contracts, lint, types, **243 distinct unit tests**, secret scan |
| `node tools/portal-e2e/run.mjs --clean --core-only` | PASS, **571 s** including production build and cleanup: two clean cycles each of owner (20 checkpoints), authorization, ten fault scenarios and eight privacy/cache checkpoints |
| `npm run phase1:local -- --clean` | PASS: fresh migrations/types/pgTAP/advisors, summary/configuration/revocation concurrency, **retention/deletion fence now executed in composition**, 49 gateway boundary scenarios, browser pairing, 41 simulator scenarios, isolated restore/tombstone replay and five-batch deletion |
| `node tools/portal-e2e/run.mjs --clean --quality-only` with `PORTAL_E2E_PORT=3107` | PASS, **568 s**, including rebuild/cleanup: 60 axe/layout checks plus four completed-receipt viewports, **220 performance navigations**, **11 WebKit checks**, five dog-lifecycle and eleven account-lifecycle checkpoints, final server/Edge/database log privacy |
| `node tools/portal-e2e/run.mjs --clean --interaction-only` with `PORTAL_E2E_PORT=3107` | PASS, **175 s**, including rebuild/cleanup: 60 axe/layout checks plus four completed-receipt viewports, **28 keyboard traversals across 14 routes at 320/1280 px**, authorized deep-link return, **seven product states, six summary-replay checkpoints, four export-recovery checkpoints**, 11 WebKit checks, five dog-lifecycle and **13 account-lifecycle checkpoints**; privacy/cache/runtime-log gates pass. Performance was not repeated |

Quality environment: Chromium **151.0.7922.34**, WebKit **26.5**, AMD Ryzen 9 5900X. Maximum per-group median LCP **724 ms**, TTFB **156.3 ms**; maximum sample CLS **0.007394**, initial JS gzip **147,721 bytes**. All original budgets pass. The first quality launch stopped at preflight because another project owned port 3000; no process was stopped. Focused gates now accept a validated loopback port and configure the exact portal origin; owner email gates retain port 3000.

Receipt proof: fresh password preparation sets the HttpOnly/Strict request cookie; finalization commits and its response is deliberately lost; the first receipt response is also lost, leaving an honest unknown state. The recovery page returns the exact original receipt. Losing the acknowledgement response preserves browser cookies; reloading repeats the receipt, then a successful acknowledgement clears recovery/Auth cookies. Cross-subject receipt reads and mismatched request IDs are denied; the old signed session cannot finalize after Auth deletion. Durable counts show zero owned dog/Auth rows. The completed receipt passes accessibility/layout checks at 320/428/768/1280 px. No application, Edge, migration or dependency change was needed for this increment; the obsolete harness was corrected. An independent Luna/max review found no actionable harness/privacy issue.

Receipt acceptance now also covers a genuinely expired signed JWT: a control signed by the same disposable Auth key returns the exact receipt (200); changing only `exp` to the past returns 401 without a receipt. The helper verifies the source signature/project/container/issuer and keeps keys/tokens in memory. Five browser responses—truncated JSON, missing hash, pending status, invalid date and extra field—leave completion unconfirmed, send no acknowledgement and preserve the recovery cookie. Keyboard retry and error focus pass; subsequent valid recovery, reload and acknowledgement still complete. Sanitized artifacts are under `output/playwright/interaction`; earlier performance evidence remains under `output/playwright/quality`.

Additional local evidence: an anonymous recording deep link returns through login to visible recording points. Seven product-state assertions cover claim expiry, stale Today/configuration, rejected unsupported brightness, a real RPC timeout with same-value retry, and viewer read-only controls. The summary fixture ingests three points, repeats the exact request without duplication (**3 → 3**), computes a visible summary, ingests three late points (**6** total), observes pending state and a changed computed metric after the next bounded batch; its temporary dog is removed. JSON/GeoJSON signed-session controls return private downloads; a failed transport is followed by a complete browser download, and both routes reject genuinely expired sessions with 401 and no attachment. The same unusable refresh token is used in valid/expired controls. Reports contain fixed stages/counts/flags only and are wired into the existing CI artifact list.

Remaining local acceptance: named human keyboard/focus/reduced-motion assertions. Remote CI, hosted preview and physical acceptance remain separate. No further local product feature is left open in M1E/M4 core/M5A.

The historical `today-projection` / `brightness-submit` failure did not reproduce in either clean owner cycle; no product fix or root-cause claim. Failed owner runs now retain bounded phase/category/numeric React-code/HTTP-status diagnostics without raw messages, stacks, URLs or bodies. Focused gates preserve artifacts for suites they do not execute and clear their own quality directory before a new run.

### Interaction validation

The first interaction-focused run stopped after **174 s** at `cancel returns focus`, after all 60 axe/layout checks and 28 keyboard traversals passed. The component schedules focus with `requestAnimationFrame`; the harness read focus immediately. Open/cancel and receipt-error focus assertions now use bounded Playwright focus polling; the clean rerun passed. No product behavior was changed for this correction. Independent Luna/max review found no additional logic/privacy defect; initial entry through the recovery link remains a pointer check, while error retries use actual Tab/Enter.

The first expanded product-state run stopped after **129 s** at its stale fixture: moving only `last_sync_at` violated the existing diagnostic snapshot constraint. The fixture now shifts/restores synchronization, diagnostic and oldest-outbox timestamps together. The database constraint was preserved.

The next run stopped after **133 s**: an expired-session fixture did not establish the expected brightness retry state. That case now reuses M1.15's scoped lock-session helper: a collar row lock causes the real RPC to reach its existing statement deadline while reads/session stay valid, then rollback releases the lock before the same-value retry. The final 175 s clean run passes both corrected fixtures and all downstream gates. Signed-session expiry remains covered separately at both export routes and receipt recovery.

### Human accessibility acceptance still required

M1.17 explicitly requires named manual assertions. Automated browser input and Codex/DevTools visual inspection do not close that boundary. Human reviewer/date: **not recorded**.

| Assertion | Human acceptance |
| --- | --- |
| Tab/Shift+Tab reach entry/recovery, selection and private controls in reading order; skip link reaches main; focus remains visible at narrow width | Pending |
| Revoke Enter/Space/Escape and receipt error/retry/completion announce meaningful status and retain/return focus | Pending |
| At 200% browser zoom, critical controls/content remain readable and reachable; layout at 320/428/768/1280 px does not lose controls | Pending |
| With reduced motion enabled, navigation/state changes remain understandable without motion-dependent information | Pending |

Codex inspected login and receipt-recovery error at 320 px in DevTools using actual Tab/Enter: readable controls, visible focus and retry-to-status focus were observed. Screenshots were ephemeral, not retained artifacts. This is agent review, not a named human sign-off.

The historical results and handoff below remain a record of the earlier Darwin run, not current failures inferred from source presence.

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
- Receipt recovery has **local lost-response/reload/acknowledgement, expired-JWT and malformed-response evidence above**: finalization prepares a fresh reauthenticated session and an HttpOnly request-ID cookie before the destructive call; `/account/deletion-receipt` retries the exact minimal completed receipt. Authorization uses an unexpired signed JWT bound to the requester; the request ID alone grants no access. The Edge verifier uses JWKS, requiring asymmetric signing-key JWTs for this post-deletion read; legacy flat HS256 tokens are unsupported. No new RPC/migration was added for this extension.
- Deletion inventory acquires the telemetry retention fence for every collar, including already-revoked collars, before taking a fresh count snapshot. Fresh requests and restore replay share this boundary. The two-session regression passed separately and now within `phase1:local`: retention deleted one point while uncommitted; deletion waited, captured zero remaining points, and completed in one batch with zero residual rows.
- The existing deletion fixture drill is not a general queue-drain command. Browser harnesses explicitly run the bounded worker for their own synthetic jobs. Hosted retention/deletion/summary schedules remain a release gate.

## Reproduction and historical stop-point evidence

Run the reset suites sequentially against the disposable local project only:

```sh
npm run phase1:check
npm run portal:build
npm run phase1:local -- --clean
npm run phase1:capacity -- --clean
node tools/portal-e2e/run.mjs --clean
```

The earlier run used Node **24.18.0**, npm **11.6.2**, Next.js **16.3.8**, Supabase CLI **2.113.0**, Playwright **1.62.1**, Darwin arm64. The table below is historical; current Windows results are above. Install Chromium and WebKit. The harness excludes optional Studio, image proxy, logs, pooler, Realtime and Storage containers; database, Auth, REST, Edge and Mailpit remain exercised.

| Check | Historical result |
| --- | --- |
| Source contracts, lint, types, unit tests, secret scan | Pre-receipt-extension composed run PASS; **242 distinct tests**: contracts 48, portal 146, analytics 12, simulator 23, tooling 13. Contracts execute twice. After receipt changes, portal lint/types and **147/147 portal unit tests** PASS; the complete composed check has not been repeated |
| Clean local foundation | PASS: **27 pgTAP files / 699 assertions**, generated types, lint/advisors without errors, real summary/configuration/revoke races, **49 gateway boundary scenarios**, browser pairing, **41 simulator scenarios**, dual restore/tombstone replay and concurrent dog deletion in **5 batches**. SQL lint reports five unused local variables; no error-level finding |
| Owner/authorization/fault/privacy matrix | Prior focused owner journey PASS: **20 checkpoints**. Final twice-clean matrix **not passed**: latest core run failed at `today-projection`; later focused run failed at `brightness-submit` after 13 checkpoints. Generic private-area error; root cause not established. An earlier owner + authorization cycle passed, then Edge readiness failed. M1.15 fixture/readiness adjustments await execution |
| Migrated capacity, 1,000,000 points | PASS: exact detail pages **3.951 / 1.805 ms**, both use the primary key without Sort/spill/unrelated telemetry scans; non-member reads **0 points**. Local measurements, not hosted SLOs |
| Production build | PASS, Next.js 16.3.8, **before receipt-recovery changes**; rebuild pending |
| Automated accessibility/performance/WebKit | PASS on the pre-receipt-recovery build: **56** axe/layout/44 px/CSS-zoom checks, **200** navigations, **10** WebKit mobile checks. Max per-group median LCP **684 ms**, TTFB **262 ms**, max CLS **0**, max initial JS gzip **147,031 bytes**. Chromium **151.0.7922.34**, WebKit **26.5**, Apple M1. Recheck the added receipt-recovery surface before final closure |
| Dog/account browser lifecycle | Pre-receipt-extension PASS: dog **5 checkpoints**, account **7 checkpoints**, zero residual dog rows/Auth identities and old-session denial; runtime-log privacy PASS. Current account harness still uses the previous finalization protocol; this result does not accept the new prepare/finalize/receipt/acknowledge flow |
| Hosted/physical/manual acceptance | Not claimed |

Retained browser artifacts contain fixed checkpoints, numeric counts and aggregate metrics only. Auth cookies, tokens, claim codes, passwords, exported routes, HTML, traces and screenshots are not CI artifacts. Test secrets are ephemeral and the runner cleans Mailpit and its temporary files.

## Historical handoff at the stop point

- Receipt changes are in `apps/portal/app/account/finalize/route.ts`, `app/components/account-deletion-panel.tsx`, `app/components/account-deletion-receipt-recovery.tsx`, `app/account/deletion-receipt/page.tsx`, `lib/privacy/account.ts`, `lib/privacy/account.test.mjs` (all under `apps/portal`), and `supabase/functions/user-v1-account-deletion/index.ts`. Preserve the saved implementation; it is not release-accepted. No Edge/Deno check ran because Deno was unavailable in the shell.
- `tools/portal-e2e/account-lifecycle.mjs` still sends the old body and assumes one finalization response. Its endpoint guards/status expectations must be reconciled with the new dispatcher before results can be interpreted. Lost response after committed Auth deletion, reload recovery, wrong requester/request, incomplete receipt, expired JWT and cookie cleanup remain untested.
- The latest sanitized owner result is `output/playwright/m113/cycle-1.json` (`brightness-submit`). Capture browser `pageerror`/console and correlate the failing request before changing timeouts; server output did not identify the cause. The temporary diagnostic was prepared but **not executed** before stopping.
- Quality/lifecycle results under `output/playwright/quality/` describe the earlier build. Add the recovery route to applicable accessibility/privacy/browser coverage, rebuild, then rerun the core two-cycle matrix and focused quality suite sequentially. Human keyboard/focus/reduced-motion review remains separate.
- The M1.15 race fixture now gives each active collar its own dog to respect the one-active-collar constraint; this correction has only a syntax check. Edge readiness now allows a 10 s probe within 90 s; its integration outcome is unverified.
- No project test or Next server was left running at handoff. The disposable local Supabase/Lima stack remains available with synthetic fixtures; the temporary Edge `.env` was removed. No deployment or additional commit was performed for this stop request.

## Remaining release boundaries

1. Complete the named human M1.17 accessibility assertions above. Local implementation/automated gates pass; this does not replace human acceptance.
2. M3A: isolated hosted preview with synthetic simulator data; actual origin/redirect/email setup, grants/RLS/Edge parity, preview performance and rollback.
3. M5B: named operator, intended users/collars, measured limits/cost ceiling, privacy/contact details, finite retention and backup lag, bounded schedules, alerts and demonstrated restore/deletion replay. The current restore drill replays dog tombstones only; post-backup account/Auth deletion replay still needs implementation and proof before restored traffic.
4. M2B/C + M3B/C: physical firmware capture/outbox/TLS/credential persistence and power/network/resource acceptance. This branch remains independent of finishing the portal.

Map tiles, invitations, additional settings, live tracking, rich charts and enterprise infrastructure remain outside core v1. The segmented route and accessible point table remain the default.
