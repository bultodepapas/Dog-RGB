const REDACTION = "[REDACTED]";
const EVIDENCE_RUN_ID = /^[0-9]{4}-[0-9]{2}-[0-9]{2}-[a-z0-9][a-z0-9-]{2,63}$/;

function normalizedSecrets(secrets) {
  return [...new Set(secrets.filter((secret) => typeof secret === "string" && secret.length >= 8))]
    .sort((left, right) => right.length - left.length);
}

export function redactText(value, secrets = []) {
  let redacted = String(value);
  for (const secret of normalizedSecrets(secrets)) {
    for (const representation of new Set([secret, encodeURIComponent(secret)])) {
      redacted = redacted.split(representation).join(REDACTION);
    }
  }
  return redacted;
}

export function sanitizeForEvidence(value, secrets = []) {
  if (typeof value === "string") return redactText(value, secrets);
  if (Array.isArray(value)) return value.map((item) => sanitizeForEvidence(item, secrets));
  if (value && typeof value === "object") {
    return Object.fromEntries(
      Object.entries(value).map(([key, item]) => [key, sanitizeForEvidence(item, secrets)]),
    );
  }
  return value;
}

export function requestDescriptor(rawUrl) {
  try {
    const url = new URL(rawUrl);
    return { origin: url.origin, pathname: url.pathname };
  } catch {
    return { origin: "invalid-url", pathname: "[unparseable]" };
  }
}

export function assertNoSecretMaterial(value, secrets = []) {
  const serialized = JSON.stringify(value);
  for (const secret of normalizedSecrets(secrets)) {
    for (const representation of new Set([secret, encodeURIComponent(secret)])) {
      if (serialized.includes(representation)) {
        throw new Error("Refusing to persist evidence containing provider credential material");
      }
    }
  }
}

export function environmentWithoutProviderCredentials(environment) {
  const sanitized = { ...environment };
  delete sanitized.DOG_RGB_MAPTILER_KEY;
  delete sanitized.DOG_RGB_MAP_ALLOWED_ORIGIN;
  delete sanitized.DOG_RGB_MAP_REJECTED_ORIGIN;
  delete sanitized.DOG_RGB_MAP_EVIDENCE_RUN_ID;
  return sanitized;
}

export function validateEvidenceRunId(value) {
  if (!EVIDENCE_RUN_ID.test(value)) {
    throw new Error("DOG_RGB_MAP_EVIDENCE_RUN_ID must be a dated, filesystem-safe unique ID for every capture");
  }
  return value;
}
