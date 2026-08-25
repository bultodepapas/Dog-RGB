import { createServerClient } from "@supabase/ssr";
import { NextResponse, type NextRequest } from "next/server";

import type { Database } from "../database.generated";
import { isPrivatePortalPath } from "../auth/protected-route";
import { getPublicSupabaseEnvironment } from "./environment";

function applyPrivateResponseHeaders(response: NextResponse): void {
  response.headers.set("Cache-Control", "private, no-store");
  response.headers.set("Pragma", "no-cache");
}

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

  const pathname = request.nextUrl.pathname;
  if (isPrivatePortalPath(pathname)) {
    // setAll() may recreate the response while rotating cookies, so private
    // cache policy is deliberately applied to the final response object. The
    // leaf page remains the sole fresh-identity/authorization authority.
    applyPrivateResponseHeaders(response);
  }

  return response;
}
