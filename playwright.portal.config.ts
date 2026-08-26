import { defineConfig } from "@playwright/test";

export default defineConfig({
  testDir: "./tests/portal-e2e",
  testMatch: ["owner-journey.spec.ts", "authorization.spec.ts"],
  fullyParallel: false,
  workers: 1,
  retries: 0,
  timeout: 120_000,
  expect: { timeout: 15_000 },
  reporter: [["list"]],
  use: {
    baseURL: "http://127.0.0.1:3000",
    serviceWorkers: "block",
    trace: "off",
    screenshot: "off",
    video: "off",
  },
  projects: [
    {
      name: "portal-owner-chromium",
      testMatch: "owner-journey.spec.ts",
      outputDir: "output/playwright/m113/test-results",
      use: { browserName: "chromium" },
    },
    {
      name: "portal-authorization-chromium",
      testMatch: "authorization.spec.ts",
      outputDir: "output/playwright/m114/test-results",
      use: { browserName: "chromium" },
    },
  ],
});
