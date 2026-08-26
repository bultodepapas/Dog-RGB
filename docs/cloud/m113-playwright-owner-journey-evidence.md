# M1.13 deterministic owner-journey evidence

**Captured:** 2026-08-25 (America/Bogota)

**Implementation:** local M1.13 commit created with this evidence; its hash is reported in Git history

**Scope:** one local owner journey through real Supabase Auth/Mailpit, the production portal, the device claim/sync gateway, and persisted database truth; not adversarial identities, fault injection, exhaustive privacy/cache scanning, the full accessibility matrix, performance budgets, hosted deployment, firmware networking, or physical-collar proof

## Frozen harness decisions

- `playwright.portal.config.ts` is a separate one-project configuration. It cannot inherit the embedded AP preview's server, URL, tests, or artifacts.
- The runner owns `127.0.0.1:3000` because the local Auth site URL, confirmation template, and portal redirect-origin allowlist all bind confirmation to that origin. An occupied port fails before the destructive reset.
- One Playwright test owns the dependent journey. It uses one worker, zero retries, one fresh browser context, auto-retrying UI assertions, and bounded readiness/Mailpit polling. It does not split state across order-dependent tests or use a browser mock collar.
- The runner refuses to reset implicitly, validates the repository project and pinned tools, starts only the local loopback stack, generates ephemeral function peppers, resets before each cycle, clears Mailpit, owns/stops the portal process, performs a final reset, removes the ignored secret file, and leaves Supabase reusable.
- Trace, screenshot, video, and HTML reporting are disabled because signup credentials, the email confirmation token, and the one-time claim appear temporarily in browser state. Playwright's automatic failure context directory is deleted unconditionally; the only retained runtime evidence is a sanitized phase/checkpoint JSON manifest under the ignored `output/playwright/m113/` boundary.
- The extended pair-only simulator retains the claim, credential identity/secret, and bearer only in its closure. Its public journey methods expose bounded pairing/upload/configuration/revocation results and never return credential material.

## Exact journey and persisted checkpoints

Each of the two independent cycles proved the following sequence after its own `supabase db reset`:

1. Signup created exactly one unconfirmed Auth user, one trigger-created profile, and no dog. Mailpit contained exactly one matching confirmation email; its one local confirmation URL was parsed only in memory, then the mailbox was cleared.
2. Confirmation marked that same Auth row confirmed and established a browser session. The product logout was then used before a separate password login, so confirmation was not misreported as the login proof.
3. Dog creation redirected with a canonical dog UUID and persisted exactly one `America/Bogota` dog plus one owner membership for the signed-in user.
4. Claim issuance persisted one future, unused, digest-only claim. Reload removed the raw code. One normal simulator claim consumed it once, created the exact active collar and one digest-only active credential, and created no sync/recording side effect before upload.
5. The simulator derived one current-time request from the frozen device-v1 sync fixture, recomputed the point digest, removed unrelated configuration mutations, and uploaded one chunk, one daily-summary input, and three exact point sequences. The committed receipt, chunk, points, recording, summary input, and pre-ACK queue snapshot all matched the generated identities.
6. Today showed the exact recent collar and three-point latest recording. History linked exactly one recording, and detail exposed that exact recording with sequences 0–2 and the frozen coordinates through the authoritative table.
7. The website wrote one brightness value. Database truth proved the exact body, winning web revision, user actor, server version, and SHA-256 with no reported row. The same simulator pulled that exact body/version/hash, reported that version/hash as applied, and the database plus refreshed UI converged to `APLICADO EN EL COLLAR`.
8. The collar page showed the accepted protocol/capability projection and the latest exact empty queue snapshot. The revoke form's hidden target matched the already checkpointed collar; one confirmed action revoked that collar and its credential at the same terminal timestamp while retaining the recording and three points. A later simulator sync was denied as `device_revoked`.
9. Final logout removed Supabase cookies. Browser Back plus refresh and a direct protected navigation resolved to login and rendered neither the dog nor private collar content. The browser observed no console/page error and no non-local request origin.

## Toolchain and result

The clean run used:

- checksum-verified Node `24.18.0`;
- Supabase CLI `2.113.0` from `.supabase-version`;
- Next.js `16.3.1` production build/start;
- Playwright `1.62.1` with installed Chromium/headless-shell revision `1234` (`151.0.7922.34`);
- local PostgreSQL 17 and Mailpit `1.30.2` supplied by the repository's Supabase stack.

The runner replayed all 15 migrations for each cycle. Cycle 1 passed once in 9.3 seconds and cycle 2 passed once in 9.7 seconds; `retries: 0` means neither result can be a hidden retry. The runner then cleared the mailbox, stopped only its portal process, reset the database again, removed `supabase/functions/.env`, and retained no Playwright trace, screenshot, video, HTML report, or raw error context.

## Reproduction

From the repository root, with the checksum-verified Node 24.18.0 selected:

```powershell
node tools/portal-e2e/run.mjs --clean
```

The explicit `--clean` is required. The command targets no hosted endpoint, performs no push, and does not run or inspect remote CI.
