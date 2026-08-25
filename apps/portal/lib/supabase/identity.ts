import "server-only";

import { identityFromVerifiedClaims } from "./identity-claims";
import { createServerSupabaseClient } from "./server";

export type VerifiedIdentity = Readonly<{
  userId: string;
}>;

export async function getVerifiedIdentity(): Promise<VerifiedIdentity | null> {
  const supabase = await createServerSupabaseClient();
  const { data, error } = await supabase.auth.getClaims();

  if (error) {
    return null;
  }

  return identityFromVerifiedClaims(data?.claims);
}
