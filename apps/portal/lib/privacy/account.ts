export const ACCOUNT_CONFIRMATION = "ELIMINAR CUENTA Y TODOS LOS PERROS";
export type AccountPreview = Readonly<{
  requestId: string; scopeHash: string; blockers: readonly string[];
  ownedDogs: readonly Readonly<{ id: string; name: string; otherMembers: number }>[];
  detach: readonly Readonly<{ id: string; name: string; role: string }>[];
}>;
export type AccountDeletion = Readonly<{
  status: "none" | "pending" | "ready" | "blocked" | "completed";
  requestId: string | null; failedJobs: number; receipt: string | null;
}>;
export type AccountDeletionReceipt = Readonly<{
  requestId: string;
  completedAt: string;
  receipt: string;
}>;
export type AccountActionState = Readonly<{
  status: "idle" | "error" | "completed";
  message: string;
  receipt?: string;
  requestId?: string;
  completedAt?: string;
}>;
export const INITIAL_ACCOUNT_ACTION: AccountActionState = { status: "idle", message: "" };

function object(value: unknown): Record<string, unknown> {
  if (!value || typeof value !== "object" || Array.isArray(value)) throw new Error("account_data_unavailable");
  return value as Record<string, unknown>;
}
function text(value: unknown): string {
  if (typeof value !== "string") throw new Error("account_data_unavailable");
  return value;
}
function identifier(value: unknown): string {
  const id = text(value);
  if (!/^[0-9a-f]{8}-[0-9a-f]{4}-[1-8][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/u.test(id)) throw new Error("account_data_unavailable");
  return id;
}
function hash(value: unknown): string {
  const digest = text(value);
  if (!/^[A-Za-z0-9_-]{43}$/u.test(digest)) throw new Error("account_data_unavailable");
  return digest;
}

export function accountPreview(value: unknown): AccountPreview {
  const row = object(value);
  if (row.schema_version !== "account-deletion-preview-v1" || row.confirmation_version !== "account-delete-v1" ||
      row.confirmation_phrase !== ACCOUNT_CONFIRMATION || !Array.isArray(row.owned_dogs) || !Array.isArray(row.memberships_to_detach) || !Array.isArray(row.blockers)) throw new Error("account_data_unavailable");
  return { requestId: identifier(row.request_id), scopeHash: hash(row.scope_sha256), blockers: row.blockers.map(text),
    ownedDogs: row.owned_dogs.map(value => { const dog = object(value); const count = dog.other_member_count; if (typeof count !== "number" || !Number.isSafeInteger(count) || count < 0) throw new Error("account_data_unavailable"); return { id: identifier(dog.dog_id), name: text(dog.name), otherMembers: count }; }),
    detach: row.memberships_to_detach.map(value => { const dog = object(value); if (!["editor", "viewer"].includes(String(dog.role))) throw new Error("account_data_unavailable"); return { id: identifier(dog.dog_id), name: text(dog.name), role: text(dog.role) }; }),
  };
}

export function accountDeletion(value: unknown): AccountDeletion {
  const row = object(value);
  if (row.schema_version !== "account-deletion-v1" || !["none", "pending", "ready", "blocked", "completed"].includes(String(row.status))) throw new Error("account_data_unavailable");
  if (row.status !== "none" && !Array.isArray(row.dog_jobs)) throw new Error("account_data_unavailable");
  const jobs = Array.isArray(row.dog_jobs) ? row.dog_jobs.map(object) : [];
  if (jobs.some(job => !["pending", "processing", "failed", "completed"].includes(String(job.status)))) throw new Error("account_data_unavailable");
  const receipt = row.receipt_sha256 == null ? null : hash(row.receipt_sha256);
  if (row.status === "completed" && (!receipt || jobs.some(job => job.status !== "completed"))) throw new Error("account_data_unavailable");
  return { status: row.status as AccountDeletion["status"], requestId: row.status === "none" ? null : identifier(row.request_id),
    failedJobs: jobs.filter(job => job.status === "failed").length, receipt };
}

export function accountDeletionReceipt(value: unknown, expectedRequestId?: string): AccountDeletionReceipt {
  const row = object(value);
  const keys = Object.keys(row).sort();
  const expectedKeys = ["completed_at", "receipt_sha256", "request_id", "schema_version", "status"];
  if (keys.length !== expectedKeys.length || keys.some((key, index) => key !== expectedKeys[index]) ||
      row.schema_version !== "account-deletion-receipt-v1" || row.status !== "completed") {
    throw new Error("account_receipt_unavailable");
  }
  const completedAt = text(row.completed_at);
  if (!Number.isFinite(Date.parse(completedAt))) throw new Error("account_receipt_unavailable");
  const requestId = identifier(row.request_id);
  if (expectedRequestId !== undefined && requestId !== expectedRequestId) throw new Error("account_receipt_unavailable");
  return { requestId, completedAt, receipt: hash(row.receipt_sha256) };
}
