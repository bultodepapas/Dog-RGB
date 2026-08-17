# Cloud credentials and external-service checklist

**Status:** Phase 0 inventory, 2026-08-13. No production credential is required or authorized for documentation/protocol work. Temporary MapTiler and Stadia test credentials/domain properties are required only to close the still-open Phase 0 comparative-map and unapproved-origin-rejection gates.

Use this as a deploy/rotation handoff. Never paste a real value into this file, an issue, chat, screenshot, fixture, URL query, serial log, or support export.

## Naming rules

For new Supabase projects use the current key model:

- `sb_publishable_…`: browser-safe project key only when grants/RLS are correct;
- `sb_secret_…`: server-only privileged key; never browser, firmware, or `NEXT_PUBLIC_*`;
- legacy `anon`/`service_role` JWT keys are not the preferred names for new implementation and must not be copied from old tutorials without reviewing current project settings.

A publishable key is not a user, device, or authorization credential. RLS and authenticated membership remain mandatory. A Supabase secret key bypasses RLS and must be treated as environment-wide compromise material.

## Credential inventory

| Credential/service | Created when / by | Allowed location | Forbidden location | Rotation/revocation evidence |
| --- | --- | --- | --- | --- |
| Supabase publishable key | Phase 1 per dev/staging/prod project | browser build env for matching environment; local `.env` ignored | firmware identity, docs/fixtures/logs; never treated as auth | build identifies correct project; rotate on exposure/operational schedule and re-deploy web |
| Supabase secret key | Phase 1 operator | Supabase Edge Function/server secret store only | browser, Vercel client env, collar, repo, CI output | service-only RPC tests; rotate and inspect privileged audit after leak |
| claim-code HMAC pepper + version | Phase 1 operator, cryptographic RNG | Edge/server secret store only; independent from device pepper | Postgres plaintext, browser, collar, logs/repo | versioned verification across 900-second claim lifetime; rotate independently and invalidate outstanding claims during incident |
| device-credential HMAC pepper + version | Phase 1 operator, cryptographic RNG | Edge/server secret store only | Postgres plaintext, browser, collar, logs/repo | versioned dual-verify/rehash migration tested; environment-wide incident runbook |
| per-collar 256-bit credential | generated and A/B-persisted by collar before claim, using approved RNG | plaintext on that collar; transient verified-TLS claim body and sync/revoke Authorization at Edge; server stores HMAC digest only | issuance response, website/password manager, logs, DB plaintext, other collars | website revoke/rotate; device `REVOKE_PENDING` exact replay; clear only on matching schema-valid `newly_revoked|already_revoked`; generic errors retain; forced-clear warning tested |
| one-use claim code | user-authorized server operation | 16-character canonical user display and pairing request only; peppered HMAC digest server-side | logs/analytics/email by default, long-term DB, firmware after exchange | 80 random bits, 900-second TTL, maximum five failed consumes, atomic one-use, consumed/expired purge <=24 h |
| Supabase Auth SMTP credentials | production account-email setup by operator | Supabase Auth provider settings/secret manager | browser/repo/collar | confirmation/recovery tests, DKIM/SPF/DMARC, provider revoke/rotate runbook |
| Vercel deployment/team token | only if CI/API deployment needs it | CI secret store with least scope; prefer native integration/workload identity | runtime browser, repo, collar, build logs | expiry/owner/scope documented; deployment + revoke rehearsal |
| map provider browser key/domain auth | Phase 0 bake-off temporary; Phase 6 production | browser only if provider designed it as public; exact allowed origins/domain/referrer; separate envs | firmware/server secret assumptions, repo permanent key, screenshots | unapproved-origin rejection, quota/budget alerts, rotate and redeploy; no route in requests |
| stable device API DNS account | before Phase 3 field firmware | registrar/DNS operator account with MFA and recovery | firmware API token, repo | CNAME/cert expiry monitoring, provider migration rehearsal, two-owner recovery |
| Vercel web DNS/domain account | web deployment | registrar/Vercel operator with MFA | app runtime secret | redirect/origin/certificate tests and recovery contacts |
| Supabase CLI access token | dev/CI only if hosted migrations need it | developer credential store or CI secret, scoped/separate | repo, frontend, Edge runtime, collar | owner/last-used/expiry; rotate on offboarding/leak |
| database password/direct connection | migrations/admin only | secret manager/approved developer tool; pooler/direct URI separated | browser/collar/logs/docs | network/scope review, rotate after leak/offboarding; application should use API/RPC path |
| optional monitoring/error service token | only after privacy review | server/CI secret with narrow ingest/project scope | client if secret; request bodies/routes | synthetic canary proves scrubbing; revoke/export/deletion contract |
| future Google Maps credential | only after a new map ADR/migration | platform-appropriate restricted key | firmware/repo/unrestricted browser key | API + exact origin/app restrictions, quotas/billing alerts, adapter regression |

## Environment separation

- Separate Supabase projects, Vercel deployments, map keys/domain allowlists, Auth redirect origins, email sender/testing domains, claim peppers, and device-credential peppers for development, staging, and production.
- Never pair a production collar to a local/staging environment without an explicit destructive reprovisioning flow.
- Synthetic coordinates and accounts only in committed fixtures/screenshots. Production database copies do not enter developer laptops or preview deployments.
- Preview deployments must not be added to production key/auth origin allowlists with broad wildcards. Use a dedicated preview/staging project or require an intentional exact-domain update.
- Record the environment visibly in AP pairing and web UI to prevent accidental cross-pairing.

## Repository and build checks

- All local secret files are ignored; commit `.env.example` with obvious placeholders and purpose comments only.
- CI secret scan covers source, generated assets, firmware binary strings, web bundles/source maps, fixtures, snapshots, test reports, and git history for high-risk changes.
- Search for `sb_secret_`, legacy service-role JWTs, authorization headers, database URIs/passwords, map keys, claim/credential canaries, and real coordinates.
- Next.js public environment variables contain only explicitly public values. Server secrets are never imported by client modules; verify the emitted browser chunks.
- Edge logs/errors never serialize environment or request headers/body. Serial firmware diagnostics never print the device credential/claim code.
- Dependency/build scripts cannot print environment by default. Lock and review third-party deployment actions.

## Provisioning checklist

### Phase 1 local/staging foundation

- [ ] Name an operator/backup owner for each account and enable phishing-resistant MFA where supported.
- [ ] Create separate Supabase project/env; record region, plan, project ID (not secrets), backup/PITR state, spending limits, and contacts.
- [ ] Review current Supabase changelog and key model; configure explicit `api` exposure/grants and `private` non-exposure.
- [ ] Generate/version independent claim-code and device-credential HMAC peppers in secret storage; document generation source and never retrieve them into client tooling.
- [ ] Create publishable/secret keys with the narrowest supported scope; run RLS and service-only function tests.
- [ ] Configure exact local/staging Auth URLs and email behavior; do not use production SMTP for load tests.
- [ ] Create temporary map key only when needed; fragment/local secret handling keeps it out of server logs and source.

### Phase 3 hosted device proof

- [ ] Use development Supabase hostname for lab only; record endpoint separately from versioned paths.
- [ ] Provision one unique development collar credential through the real claim flow; no manual global token.
- [ ] Verify certificate chain/hostname/time failure, revoked credential, and binary/log negative secret scan.
- [ ] Configure rate/body limits and budget alerts before Internet exposure.
- [ ] Rehearse device rotation, normal unlink through `device-v1-revoke`, lost response/exact replay of the original disposition, prior website/different-request revoke returning `already_revoked`, generic-error retention, forced local clear warning, and website-side revocation.

### Phase 6/7 field production

- [ ] User supplies/owns a stable device API domain; enable Supabase custom domain and validate CNAME/certificate before flashing field endpoints.
- [ ] Configure production Vercel domain, Supabase Auth site/redirect URLs, exact CORS/origin policies, and map-origin restrictions.
- [ ] Choose paid/free tiers from measured traffic and intended commercial status; recheck current terms/pricing.
- [ ] Configure custom SMTP and DNS records with recovery sender/contact; test bounce/rate/abuse paths.
- [ ] Set backups/PITR and run an isolated restore plus deletion replay.
- [ ] Set credential rotation calendar, last-used/owner inventory, budget/expiry alerts, and compromise contacts.
- [ ] Run full bundle/binary/log/network secret/location scan and cross-user authorization suite.

## Rotation and incident minimum

For every credential record: service/environment, owner and backup owner, creation/last rotation/next review, where stored, consumers, scope, last-used visibility, revocation method, blast radius, and tested recovery. Do not record the value.

If exposed:

1. classify whether it is one collar, publishable key, server secret, pepper, DB/admin, map quota, DNS, or deployment access;
2. revoke/rotate at the narrowest safe scope immediately; stopping logs or deleting a commit is not revocation;
3. inspect coordinate-free auth/audit/billing evidence and disable affected ingestion/deploy paths if needed;
4. reissue/redeploy and test old value rejection plus new path;
5. assess route/account notification obligations and remove leaked material from active artifacts/history where appropriate;
6. document cause and add a prevention/detection test.

## External inputs still required

No value is needed to author or review the Phase 0 contracts. Formal Phase 0 exit does require the temporary provider credentials below; later implementation requires the remaining user/operator inputs:

- an owned domain and DNS access for stable device API/web names;
- separate Supabase and Vercel projects/accounts and plan decisions;
- a temporary origin-restricted MapTiler key and temporary Stadia property/domain-auth setup to finish the comparative bake-off and prove unapproved-origin rejection, then the selected provider account/domain restrictions;
- a production email domain/provider/SMTP credentials;
- billing/budget contacts and intended commercial/non-commercial usage classification;
- Google Maps credentials only if a later ADR selects it.

## References

- [Supabase API keys](https://supabase.com/docs/guides/api/api-keys)
- [Supabase Edge Function secrets](https://supabase.com/docs/guides/functions/secrets)
- [Supabase Data API security](https://supabase.com/docs/guides/api/securing-your-api)
- [Stadia Maps authentication](https://docs.stadiamaps.com/authentication/)
- [MapTiler API key protection](https://docs.maptiler.com/cloud/api/authentication-key/)
- [Vercel environment variables](https://vercel.com/docs/environment-variables)
