# M1.16 cross-surface privacy and cache evidence

**Captured:** 2026-08-26 (America/Bogota)

**Implementation:** local M1.16 commit created with this evidence; its hash is reported in Git history

**Scope:** deterministic inspection of the current local portal/browser/static/cache/log/artifact surfaces against synthetic M1.14 identities and M1.7 device/claim secrets; not a hosted-log audit, generic penetration test, third-party scanner, accessibility run, performance run, deployment, firmware proof, or production privacy certification

## Frozen leak contract

M1.16 reuses the existing two-owner authorization fixture. Each independent cycle creates two owners, two dogs, two paired simulator collars, consumed raw claim codes, device credentials, recordings, and brightness state. Claim codes, device credentials, user tokens, passwords, and local infrastructure secrets stay in the orchestrator process and are compared directly against observed content; they are never exported to the browser process, command line, retained report, trace, screenshot, video, or HAR.

The gate rejects:

- any exact claim, device credential/bearer, fixture user token/password, service-role/secret/JWT/database credential, or known internal-error signature outside its necessary bounded request boundary;
- owner B's email or dog name in anonymous or owner A response content;
- the seeded exact coordinate integers or rendered coordinate strings outside owner A's authorized recording-detail HTML/RSC;
- a protected document/RSC response without `private, no-store`, or a Server Action/`Set-Cookie` response without non-public `no-store`;
- unexpected browser storage, cookies outside the Supabase SSR auth-cookie namespace, console/page errors outside the one consumed generic cross-owner 404, workers, service workers, WebSockets, analytics, or non-loopback browser requests;
- a retained HAR, trace, image, video, or other rich Playwright artifact.

The one intentional exception is the authorized recording-detail table: its three owner route observations are required for the accessible non-map fallback. The exact HTML and explicit RSC response are accepted only on the owner dog/recording route and only with `private, no-store`; the same coordinates remain prohibited on Today, History, configuration, cross-owner denial, action, console, URL, storage, log, and artifact surfaces.

## Inspected surfaces

Every passing cycle completed the same ordered checkpoints:

1. `browser-build-surfaces`: scan bounded text files under `.next/static` and `.next/server/app` for current fixture and local infrastructure secrets;
2. `anonymous-html-rsc`: inspect home/login HTML, an explicit login RSC response, and an anonymous protected-route redirect;
3. `authenticated-private-cache`: inspect owner Today/History/detail HTML plus explicit protected RSC, and enforce cache headers on every observed protected response;
4. `recording-route-containment`: permit the exact route table only on owner detail and prove a cross-owner detail request discloses neither owner B name/email nor route coordinates;
5. `server-action-payload`: inspect login/product action requests, the exact brightness Server Action response, and the post-action DOM while allowing the login password only in its required login POST body;
6. `browser-state-and-observers`: require no local/session storage, IndexedDB, Cache Storage, external request, analytics, worker, service worker, WebSocket, or unexplained console/page error; accept only server-managed Supabase auth cookies;
7. `server-edge-database-logs`: scan bounded in-memory production portal output plus every running `*_Dog-RGB-1` local Supabase container log since fixture creation, including database and Edge runtime;
8. `retained-artifacts`: reject trace/HAR/screenshot/video files and retain only the exact zero-findings JSON manifest.

Global response observers check headers without consuming arbitrary canceled/prefetched response streams. Required HTML is inspected from the rendered page, explicit RSC is fetched and read directly with the same authenticated context, and the selected product Server Action body is captured immediately when its response arrives. This prevents an unrelated streaming/prefetch response from weakening or stalling the gate.

## Cache boundary

Protected HTML and RSC must include both `private` and `no-store` and may not include `public` or `s-maxage`. Server Action and `Set-Cookie` responses must include `no-store` and may not include `public` or `s-maxage`; the Next.js action response does not consistently repeat the redundant `private` directive, but `no-store` forbids storage by private and shared caches. Public immutable Next.js assets remain eligible for public caching after their built bytes pass the secret scan.

This local result also verifies that the portal creates a request-scoped SSR client and that browser authentication remains in the expected Supabase cookie namespace rather than Web Storage. It does not prove Vercel/CDN behavior, hosted Supabase Logs Explorer retention, hosted TLS, third-party browser extensions, or production observability configuration.

## Artifact and result

Each retained `output/playwright/m116/cycle-<n>.json` file has exactly the phase, cycle, ordered checkpoints, frozen surface list, and these eight zero counters: secret leaks, unauthorized-identity leaks, route-payload leaks, cache violations, external requests, browser errors, unexpected storage entries, and retained rich artifacts. Failed runs retain the same schema and only the exact completed-checkpoint prefix; raw errors are rethrown but never copied into the artifact.

The reviewed focused command used checksum-verified Node `24.18.0`, Supabase CLI `2.113.0`, local PostgreSQL 17, the local Edge runtime, Next.js `16.3.1` production build/start, and Playwright `1.62.1` with no trace, screenshot, video, HAR, retry, or external service. It passed M1.16 cycle 1 and cycle 2 after separate migration/seed resets and completed the final reusable reset. The default combined runner then passed M1.13, M1.14, M1.15, and M1.16 twice after eight independent resets and completed its final reusable reset. After the token detector was tightened, the focused M1.16 command passed both cycles once more. A post-run read-only database query confirmed PostgreSQL 17 and all 16 migration records.

Final recorded commands:

```powershell
node tools/portal-e2e/run.mjs --clean --m116-only
node tools/portal-e2e/run.mjs --clean
```

Remote CI, push, hosted endpoints, accessibility, performance, firmware, and physical hardware were not run or inspected for this phase.

## Research decisions

- Supabase warns that a cached `Set-Cookie` response can deliver one user's session to another and requires request-scoped SSR clients plus dynamic/private authenticated routes. The gate therefore treats every session-setting response as non-cacheable and scans each authenticated surface. [Supabase SSR advanced guide](https://supabase.com/docs/guides/auth/server-side/advanced-guide)
- Next.js documents Server Actions as public endpoints and warns that encrypted closures still reach the client. The gate inspects the real action request/response and permits only the minimum product payload. [Next.js data security](https://nextjs.org/docs/15/app/guides/data-security)
- Next.js self-hosting documents private no-cache/no-store headers for dynamic App Router responses while static assets may be public and immutable. The gate separates those two classes instead of prohibiting all caching. [Next.js self-hosting](https://nextjs.org/docs/app/guides/self-hosting)
- Vercel recommends `no-store` for sensitive data. Product actions and cookie-setting responses therefore require non-public `no-store`, even where the runtime omits the redundant `private` token. [Vercel cache-control headers](https://vercel.com/docs/caching/cache-control-headers)
- Playwright traces contain DOM snapshots, network data, console output, and sources; HAR can record headers, cookies, and response bodies. Trace, HAR, screenshot, and video capture remain disabled, and the gate rejects retained rich artifacts. [Playwright Trace Viewer](https://playwright.dev/docs/trace-viewer), [HAR replay](https://playwright.dev/docs/mock)
- Playwright exposes console, request, response, WebSocket, worker, and storage context surfaces. The gate uses those observers plus direct bounded HTML/RSC/action reads and a non-persistent context. [Playwright Page](https://playwright.dev/docs/api/class-page), [BrowserContext](https://playwright.dev/docs/api/class-browsercontext)
- Supabase local Edge logs are emitted to the terminal/runtime, while hosted logging has a separate operational surface. M1.16 scans the real local database/Edge/service containers but makes no hosted log-retention claim. [Supabase Edge logging](https://supabase.com/docs/guides/functions/logging)
