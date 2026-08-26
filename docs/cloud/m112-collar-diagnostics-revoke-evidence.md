# M1.12 Collar diagnostics and revocation evidence

**Captured:** 2026-08-25 (America/Bogota)

**Implementation:** local M1.12 commit created with this evidence; its hash is reported in the implementation handoff

**Scope:** local accepted-capability and pre-ACK queue snapshot persistence, authenticated collar page, owner-only website revocation, sync/revoke concurrency, simulator, browser, and accessibility evidence; not diagnostics history, hosted deployment, firmware networking, physical-collar acceptance, polling, or Realtime

## Frozen product and selection decisions

- The page selects exactly one active collar with the existing order `last_sync_at DESC NULLS LAST, linked_at DESC NULLS LAST, id ASC`. It does not list or select revoked, retired, pending, or alternate active collars and does not add a collar picker.
- The displayed firmware, hardware revision, protocol, schema versions, resources, effects, palettes, and sync limits are the last complete capability manifest accepted by the cloud. They are not a live hardware probe. A hash-only sync must match that accepted manifest; a changed manifest must be sent in full, pass semantic validation, and persist atomically with its canonical hash.
- Queue values are one bounded snapshot sent before the collar processes the sync response. Exact zeros mean the queue was empty at that observation; null means unavailable. The UI never calls the snapshot live and never exposes request bodies, credentials, hashes, claim digests, coordinates, or raw machine errors.
- Website revocation is owner-only. Editors and viewers retain the same RLS-scoped read truth but receive no revoke action. The two-stage UI names the consequences, requires an explicit checkbox, prevents accidental duplicate submission, supports Escape/cancel, and moves focus to the disclosure or final result.
- Revocation blocks later device authentication and configuration download without deleting retained recordings or affecting local collar features. This web slice does not reactivate or re-pair the same device.

## Transaction and protocol result

The additive migration adds only bounded nullable diagnostic snapshot columns and replaces the existing gateway/revoke function bodies under their existing signatures and least-privilege grants.

- Device sync locks the credential before the collar, matching revoke. The four independent true multi-connection sync/revoke races completed without a deadlock or timeout. Every terminal state was coherent: revoke succeeded, the collar and credential were revoked together, and no post-revoke credential remained active.
- Exact sync replay still returns its durable prior result before current mutable-state processing. A successful new sync persists protocol/capability and diagnostic truth only with the same committed transaction as its receipt/data effects; invalid or rolled-back requests leave prior truth unchanged.
- The Edge boundary now validates diagnostic capacity coherence and a changed full manifest's duplicate-free resources/effects/palettes, declared support semantics, configuration resource compatibility, and canonical SHA-256. Device claim now persists the validated root `protocol_version` rather than reading a nonexistent nested field.
- The simulator sends one changed full capability manifest and verifies its accepted hash/persistence, then uses hash-only continuation. An empty sync reports exact zero queue state, while sealed-chunk sync reports the actual pre-ACK queue.

The focused M1.12 pgTAP file passed 38/38 assertions. The complete database suite passed 473/473 assertions across 20 files. The raw local Data API and RPC matrix passed:

| Boundary | Owner | Editor | Viewer | Non-member | Anonymous |
| --- | --- | --- | --- | --- | --- |
| Selected collar diagnostic read | allowed | allowed | allowed | zero rows | denied |
| Website revoke RPC | allowed | denied | denied | denied | denied |

The matrix also proved exact owner retry confirms the same already-revoked target, selection drift never revokes a different collar, and the corresponding private credential is revoked.

## Portal and browser result

- 121/121 portal tests passed. Focused coverage includes one-client authorization/query order, deterministic active selection, frozen minimal DTOs, missing/empty/pending queue states, malformed capability/diagnostic fail-closed behavior, owner/editor/viewer permissions, exact-target reselection, exact retry, selection drift, ambiguous RPC confirmation, strict form parsing, semantic disclosure, and absence of browser data access, polling, or WebSockets.
- 23/23 shared gateway/simulator unit tests passed. The final clean local gate replayed all 15 migrations, regenerated exact API types, passed 473 database assertions, database lint/advisors, 49 adversarial Edge scenarios, the M1.11 concurrency/Data API proofs, the M1.12 four-race/Data API proof, simulator claim/sync/replay/configuration/capability scenarios, restore/tombstone verification, and deletion drills.
- The production Next.js build passed and kept `/app/[dogId]/collars` dynamic. Next.js runtime inspection reported no compilation, configuration, or session error.
- Authenticated browser proof showed the same bounded diagnostics to owner and viewer, no revoke control for the viewer, and the owner confirmation heading receiving focus. Escape collapsed the disclosure and restored focus to `REVISAR REVOCACIÓN`.
- Confirmed owner revocation produced a focused polite status, then a fresh page selected no active collar. Direct database inspection showed the exact collar and its private credential in `revoked` state with the same `revoked_at` instant.
- The authenticated document returned `Cache-Control: no-cache, must-revalidate`. The final navigation produced no browser console errors and requested only same-origin localhost document/static assets.

## Accessibility and responsive result

- The active and empty states each expose one `main`, one `h1`, ordered labelled regions, status text independent of color, native confirmation input, and visible keyboard-operable actions. Measured visible controls were at least 44 CSS pixels high; primary controls use the repository's 48-pixel target.
- The page had no horizontal overflow at the desktop width and the browser's minimum resizable viewport of 501 CSS pixels. Mobile emulation supplied the narrower automated audit; exact 320/428/768/1280 Playwright layout coverage remains intentionally assigned to M1.17 rather than being falsely closed here.
- Lighthouse snapshot audits reported Accessibility, Best Practices, SEO, and Agentic Browsing scores of 100 on both desktop and mobile, with 34 passed and zero failed audits per run.

## Reproduction

Use the repository's checksum-verified Node `24.18.0` runtime and disposable local Supabase stack:

```powershell
npm run phase1:check
npm run phase1:local -- --clean
npm run portal:build
```

The clean gate runs the M1.12 transport proof automatically:

```powershell
node tools/cloud_collars/m112_revoke_matrix.mjs
```

The script receives only local Supabase connection material from the gate process, prints no credential, targets no hosted project, and removes its synthetic fixture. Remote CI was intentionally not run or inspected, and no push or hosted mutation is part of this evidence.
