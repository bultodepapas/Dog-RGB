import test from "node:test";
import assert from "node:assert/strict";
import {
  CLOUD_ANALYTICS_RULES,
  CLOUD_ANALYTICS_VERSION,
  computeSummaryV1,
  coverageRatio,
} from "./index.js";

const F = CLOUD_ANALYTICS_RULES.flags;

function point({
  collar = "c1",
  boot = 1,
  sequence,
  time,
  kind = "stationary",
  speed = kind === "moving" ? 100 : 0,
  flags = null,
  quality = "gnss_trusted",
  schema = 3,
  lat = 0,
  lon = 0,
}) {
  const evidence =
    flags ??
    (F.timeTrusted |
      F.fixValid |
      (kind === "moving" ? F.movementEvidence : F.stationaryHeartbeat));
  return {
    collar_id: collar,
    boot_sequence: boot,
    point_sequence: sequence,
    recorded_at: time,
    lat_e7: lat,
    lon_e7: lon,
    reported_speed_cmps: speed,
    flags: evidence,
    time_quality: quality,
    telemetry_schema: schema,
  };
}

test("coverage is bounded and invalid windows have typed absence", () => {
  assert.equal(coverageRatio(30, 60), 0.5);
  assert.equal(coverageRatio(90, 60), 1);
  assert.equal(coverageRatio(0, 0), null);
  assert.equal(coverageRatio(Number.NaN, 10), null);
  assert.equal(CLOUD_ANALYTICS_VERSION, 1);
});

test("v1 publishes units, thresholds, and bounded input limits", () => {
  assert.equal(CLOUD_ANALYTICS_RULES.units.duration, "seconds");
  assert.equal(CLOUD_ANALYTICS_RULES.units.distance, "meters");
  assert.equal(CLOUD_ANALYTICS_RULES.units.speed, "centimeters_per_second");
  assert.equal(CLOUD_ANALYTICS_RULES.minMovingSpeedCmps, 20);
  assert.equal(CLOUD_ANALYTICS_RULES.maxSpeedCmps, 1111);
  assert.equal(CLOUD_ANALYTICS_RULES.minDistanceM, 3);
  assert.equal(CLOUD_ANALYTICS_RULES.maxIntervalS, 65);
  assert.equal(CLOUD_ANALYTICS_RULES.maxRecordingPoints, 250_000);
});

test("moving, stationary, transition, and unknown intervals conserve the window", () => {
  const result = computeSummaryV1({
    windowStart: 100,
    windowEnd: 160,
    points: [
      point({ sequence: 1, time: 100 }),
      point({ sequence: 2, time: 110 }),
      point({ sequence: 3, time: 120, kind: "moving", lon: 0 }),
      point({ sequence: 4, time: 130, kind: "moving", lon: 1000 }),
      point({ sequence: 5, time: 140, kind: "moving", lon: 2000 }),
      point({ sequence: 6, time: 150 }),
    ],
  });

  assert.equal(result.summary_status, "available");
  assert.equal(result.observed_s, 30);
  assert.equal(result.moving_s, 20);
  assert.equal(result.inactive_s, 10);
  assert.equal(result.unknown_s, 30);
  assert.equal(result.observed_s + result.unknown_s, 60);
  assert.equal(result.distance_m, 22);
  assert.equal(result.average_moving_cmps, 111);
  assert.equal(result.filtered_max_speed_cmps, 111);
  assert.equal(result.coverage_ratio, 0.5);
});

test("identical point replays collapse to one input and produce the same summary", () => {
  const stationary = point({ sequence: 1, time: 0 });
  const next = point({ sequence: 2, time: 10 });
  const expected = computeSummaryV1({
    points: [stationary, next],
    windowStart: 0,
    windowEnd: 60,
  });
  const replayed = computeSummaryV1({
    points: [stationary, next, structuredClone(stationary), structuredClone(next)],
    windowStart: 0,
    windowEnd: 60,
  });
  assert.deepEqual(replayed, expected);
});

test("poor-fix, legacy, unknown-clock, and invalid duration never become observed time", () => {
  const noWindow = computeSummaryV1({ points: [], windowStart: 5, windowEnd: 5 });
  assert.equal(noWindow.summary_status, "insufficient_time_evidence");
  assert.equal(noWindow.observed_s, null);
  assert.equal(noWindow.coverage_ratio, null);

  const unknownClock = computeSummaryV1({
    points: [
      point({
        sequence: 1,
        time: null,
        quality: "unknown",
        flags: F.stationaryHeartbeat,
      }),
    ],
    windowStart: 0,
    windowEnd: 60,
  });
  assert.equal(unknownClock.summary_status, "insufficient_time_evidence");
  assert.equal(unknownClock.observed_s, null);

  const poorFix = computeSummaryV1({
    points: [
      point({ sequence: 1, time: 0 }),
      point({ sequence: 2, time: 10, flags: F.timeTrusted | F.lowQuality }),
      point({ sequence: 3, time: 20 }),
    ],
    windowStart: 0,
    windowEnd: 60,
  });
  assert.equal(poorFix.summary_status, "available");
  assert.equal(poorFix.observed_s, 0);
  assert.equal(poorFix.unknown_s, 60);
  assert.equal(poorFix.distance_m, null);
  assert.equal(poorFix.warning_points, 1);

  const legacy = computeSummaryV1({
    points: [
      point({ sequence: 1, time: 0, quality: "legacy_minute", schema: 2 }),
      point({ sequence: 2, time: 10, quality: "legacy_minute", schema: 2 }),
    ],
    windowStart: 0,
    windowEnd: 60,
  });
  assert.equal(legacy.summary_status, "insufficient_time_evidence");
  assert.equal(legacy.observed_s, null);
});

test("intervals over 65 seconds stay unknown and sequence holes are counted", () => {
  const result = computeSummaryV1({
    points: [
      point({ sequence: 1, time: 0 }),
      point({ sequence: 3, time: 70 }),
    ],
    windowStart: 0,
    windowEnd: 100,
  });
  assert.equal(result.summary_status, "available");
  assert.equal(result.observed_s, 0);
  assert.equal(result.unknown_s, 100);
  assert.equal(result.gap_count, 2);
});

test("overlapping collars are unioned and conflicting classifications stay unknown", () => {
  const result = computeSummaryV1({
    points: [
      point({ collar: "moving-collar", sequence: 1, time: 0, kind: "moving" }),
      point({ collar: "moving-collar", sequence: 2, time: 20, kind: "moving" }),
      point({ collar: "stationary-collar", sequence: 1, time: 0 }),
      point({ collar: "stationary-collar", sequence: 2, time: 20 }),
    ],
    windowStart: 0,
    windowEnd: 20,
  });
  assert.equal(result.observed_s, 0);
  assert.equal(result.unknown_s, 20);
});

test("23-hour and 25-hour civil windows use their actual elapsed seconds", () => {
  const points = [point({ sequence: 1, time: 100 }), point({ sequence: 2, time: 110 })];
  const shortDay = computeSummaryV1({
    points,
    windowStart: 0,
    windowEnd: 82_800,
  });
  const longDay = computeSummaryV1({
    points,
    windowStart: 0,
    windowEnd: 90_000,
  });
  assert.equal(shortDay.unknown_s, 82_790);
  assert.equal(longDay.unknown_s, 89_990);
  assert.equal(shortDay.coverage_ratio, 0.000121);
  assert.equal(longDay.coverage_ratio, 0.000111);
});

test("oversized sources return explicit absence instead of partial completeness", () => {
  const result = computeSummaryV1({
    points: [point({ sequence: 1, time: 0 }), point({ sequence: 2, time: 10 })],
    windowStart: 0,
    windowEnd: 60,
    maxPoints: 1,
  });
  assert.equal(result.summary_status, "source_limit_exceeded");
  assert.equal(result.observed_s, null);
  assert.equal(result.distance_m, null);
});

test("known loss markers count once by identity", () => {
  const result = computeSummaryV1({
    points: [point({ sequence: 1, time: 0 }), point({ sequence: 2, time: 10 })],
    losses: [
      { id: "loss-a", dropped_points: 3 },
      { id: "loss-a", dropped_points: 3 },
      { id: "loss-b", dropped_points: 2 },
    ],
    windowStart: 0,
    windowEnd: 60,
  });
  assert.equal(result.dropped_points, 5);
});

test("moving duration without valid GPS segments does not invent zero distance or mean speed", () => {
  const flags = F.timeTrusted | F.fixValid | F.movementEvidence;
  const result = computeSummaryV1({
    points: [
      point({ sequence: 1, time: 0, kind: "moving", speed: 100, flags, lat: null, lon: null }),
      point({ sequence: 2, time: 10, kind: "moving", speed: 100, flags, lat: 0, lon: 8000 }),
      point({ sequence: 3, time: 20, kind: "moving", speed: 100, flags, lat: 0, lon: 8000 }),
    ],
    windowStart: 0,
    windowEnd: 30,
  });

  assert.equal(result.moving_s, 20);
  assert.equal(result.distance_m, null);
  assert.equal(result.average_moving_cmps, null);
  assert.equal(result.filtered_max_speed_cmps, 100);
});

test("loss-marker input is capped and does not return a partial total", () => {
  const result = computeSummaryV1({
    points: [point({ sequence: 1, time: 0 })],
    losses: [
      { id: "loss-a", dropped_points: 1 },
      { id: "loss-b", dropped_points: 1 },
    ],
    windowStart: 0,
    windowEnd: 10,
    maxPoints: 1,
  });

  assert.equal(result.summary_status, "source_limit_exceeded");
  assert.equal(result.dropped_points, null);
});
