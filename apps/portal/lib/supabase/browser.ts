"use client";

import "client-only";

import { createBrowserClient } from "@supabase/ssr";

import type { Database } from "../database.generated";
import { getPublicSupabaseEnvironment } from "./environment";

export function createBrowserSupabaseClient() {
  const environment = getPublicSupabaseEnvironment();

  return createBrowserClient<Database, "api">(
    environment.url,
    environment.publishableKey,
    {
      db: { schema: "api" },
    },
  );
}
