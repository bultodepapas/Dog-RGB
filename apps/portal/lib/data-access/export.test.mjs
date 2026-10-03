import assert from "node:assert/strict";
import test from "node:test";

import {
  createRecordingGeoJson,
  DOG_EXPORT_MAX_BYTES,
  DOG_EXPORT_MAX_POINTS,
  ExportDocumentError,
  parseDogExportDocument,
  serializeExport,
} from "./export-core.ts";
import { exportErrorResponse, privateDownloadHeaders } from "./export-http.ts";

const DOG_ID = "20000000-0000-4000-8000-000000000001";
const COLLAR_ID = "30000000-0000-4000-8000-000000000001";
const RECORDING_ID = "40000000-0000-4000-8000-000000000001";
const BASE_TIME = Date.parse("2026-08-25T10:00:00.000Z");

function point(sequence, overrides = {}) {
  const lat_e7 = 47_110_000 + sequence;
  const lon_e7 = -740_721_000 + sequence;
  return {
    recording_id: RECORDING_ID,
    collar_id: COLLAR_ID,
    boot_sequence: 7,
    chunk_sequence: 9,
    point_sequence: sequence,
    recorded_at: new Date(BASE_TIME + sequence * 5_000).toISOString(),
    received_at: new Date(BASE_TIME + sequence * 5_000).toISOString(),
    lat_e7,
    lon_e7,
    latitude: lat_e7 / 10_000_000,
    longitude: lon_e7 / 10_000_000,
    reported_speed_cmps: 123,
    satellites: 9,
    flags: 0x05,
    time_quality: "gnss_trusted",
    telemetry_schema: 3,
    firmware_version: "test-fw",
    ...overrides,
  };
}

function document(points = [], lossMarkers = []) {
  return parseDogExportDocument({
    schema_version: 1,
    complete: true,
    export_type: "recording_geojson_source",
    snapshot_at: "2026-08-25T12:00:00.000Z",
    timezone: "America/Bogota",
    units: { distance: "m", speed: "cm/s", weight: "kg" },
    dog: { id: DOG_ID, name: "Mora", timezone: "America/Bogota" },
    collars: [{ id: COLLAR_ID }],
    recordings: [{
      id: RECORDING_ID,
      collar_id: COLLAR_ID,
      boot_sequence: 7,
      timezone_at_start: "America/Bogota",
      clock_quality: "gnss_trusted",
    }],
    daily_summaries: [],
    recording_summaries: [],
    configuration: { resource_heads: [], device_reported: [], revisions: [] },
    telemetry_points: points,
    loss_markers: lossMarkers,
  }, DOG_ID, RECORDING_ID);
}

test("export document validation requires a complete, matching snapshot", () => {
  const valid = document([point(1)]);
  assert.equal(valid.complete, true);
  assert.equal(valid.telemetry_points.length, 1);

  const base = {
    schema_version: 1,
    export_type: "recording_geojson_source",
    snapshot_at: "2026-08-25T12:00:00.000Z",
    timezone: "America/Bogota",
    units: {},
    dog: { id: DOG_ID, name: "Mora", timezone: "America/Bogota" },
    collars: [],
    recordings: [{ id: RECORDING_ID }],
    daily_summaries: [],
    recording_summaries: [],
    configuration: { resource_heads: [], device_reported: [], revisions: [] },
    telemetry_points: [],
    loss_markers: [],
  };
  assert.throws(() => parseDogExportDocument({ ...base, complete: false }, DOG_ID, RECORDING_ID), ExportDocumentError);
  assert.throws(() => parseDogExportDocument({ ...base, complete: true }, "20000000-0000-4000-8000-000000000099", RECORDING_ID), ExportDocumentError);
  assert.throws(() => parseDogExportDocument({ ...base, complete: true, recordings: [{ id: "40000000-0000-4000-8000-000000000099" }] }, DOG_ID, RECORDING_ID), ExportDocumentError);
});

test("GeoJSON retains every observation and splits lines at explicit, invalid, and sequence gaps", () => {
  const source = document([
    point(1),
    point(2),
    point(3, { flags: 0x20, lat_e7: null, lon_e7: null, latitude: null, longitude: null, reported_speed_cmps: null }),
    point(4),
    point(6),
    point(7, { flags: 0, lat_e7: null, lon_e7: null, latitude: null, longitude: null, recorded_at: null, time_quality: "unknown", reported_speed_cmps: null }),
    point(8),
  ], [{
    recording_id: RECORDING_ID,
    collar_id: COLLAR_ID,
    boot_sequence: 7,
    id: "50000000-0000-4000-8000-000000000001",
    first_missing_point_sequence: 5,
    last_missing_point_sequence: 5,
    dropped_points: 1,
    reason: "buffer_overflow",
    recorded_at: "2026-08-25T10:01:00.000Z",
    recorded_utc_ms: null,
  }]);
  const geojson = createRecordingGeoJson(source, RECORDING_ID);
  const pointFeatures = geojson.features.filter((feature) => feature.id?.startsWith("point-"));
  const lines = geojson.features.filter((feature) => feature.properties.kind === "track_segment");
  const losses = geojson.features.filter((feature) => feature.properties.kind === "telemetry_loss");

  assert.equal(geojson.type, "FeatureCollection");
  assert.equal(geojson.complete, true);
  assert.equal(pointFeatures.length, 7);
  assert.equal(lines.length, 1);
  assert.deepEqual(lines[0].geometry.coordinates, [
    [point(1).longitude, point(1).latitude],
    [point(2).longitude, point(2).latitude],
  ]);
  assert.equal(pointFeatures.find((feature) => feature.id === "point-3").geometry, null);
  assert.equal(pointFeatures.find((feature) => feature.id === "point-7").geometry, null);
  assert.equal(losses.length, 1);
  assert.equal(losses[0].properties.dropped_points, 1);
  assert.equal(geojson.metadata.timezone, "America/Bogota");
});

test("GeoJSON also splits when retained timestamps have a gap over 65 seconds", () => {
  const geojson = createRecordingGeoJson(document([
    point(1),
    point(2, { recorded_at: new Date(BASE_TIME + 80_000).toISOString() }),
  ]), RECORDING_ID);
  assert.equal(geojson.features.filter((feature) => feature.properties.kind === "track_segment").length, 0);
  assert.equal(geojson.features.filter((feature) => feature.geometry?.type === "Point").length, 2);
});

test("GeoJSON does not connect observations with missing, reversed, or changed clock evidence", () => {
  const cases = [
    [point(1, { recorded_at: null, time_quality: "unknown", flags: 0x01 }), point(2, { recorded_at: null, time_quality: "unknown", flags: 0x01 })],
    [point(1), point(2, { recorded_at: new Date(BASE_TIME).toISOString() })],
    [point(1), point(2, { time_quality: "sntp_synced" })],
    [point(1), point(2, { telemetry_schema: 2 })],
  ];

  for (const points of cases) {
    const geojson = createRecordingGeoJson(document(points), RECORDING_ID);
    assert.equal(geojson.features.filter((feature) => feature.properties.kind === "track_segment").length, 0);
  }
});

test("complete exports are byte bounded and error pages stay private and generic", async () => {
  assert.ok(DOG_EXPORT_MAX_POINTS > 0);
  assert.ok(DOG_EXPORT_MAX_BYTES > 0);
  assert.throws(() => serializeExport({ data: "x".repeat(DOG_EXPORT_MAX_BYTES) }), ExportDocumentError);

  const response = exportErrorResponse("limit_exceeded", DOG_ID, `/app/${DOG_ID}/data/export`);
  const html = await response.text();
  assert.equal(response.status, 413);
  assert.equal(response.headers.get("cache-control"), "private, no-store");
  assert.equal(response.headers.get("content-type"), "text/html; charset=utf-8");
  assert.match(html, /No se entregó un archivo parcial/u);
  assert.doesNotMatch(html, /stack|54000|request_id|secret_digest/iu);
  assert.equal(privateDownloadHeaders("application/geo+json; charset=utf-8", "recording.geojson").get("content-disposition"), 'attachment; filename="recording.geojson"');
});
