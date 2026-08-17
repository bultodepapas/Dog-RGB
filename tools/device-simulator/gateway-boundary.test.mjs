import test from "node:test";
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import {
  boundedJson,
  HttpProblem,
  problem,
  validateSyncSemantics,
} from "../../supabase/functions/_shared/gateway.ts";

const syncFixtureUrl = new URL("../../contracts/device-v1/fixtures/valid/device-v1-sync-request.json", import.meta.url);
const syncFixture = async () => JSON.parse(await readFile(syncFixtureUrl, "utf8"));

function streamRequest(body, headers = {}) {
  const bytes = typeof body === "string" ? Buffer.from(body) : body;
  return new Request("http://localhost/functions/v1/test", {
    method: "POST",
    headers: { "content-type": "application/json", ...headers },
    body: new ReadableStream({
      start(controller) {
        controller.enqueue(bytes);
        controller.close();
      },
    }),
    duplex: "half",
  });
}

test("boundedJson requires an explicit length when the endpoint requests it", async () => {
  await assert.rejects(() => boundedJson(streamRequest("{}"), 4096), {
    name: "Error",
    status: 411,
    code: "length_required",
  });
  const parsed = await boundedJson(streamRequest("{}"), 4096, false);
  assert.deepEqual(parsed.body, {});
});

test("boundedJson rejects JSON-like media type suffixes", async () => {
  await assert.rejects(() => boundedJson(streamRequest("{}", {
    "content-type": "application/jsonp",
    "content-length": "2",
  }), 4096), {
    status: 415,
    code: "unsupported_media_type",
  });
});

test("boundedJson rejects invalid UTF-8 before JSON validation", async () => {
  const invalidUtf8 = Buffer.from([0x7b, 0x22, 0x78, 0x22, 0x3a, 0x22, 0xc3, 0x28, 0x22, 0x7d]);
  await assert.rejects(() => boundedJson(streamRequest(invalidUtf8, {
    "content-length": String(invalidUtf8.byteLength),
  }), 4096), {
    status: 400,
    code: "malformed_json",
  });
});

test("boundedJson drains declared oversize bodies and bounds streamed bodies", async () => {
  const declared = streamRequest("{}", { "content-length": "4097" });
  await assert.rejects(() => boundedJson(declared, 4096), {
    status: 413,
    code: "payload_too_large",
  });
  assert.equal(declared.bodyUsed, true);

  const streamed = streamRequest(`{"padding":"${"x".repeat(128)}"}`);
  await assert.rejects(() => boundedJson(streamed, 32, false), {
    status: 413,
    code: "payload_too_large",
  });
  assert.equal(streamed.bodyUsed, true);
});

test("boundedJson fails closed on extreme nesting without recursive traversal", async () => {
  const raw = `${'{"value":'.repeat(10_000)}null${"}".repeat(10_000)}`;
  await assert.rejects(() => boundedJson(streamRequest(raw, {
    "content-length": String(Buffer.byteLength(raw)),
  }), 128 * 1024), {
    status: 400,
    code: "malformed_json",
  });
});

test("problem responses include required method and authentication metadata", () => {
  const method = problem(new HttpProblem(405, "method_not_allowed", "Method not allowed", "Use POST."));
  assert.equal(method.headers.get("allow"), "POST");
  assert.equal(method.headers.get("cache-control"), "no-store");

  const auth = problem(new HttpProblem(401, "device_credential_invalid", "Invalid", "Invalid."));
  assert.equal(auth.headers.get("www-authenticate"), 'Bearer realm="dog-rgb-device"');
});

test("sync semantics reject overlapping point ranges before persistence", async () => {
  const body = await syncFixture();
  const overlapping = structuredClone(body.upload.chunks[0]);
  overlapping.chunk_sequence += 1;
  overlapping.first_point_sequence = 1;
  body.upload.chunks.push(overlapping);
  await assert.rejects(() => validateSyncSemantics(body), {
    status: 422,
    code: "invalid_telemetry",
  });
});

test("sync semantics require exact summary duration accounting", async () => {
  const body = await syncFixture();
  body.upload.summaries[0].inactive_s -= 1;
  await assert.rejects(() => validateSyncSemantics(body), {
    status: 422,
    code: "invalid_telemetry",
  });
});

test("sync semantics accept a disjoint loss marker and reject one covering an uploaded point", async () => {
  const valid = await syncFixture();
  valid.upload.loss_markers = [{
    marker_id: "88888888-8888-4888-8888-888888888888",
    boot_sequence: 42,
    first_missing_point_sequence: 3,
    last_missing_point_sequence: 4,
    lost_points: 2,
    reason: "storage_pressure",
    recorded_utc_ms: null,
  }];
  await validateSyncSemantics(valid);

  const overlapping = structuredClone(valid);
  overlapping.upload.loss_markers[0].first_missing_point_sequence = 2;
  overlapping.upload.loss_markers[0].last_missing_point_sequence = 3;
  await assert.rejects(() => validateSyncSemantics(overlapping), {
    status: 422,
    code: "invalid_telemetry",
  });
});
