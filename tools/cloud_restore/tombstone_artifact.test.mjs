import assert from "node:assert/strict";
import { createHash, generateKeyPairSync, sign as cryptoSign } from "node:crypto";
import test from "node:test";
import {
  canonicalJson,
  signTombstoneArtifact,
  verifyTombstoneArtifact,
  verifyTombstoneArtifactChain,
} from "./tombstone_artifact.mjs";

const REQUEST_ID_1 = "ea000000-0000-4000-8000-000000000001";
const REQUEST_ID_2 = "ea000000-0000-4000-8000-000000000002";
const HASH = Buffer.alloc(32, 0xa5).toString("base64url");
const CONTEXT_ID = "local/Dog-RGB-1/test";

function item(requestId, requestedAt) {
  return {
    schema_version: "dog-deletion-tombstone-v1",
    request_id: requestId,
    request_sha256: HASH,
    scope: "dog",
    scope_id: "30000000-0000-4000-8000-000000000003",
    confirmation_version: "dog-delete-v1",
    requested_by_sha256: HASH,
    requested_at: requestedAt,
    tombstone_sha256: HASH,
    replay_sha256: HASH,
  };
}

function page(items, hasMore) {
  return {
    schema_version: "dog-deletion-tombstone-export-v1",
    items,
    has_more: hasMore,
    next_cursor: items.length === 0 ? null : {
      requested_at: items.at(-1).requested_at,
      request_id: items.at(-1).request_id,
    },
  };
}

function keys() {
  return generateKeyPairSync("ed25519");
}

test("canonical JSON is stable and rejects values JSON would silently alter", () => {
  assert.equal(
    canonicalJson({ z: [true, null, "ñ"], a: { y: 2, x: 1 } }),
    '{"a":{"x":1,"y":2},"z":[true,null,"ñ"]}',
  );
  assert.equal(canonicalJson({ b: 2, a: 1 }), canonicalJson({ a: 1, b: 2 }));
  assert.throws(() => canonicalJson({ invalid: undefined }), /unsupported JSON type/u);
  assert.throws(() => canonicalJson({ invalid: Number.NaN }), /non-finite/u);
  const cyclic = {};
  cyclic.self = cyclic;
  assert.throws(() => canonicalJson(cyclic), /cyclic/u);
});

test("one complete Ed25519 artifact verifies only with its trusted key ID", () => {
  const { privateKey, publicKey } = keys();
  const artifact = signTombstoneArtifact({
    page: page([item(REQUEST_ID_1, "2026-08-18T18:00:00.000001Z")], false),
    keyId: "local-test-key-v1",
    contextId: CONTEXT_ID,
    privateKey,
    createdAt: "2026-08-18T18:01:00.000Z",
  });
  const verified = verifyTombstoneArtifact(
    artifact,
    new Map([["local-test-key-v1", publicKey]]),
    { expectedContextId: CONTEXT_ID },
  );
  assert.equal(verified.page.items[0].request_id, REQUEST_ID_1);
  assert.throws(
    () => verifyTombstoneArtifact(
      artifact,
      new Map([["local-test-key-v1", publicKey]]),
      { expectedContextId: "hosted/another-project" },
    ),
    /context_id mismatch/u,
  );
  assert.throws(
    () => verifyTombstoneArtifact(
      artifact,
      new Map([["other-key", publicKey]]),
      { expectedContextId: CONTEXT_ID },
    ),
    /untrusted key_id/u,
  );
});

test("recomputed digest cannot make a modified payload pass its signature", () => {
  const { privateKey, publicKey } = keys();
  const artifact = signTombstoneArtifact({
    page: page([item(REQUEST_ID_1, "2026-08-18T18:00:00.000001Z")], false),
    keyId: "local-test-key-v1",
    contextId: CONTEXT_ID,
    privateKey,
    createdAt: "2026-08-18T18:01:00.000Z",
  });
  const modified = structuredClone(artifact);
  modified.payload.page.items[0].scope_id = "30000000-0000-4000-8000-000000000099";
  const bytes = Buffer.from(canonicalJson(modified.payload), "utf8");
  modified.payload_sha256 = createHash("sha256").update(bytes).digest("hex");
  assert.throws(
    () => verifyTombstoneArtifact(
      modified,
      { "local-test-key-v1": publicKey },
      { expectedContextId: CONTEXT_ID },
    ),
    /signature verification failed/u,
  );
});

test("a complete paginated chain binds sequence, digest, cursor, and uniqueness", () => {
  const { privateKey, publicKey } = keys();
  const firstPage = page([item(REQUEST_ID_1, "2026-08-18T18:00:00.000001Z")], true);
  const first = signTombstoneArtifact({
    page: firstPage,
    keyId: "local-test-key-v1",
    contextId: CONTEXT_ID,
    privateKey,
    createdAt: "2026-08-18T18:01:00.000Z",
  });
  const second = signTombstoneArtifact({
    page: page([item(REQUEST_ID_2, "2026-08-18T18:00:00.000002Z")], false),
    keyId: "local-test-key-v1",
    contextId: CONTEXT_ID,
    privateKey,
    createdAt: "2026-08-18T18:01:01.000Z",
    batchSequence: 1,
    previousPayloadSha256: first.payload_sha256,
    afterCursor: firstPage.next_cursor,
  });
  const verified = verifyTombstoneArtifactChain(
    [first, second],
    new Map([["local-test-key-v1", publicKey]]),
    { expectedContextId: CONTEXT_ID },
  );
  assert.deepEqual(verified.items.map(({ request_id: requestId }) => requestId), [
    REQUEST_ID_1,
    REQUEST_ID_2,
  ]);
  assert.equal(verified.complete, true);

  const broken = structuredClone(second);
  broken.payload.after_cursor.request_id = REQUEST_ID_2;
  const bytes = Buffer.from(canonicalJson(broken.payload), "utf8");
  broken.payload_sha256 = createHash("sha256").update(bytes).digest("hex");
  broken.signature = cryptoSign(null, bytes, privateKey).toString("base64url");
  assert.throws(
    () => verifyTombstoneArtifactChain(
      [first, broken],
      new Map([["local-test-key-v1", publicKey]]),
      { expectedContextId: CONTEXT_ID },
    ),
    /continuation cursor/u,
  );
});

test("incomplete, duplicate, and non-canonical export sets fail closed", () => {
  const { privateKey, publicKey } = keys();
  const firstPage = page([item(REQUEST_ID_1, "2026-08-18T18:00:00.000001Z")], true);
  const first = signTombstoneArtifact({
    page: firstPage,
    keyId: "local-test-key-v1",
    contextId: CONTEXT_ID,
    privateKey,
    createdAt: "2026-08-18T18:01:00.000Z",
  });
  assert.throws(
    () => verifyTombstoneArtifactChain(
      [first],
      new Map([["local-test-key-v1", publicKey]]),
      { expectedContextId: CONTEXT_ID },
    ),
    /incomplete/u,
  );
  const duplicate = signTombstoneArtifact({
    page: page([item(REQUEST_ID_1, "2026-08-18T18:00:00.000002Z")], false),
    keyId: "local-test-key-v1",
    contextId: CONTEXT_ID,
    privateKey,
    createdAt: "2026-08-18T18:01:01.000Z",
    batchSequence: 1,
    previousPayloadSha256: first.payload_sha256,
    afterCursor: firstPage.next_cursor,
  });
  assert.throws(
    () => verifyTombstoneArtifactChain(
      [first, duplicate],
      new Map([["local-test-key-v1", publicKey]]),
      { expectedContextId: CONTEXT_ID },
    ),
    /request_id appears in multiple pages/u,
  );
  assert.throws(
    () => signTombstoneArtifact({
      page: page([
        item(REQUEST_ID_2, "2026-08-18T18:00:00.000002Z"),
        item(REQUEST_ID_1, "2026-08-18T18:00:00.000001Z"),
      ], false),
      keyId: "local-test-key-v1",
      contextId: CONTEXT_ID,
      privateKey,
      createdAt: "2026-08-18T18:01:00.000Z",
    }),
    /strictly cursor ordered/u,
  );
});
