import { defineConfig, devices } from '@playwright/test';

// Screenshots for review (e2e/capture), run by `pnpm verify`: one browser,
// one worker, the pixel ratio fixed at 1, so two runs compare image to image.
// Like the browser tests, it drives the production build made with `--mode e2e`.
export default defineConfig({
  testDir: 'e2e/capture',
  workers: 1,
  reporter: 'list',
  timeout: 120_000,
  use: { baseURL: 'http://localhost:4175', deviceScaleFactor: 1, trace: 'retain-on-failure' },
  projects: [{ name: 'chromium', use: { ...devices['Desktop Chrome'], deviceScaleFactor: 1 } }],
  webServer: {
    command: 'pnpm exec vite preview --outDir dist-e2e --port 4175 --strictPort',
    port: 4175,
    reuseExistingServer: false,
  },
});
