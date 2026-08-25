import type { EmailOtpType } from "@supabase/supabase-js";
import { type NextRequest, NextResponse } from "next/server";

import {
  confirmationErrorRedirect,
  parseEmailOtpType,
  resolveConfirmationRedirect,
  resolveLocalAuthOrigin,
} from "../../../lib/auth/redirect";
import { createServerSupabaseClient } from "../../../lib/supabase/server";

export const dynamic = "force-dynamic";

export async function GET(request: NextRequest) {
  const tokenHash = request.nextUrl.searchParams.get("token_hash");
  const type = parseEmailOtpType(request.nextUrl.searchParams.get("type"));
  const redirectOrigin = resolveLocalAuthOrigin(request.headers.get("host"));

  if (tokenHash && type) {
    const supabase = await createServerSupabaseClient();
    const { error } = await supabase.auth.verifyOtp({
      token_hash: tokenHash,
      type: type as EmailOtpType,
    });

    if (!error) {
      const target = resolveConfirmationRedirect(
        type,
        request.nextUrl.searchParams.get("next"),
      );
      return NextResponse.redirect(new URL(target, redirectOrigin), 303);
    }
  }

  const target = confirmationErrorRedirect(type);
  return NextResponse.redirect(new URL(target, redirectOrigin), 303);
}
