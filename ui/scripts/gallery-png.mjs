// Renders the layout gallery's SVGs to PNG in Chromium, with the measured
// faces the SVGs load from their fonts/ folder, so they can be looked at and
// put beside each other.
//
//   node scripts/gallery-png.mjs <gallery-dir> [max-width]
//
// Each <name>.svg gets <name>.png at most max-width CSS px wide (default 1600),
// and <name>-actual.png, the top-left corner at 100%, 1600 × 1000, where the
// text is read at the size the application draws it.

import { readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { chromium } from '@playwright/test';

const dir = resolve(process.argv[2] ?? '.');
const maxWidth = Number(process.argv[3] ?? 1600);
const browser = await chromium.launch();
const page = await browser.newPage({ deviceScaleFactor: 1 });
for (const file of readdirSync(dir).filter((name) => name.endsWith('.svg'))) {
  const svg = readFileSync(resolve(dir, file), 'utf8');
  const [, width, height] = /width="([\d.]+)" height="([\d.]+)"/.exec(svg) ?? [];
  const scale = Math.min(1, maxWidth / Number(width));
  const shown = { width: Math.ceil(Number(width) * scale), height: Math.ceil(Number(height) * scale) };
  for (const [suffix, size, zoom] of [
    ['', shown, scale],
    ['-actual', { width: Math.min(1600, Math.ceil(Number(width))), height: Math.min(1000, Math.ceil(Number(height))) }, 1],
  ]) {
    const html = resolve(dir, `.${file}${suffix}.html`);
    writeFileSync(
      html,
      `<!doctype html><meta charset="utf-8"><style>html,body{margin:0;background:#fff}svg{display:block;transform-origin:0 0;transform:scale(${zoom})}</style>${svg}`,
    );
    await page.setViewportSize(size);
    await page.goto(pathToFileURL(html).href);
    await page.evaluate(() => document.fonts.ready);
    await page.screenshot({ path: resolve(dir, file.replace(/\.svg$/, `${suffix}.png`)) });
  }
}
await browser.close();
console.log(`rendered the SVGs in ${dir}`);
