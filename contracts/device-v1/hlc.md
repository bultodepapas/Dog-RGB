# Hybrid logical clock contract

This document is normative for device-v1 configuration ordering. Keywords
MUST, MUST NOT, SHOULD, and MAY have the meanings in RFC 2119/RFC 8174.

## Representation and total order

A wire stamp is `(physical_ms, logical, actor_id)`:

- `physical_ms` is an exact JSON integer in `0..4102444800000` (UTC through
  2100-01-01). `0` is allowed only as an untrusted/audit value.
- `logical` is an unsigned 32-bit integer.
- `actor_id` is a canonical lowercase UUID. It is public identity, not a
  credential.

Compare stamps lexicographically by `physical_ms`, then `logical`, then the 16
UUID octets in network order. Identical triples compare equal and represent an
idempotent replay, not a new edit. No AP/web source priority is applied.

The five wire time qualities are `unknown`, `approximate_persisted`,
`server_anchored`, `sntp_synced`, and `gnss_trusted`. Only the last three are
eligible for authored-time ordering, and only while their physical time is
within the inclusive ±600,000 ms server receipt window. The server rebases all
other stamps and retains the submitted stamp for audit.

## Device local event

```text
function DEVICE_TICK(last, plausible_now_ms_or_null, actor_id):
    require persisted(last)

    if plausible_now_ms_or_null is not null:
        p2 = max(last.physical_ms, plausible_now_ms_or_null)
        l2 = 0 if p2 > last.physical_ms else last.logical + 1
        trusted_anchor = true
    else:
        p2 = last.physical_ms
        l2 = last.logical + 1
        trusted_anchor = false

    if l2 > UINT32_MAX:
        if not trusted_anchor or p2 == MAX_WIRE_PHYSICAL_MS:
            return error HLC_LOGICAL_OVERFLOW
        p2 = max(plausible_now_ms_or_null, p2 + 1)
        l2 = 0

    next = (p2, l2, actor_id)
    persist next in the verified A/B config envelope
    only after persistence succeeds may the local config write be acknowledged
    return next
```

A clock correction MUST NOT reduce the persisted state. An unknown-time edit
still receives a monotonic local stamp and a persisted `local_sequence`, but the
server does not trust that physical component for global ordering.

## Receive/merge

For local state `(p,l)`, received state `(rp,rl)`, and plausible `now`:

```text
p2 = max(p, rp, now)
if p2 == p and p2 == rp: l2 = max(l, rl) + 1
else if p2 == p:         l2 = l + 1
else if p2 == rp:        l2 = rl + 1
else:                    l2 = 0
```

If the selected increment exceeds `UINT32_MAX`, advance `p2` by one millisecond
and reset `l2` to zero only when a trusted physical anchor exists and the wire
maximum is not exceeded. Otherwise fail closed with `HLC_LOGICAL_OVERFLOW`.
Persist the merged state before acknowledging a local write or using it as the
basis of a later mutation.

## Server acceptance

```text
function ACCEPT_DEVICE_MUTATIONS(server_state, mutations, received_at_ms):
    require one transaction and a locked collar/resource context
    require mutation_id replays have the same body hash

    trusted = mutations where
        time_quality in {server_anchored, sntp_synced, gnss_trusted}
        and abs(authored_hlc.physical_ms - received_at_ms) <= 600000

    for each trusted mutation:
        accepted_hlc = authored_hlc
        ordering = authored
        server_state = MERGE(server_state, authored_hlc, received_at_ms)

    fallback = all remaining mutations sorted by persisted local_sequence
    require fallback local_sequence values are unique in the request
    for each fallback mutation:
        server_state = DEVICE_TICK(server_state, received_at_ms, SERVER_ACTOR_ID)
        accepted_hlc = server_state
        ordering = fallback_received

    compare each accepted_hlc with its locked resource head using total order
    increment server_version only for a winning revision
    persist result and request receipt, then commit
```

The pseudocode names `DEVICE_TICK` for a shared mathematical operation; the
server performs it in the transaction and does not use firmware persistence.
Unknown-time array order is never authoritative. An exact request replay returns
the persisted outcomes and stamps without ticking again.

## Overflow and failures

Logical values MUST NOT wrap. A device that cannot safely advance a trusted
physical millisecond rejects the config mutation locally, leaves the previous
config active, and exposes a clock fault. A server overflow aborts the entire
transaction with a safe internal diagnostic; it cannot return an ACK.

Deterministic executable cases live in `fixtures/hlc-vectors.json` and are run
by `node --test contracts/device-v1/test-contracts.mjs`.
