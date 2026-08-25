import "server-only";

import { createServerClient } from "@supabase/ssr";
import { cookies } from "next/headers";

import type { Database } from "../database.generated";
import { getPublicSupabaseEnvironment } from "./environment";

export async function createServerSupabaseClient() {
  const cookieStore = await cookies();
  const environment = getPublicSupabaseEnvironment();

  return createServerClient<Database, "api">(
    environment.url,
    environment.publishableKey,
    {
      db: { schema: "api" },
      cookies: {
        getAll() {
          return cookieStore.getAll();
        },
        setAll(cookiesToSet) {
          try {
            cookiesToSet.forEach(({ name, options, value }) => {
              cookieStore.set(name, value, options);
            });
          } catch {
            // Server Components cannot write response cookies. The request
            // proxy refreshes the session and writes cookies before rendering.
          }
        },
      },
    },
  );
}
