import { createServerClient } from "@supabase/ssr";
import { NextResponse, type NextRequest } from "next/server";

import type { Database } from "../database.generated";
import { getPublicSupabaseEnvironment } from "./environment";

export async function refreshSupabaseSession(request: NextRequest) {
  let response = NextResponse.next({ request });
  const environment = getPublicSupabaseEnvironment();
  const supabase = createServerClient<Database, "api">(
    environment.url,
    environment.publishableKey,
    {
      db: { schema: "api" },
      cookies: {
        getAll() {
          return request.cookies.getAll();
        },
        setAll(cookiesToSet, headers) {
          cookiesToSet.forEach(({ name, value }) => {
            request.cookies.set(name, value);
          });

          response = NextResponse.next({ request });

          cookiesToSet.forEach(({ name, options, value }) => {
            response.cookies.set(name, value, options);
          });

          Object.entries(headers).forEach(([name, value]) => {
            response.headers.set(name, value);
          });
        },
      },
    },
  );

  // This must remain the first operation after client creation so a refresh
  // can update both the request and response cookies before rendering starts.
  await supabase.auth.getClaims();

  return response;
}
