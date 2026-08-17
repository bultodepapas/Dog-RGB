import { gzipSync } from "node:zlib";

function makePoint(sequence, epochMs, moving) {
  const phase = sequence / 900;
  const latE7 = 47_110_000 + Math.round(16_000 * Math.sin(phase));
  const lonE7 = -740_721_000 + Math.round(22_000 * Math.cos(sequence / 1_100));
  const speedCmps = moving ? 110 + (sequence % 510) : 0;
  const satellites = 6 + (sequence % 9);
  const flags =
    0x01 | // FIX_VALID
    0x04 | // TIME_TRUSTED: every generated point has non-zero recorded UTC
    (moving ? 0x02 : 0x08) | // movement or stationary evidence, never both
    (sequence % 97 === 0 ? 0x10 : 0); // LOW_QUALITY
  return { sequence, epochMs, latE7, lonE7, speedCmps, satellites, flags };
}

function makeContinuousDay() {
  const start = Date.parse("2026-01-10T05:00:00.000Z");
  return Array.from({ length: 17_280 }, (_, index) =>
    makePoint(index + 1, start + (index + 1) * 5_000, index % 29 >= 20),
  );
}

function makeAdaptiveDay() {
  const start = Date.parse("2026-01-10T05:00:00.000Z");
  const moving = Array.from({ length: 2_880 }, (_, index) =>
    makePoint(index + 1, start + (index + 1) * 5_000, true),
  );
  const stationaryStart = start + 4 * 60 * 60 * 1_000;
  const stationary = Array.from({ length: 1_200 }, (_, index) =>
    makePoint(
      moving.length + index + 1,
      stationaryStart + (index + 1) * 60_000,
      false,
    ),
  );
  return [...moving, ...stationary];
}

function encode(points, compact) {
  const encodedPoints = compact
    ? points.map((point) => [
        point.sequence,
        point.epochMs,
        point.latE7,
        point.lonE7,
        point.speedCmps,
        point.satellites,
        point.flags,
      ])
    : points;

  return Buffer.from(
    JSON.stringify({
      protocol_version: 1,
      device_id: "018f6a94-59d8-7a21-9b56-6f4d1ee06000",
      boot_sequence: 1,
      telemetry_schema: 3,
      points: encodedPoints,
    }),
  );
}

function report(name, points) {
  for (const compact of [true, false]) {
    const body = encode(points, compact);
    const gzip = gzipSync(body, { level: 6 });
    console.log(
      JSON.stringify({
        profile: name,
        representation: compact ? "compact-tuples" : "named-objects",
        points: points.length,
        json_bytes: body.byteLength,
        gzip_bytes: gzip.byteLength,
        json_bytes_per_point: Number((body.byteLength / points.length).toFixed(2)),
        gzip_bytes_per_point: Number((gzip.byteLength / points.length).toFixed(2)),
      }),
    );
  }
}

report("continuous-5-second-day", makeContinuousDay());
report("adaptive-4h-moving-20h-stationary", makeAdaptiveDay());
