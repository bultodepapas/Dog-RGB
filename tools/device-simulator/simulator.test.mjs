import test from "node:test";
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";

test("simulator uses the frozen device-v1 fixture", async () => {
  const fixture = JSON.parse(await readFile(
    new URL("../../contracts/device-v1/fixtures/valid/device-v1-sync-request.json", import.meta.url),
    "utf8",
  ));
  assert.equal(fixture.protocol_version, 1);
  assert.equal(fixture.device.telemetry_schema, 3);
  assert.equal(fixture.device.config_schema, 7);
  assert.equal(fixture.upload.chunks[0].points.length, 3);
});
