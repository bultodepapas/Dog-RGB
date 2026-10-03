export type DeletionState = Readonly<{ status: "idle" | "error"; message: string }>;
export const INITIAL_DELETION_STATE: DeletionState = { status: "idle", message: "" };

export function parseDeletionForm(form: FormData) {
  const dogId = form.get("dogId");
  const requestId = form.get("requestId");
  const password = form.get("password");
  const uuid = /^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/iu;
  if (["dogId", "requestId", "password", "confirmation"].some(key => form.getAll(key).length !== 1) ||
      typeof dogId !== "string" || !uuid.test(dogId) ||
      typeof requestId !== "string" || !uuid.test(requestId) ||
      typeof password !== "string" || password.length < 1 || password.length > 128 ||
      form.get("confirmation") !== "ELIMINAR") return null;
  return { dogId, requestId, password };
}

export type DeletionJob = Readonly<{
  jobId: string; dogId: string; status: "pending" | "processing" | "failed" | "completed";
  requestedAt: string; completedAt: string | null;
}>;

export function parseDeletionJobs(value: unknown): readonly DeletionJob[] {
  if (!Array.isArray(value)) throw new Error("deletion_status_unavailable");
  return value.map(row => {
    if (!row || typeof row !== "object" || typeof row.job_id !== "string" ||
        typeof row.scope_id !== "string" || typeof row.requested_at !== "string" ||
        !["pending", "processing", "failed", "completed"].includes(row.status)) {
      throw new Error("deletion_status_unavailable");
    }
    return { jobId: row.job_id, dogId: row.scope_id, status: row.status,
      requestedAt: row.requested_at, completedAt: typeof row.completed_at === "string" ? row.completed_at : null };
  });
}
