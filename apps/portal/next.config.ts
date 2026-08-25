import type { NextConfig } from "next";

const nextConfig: NextConfig = {
  // Supabase local Auth uses this exact Site URL in email links. Keep the
  // development exception host-only and do not broaden it with wildcards.
  allowedDevOrigins: ["127.0.0.1"],
};

export default nextConfig;
