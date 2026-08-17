import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { readFile, readdir } from "node:fs/promises";
import { execFileSync } from "node:child_process";
import test from "node:test";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const ROOT = dirname(fileURLToPath(import.meta.url));
const SCHEMA_DIR = join(ROOT, "schemas");
const FIXTURE_DIR = join(ROOT, "fixtures");
const UINT32_MAX = 4_294_967_295;
const MAX_WIRE_PHYSICAL_MS = 4_102_444_800_000;
const SYNC_REQUEST_MAX_BYTES = 128 * 1024;
const SYNC_RESPONSE_MAX_BYTES = 64 * 1024;
const REVOKE_REQUEST_MAX_BYTES = 4 * 1024;
const REVOKE_RESPONSE_MAX_BYTES = 4 * 1024;
const MAX_JSON_DEPTH = 12;

async function json(path) {
  return JSON.parse(await readFile(path, "utf8"));
}

function canonicalize(value) {
  if (value === null || typeof value !== "object") return JSON.stringify(value);
  if (Array.isArray(value)) return `[${value.map(canonicalize).join(",")}]`;
  return `{${Object.keys(value)
    .sort()
    .map((key) => `${JSON.stringify(key)}:${canonicalize(value[key])}`)
    .join(",")}}`;
}

function sha256Base64Url(value) {
  return createHash("sha256").update(value).digest("base64url");
}

function deepEqual(left, right) {
  return canonicalize(left) === canonicalize(right);
}

function valueType(value) {
  if (value === null) return "null";
  if (Array.isArray(value)) return "array";
  if (Number.isInteger(value)) return "integer";
  return typeof value;
}

function typeMatches(expected, value) {
  if (Array.isArray(expected)) return expected.some((item) => typeMatches(item, value));
  if (expected === "object") return value !== null && typeof value === "object" && !Array.isArray(value);
  if (expected === "array") return Array.isArray(value);
  if (expected === "integer") return Number.isSafeInteger(value);
  if (expected === "number") return typeof value === "number" && Number.isFinite(value);
  if (expected === "null") return value === null;
  return typeof value === expected;
}

function pointerGet(root, pointer) {
  if (!pointer || pointer === "#") return root;
  assert.ok(pointer.startsWith("#/"), `unsupported JSON pointer ${pointer}`);
  return pointer
    .slice(2)
    .split("/")
    .map((part) => part.replaceAll("~1", "/").replaceAll("~0", "~"))
    .reduce((node, part) => node?.[part], root);
}

function splitRef(ref, currentId) {
  if (ref.startsWith("#")) return [currentId, ref];
  const hash = ref.indexOf("#");
  return hash === -1 ? [ref, "#"] : [ref.slice(0, hash), ref.slice(hash)];
}

function validDate(value) {
  if (!/^\d{4}-\d{2}-\d{2}$/.test(value)) return false;
  const date = new Date(`${value}T00:00:00.000Z`);
  return !Number.isNaN(date.valueOf()) && date.toISOString().slice(0, 10) === value;
}

function validDateTime(value) {
  if (!/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$/.test(value)) return false;
  const date = new Date(value);
  return !Number.isNaN(date.valueOf()) && date.toISOString() === value;
}

function validateSchema(schema, data, registry, rootSchema = schema, path = "$") {
  const errors = [];
  const add = (code, message, at = path) => errors.push({ code: `schema.${code}`, path: at, message });

  if (typeof schema === "boolean") {
    if (!schema) add("false_schema", "value is forbidden");
    return errors;
  }

  if (schema.$ref) {
    const [id, pointer] = splitRef(schema.$ref, rootSchema.$id);
    const targetRoot = registry.get(id);
    if (!targetRoot) {
      add("unresolved_ref", `unresolved schema ${id}`);
      return errors;
    }
    const target = pointerGet(targetRoot, pointer);
    if (target === undefined) {
      add("unresolved_ref", `unresolved pointer ${schema.$ref}`);
      return errors;
    }
    errors.push(...validateSchema(target, data, registry, targetRoot, path));
  }

  if (schema.allOf) {
    for (const branch of schema.allOf) {
      errors.push(...validateSchema(branch, data, registry, rootSchema, path));
    }
  }

  if (schema.anyOf) {
    const matches = schema.anyOf.filter(
      (branch) => validateSchema(branch, data, registry, rootSchema, path).length === 0,
    ).length;
    if (matches === 0) add("any_of", "must match at least one branch");
  }

  if (schema.oneOf) {
    const matches = schema.oneOf.filter(
      (branch) => validateSchema(branch, data, registry, rootSchema, path).length === 0,
    ).length;
    if (matches !== 1) add("one_of", `must match exactly one branch; matched ${matches}`);
  }

  if (schema.not && validateSchema(schema.not, data, registry, rootSchema, path).length === 0) {
    add("not", "must not match forbidden schema");
  }

  if (schema.if) {
    const conditionMatches = validateSchema(schema.if, data, registry, rootSchema, path).length === 0;
    if (conditionMatches && schema.then) {
      errors.push(...validateSchema(schema.then, data, registry, rootSchema, path));
    } else if (!conditionMatches && schema.else) {
      errors.push(...validateSchema(schema.else, data, registry, rootSchema, path));
    }
  }

  if (schema.type && !typeMatches(schema.type, data)) {
    add("type", `expected ${JSON.stringify(schema.type)}, got ${valueType(data)}`);
    return errors;
  }

  if (Object.hasOwn(schema, "const") && !deepEqual(schema.const, data)) {
    add("const", `expected constant ${JSON.stringify(schema.const)}`);
  }

  if (schema.enum && !schema.enum.some((item) => deepEqual(item, data))) {
    add("enum", `value is not in enum`);
  }

  if (typeof data === "string") {
    const length = [...data].length;
    if (schema.minLength !== undefined && length < schema.minLength) add("min_length", `minimum length is ${schema.minLength}`);
    if (schema.maxLength !== undefined && length > schema.maxLength) add("max_length", `maximum length is ${schema.maxLength}`);
    if (schema.pattern && !new RegExp(schema.pattern, "u").test(data)) add("pattern", `does not match ${schema.pattern}`);
    if (schema.format === "date" && !validDate(data)) add("format", "invalid RFC 3339 full-date");
    if (schema.format === "date-time" && !validDateTime(data)) add("format", "invalid canonical UTC date-time");
  }

  if (typeof data === "number" && Number.isFinite(data)) {
    if (schema.minimum !== undefined && data < schema.minimum) add("minimum", `minimum is ${schema.minimum}`);
    if (schema.maximum !== undefined && data > schema.maximum) add("maximum", `maximum is ${schema.maximum}`);
    if (schema.exclusiveMinimum !== undefined && data <= schema.exclusiveMinimum) add("exclusive_minimum", `must exceed ${schema.exclusiveMinimum}`);
    if (schema.exclusiveMaximum !== undefined && data >= schema.exclusiveMaximum) add("exclusive_maximum", `must be below ${schema.exclusiveMaximum}`);
  }

  if (Array.isArray(data)) {
    if (schema.minItems !== undefined && data.length < schema.minItems) add("min_items", `minimum item count is ${schema.minItems}`);
    if (schema.maxItems !== undefined && data.length > schema.maxItems) add("max_items", `maximum item count is ${schema.maxItems}`);
    if (schema.uniqueItems) {
      const keys = data.map(canonicalize);
      if (new Set(keys).size !== keys.length) add("unique_items", "array items must be unique");
    }
    if (schema.prefixItems) {
      for (let index = 0; index < Math.min(data.length, schema.prefixItems.length); index += 1) {
        errors.push(...validateSchema(schema.prefixItems[index], data[index], registry, rootSchema, `${path}/${index}`));
      }
      if (schema.items === false && data.length > schema.prefixItems.length) {
        add("additional_items", "additional tuple items are forbidden");
      } else if (schema.items && typeof schema.items === "object") {
        for (let index = schema.prefixItems.length; index < data.length; index += 1) {
          errors.push(...validateSchema(schema.items, data[index], registry, rootSchema, `${path}/${index}`));
        }
      }
    } else if (schema.items) {
      for (let index = 0; index < data.length; index += 1) {
        errors.push(...validateSchema(schema.items, data[index], registry, rootSchema, `${path}/${index}`));
      }
    }
  }

  if (data !== null && typeof data === "object" && !Array.isArray(data)) {
    const keys = Object.keys(data);
    if (schema.minProperties !== undefined && keys.length < schema.minProperties) add("min_properties", `minimum property count is ${schema.minProperties}`);
    if (schema.maxProperties !== undefined && keys.length > schema.maxProperties) add("max_properties", `maximum property count is ${schema.maxProperties}`);
    for (const required of schema.required ?? []) {
      if (!Object.hasOwn(data, required)) add("required", `missing required property ${required}`, `${path}/${required}`);
    }
    for (const [key, child] of Object.entries(schema.properties ?? {})) {
      if (Object.hasOwn(data, key)) errors.push(...validateSchema(child, data[key], registry, rootSchema, `${path}/${key}`));
    }
    if (schema.additionalProperties === false) {
      const allowed = new Set(Object.keys(schema.properties ?? {}));
      for (const key of keys) {
        if (!allowed.has(key)) add("additional_properties", `unknown property ${key}`, `${path}/${key}`);
      }
    }
  }

  return errors;
}

function semantic(code, path, message) {
  return { code: `semantic.${code}`, path, message };
}

function duplicates(values) {
  const seen = new Set();
  return values.filter((value) => (seen.has(value) ? true : (seen.add(value), false)));
}

function chunkBytes(points) {
  const buffer = Buffer.alloc(points.length * 16);
  points.forEach((point, index) => {
    const offset = index * 16;
    buffer.writeInt32LE(point[0], offset);
    buffer.writeInt32LE(point[1], offset + 4);
    buffer.writeUInt32LE(point[2], offset + 8);
    buffer.writeUInt16LE(point[3], offset + 12);
    buffer.writeUInt8(point[4], offset + 14);
    buffer.writeUInt8(point[5], offset + 15);
  });
  return buffer;
}

function jsonDepth(value) {
  if (value === null || typeof value !== "object") return 1;
  const children = Array.isArray(value) ? value : Object.values(value);
  return children.length === 0 ? 1 : 1 + Math.max(...children.map(jsonDepth));
}

function semanticConfigResource(resource, path = "$") {
  const errors = [];
  if (resource.resource_key === "speed_profile") {
    const ranges = resource.body.ranges_kph;
    for (let index = 1; index < ranges.length; index += 1) {
      if (!(ranges[index] > ranges[index - 1])) {
        errors.push(semantic("ranges_not_strictly_increasing", `${path}/body/ranges_kph/${index}`, "speed thresholds must strictly increase"));
      }
    }
  }
  if (resource.resource_key === "gps_quality" && resource.body.min_segment_m > resource.body.max_min_segment_m) {
    errors.push(semantic("gps_segment_bounds_inverted", `${path}/body`, "min_segment_m exceeds max_min_segment_m"));
  }
  return errors;
}

function semanticCapabilities(manifest, path = "$") {
  const errors = [];
  const resources = manifest.config_resources.map((item) => item.resource_key);
  for (const duplicate of duplicates(resources)) errors.push(semantic("duplicate_resource_key", `${path}/config_resources`, `duplicate ${duplicate}`));
  const effectIds = manifest.led.effects.map((item) => item.id);
  const effectKeys = manifest.led.effects.map((item) => item.key);
  const paletteIds = manifest.led.palettes.map((item) => item.id);
  const paletteKeys = manifest.led.palettes.map((item) => item.key);
  for (const duplicate of duplicates(effectIds)) errors.push(semantic("duplicate_effect_id", `${path}/led/effects`, `duplicate ${duplicate}`));
  for (const duplicate of duplicates(effectKeys)) errors.push(semantic("duplicate_effect_key", `${path}/led/effects`, `duplicate ${duplicate}`));
  for (const duplicate of duplicates(paletteIds)) errors.push(semantic("duplicate_palette_id", `${path}/led/palettes`, `duplicate ${duplicate}`));
  for (const duplicate of duplicates(paletteKeys)) errors.push(semantic("duplicate_palette_key", `${path}/led/palettes`, `duplicate ${duplicate}`));
  for (const [index, effect] of manifest.led.effects.entries()) {
    if (effect.speed_min > effect.speed_max || effect.intensity_min > effect.intensity_max) {
      errors.push(semantic("effect_bounds_inverted", `${path}/led/effects/${index}`, "effect useful bounds are inverted"));
    }
    if (effect.palette_mode === "none" && effect.default_palette_id !== null) {
      errors.push(semantic("unexpected_default_palette", `${path}/led/effects/${index}/default_palette_id`, "palette_mode none requires null"));
    }
    if (effect.default_palette_id !== null && !paletteIds.includes(effect.default_palette_id)) {
      errors.push(semantic("unknown_default_palette", `${path}/led/effects/${index}/default_palette_id`, "default palette is absent"));
    }
    if (Buffer.byteLength(effect.label, "utf8") > 96) {
      errors.push(semantic("label_too_many_bytes", `${path}/led/effects/${index}/label`, "label exceeds 96 UTF-8 bytes"));
    }
  }
  for (const [index, palette] of manifest.led.palettes.entries()) {
    if (Buffer.byteLength(palette.label, "utf8") > 96) {
      errors.push(semantic("label_too_many_bytes", `${path}/led/palettes/${index}/label`, "label exceeds 96 UTF-8 bytes"));
    }
  }
  if (!manifest.protocol_versions.includes(1)) errors.push(semantic("protocol_not_declared", `${path}/protocol_versions`, "protocol 1 missing"));
  if (!manifest.telemetry.schemas.includes(3)) errors.push(semantic("telemetry_schema_not_declared", `${path}/telemetry/schemas`, "telemetry 3 missing"));
  if (!manifest.config_schemas.includes(7)) errors.push(semantic("config_schema_not_declared", `${path}/config_schemas`, "config 7 missing"));
  return errors;
}

function semanticTelemetry(upload, path = "$") {
  const errors = [];
  const chunkIdentities = new Set();
  const pointIdentities = new Set();
  let totalPoints = 0;
  const finals = new Map();

  for (const [chunkIndex, chunk] of upload.chunks.entries()) {
    const chunkPath = `${path}/chunks/${chunkIndex}`;
    totalPoints += chunk.points.length;
    if (chunk.point_count !== chunk.points.length) {
      errors.push(semantic("point_count_mismatch", `${chunkPath}/point_count`, "point_count differs from points length"));
    }
    if (chunk.points.length > 96) {
      errors.push(semantic("chunk_point_limit_exceeded", `${chunkPath}/points`, "sealed chunks contain at most 96 points"));
    }
    if (chunk.first_point_sequence + chunk.points.length - 1 > UINT32_MAX) {
      errors.push(semantic("point_sequence_overflow", `${chunkPath}/first_point_sequence`, "derived last point exceeds uint32"));
    }
    const identity = `${chunk.boot_sequence}:${chunk.chunk_sequence}`;
    if (chunkIdentities.has(identity)) errors.push(semantic("duplicate_chunk_identity", chunkPath, `duplicate ${identity}`));
    chunkIdentities.add(identity);
    if (chunk.is_final) {
      if (finals.has(chunk.boot_sequence)) errors.push(semantic("multiple_final_chunks", chunkPath, "more than one final chunk for boot"));
      finals.set(chunk.boot_sequence, chunk.chunk_sequence);
    }

    for (const [pointIndex, point] of chunk.points.entries()) {
      const pointPath = `${chunkPath}/points/${pointIndex}`;
      const pointSequence = chunk.first_point_sequence + pointIndex;
      const pointIdentity = `${chunk.boot_sequence}:${pointSequence}`;
      if (pointIdentities.has(pointIdentity)) errors.push(semantic("overlapping_point_sequence", pointPath, `duplicate ${pointIdentity}`));
      pointIdentities.add(pointIdentity);
      const [lat, lon, utc, speed, , flags] = point;
      const fixValid = (flags & 0x01) !== 0;
      const moving = (flags & 0x02) !== 0;
      const timeTrusted = (flags & 0x04) !== 0;
      const stationary = (flags & 0x08) !== 0;
      const gap = (flags & 0x20) !== 0;
      const legacy = (flags & 0x40) !== 0;
      if ((utc !== 0) !== timeTrusted) errors.push(semantic("time_flag_mismatch", pointPath, "TIME_TRUSTED and nonzero utc_s disagree"));
      if (!fixValid && (lat !== 0 || lon !== 0 || speed !== 0xffff)) {
        errors.push(semantic("invalid_fix_payload", pointPath, "invalid fix must zero coordinates and use unavailable speed"));
      }
      if (moving && stationary) errors.push(semantic("conflicting_motion_evidence", pointPath, "movement and stationary evidence are mutually exclusive"));
      if (gap && (fixValid || moving || stationary)) errors.push(semantic("invalid_gap_payload", pointPath, "gap cannot claim fix or motion evidence"));
      if (legacy && (chunk.boot_sequence !== 0 || chunk.time_quality !== 5)) {
        errors.push(semantic("legacy_namespace_mismatch", pointPath, "legacy requires boot zero and legacy_minute"));
      }
    }

    const timestamped = chunk.points.map((point) => point[2] !== 0);
    const trustedFlags = chunk.points.map((point) => (point[5] & 0x04) !== 0);
    if (chunk.time_quality === 0 && (timestamped.some(Boolean) || trustedFlags.some(Boolean))) {
      errors.push(semantic("chunk_time_quality_mismatch", `${chunkPath}/time_quality`, "unknown chunk cannot have UTC/TIME_TRUSTED"));
    }
    if (chunk.time_quality !== 0 && (!timestamped.every(Boolean) || !trustedFlags.every(Boolean))) {
      errors.push(semantic("chunk_time_quality_mismatch", `${chunkPath}/time_quality`, "known chunk requires UTC/TIME_TRUSTED on every point"));
    }
    const legacyFlags = chunk.points.map((point) => (point[5] & 0x40) !== 0);
    if (chunk.boot_sequence === 0 && (chunk.time_quality !== 5 || !legacyFlags.every(Boolean))) {
      errors.push(semantic("legacy_namespace_mismatch", chunkPath, "boot zero is reserved for legacy conversion"));
    }
    if (chunk.boot_sequence !== 0 && legacyFlags.some(Boolean)) {
      errors.push(semantic("legacy_namespace_mismatch", chunkPath, "native boot cannot carry legacy points"));
    }
    if (chunk.time_quality === 5 && !legacyFlags.every(Boolean)) {
      errors.push(semantic("legacy_namespace_mismatch", chunkPath, "legacy_minute requires legacy flags"));
    }
    const times = chunk.points.map((point) => point[2]).filter(Boolean);
    if (times.some((value, index) => index > 0 && value < times[index - 1])) {
      errors.push(semantic("chunk_time_not_monotonic", `${chunkPath}/points`, "timestamps moved backwards"));
    }

    const actualHash = sha256Base64Url(chunkBytes(chunk.points));
    if (actualHash !== chunk.content_sha256) {
      errors.push(semantic("chunk_hash_mismatch", `${chunkPath}/content_sha256`, `expected ${actualHash}`));
    }
  }

  for (const [bootSequence, finalChunkSequence] of finals.entries()) {
    if (upload.chunks.some((chunk) => chunk.boot_sequence === bootSequence && chunk.chunk_sequence > finalChunkSequence)) {
      errors.push(semantic("chunk_after_final", `${path}/chunks`, `boot ${bootSequence} has a chunk after final ${finalChunkSequence}`));
    }
  }

  if (totalPoints > 384) errors.push(semantic("total_points_exceeded", `${path}/chunks`, `${totalPoints} exceeds 384`));

  const summaryIds = new Set();
  const summaryRevisions = new Set();
  for (const [index, summary] of upload.summaries.entries()) {
    const summaryPath = `${path}/summaries/${index}`;
    const revisionIdentity = `${summary.local_date}:${summary.source_revision}`;
    if (summaryIds.has(summary.summary_id)) {
      errors.push(semantic("duplicate_summary_identity", summaryPath, "summary_id occurs more than once"));
    }
    if (summaryRevisions.has(revisionIdentity)) {
      errors.push(semantic("duplicate_summary_revision", summaryPath, `duplicate ${revisionIdentity}`));
    }
    summaryIds.add(summary.summary_id);
    summaryRevisions.add(revisionIdentity);
    if (summary.moving_s + summary.inactive_s !== summary.observed_s) {
      errors.push(semantic("summary_duration_mismatch", summaryPath, "moving plus inactive must equal observed"));
    }
    const start = Date.parse(summary.window_start);
    const end = Date.parse(summary.window_end);
    if (!(end > start)) errors.push(semantic("summary_window_invalid", summaryPath, "window end must follow start"));
    if (summary.observed_s > Math.floor((end - start) / 1000)) {
      errors.push(semantic("summary_observed_exceeds_window", summaryPath, "observed seconds exceed reporting window"));
    }
  }

  const lossIds = new Set();
  const lossRanges = new Map();
  for (const [index, marker] of upload.loss_markers.entries()) {
    const markerPath = `${path}/loss_markers/${index}`;
    const ranges = lossRanges.get(marker.boot_sequence) ?? [];
    if (lossIds.has(marker.marker_id)) {
      errors.push(semantic("duplicate_loss_marker_identity", markerPath, "marker_id occurs more than once"));
    }
    lossIds.add(marker.marker_id);
    if (marker.last_missing_point_sequence < marker.first_missing_point_sequence) {
      errors.push(semantic("loss_range_inverted", markerPath, "loss range is inverted"));
    } else if (marker.last_missing_point_sequence - marker.first_missing_point_sequence + 1 !== marker.lost_points) {
      errors.push(semantic("loss_count_mismatch", markerPath, "inclusive range differs from lost_points"));
    } else {
      if (ranges.some(([first, last]) => first <= marker.last_missing_point_sequence && last >= marker.first_missing_point_sequence)) {
        errors.push(semantic("overlapping_loss_range", markerPath, "loss markers overlap"));
      }
      if (upload.chunks.some((chunk) =>
        chunk.boot_sequence === marker.boot_sequence &&
        chunk.first_point_sequence <= marker.last_missing_point_sequence &&
        chunk.first_point_sequence + chunk.point_count - 1 >= marker.first_missing_point_sequence
      )) {
        errors.push(semantic("loss_overlaps_point", markerPath, "a loss marker covers an uploaded point"));
      }
      ranges.push([marker.first_missing_point_sequence, marker.last_missing_point_sequence]);
      lossRanges.set(marker.boot_sequence, ranges);
    }
  }
  return errors;
}

function semanticIssueClaimSuccess(body) {
  const expiry = Date.parse(body.claim.expires_at) - Date.parse(body.server_time);
  return expiry === 900_000 ? [] : [semantic("claim_expiry_mismatch", "$/claim/expires_at", "expiry must be exactly 900 seconds")];
}

function semanticClaimRequest(body) {
  const errors = semanticCapabilities(body.capabilities, "$/capabilities");
  const expectedHash = sha256Base64Url(canonicalize(body.capabilities));
  if (body.device.capability_hash !== expectedHash) errors.push(semantic("capability_hash_mismatch", "$/device/capability_hash", `expected ${expectedHash}`));
  if (body.device.hardware_revision !== body.capabilities.hardware_revision) errors.push(semantic("hardware_revision_mismatch", "$/device/hardware_revision", "descriptor and manifest disagree"));
  if (Buffer.from(body.credential_secret, "base64url").length !== 32) errors.push(semantic("credential_secret_length", "$/credential_secret", "secret must decode to 32 bytes"));
  return errors;
}

function semanticSyncRequest(body, raw) {
  const errors = [];
  if (Buffer.byteLength(raw, "utf8") > SYNC_REQUEST_MAX_BYTES) errors.push(semantic("request_body_too_large", "$", "sync request exceeds 128 KiB"));
  if (jsonDepth(body) > MAX_JSON_DEPTH) errors.push(semantic("json_depth_exceeded", "$", "JSON depth exceeds 12"));
  errors.push(...semanticTelemetry(body.upload, "$/upload"));
  if (body.capabilities !== null) {
    errors.push(...semanticCapabilities(body.capabilities, "$/capabilities"));
    const expectedHash = sha256Base64Url(canonicalize(body.capabilities));
    if (body.device.capability_hash !== expectedHash) errors.push(semantic("capability_hash_mismatch", "$/device/capability_hash", `expected ${expectedHash}`));
    if (body.device.hardware_revision !== body.capabilities.hardware_revision) errors.push(semantic("hardware_revision_mismatch", "$/device/hardware_revision", "descriptor and manifest disagree"));
  }
  if (body.diagnostics.outbox_used_bytes > body.diagnostics.outbox_capacity_bytes) {
    errors.push(semantic("outbox_usage_exceeds_capacity", "$/diagnostics", "used bytes exceed capacity"));
  }
  const mutationIds = new Set();
  const localSequences = new Set();
  for (const [index, mutation] of body.configuration.mutations.entries()) {
    const mutationPath = `$/configuration/mutations/${index}`;
    if (mutationIds.has(mutation.mutation_id)) errors.push(semantic("duplicate_mutation_id", `${mutationPath}/mutation_id`, "duplicate mutation ID"));
    mutationIds.add(mutation.mutation_id);
    if (localSequences.has(mutation.local_sequence)) errors.push(semantic("duplicate_local_sequence", `${mutationPath}/local_sequence`, "duplicate local sequence"));
    localSequences.add(mutation.local_sequence);
    if (mutation.authored_hlc.actor_id !== body.device.device_id) errors.push(semantic("mutation_actor_mismatch", `${mutationPath}/authored_hlc/actor_id`, "actor must be public device ID"));
    const expectedHash = sha256Base64Url(canonicalize(mutation.body));
    if (mutation.body_sha256 !== expectedHash) errors.push(semantic("config_body_hash_mismatch", `${mutationPath}/body_sha256`, `expected ${expectedHash}`));
    errors.push(...semanticConfigResource(mutation, mutationPath));
    if (body.capabilities !== null) {
      const declared = body.capabilities.config_resources.some(
        (resource) => resource.resource_key === mutation.resource_key && resource.resource_schema === mutation.resource_schema,
      );
      if (!declared) errors.push(semantic("config_resource_not_capable", mutationPath, "resource absent from current manifest"));
      const effectIds = new Set(body.capabilities.led.effects.map((effect) => effect.id));
      const usedEffectIds = mutation.resource_key === "simple_effect"
        ? [mutation.body.effect_id]
        : mutation.resource_key === "speed_profile"
          ? mutation.body.effects.flatMap((effect) => [effect.effect_a, effect.effect_b])
          : [];
      for (const effectId of usedEffectIds) {
        if (!effectIds.has(effectId)) errors.push(semantic("effect_not_capable", `${mutationPath}/body`, `effect ${effectId} absent from manifest`));
      }
    }
  }
  return errors;
}

function semanticSyncSuccess(body, raw) {
  const errors = [];
  if (Buffer.byteLength(raw, "utf8") > SYNC_RESPONSE_MAX_BYTES) errors.push(semantic("response_body_too_large", "$", "sync success exceeds 64 KiB"));
  const accepted = body.telemetry.accepted_chunks.map((item) => `${item.boot_sequence}:${item.chunk_sequence}`);
  const rejected = body.telemetry.rejected_chunks.map((item) => `${item.boot_sequence}:${item.chunk_sequence}`);
  if (new Set(accepted).size !== accepted.length) errors.push(semantic("duplicate_accepted_chunk", "$/telemetry/accepted_chunks", "duplicate accepted chunk"));
  if (new Set(rejected).size !== rejected.length) errors.push(semantic("duplicate_rejected_chunk", "$/telemetry/rejected_chunks", "duplicate rejected chunk"));
  for (const identity of accepted) if (rejected.includes(identity)) errors.push(semantic("chunk_both_accepted_and_rejected", "$/telemetry", identity));
  const desiredKeys = new Set();
  for (const [index, desired] of body.configuration.desired_resources.entries()) {
    const desiredPath = `$/configuration/desired_resources/${index}`;
    if (desiredKeys.has(desired.resource_key)) errors.push(semantic("duplicate_desired_resource", desiredPath, desired.resource_key));
    desiredKeys.add(desired.resource_key);
    const expectedHash = sha256Base64Url(canonicalize(desired.body));
    if (desired.body_sha256 !== expectedHash) errors.push(semantic("config_body_hash_mismatch", `${desiredPath}/body_sha256`, `expected ${expectedHash}`));
    errors.push(...semanticConfigResource(desired, desiredPath));
  }
  const outcomeIds = body.configuration.outcomes.map((item) => item.mutation_id);
  if (new Set(outcomeIds).size !== outcomeIds.length) errors.push(semantic("duplicate_config_outcome", "$/configuration/outcomes", "duplicate mutation outcome"));
  return errors;
}

function semanticRevokeRequest(body, raw) {
  const errors = [];
  if (Buffer.byteLength(raw, "utf8") > REVOKE_REQUEST_MAX_BYTES) {
    errors.push(semantic("request_body_too_large", "$", "revoke request exceeds 4 KiB"));
  }
  if (jsonDepth(body) > MAX_JSON_DEPTH) {
    errors.push(semantic("json_depth_exceeded", "$", "JSON depth exceeds 12"));
  }
  return errors;
}

function semanticRevokeSuccess(body, raw) {
  const errors = [];
  if (Buffer.byteLength(raw, "utf8") > REVOKE_RESPONSE_MAX_BYTES) {
    errors.push(semantic("response_body_too_large", "$", "revoke success exceeds 4 KiB"));
  }
  if (Date.parse(body.revoked_at) > Date.parse(body.server_time)) {
    errors.push(semantic("revoked_at_after_server_time", "$/revoked_at", "revocation cannot be in the future"));
  }
  return errors;
}

function semanticProblem(body, catalog) {
  const entry = catalog.entries.find((item) => item.code === body.code);
  if (!entry) return [semantic("problem_code_unknown", "$/code", body.code)];
  const errors = [];
  if (body.status !== entry.status) errors.push(semantic("problem_status_mismatch", "$/status", `catalog says ${entry.status}`));
  if (body.title !== entry.title) errors.push(semantic("problem_title_mismatch", "$/title", `catalog says ${entry.title}`));
  if (body.type !== `${catalog.type_prefix}${body.code}`) errors.push(semantic("problem_type_mismatch", "$/type", "type must be catalog URN"));
  if (body.retry_after_seconds !== undefined && !entry.retry_after_allowed) errors.push(semantic("retry_after_forbidden", "$/retry_after_seconds", "catalog forbids retry delay"));
  return errors;
}

function tick(last, now, trusted, maxPhysical = MAX_WIRE_PHYSICAL_MS) {
  let physical = now === null ? last.physical_ms : Math.max(last.physical_ms, now);
  let logical = physical > last.physical_ms ? 0 : last.logical + 1;
  if (logical > UINT32_MAX) {
    if (!trusted || now === null || physical >= maxPhysical) return { error: "hlc_logical_overflow" };
    physical = Math.max(now, physical + 1);
    if (physical > maxPhysical) return { error: "hlc_logical_overflow" };
    logical = 0;
  }
  return { physical_ms: physical, logical };
}

function merge(local, received, now, trusted, maxPhysical = MAX_WIRE_PHYSICAL_MS) {
  let physical = Math.max(local.physical_ms, received.physical_ms, now);
  let logical;
  if (physical === local.physical_ms && physical === received.physical_ms) logical = Math.max(local.logical, received.logical) + 1;
  else if (physical === local.physical_ms) logical = local.logical + 1;
  else if (physical === received.physical_ms) logical = received.logical + 1;
  else logical = 0;
  if (logical > UINT32_MAX) {
    if (!trusted || physical >= maxPhysical) return { error: "hlc_logical_overflow" };
    physical += 1;
    if (physical > maxPhysical) return { error: "hlc_logical_overflow" };
    logical = 0;
  }
  return { physical_ms: physical, logical };
}

function compareHlc(left, right) {
  if (left.physical_ms !== right.physical_ms) return Math.sign(left.physical_ms - right.physical_ms);
  if (left.logical !== right.logical) return Math.sign(left.logical - right.logical);
  const leftActor = left.actor_id.replaceAll("-", "");
  const rightActor = right.actor_id.replaceAll("-", "");
  return leftActor === rightActor ? 0 : leftActor < rightActor ? -1 : 1;
}

function trustedAuthored(quality, authoredMs, receivedMs, skewMs) {
  return ["server_anchored", "sntp_synced", "gnss_trusted"].includes(quality) && Math.abs(authoredMs - receivedMs) <= skewMs;
}

function rebaseUnknown(vector) {
  const serverActor = HLC_VECTORS.parameters.server_actor_id;
  let state = { ...vector.server_last };
  return [...vector.input]
    .sort((left, right) => left.local_sequence - right.local_sequence)
    .map((mutation) => {
      state = tick(state, vector.received_at_ms, true);
      return {
        mutation_id: mutation.mutation_id,
        accepted_hlc: { ...state, actor_id: serverActor },
      };
    });
}

const schemaFiles = (await readdir(SCHEMA_DIR)).filter((name) => name.endsWith(".json")).sort();
const schemas = await Promise.all(schemaFiles.map((name) => json(join(SCHEMA_DIR, name))));
const registry = new Map(schemas.map((schema) => [schema.$id, schema]));
const FIXTURE_MANIFEST = await json(join(FIXTURE_DIR, "manifest.json"));
const PROBLEM_CATALOG = await json(join(ROOT, "problem-catalog.json"));
const COMPATIBILITY = await json(join(ROOT, "compatibility-matrix.json"));
const HLC_VECTORS = await json(join(FIXTURE_DIR, "hlc-vectors.json"));

function semanticErrors(name, body, raw) {
  switch (name) {
    case "capabilities": return semanticCapabilities(body);
    case "config_resource": return semanticConfigResource(body);
    case "issue_claim_request": return [];
    case "issue_claim_success": return semanticIssueClaimSuccess(body);
    case "claim_request": return semanticClaimRequest(body);
    case "claim_success": return [];
    case "telemetry": return semanticTelemetry(body);
    case "sync_request": return semanticSyncRequest(body, raw);
    case "sync_success": return semanticSyncSuccess(body, raw);
    case "revoke_request": return semanticRevokeRequest(body, raw);
    case "revoke_success": return semanticRevokeSuccess(body, raw);
    case "problem": return semanticProblem(body, PROBLEM_CATALOG);
    default: throw new Error(`unknown semantic validator ${name}`);
  }
}

function walkRefs(value, found = []) {
  if (value === null || typeof value !== "object") return found;
  if (typeof value.$ref === "string") found.push(value.$ref);
  for (const child of Object.values(value)) walkRefs(child, found);
  return found;
}

test("all schemas declare unique Draft 2020-12 IDs and resolvable references", () => {
  assert.equal(registry.size, schemas.length);
  for (const schema of schemas) {
    assert.equal(schema.$schema, "https://json-schema.org/draft/2020-12/schema");
    assert.match(schema.$id, /^urn:dog-rgb:contract:device-v1:/);
    for (const ref of walkRefs(schema)) {
      const [id, pointer] = splitRef(ref, schema.$id);
      const target = registry.get(id);
      assert.ok(target, `${schema.$id}: unresolved ${ref}`);
      assert.notEqual(pointerGet(target, pointer), undefined, `${schema.$id}: unresolved ${ref}`);
    }
  }
});

for (const fixture of FIXTURE_MANIFEST.cases) {
  test(`golden ${fixture.valid ? "valid" : "invalid"}: ${fixture.file}`, async () => {
    const path = join(FIXTURE_DIR, fixture.file);
    const raw = await readFile(path, "utf8");
    const body = JSON.parse(raw);
    const schema = registry.get(fixture.schema);
    assert.ok(schema, `unknown fixture schema ${fixture.schema}`);
    const schemaErrors = validateSchema(schema, body, registry);
    const semantics = schemaErrors.length === 0 ? semanticErrors(fixture.semantic, body, raw) : [];
    const errors = [...schemaErrors, ...semantics];
    if (fixture.valid) {
      assert.deepEqual(errors, [], JSON.stringify(errors, null, 2));
    } else {
      assert.ok(errors.length > 0, "invalid fixture unexpectedly passed");
      assert.ok(errors.some((error) => error.code === fixture.expected_error), `${fixture.expected_error} absent from ${JSON.stringify(errors, null, 2)}`);
    }
  });
}

test("stable problem catalog validates and has unique code/status policy", () => {
  const schema = registry.get("urn:dog-rgb:contract:device-v1:problem-catalog");
  assert.deepEqual(validateSchema(schema, PROBLEM_CATALOG, registry), []);
  const codes = PROBLEM_CATALOG.entries.map((entry) => entry.code);
  assert.equal(new Set(codes).size, codes.length);
  for (const entry of PROBLEM_CATALOG.entries) {
    if (entry.retry_class === "retry_after") assert.equal(entry.retry_after_allowed, true);
    if (entry.status === 429) assert.equal(entry.retry_class, "retry_after");
    if (entry.status >= 500) assert.equal(entry.device_action, "retain_and_retry_with_jitter");
    for (const endpoint of Object.keys(entry.device_action_overrides ?? {})) {
      assert.ok(entry.applies_to.includes(endpoint), `${entry.code}: action override for non-applicable ${endpoint}`);
    }
  }
  const revokeCodes = new Set(
    PROBLEM_CATALOG.entries
      .filter((entry) => entry.applies_to.includes("revoke"))
      .map((entry) => entry.code),
  );
  for (const required of [
    "malformed_json",
    "invalid_envelope",
    "device_credential_invalid",
    "device_credential_expired",
    "method_not_allowed",
    "request_id_reused",
    "device_identity_conflict",
    "length_required",
    "payload_too_large",
    "unsupported_media_type",
    "unsupported_protocol",
    "rate_limited",
    "internal_error",
    "transaction_conflict",
    "server_busy",
    "gateway_timeout",
  ]) {
    assert.ok(revokeCodes.has(required), `revoke problem policy missing ${required}`);
  }
  assert.equal(revokeCodes.has("device_revoked"), false, "revoke retries use the authenticated tombstone result, not a generic 403 proof");
  for (const entry of PROBLEM_CATALOG.entries.filter((item) => item.applies_to.includes("revoke"))) {
    const action = entry.device_action_overrides?.revoke ?? entry.device_action;
    assert.notEqual(action, "split_and_retain", `${entry.code}: a fixed revoke envelope cannot be split`);
    assert.notEqual(action, "retain_and_repair_claim", `${entry.code}: revoke recovery cannot silently re-enter pairing`);
    assert.notEqual(action, "mark_revoked_and_stop", `${entry.code}: a generic problem is not revocation proof`);
  }
});

test("compatibility matrix is closed and matches resource schema keys", () => {
  assert.equal(COMPATIBILITY.contract, "device-v1");
  assert.equal(COMPATIBILITY.protocol_version, 1);
  assert.deepEqual(COMPATIBILITY.required_device_schemas, { telemetry_schema: 3, config_schema: 7 });
  assert.equal(COMPATIBILITY.combinations.filter((row) => row.result === "supported").length, 1);
  const resourceEnum = registry.get("urn:dog-rgb:contract:device-v1:config-resource").$defs.resourceKey.enum;
  assert.deepEqual(Object.keys(COMPATIBILITY.resource_schemas).sort(), [...resourceEnum].sort());
  assert.ok(Object.values(COMPATIBILITY.resource_schemas).every((version) => version === 1));
  assert.equal(COMPATIBILITY.legacy_policy.track_v2_upload, "supported_only_as_schema3_legacy_conversion");
});

test("HLC local tick vectors", () => {
  for (const vector of HLC_VECTORS.ticks) {
    assert.deepEqual(tick(vector.last, vector.now, vector.trusted), vector.expected, vector.name);
  }
});

test("HLC merge vectors exercise all four branches", () => {
  for (const vector of HLC_VECTORS.merges) {
    assert.deepEqual(merge(vector.local, vector.received, vector.now, vector.trusted), vector.expected, vector.name);
  }
});

test("HLC total-order tie vectors", () => {
  for (const vector of HLC_VECTORS.comparisons) {
    assert.equal(compareHlc(vector.left, vector.right), vector.expected, vector.name);
  }
});

test("HLC overflow fails closed or advances a trusted millisecond", () => {
  for (const vector of HLC_VECTORS.overflows) {
    const result = vector.operation === "tick"
      ? tick(vector.last, vector.now, vector.trusted, HLC_VECTORS.parameters.max_wire_physical_ms)
      : merge(vector.local, vector.received, vector.now, vector.trusted, HLC_VECTORS.parameters.max_wire_physical_ms);
    if (vector.expected_error) assert.equal(result.error, vector.expected_error, vector.name);
    else assert.deepEqual(result, vector.expected, vector.name);
  }
});

test("HLC authored-time trust window is inclusive and quality-gated", () => {
  for (const vector of HLC_VECTORS.trust_boundaries) {
    assert.equal(
      trustedAuthored(vector.quality, vector.authored_ms, vector.received_ms, HLC_VECTORS.parameters.trusted_skew_ms),
      vector.expected_trusted,
      JSON.stringify(vector),
    );
  }
});

test("unknown-time mutations rebase by persisted sequence and replay exactly", () => {
  const first = rebaseUnknown(HLC_VECTORS.unknown_time_rebase);
  assert.deepEqual(first, HLC_VECTORS.unknown_time_rebase.expected);
  const replay = structuredClone(first);
  assert.deepEqual(replay, HLC_VECTORS.unknown_time_rebase.expected);
});

test("semantic aggregate point cap is enforced across schema-valid chunks", async () => {
  const upload = await json(join(FIXTURE_DIR, "valid", "telemetry-upload.json"));
  const point = upload.chunks[0].points[0];
  upload.chunks = Array.from({ length: 5 }, (_, chunkSequence) => {
    const points = Array.from({ length: 80 }, () => [...point]);
    return {
      telemetry_schema: 3,
      boot_sequence: 42,
      chunk_sequence: chunkSequence,
      first_point_sequence: chunkSequence * 80,
      point_count: 80,
      content_sha256: sha256Base64Url(chunkBytes(points)),
      is_final: false,
      points,
    };
  });
  const errors = semanticTelemetry(upload);
  assert.ok(errors.some((error) => error.code === "semantic.total_points_exceeded"));
});

test("sync semantic checks bind config hash and mutation actor", async () => {
  const body = await json(join(FIXTURE_DIR, "valid", "device-v1-sync-request.json"));
  body.configuration.mutations[0].body.brightness = 97;
  body.configuration.mutations[0].authored_hlc.actor_id = "99999999-9999-4999-8999-999999999999";
  const errors = semanticSyncRequest(body, JSON.stringify(body));
  assert.ok(errors.some((error) => error.code === "semantic.config_body_hash_mismatch"));
  assert.ok(errors.some((error) => error.code === "semantic.mutation_actor_mismatch"));
});

test("pre-parse byte/depth boundaries are exact", async () => {
  const body = await json(join(FIXTURE_DIR, "valid", "device-v1-sync-request.json"));
  assert.ok(Buffer.byteLength(JSON.stringify(body), "utf8") <= SYNC_REQUEST_MAX_BYTES);
  assert.ok(jsonDepth(body) <= MAX_JSON_DEPTH);
  assert.equal(Buffer.alloc(SYNC_REQUEST_MAX_BYTES + 1).length > SYNC_REQUEST_MAX_BYTES, true);
  let deep = 0;
  for (let level = 0; level < MAX_JSON_DEPTH; level += 1) deep = { child: deep };
  assert.ok(jsonDepth(deep) > MAX_JSON_DEPTH);
});

test("revoke request and result bind exact identity and stable retry dispositions", async () => {
  const request = await json(join(FIXTURE_DIR, "valid", "device-v1-revoke-request.json"));
  const success = await json(join(FIXTURE_DIR, "valid", "device-v1-revoke-success.json"));
  assert.equal(success.request_id, request.request_id);
  assert.equal(success.device_id, request.device_id);
  assert.equal(success.credential_id, request.credential_id);
  assert.equal(success.state, "revoked");
  assert.ok(Buffer.byteLength(JSON.stringify(request), "utf8") <= REVOKE_REQUEST_MAX_BYTES);
  assert.ok(Buffer.byteLength(JSON.stringify(success), "utf8") <= REVOKE_RESPONSE_MAX_BYTES);

  const schema = registry.get("urn:dog-rgb:contract:device-v1:device-v1-revoke-success");
  const alreadyRevoked = {
    ...success,
    disposition: "already_revoked",
    revoked_at: "2026-08-13T16:59:00.000Z",
  };
  assert.deepEqual(validateSchema(schema, alreadyRevoked, registry), []);
  assert.deepEqual(semanticRevokeSuccess(alreadyRevoked, JSON.stringify(alreadyRevoked)), []);
  assert.deepEqual(structuredClone(success), success, "stored exact replay result is stable");
});

test("wire tuple hashes match the actual Phase 0B little-endian codec", async () => {
  const upload = await json(join(FIXTURE_DIR, "valid", "telemetry-upload.json"));
  const legacy = await json(join(FIXTURE_DIR, "valid", "telemetry-legacy-upload.json"));
  assert.equal(sha256Base64Url(chunkBytes(upload.chunks[0].points)), upload.chunks[0].content_sha256);
  assert.equal(sha256Base64Url(chunkBytes(legacy.chunks[0].points)), legacy.chunks[0].content_sha256);

  const script = [
    "import hashlib, json, sys",
    "sys.path.insert(0, 'tools/cloud_phase0')",
    "from reference_fixtures import reference_fixtures",
    "from track_v3 import CHUNK_HEADER_SIZE, KNOWN_POINT_FLAGS, MAX_POINTS_PER_CHUNK, POINT_STRUCT, TimeQuality, encode_chunk",
    "rows=[]",
    "for f in reference_fixtures():",
    " b=encode_chunk(f.chunk)",
    " rows.append({'id':f.fixture_id,'payload':hashlib.sha256(b[CHUNK_HEADER_SIZE:]).hexdigest(),'chunk':hashlib.sha256(b).hexdigest(),'points':len(f.chunk.points)})",
    "print(json.dumps({'rows':rows,'point_format':POINT_STRUCT.format,'flags_mask':KNOWN_POINT_FLAGS,'max_points':MAX_POINTS_PER_CHUNK,'time_quality':{item.name:int(item) for item in TimeQuality}}, sort_keys=True))",
  ].join("\n");
  const actual = JSON.parse(execFileSync("python", ["-c", script], { cwd: join(ROOT, "..", ".."), encoding: "utf8" }));
  const manifest = await json(join(ROOT, "..", "..", "tools", "cloud_phase0", "fixtures", "reference_manifest.json"));
  const expectedChunks = new Map(manifest.fixtures.map((item) => [item.fixture_id, item.encoded_chunk_sha256]));
  assert.equal(actual.point_format, "<iiIHBB");
  assert.equal(actual.flags_mask, 0x7f);
  assert.equal(actual.max_points, 96);
  assert.deepEqual(actual.time_quality, {
    APPROXIMATE_PERSISTED: 1,
    GNSS_TRUSTED: 4,
    LEGACY_MINUTE: 5,
    SERVER_ANCHORED: 2,
    SNTP_SYNCED: 3,
    UNKNOWN: 0,
  });
  assert.equal(actual.rows.length, manifest.fixtures.length);
  for (const row of actual.rows) {
    assert.equal(row.chunk, expectedChunks.get(row.id), row.id);
    assert.ok(row.points <= 96, row.id);
    assert.match(row.payload, /^[0-9a-f]{64}$/);
  }
});

test("Track v3 chunk time-quality names have the exact uint8 header mapping", () => {
  const mapping = {
    unknown: 0,
    approximate_persisted: 1,
    server_anchored: 2,
    sntp_synced: 3,
    gnss_trusted: 4,
    legacy_minute: 5,
  };
  const values = registry.get("urn:dog-rgb:contract:device-v1:telemetry").$defs.chunkV3.properties.time_quality.enum;
  assert.deepEqual(values, Object.values(mapping));
  assert.deepEqual(
    Object.entries(mapping).filter(([, value]) => [2, 3, 4].includes(value)).map(([name]) => name),
    ["server_anchored", "sntp_synced", "gnss_trusted"],
  );
});
