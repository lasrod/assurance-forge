import { defineConfig, devices } from '@playwright/test';

// The browser checks run against the production build made with `--mode e2e`,
// which adds the test hook (src/testHook.ts) and nothing else.
export default defineConfig({
  testDir: 'e2e',
  // Screenshots for review, run by `pnpm verify` (playwright.capture.config.ts).
  testIgnore: 'capture/**',
  fullyParallel: true,
  // More workers made WebKit on Windows close its browser at page.goto, in a
  // different test each run.
  workers: 4,
  reporter: 'list',
  use: { baseURL: 'http://localhost:4174' },
  // Chromium, Firefox and WebKit (the nearest stand-in for Tauri on macOS and Linux): plan decision D1.
  projects: [
    { name: 'chromium', use: { ...devices['Desktop Chrome'] } },
    { name: 'firefox', use: { ...devices['Desktop Firefox'] } },
    { name: 'webkit', use: { ...devices['Desktop Safari'] } },
  ],
  webServer: {
    command: 'pnpm exec vite preview --outDir dist-e2e --port 4174 --strictPort',
    port: 4174,
    reuseExistingServer: false,
  },
});
