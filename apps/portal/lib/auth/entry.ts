export type DogEntryMembership = Readonly<{ id: string }>;

export type DogEntryDecision =
  | Readonly<{ kind: "create" }>
  | Readonly<{ kind: "open"; dogId: string }>
  | Readonly<{ kind: "select" }>;

export function resolveDogEntry(
  memberships: readonly DogEntryMembership[],
): DogEntryDecision {
  if (memberships.length === 0) return { kind: "create" };
  if (memberships.length === 1) {
    return { kind: "open", dogId: memberships[0].id };
  }
  return { kind: "select" };
}
