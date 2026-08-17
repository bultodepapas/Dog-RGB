import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { readFile } from "node:fs/promises";
import { dirname, join } from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";
import {
  FIXTURE_VERSION,
  PROVIDERS,
  SCENARIOS,
  routeFacts,
  selectScenarios,
} from "./fixtures.mjs";

const root = dirname(fileURLToPath(import.meta.url));

test("fixture generation is deterministic and versioned", () => {
  assert.equal(FIXTURE_VERSION, "2026-08-13.2");
  const digest = createHash("sha256").update(JSON.stringify(SCENARIOS)).digest("hex");
  assert.equal(digest, "8c8ad9e6cc4f02f6eed2a5b3da083f8edfd519c113758faaf191a8c3038228f5");
});

test("sparse fixture is approximately one kilometre with deliberately few samples", () => {
  const scenario = SCENARIOS.find(({ id }) => id === "sparse-1km");
  assert.ok(scenario);
  const facts = routeFacts(scenario);
  assert.equal(facts.pointCount, 11);
  assert.equal(facts.gapCount, 1);
  assert.ok(facts.distanceMeters >= 950 && facts.distanceMeters <= 1_050, facts);
});

test("dense fixture represents exactly two hours at a 30-second cadence", () => {
  const scenario = SCENARIOS.find(({ id }) => id === "dense-2h");
  assert.ok(scenario);
  const facts = routeFacts(scenario);
  assert.equal(scenario.sampleIntervalSeconds, 30);
  assert.equal(facts.durationSeconds, 7_200);
  assert.equal(facts.pointCount, 241);
  assert.equal(facts.segmentCount, 240);
  assert.equal(facts.gapCount, 2);
});

test("all route points and gap indexes are finite and internally valid", () => {
  assert.equal(new Set(SCENARIOS.map(({ id }) => id)).size, SCENARIOS.length);
  for (const scenario of SCENARIOS) {
    assert.match(scenario.title, /Synthetic|route/i);
    assert.ok(scenario.coordinates.length >= 2);
    scenario.coordinates.forEach(([longitude, latitude], index) => {
      assert.ok(Number.isFinite(longitude) && longitude >= -180 && longitude <= 180, `${scenario.id}/${index}`);
      assert.ok(Number.isFinite(latitude) && latitude >= -90 && latitude <= 90, `${scenario.id}/${index}`);
      if (index > 0) assert.notDeepEqual(scenario.coordinates[index - 1], scenario.coordinates[index]);
    });
    scenario.gapIndexes.forEach((index) => {
      assert.ok(Number.isInteger(index) && index >= 0 && index < scenario.coordinates.length - 1);
    });
  }
});

test("stress selector contains only sparse and dense fixtures", () => {
  assert.deepEqual(selectScenarios("stress").map(({ id }) => id), ["sparse-1km", "dense-2h"]);
  assert.throws(() => selectScenarios("unknown"), /Unknown scenario set/);
});

test("provider matrix exposes dark, light, and outdoor variants without embedded credentials", () => {
  for (const family of ["stadia", "maptiler"]) {
    assert.deepEqual(
      Object.values(PROVIDERS).filter((provider) => provider.family === family).map(({ variant }) => variant).sort(),
      ["dark", "light", "outdoor"],
    );
  }
  assert.equal(PROVIDERS["stadia-outdoor"].style, "https://tiles.stadiamaps.com/styles/outdoors.json");
  assert.equal(PROVIDERS["maptiler-outdoor"].maptilerStyle, "outdoor-v4");
  Object.values(PROVIDERS).forEach((provider) => {
    assert.equal(Object.hasOwn(provider, "key"), false);
    assert.doesNotMatch(JSON.stringify(provider), /(?:api[_-]?key|access[_-]?token)["']?\s*[:=]\s*["'][A-Za-z0-9_-]{8,}/i);
  });
});

test("executable harness sources do not contain a committed provider secret", async () => {
  const source = (await Promise.all(
    ["app.js", "capture-evidence.mjs", "fixtures.mjs", "server.mjs"].map((name) => readFile(join(root, name), "utf8")),
  )).join("\n");
  assert.doesNotMatch(source, /(?:pk|sk)\.[A-Za-z0-9_-]{16,}/);
  assert.doesNotMatch(source, /(?:api[_-]?key|access[_-]?token)\s*[:=]\s*["'][A-Za-z0-9_-]{8,}/i);
});
