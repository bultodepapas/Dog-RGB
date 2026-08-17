import test from "node:test";
import assert from "node:assert/strict";
import {
  boundedJson,
  HttpProblem,
  problem,
} from "../../supabase/functions/_shared/gateway.ts";

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
