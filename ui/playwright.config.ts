import { defineConfig, devices } from '@playwright/test';

// The browser checks run against the production build made with `--mode e2e`,
// which adds the test hook (src/testHook.ts) and nothing else.
export default defineConfig({
  testDir: 'e2e',
  fullyParallel: true,
  reporter: 'list',
  use: { baseURL: 'http://localhost:4174' },
  projects: [{ name: 'chromium', use: { ...devices['Desktop Chrome'] } }],
  webServer: {
    command: 'pnpm exec vite preview --outDir dist-e2e --port 4174 --strictPort',
    port: 4174,
    reuseExistingServer: false,
  },
});
