# Phase 0 map-provider independent review scorecard

Status: **template only — not acceptance evidence**

Copy this file once per reviewer after a complete credentialed run. Each
reviewer must work from the same committed manifest/screenshots without seeing
the other scorecard. Replace every placeholder; an incomplete row invalidates
the review. Never add provider credentials or credential-bearing request URLs.

## Evidence identity

- Reviewer: `<name or stable reviewer ID>`
- Review completed at UTC: `<YYYY-MM-DDTHH:MM:SSZ>`
- Repository commit: `<40-character commit SHA>`
- Manifest SHA-256: `<64 lowercase hexadecimal characters>`
- Manifest capture timestamp: `<capturedAtUtc>`
- Credential revocation verified by: `<name/ID and UTC timestamp, immediately after automated artifact verification>`

## Independent scoring

Use integers from 1 (unacceptable) through 5 (excellent). Explain every score;
do not use a tie as a way to avoid a decision.

| Criterion | Stadia score | Stadia evidence/reason | MapTiler score | MapTiler evidence/reason |
| --- | ---: | --- | ---: | --- |
| Route salience: dark | `<1-5>` | `<artifact + reason>` | `<1-5>` | `<artifact + reason>` |
| Route salience: light | `<1-5>` | `<artifact + reason>` | `<1-5>` | `<artifact + reason>` |
| Terrain/trail usefulness: outdoor | `<1-5>` | `<artifact + reason>` | `<1-5>` | `<artifact + reason>` |
| Label noise and orientation context | `<1-5>` | `<artifact + reason>` | `<1-5>` | `<artifact + reason>` |
| Mobile readability at 428 CSS px | `<1-5>` | `<artifact + reason>` | `<1-5>` | `<artifact + reason>` |
| Touch/interaction behavior | `<1-5>` | `<observed behavior>` | `<1-5>` | `<observed behavior>` |
| CVD diagnostic aids and non-color cues | `<1-5>` | `<artifact + reason>` | `<1-5>` | `<artifact + reason>` |
| Attribution legibility/compliance | `<1-5>` | `<artifact + reason>` | `<1-5>` | `<artifact + reason>` |
| Request count/cache/network diagnostics | `<1-5>` | `<manifest fields + reason>` | `<1-5>` | `<manifest fields + reason>` |
| Current terms and price fit | `<1-5>` | `<dated source + assumption>` | `<1-5>` | `<dated source + assumption>` |

## Gate assertions

- [ ] All 24 standard cells passed.
- [ ] All 10 diagnostic cells passed.
- [ ] Both unapproved-origin proofs are `rejected_as_expected`.
- [ ] No raw fixture coordinate appeared in provider URLs or DOM text.
- [ ] The secret scan passed after evidence was written.
- [ ] I inspected attribution and the non-map fallback.
- [ ] I completed this scorecard independently.

Preferred provider: `<Stadia | MapTiler>`

Blocking findings: `<none or explicit findings>`

Signed by reviewer: `<name/ID>` at `<UTC timestamp>`
