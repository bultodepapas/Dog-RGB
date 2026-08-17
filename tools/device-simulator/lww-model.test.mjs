import test from "node:test";
import assert from "node:assert/strict";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import {
  applyDeviceBatch,
  applyWebMutation,
  assertLwwInvariants,
  compareStamp,
  createLwwState,
  SERVER_ACTOR_ID,
} from "./lww-model.mjs";

const regressionSeeds = JSON.parse(await readFile(
  new URL("./lww-regression-seeds.json", import.meta.url),
  "utf8",
)).seeds;

function mutationId(value) {
  return `90000000-0000-4000-8000-${value.toString(16).padStart(12, "0")}`;
}

function deviceMutation({ id, sequence, resource = "brightness", value, physical, logical = 0, quality = "sntp_synced", actor }) {
  return {
    mutation_id: mutationId(id),
    local_sequence: sequence,
    resource_key: resource,
    resource_schema: 1,
    body: { value },
    authored_hlc: { physical_ms: physical, logical, actor_id: actor },
    time_quality: quality,
  };
}

function webMutation({ id, resource = "brightness", value, base, actor }) {
  return {
    mutation_id: mutationId(id),
    resource_key: resource,
    resource_schema: 1,
    body: { value },
    base_server_version: base,
    actor_id: actor,
  };
}

test("the normative LWW matrix converges for web, AP, resources, ties, fallback, and replay", () => {
  const device = "f0000000-0000-4000-8000-000000000001";
  const user = "10000000-0000-4000-8000-000000000001";
  const state = createLwwState();

  applyWebMutation(state, webMutation({ id: 1, value: 80, base: 0, actor: user }), 10);
  assert.equal(applyDeviceBatch(state, [deviceMutation({
    id: 2, sequence: 1, value: 100, physical: 11, actor: device,
  })], 11)[0].disposition, "winning");
  assert.equal(state.heads.get("brightness").body.value, 100, "web then later trusted AP");

  applyWebMutation(state, webMutation({ id: 3, value: 70, base: 2, actor: user }), 12);
  assert.equal(state.heads.get("brightness").body.value, 70, "trusted AP then web");

  applyDeviceBatch(state, [deviceMutation({
    id: 4, sequence: 2, resource: "gps_quality", value: 7, physical: 12, logical: 1, actor: device,
  })], 12);
  assert.equal(state.heads.get("brightness").body.value, 70);
  assert.equal(state.heads.get("gps_quality").body.value, 7, "different resources survive");

  const tied = deviceMutation({ id: 5, sequence: 3, value: 90, physical: 12, logical: 0, actor: device });
  const beforeTie = state.heads.get("brightness").accepted_hlc;
  tied.authored_hlc = { ...beforeTie, actor_id: device };
  applyDeviceBatch(state, [tied], beforeTie.physical_ms);
  assert.equal(state.heads.get("brightness").body.value, 90, "actor bytes break a full clock tie");

  const fallback = deviceMutation({
    id: 6, sequence: 4, value: 120, physical: 0, quality: "unknown", actor: device,
  });
  const fallbackOutcome = applyDeviceBatch(state, [fallback], 13)[0];
  assert.equal(fallbackOutcome.accepted_hlc.actor_id, SERVER_ACTOR_ID);
  assert.equal(state.heads.get("brightness").body.value, 120, "fallback accepted after web wins");
  const replay = applyDeviceBatch(state, [fallback], 99)[0];
  assert.equal(replay.replayed, true);
  assert.deepEqual(replay.accepted_hlc, fallbackOutcome.accepted_hlc, "replay preserves accepted HLC");
  assertLwwInvariants(state);
});

function rng(seed) {
  let state = seed >>> 0 || 0x6d2b79f5;
  return () => {
    state ^= state << 13;
    state ^= state >>> 17;
    state ^= state << 5;
    return state >>> 0;
  };
}

function pick(random, values) {
  return values[random() % values.length];
}

function runSeed(seed, steps = 300) {
  const random = rng(seed);
  const state = createLwwState();
  const actions = [];
  const known = [];
  const resources = ["brightness", "visual_mode", "speed_profile", "simple_effect", "gps_quality", "geofence_policy"];
  const device = "f0000000-0000-4000-8000-000000000001";
  const users = [
    "10000000-0000-4000-8000-000000000001",
    "20000000-0000-4000-8000-000000000002",
  ];
  let now = 1_800_000_000_000;
  let nextId = 1;
  let localSequence = 1;

  for (let step = 0; step < steps; step += 1) {
    now += random() % 4;
    const mode = random() % 10;
    if (mode < 3) {
      const resource = pick(random, resources);
      const head = state.heads.get(resource);
      const stale = mode === 0 && head;
      const mutation = webMutation({
        id: nextId++, resource, value: random() % 256,
        base: stale ? Math.max(0, head.server_version - 1) : head?.server_version ?? 0,
        actor: pick(random, users),
      });
      const outcome = applyWebMutation(state, mutation, now);
      actions.push({ type: "web", mutation, now, outcome });
      if (outcome.disposition !== "stale") known.push(mutation);
    } else if (mode === 9 && known.length > 0) {
      const original = structuredClone(pick(random, known));
      const conflict = (random() & 3) === 0;
      if (conflict) original.body.value = (original.body.value + 1) % 256;
      const snapshot = JSON.stringify({ clock: state.clock, heads: [...state.heads], revisions: [...state.revisions] });
      try {
        const outcome = original.actor_id
          ? applyWebMutation(state, original, now)
          : applyDeviceBatch(state, [original], now)[0];
        if (conflict) throw new Error("conflicting_replay_was_accepted");
        if (!outcome.replayed) throw new Error("exact_replay_not_marked");
      } catch (error) {
        if (!conflict || error.message !== "mutation_id_conflict") throw error;
      }
      const after = JSON.stringify({ clock: state.clock, heads: [...state.heads], revisions: [...state.revisions] });
      if (snapshot !== after) throw new Error("replay_changed_state");
      actions.push({ type: conflict ? "conflicting_replay" : "exact_replay", mutation: original, now });
    } else {
      const batchSize = 1 + random() % 4;
      const batch = [];
      for (let index = 0; index < batchSize; index += 1) {
        const quality = pick(random, ["unknown", "approximate_persisted", "server_anchored", "sntp_synced", "gnss_trusted"]);
        const outOfWindow = (random() & 7) === 0;
        const offset = outOfWindow ? 600_001 + random() % 1000 : (random() % 1_200_001) - 600_000;
        batch.push(deviceMutation({
          id: nextId++, sequence: localSequence++, resource: pick(random, resources),
          value: random() % 256, physical: quality === "unknown" ? 0 : now + offset,
          logical: random() % 5, quality, actor: device,
        }));
      }
      if ((random() & 1) === 0) batch.reverse();
      const outcomes = applyDeviceBatch(state, batch, now);
      actions.push({ type: "device_batch", batch, now, outcomes });
      known.push(...batch);
      const fallback = outcomes.filter((outcome) => outcome.ordering === "fallback_received");
      for (let index = 1; index < fallback.length; index += 1) {
        if (compareStamp(fallback[index - 1].accepted_hlc, fallback[index].accepted_hlc) >= 0) {
          throw new Error("fallback_batch_not_strictly_increasing");
        }
      }
    }
    assertLwwInvariants(state);
  }
  return actions;
}

test("randomized LWW state machines retain a replayable artifact for every failing seed", async () => {
  const explicit = process.env.DOG_RGB_LWW_SEED;
  const seeds = explicit === undefined
    ? [...new Set([
      ...regressionSeeds,
      ...Array.from({ length: 96 }, (_, index) => Math.imul(index + 1, 0x9e3779b9) >>> 0),
    ])]
    : [Number.parseInt(explicit, 0) >>> 0];

  for (const seed of seeds) {
    try {
      runSeed(seed);
    } catch (error) {
      const directory = new URL("../../test-results/lww-failures/", import.meta.url);
      await mkdir(directory, { recursive: true });
      const artifact = new URL(`${seed}.json`, directory);
      let actions = [];
      try {
        actions = runSeed(seed);
      } catch {
        // The deterministic seed is sufficient to reproduce; actions may be
        // unavailable when the failure occurs before runSeed returns.
      }
      await writeFile(artifact, JSON.stringify({ seed, error: error.stack, actions }, null, 2));
      assert.fail(`LWW seed ${seed} failed; retained ${artifact.pathname}: ${error.stack}`);
    }
  }
});
