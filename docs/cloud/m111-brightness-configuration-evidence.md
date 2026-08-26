# M1.11 Brightness-configuration evidence

**Captured:** 2026-08-25 (America/Bogota)

**Implementation:** local M1.11 commit created with this evidence; its hash is reported in the implementation handoff

**Scope:** local brightness-only portal, authenticated Data API/RPC, concurrency, simulator convergence, browser, and accessibility evidence; not hosted deployment or physical-collar acceptance

## Frozen product and transaction decisions

- The page controls only resource key `brightness`, schema `1`, and the exact canonical body `{"brightness":N}` where `N` is an integer from 1 through 255. It adds no collar picker, second resource, polling, Realtime, firmware path, or browser Supabase client.
- The read and action paths each use one fresh user/RLS client. They select the same active collar as Today: `last_sync_at DESC NULLS LAST, linked_at DESC NULLS LAST, id ASC`. The action reauthorizes `write`, reselects that collar, then makes exactly one existing RPC call.
- Desired state is cloud truth, not hardware truth. `APLICADO EN EL COLLAR` and the three rejection outcomes require the report's exact desired `server_version` and SHA-256. Missing or mismatched evidence remains pending. Collar freshness is an independent warning.
- A new mutation ID with the same canonical value stores one non-winning `superseded` receipt for durable replay/conflict identity. It does not advance the head, HLC, winning revision ID, server version, or head timestamp; the bounded RPC disposition is `unchanged`.
- Web mutations lock the authorized active collar before replay/head inspection. This serializes missing-head and existing-head writes with one stable row and with the existing device-sync lock order.
- Stale bases use PostgREST custom SQLSTATE `PT409`, producing an immediate HTTP 409 with message `stale_base_server_version`. PostgreSQL `40001` was rejected here because the local Data API treats it as retryable serialization failure and can hide the required bounded stale-form response behind automatic retries.

## Database, RLS, and concurrency result

The additive migration changes only the body of `api.mutate_config_resource_v1` under its existing signature and grants. It also binds the supplied digest to the exact canonical brightness bytes and rejects fractional or noncanonical numeric bodies before any write.

- The focused M1.11 pgTAP file passed 41/41 assertions; the complete database suite passed 435/435 assertions across 19 files.
- The multi-connection harness proved one winner plus one HTTP-409-equivalent stale result for concurrent missing-head and existing-head submissions. It also proved concurrent exact replay creates one receipt and concurrent same-value IDs preserve the head/HLC while recording two non-winning receipts.
- The raw local Data API matrix passed:

| Boundary | Owner | Editor | Viewer | Non-member | Anonymous |
| --- | --- | --- | --- | --- | --- |
| Desired/report RLS read | 1 / 1 | 1 / 1 | 1 / 1 | 0 / 0 | denied |
| Brightness mutation | allowed | allowed | denied | denied | denied |

The same raw probe proved exact replay returns its original receipt and a stale base returns exact HTTP `409`, code `PT409`, without overwriting version 2.

## Portal and simulator result

- 107/107 portal tests passed. Focused coverage includes one-client authorization/query order, no configuration query without an active collar, exact active-collar reselection, canonical digest vectors, malformed evidence fail-closed behavior, every desired/reported truth state, the exact 24-hour freshness boundary, viewer denial, strict form parsing, mutation response validation, stale handling, and response-loss exact retry state.
- The existing end-to-end simulator path created a web brightness winner, returned it to the paired device as desired state, accepted the device's exact applied report, and retained the existing replay/LWW scenarios. No simulator protocol or firmware code changed.
- Production type-check, lint, and build passed. The protected configuration route remained dynamic. Next.js runtime inspection reported no compilation or runtime error and confirmed the expected route and page metadata.
- Owner runtime proof covered blank/no-head input, native invalid-range focus, winning save, same-value no-op, stale-base non-overwrite plus explicit refresh, and exact applied state. Viewer runtime proof showed the same desired/reported truth with no form or mutation control.
- The browser made no Supabase REST/RPC request, WebSocket, polling request, or external-origin request; all mutation traffic crossed the Next.js Server Action boundary.

## Accessibility and responsive result

- The page has one `h1`, ordered section headings, native labelled number input, connected help/error text, status-independent copy, focused action results, keyboard-native step behavior, and 48-pixel input/action targets. Viewer and stale states do not expose a mutable form.
- At 320, 428, 768, and 1280 CSS pixels the document had no horizontal overflow. Reduced-motion emulation produced no animation.
- Axe WCAG 2 A/AA inspection reported zero violations for both owner and viewer. Its contrast rule was incomplete because the application background is a gradient, so the configuration colors were checked directly: the lowest relevant pair is `#91a995` on `#171400` at 7.31:1; the remaining checked status/text pairs ranged from 7.80:1 to 18.48:1.

## Reproduction

Use the repository's checksum-verified Node `24.18.0` runtime and disposable local Supabase stack:

```powershell
npm run phase1:check
npm run phase1:local -- --clean
npm run portal:build
```

The clean gate runs both M1.11 transport proofs automatically:

```powershell
node tools/cloud_configuration/m111_rpc_concurrency.mjs
node tools/cloud_configuration/m111_rest_matrix.mjs
```

The second command receives only local Supabase URL, publishable key, and JWT secret from the gate process. Neither script prints credentials, targets a hosted project, or leaves its synthetic fixture behind.

Remote CI was intentionally not inspected or run for this subphase, and no push or hosted change is part of this evidence.
