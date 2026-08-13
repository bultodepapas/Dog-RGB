import { defineConfig } from '@playwright/test';

const previewPort = Number(process.env.AP_PORTAL_PREVIEW_PORT ?? 4173);
if (!Number.isInteger(previewPort) || previewPort < 1 || previewPort > 65_535) {
  throw new Error(`Invalid AP_PORTAL_PREVIEW_PORT: ${process.env.AP_PORTAL_PREVIEW_PORT}`);
}
const previewBaseURL = `http://127.0.0.1:${previewPort}`;

export default defineConfig({
  testDir: './tests',
  timeout: 30_000,
  fullyParallel: false,
  reporter: [['list'], ['html', { open: 'never' }]],
  use: {
    baseURL: previewBaseURL,
    trace: 'retain-on-failure',
    screenshot: 'only-on-failure',
    actionTimeout: 10_000,
  },
  expect: {
    toHaveScreenshot: {
      maxDiffPixelRatio: 0.01,
      threshold: 0.2,
    },
  },
  webServer: {
    command: `node tools/ap_portal_preview/server.mjs --port ${previewPort}`,
    url: `${previewBaseURL}/`,
    reuseExistingServer: !process.env.CI,
    timeout: 15_000,
  },
  projects: [
    {
      name: 'iphone-13-pro-max-chromium',
      use: {
        browserName: 'chromium',
        viewport: { width: 428, height: 926 },
        deviceScaleFactor: 3,
        isMobile: true,
        hasTouch: true,
        colorScheme: 'light',
      },
    },
  ],
});
