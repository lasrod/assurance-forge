/// <reference types="vitest/config" />
import { readFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import type { Connect, Plugin } from 'vite';
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

const testdata = resolve(__dirname, '../testdata/sacm');

/**
 * Serves the sample cases at /samples/<file> in the development and preview
 * servers, so `?sample=<file>` opens one. A production build has no such route.
 */
function samples(): Plugin {
  const handler: Connect.NextHandleFunction = (request, response, next) => {
    const match = /^\/samples\/([\w.-]+\.xmi)$/.exec(request.url ?? '');
    if (!match) return next();
    readFile(resolve(testdata, match[1])).then(
      (bytes) => {
        response.setHeader('Content-Type', 'application/xml');
        response.end(bytes);
      },
      () => {
        response.statusCode = 404;
        response.end();
      },
    );
  };
  return {
    name: 'assurance-forge-samples',
    configureServer: (server) => void server.middlewares.use(handler),
    configurePreviewServer: (server) => void server.middlewares.use(handler),
  };
}

export default defineConfig({
  plugins: [react(), samples()],
  server: { fs: { allow: ['..'] } },
  build: { target: 'es2022' },
  test: { include: ['src/**/*.test.ts'] },
});
