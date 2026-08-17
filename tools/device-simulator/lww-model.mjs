export const SERVER_ACTOR_ID = "00000000-0000-4000-8000-0000000000ff";
export const TRUSTED_QUALITIES = new Set(["server_anchored", "sntp_synced", "gnss_trusted"]);
export const TRUSTED_SKEW_MS = 600_000;

export function compareStamp(left, right) {
  if (left.physical_ms !== right.physical_ms) return Math.sign(left.physical_ms - right.physical_ms);
  if (left.logical !== right.logical) return Math.sign(left.logical - right.logical);
  const leftActor = left.actor_id.replaceAll("-", "");
  const rightActor = right.actor_id.replaceAll("-", "");
  return leftActor === rightActor ? 0 : leftActor < rightActor ? -1 : 1;
}

function nextLogical(value) {
  if (value >= 0xffff_ffff) throw new Error("hlc_logical_overflow");
  return value + 1;
}

export function tickServer(clock, nowMs) {
  const physical = Math.max(clock.physical_ms, nowMs);
  return {
    physical_ms: physical,
    logical: physical > clock.physical_ms ? 0 : nextLogical(clock.logical),
  };
}

export function mergeServer(clock, received, nowMs) {
  const physical = Math.max(clock.physical_ms, received.physical_ms, nowMs);
  let logical;
  if (physical === clock.physical_ms && physical === received.physical_ms) {
    logical = nextLogical(Math.max(clock.logical, received.logical));
  } else if (physical === clock.physical_ms) {
    logical = nextLogical(clock.logical);
  } else if (physical === received.physical_ms) {
    logical = nextLogical(received.logical);
  } else {
    logical = 0;
  }
  return { physical_ms: physical, logical };
}

export function isTrustedMutation(mutation, receivedMs) {
  return TRUSTED_QUALITIES.has(mutation.time_quality) &&
    Math.abs(mutation.authored_hlc.physical_ms - receivedMs) <= TRUSTED_SKEW_MS;
}

export function createLwwState() {
  return {
    clock: { physical_ms: 0, logical: 0 },
    heads: new Map(),
    revisions: new Map(),
  };
}

function fingerprint(mutation) {
  return JSON.stringify([mutation.resource_key, mutation.resource_schema, mutation.body]);
}

function applyAccepted(state, mutation, acceptedHlc, ordering) {
  const prior = state.revisions.get(mutation.mutation_id);
  if (prior) {
    if (prior.fingerprint !== fingerprint(mutation)) throw new Error("mutation_id_conflict");
    return { ...prior.outcome, replayed: true };
  }

  const head = state.heads.get(mutation.resource_key);
  const winning = !head || compareStamp(acceptedHlc, head.accepted_hlc) > 0;
  const serverVersion = winning ? (head?.server_version ?? 0) + 1 : null;
  const outcome = {
    mutation_id: mutation.mutation_id,
    resource_key: mutation.resource_key,
    disposition: winning ? "winning" : "superseded",
    replayed: false,
    ordering,
    server_version: serverVersion,
    accepted_hlc: acceptedHlc,
  };
  state.revisions.set(mutation.mutation_id, {
    fingerprint: fingerprint(mutation),
    outcome: structuredClone(outcome),
    body: structuredClone(mutation.body),
  });
  if (winning) {
    state.heads.set(mutation.resource_key, {
      body: structuredClone(mutation.body),
      accepted_hlc: structuredClone(acceptedHlc),
      server_version: serverVersion,
      mutation_id: mutation.mutation_id,
    });
  }
  return outcome;
}

export function applyDeviceBatch(state, mutations, receivedMs) {
  const classified = mutations.map((mutation, index) => ({
    mutation,
    index,
    trusted: isTrustedMutation(mutation, receivedMs),
  }));
  classified.sort((left, right) => {
    if (left.trusted !== right.trusted) return left.trusted ? -1 : 1;
    if (left.trusted) return left.index - right.index;
    return left.mutation.local_sequence - right.mutation.local_sequence;
  });

  return classified.map(({ mutation, trusted }) => {
    const prior = state.revisions.get(mutation.mutation_id);
    if (prior) {
      if (prior.fingerprint !== fingerprint(mutation)) throw new Error("mutation_id_conflict");
      return { ...prior.outcome, replayed: true };
    }
    let acceptedHlc;
    let ordering;
    if (trusted) {
      acceptedHlc = structuredClone(mutation.authored_hlc);
      state.clock = mergeServer(state.clock, mutation.authored_hlc, receivedMs);
      ordering = "authored";
    } else {
      state.clock = tickServer(state.clock, receivedMs);
      acceptedHlc = { ...state.clock, actor_id: SERVER_ACTOR_ID };
      ordering = "fallback_received";
    }
    return applyAccepted(state, mutation, acceptedHlc, ordering);
  });
}

export function applyWebMutation(state, mutation, nowMs) {
  const prior = state.revisions.get(mutation.mutation_id);
  if (prior) {
    if (prior.fingerprint !== fingerprint(mutation)) throw new Error("mutation_id_conflict");
    return { ...prior.outcome, replayed: true };
  }
  const head = state.heads.get(mutation.resource_key);
  if ((head?.server_version ?? 0) !== mutation.base_server_version) {
    return { disposition: "stale", replayed: false };
  }
  state.clock = tickServer(state.clock, nowMs);
  return applyAccepted(state, mutation, {
    ...state.clock,
    actor_id: mutation.actor_id,
  }, "authored");
}

export function assertLwwInvariants(state) {
  const revisionsByResource = new Map();
  for (const revision of state.revisions.values()) {
    const list = revisionsByResource.get(revision.outcome.resource_key) ?? [];
    list.push(revision);
    revisionsByResource.set(revision.outcome.resource_key, list);
    if (revision.outcome.ordering === "fallback_received" &&
        revision.outcome.accepted_hlc.actor_id !== SERVER_ACTOR_ID) {
      throw new Error("fallback_actor_mismatch");
    }
  }

  for (const [resourceKey, revisions] of revisionsByResource) {
    const expected = revisions.reduce((best, candidate) =>
      !best || compareStamp(candidate.outcome.accepted_hlc, best.outcome.accepted_hlc) > 0
        ? candidate
        : best, null);
    const head = state.heads.get(resourceKey);
    if (!head || head.mutation_id !== expected.outcome.mutation_id) {
      throw new Error(`head_not_maximal:${resourceKey}`);
    }
    const winningCount = revisions.filter((revision) => revision.outcome.disposition === "winning").length;
    if (head.server_version !== winningCount) throw new Error(`server_version_gap:${resourceKey}`);
  }
}
