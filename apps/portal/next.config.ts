import type { NextConfig } from "next";

const privateResponseHeaders = [
  { key: "Cache-Control", value: "private, no-store" },
  { key: "Pragma", value: "no-cache" },
] as const;

const nextConfig: NextConfig = {
  // Supabase local Auth uses this exact Site URL in email links. Keep the
  // development exception host-only and do not broaden it with wildcards.
  allowedDevOrigins: ["127.0.0.1"],
  async headers() {
    return [
      { source: "/onboarding", headers: [...privateResponseHeaders] },
      { source: "/app/:path*", headers: [...privateResponseHeaders] },
    ];
  },
};

export default nextConfig;
