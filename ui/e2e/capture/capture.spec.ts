// Screenshots for review, not assertions: the whole application in the states
// a reviewer looks at, and the canvas of every gallery fixture at fit and at
// 100%. `pnpm verify` runs this (playwright.capture.config.ts) and puts the
// images in its report; the ordinary browser tests do not.
//
// Where the images go: AF_CAPTURE_DIR, default verification/latest/screens.

import { mkdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { expect, test, type Page } from '@playwright/test';

const OUT = resolve(process.env.AF_CAPTURE_DIR ?? 'verification/latest/screens');
mkdirSync(OUT, { recursive: true });

/** What each image shows, for the report. */
const notes: Record<string, unknown> = {};
test.afterAll(() => {
  writeFileSync(resolve(OUT, `captures-${test.info().project.name}.json`), `${JSON.stringify(notes, null, 2)}\n`);
});

// By element: its accessible name is in the interface language.
const canvas = (page: Page) => page.locator('canvas').first();

async function opened(page: Page, sample: string, query = '') {
  await page.goto(`/?sample=${sample}${query}`);
  await expect.poll(() => page.evaluate(() => window.__assuranceForge?.revision)).toBe(1);
  await expect.poll(() => page.evaluate(() => window.__assuranceForge?.viewport?.scale ?? null)).not.toBeNull();
  await page.evaluate(() => document.fonts.ready);
}

async function hook(page: Page) {
  const state = await page.evaluate(() => window.__assuranceForge);
  if (!state?.viewport || !state.diagram) throw new Error('nothing drawn');
  return { viewport: state.viewport, diagram: state.diagram.diagram, selected: state.selected };
}

async function nodeCentre(page: Page, id: string) {
  const { viewport: view, diagram } = await hook(page);
  const box = (await canvas(page).boundingBox())!;
  const node = diagram.nodes.find((candidate) => candidate.id === id);
  if (!node) throw new Error(`no node ${id}`);
  const { x, y, width, height } = node.shape.rect;
  return { x: box.x + (x + width / 2) * view.scale + view.offsetX, y: box.y + (y + height / 2) * view.scale + view.offsetY };
}

/** The middle of a relationship's first segment, in page coordinates. */
async function onLine(page: Page, relationship: string) {
  const { viewport: view, diagram } = await hook(page);
  const box = (await canvas(page).boundingBox())!;
  const edge = diagram.edges.find((candidate) => candidate.relationship === relationship);
  if (!edge) throw new Error(`no edge ${relationship}`);
  const [a, b] = edge.points;
  return { x: box.x + ((a.x + b.x) / 2) * view.scale + view.offsetX, y: box.y + ((a.y + b.y) / 2) * view.scale + view.offsetY };
}

/**
 * Selects a node from the Argument Navigator, as a user finds one in a large
 * case, then shows it at 100% in the middle of the canvas.
 */
async function workAt(page: Page, id: string) {
  const { diagram } = await hook(page);
  const node = diagram.nodes.find((candidate) => candidate.id === id);
  const identifier = node?.text.find((line) => line.role === 'identifier')?.text ?? id;
  const item = page.getByRole('treeitem').filter({ hasText: identifier }).first();
  if (await item.isVisible()) await item.click();
  else {
    // A narrow window shows the navigator as a tab: the node is clicked on the canvas.
    const at = await nodeCentre(page, id);
    await page.mouse.click(at.x, at.y);
  }
  await expect.poll(async () => (await hook(page)).selected).toBe(id);
  await canvas(page).focus();
  await page.keyboard.press('Shift+Digit2');
  await page.keyboard.press('Shift+Digit0');
  await expect.poll(async () => (await hook(page)).viewport.scale).toBe(1);
}

async function shot(page: Page, name: string, what: string, target: 'page' | 'canvas' = 'page') {
  const { viewport, diagram, selected } = await hook(page).catch(() => ({ viewport: null, diagram: null, selected: null }));
  const file = `${name}.png`;
  if (target === 'canvas') await canvas(page).screenshot({ path: resolve(OUT, file) });
  else await page.screenshot({ path: resolve(OUT, file) });
  notes[name] = {
    file,
    what,
    window: page.viewportSize(),
    zoom: viewport ? Math.round(viewport.scale * 1000) / 10 : null,
    selected,
    diagram: diagram ? { width: Math.round(diagram.bounds.width), height: Math.round(diagram.bounds.height) } : null,
  };
}

test('1 the welcome page', async ({ page }) => {
  await page.setViewportSize({ width: 1440, height: 900 });
  await page.goto('/?lang=en&theme=light');
  await expect(page.getByRole('button', { name: /example/i }).first()).toBeVisible();
  await page.evaluate(() => document.fonts.ready);
  await shot(page, 'app-1-welcome', 'The welcome page, nothing open.');
});

test('2 a working view with a node selected', async ({ page }) => {
  await page.setViewportSize({ width: 1440, height: 900 });
  await opened(page, 'sacm_example_core_argument.sacm.xmi', '&lang=en&theme=light');
  const at = await nodeCentre(page, 'cl_sub_1');
  await page.mouse.click(at.x, at.y);
  await expect.poll(async () => (await hook(page)).selected).toBe('cl_sub_1');
  await shot(page, 'app-2-selected-node', 'The core sample at fit, Goal cl_sub_1 selected, its properties on the right.');
});

test('3 a dense case at a working zoom', async ({ page }) => {
  await page.setViewportSize({ width: 1440, height: 900 });
  await opened(page, 'safety-case-sanitized-strict.sacm.xmi', '&lang=en&theme=light');
  await shot(page, 'app-3a-dense-fit', 'The large case at fit: the overview.');
  const { diagram } = await hook(page);
  // A Goal with support below it, near the middle of the case.
  const below = new Set(diagram.edges.filter((edge) => edge.kind === 'supportedBy').map((edge) => edge.from));
  const goals = diagram.nodes.filter((node) => node.kind === 'goal' && below.has(node.id));
  const target = goals[Math.floor(goals.length / 2)];
  await workAt(page, target.id);
  await shot(page, 'app-3b-dense-100', `The large case at 100% on Goal ${target.id}.`);
});

test('4 a challenge of a relationship selected', async ({ page }) => {
  await page.setViewportSize({ width: 1440, height: 900 });
  await opened(page, 'challenges.sacm.xmi', '&lang=en&theme=light');
  const at = await onLine(page, 'k_sample');
  await page.mouse.click(at.x, at.y);
  await expect.poll(async () => (await hook(page)).selected).not.toBeNull();
  await shot(page, 'app-4-selected-challenge', 'challenges.sacm.xmi at fit, the challenge k_sample (of the evidence relationship e_trials) selected.');
});

test('5 a small window with Japanese labels', async ({ page }) => {
  await page.setViewportSize({ width: 1024, height: 680 });
  await opened(page, 'oasc-ja.sacm.xmi', '&lang=ja&theme=light');
  const { diagram } = await hook(page);
  const target = diagram.nodes.find((node) => node.kind === 'goal' && node.text.length > 3) ?? diagram.nodes[0];
  await workAt(page, target.id);
  await shot(page, 'app-5-small-japanese', `oasc-ja.sacm.xmi in Japanese, 1024 × 680, Goal ${target.id} at 100% and selected.`);
});

test('7 changes not saved, and the question before they are lost', async ({ page }) => {
  await page.setViewportSize({ width: 1440, height: 900 });
  await opened(page, 'sacm_example_core_argument.sacm.xmi', '&lang=en&theme=light');
  const at = await nodeCentre(page, 'cl_top');
  await page.mouse.click(at.x, at.y, { button: 'right' });
  await page.getByRole('menu').getByRole('menuitem', { name: 'Add Goal' }).click();
  await expect(page.getByTestId('modified')).toBeVisible();
  await shot(page, 'app-7a-not-saved', 'A Goal added to the core sample: the status bar says Not saved, the title is marked.');
  await page.getByRole('menuitem', { name: 'File', exact: true }).click();
  await page.getByRole('menu', { name: 'File' }).getByRole('menuitem', { name: 'Close' }).click();
  await expect(page.getByRole('alertdialog')).toBeVisible();
  await shot(page, 'app-7b-unsaved-question', 'File ▸ Close with the change not saved: Cancel, Don’t save, Save.');
});

test('6 the dark theme', async ({ page }) => {
  await page.setViewportSize({ width: 1440, height: 900 });
  await opened(page, 'challenges.sacm.xmi', '&lang=en&theme=dark');
  await shot(page, 'app-6-dark', 'challenges.sacm.xmi at fit in the dark theme.');
});

// The gallery fixtures, as the application draws them (af-layout's gallery example lists the same).
const FIXTURES: [string, string][] = [
  ['sacm_example_core_argument.sacm.xmi', 'en'],
  ['gallery-shared-support.sacm.xmi', 'en'],
  ['gallery-attachments.sacm.xmi', 'en'],
  ['gallery-long-text.sacm.xmi', 'en'],
  ['gallery-japanese.sacm.xmi', 'ja'],
  ['challenges.sacm.xmi', 'en'],
  ['gallery-roots-and-cycle.sacm.xmi', 'en'],
  ['oasc-ja.sacm.xmi', 'ja'],
  ['safety-case-sanitized-strict.sacm.xmi', 'en'],
];

for (const [sample, language] of FIXTURES) {
  const name = sample.replace(/\.sacm\.xmi$/, '');
  test(`fixture ${name}`, async ({ page }) => {
    await page.setViewportSize({ width: 1440, height: 900 });
    await opened(page, sample, `&lang=${language}&theme=light`);
    await shot(page, `fixture-${name}-fit`, `${sample} at fit.`, 'canvas');
    // At 100%, on the first node of the Argument Navigator: a root.
    const first = await page.getByRole('tree', { name: /Argument Navigator|論証ナビゲーター/ }).getByRole('treeitem').first();
    await first.click();
    await canvas(page).focus();
    await page.keyboard.press('Shift+Digit2');
    await page.keyboard.press('Shift+Digit0');
    await expect.poll(async () => (await hook(page)).viewport.scale).toBe(1);
    await shot(page, `fixture-${name}-100`, `${sample} at 100% on its first root.`, 'canvas');
  });
}

// Responsiveness in the browser, measured before anything is done about it:
// a file chosen with the page already started, to its first paint; and the
// interval between paints while the view is panned with the wheel.
test('timing', async ({ page }) => {
  await page.setViewportSize({ width: 1440, height: 900 });
  await page.goto('/?lang=en&theme=light');
  await expect(page.getByRole('button', { name: /example/i }).first()).toBeVisible();
  const timing: Record<string, unknown> = {};
  for (const sample of ['sacm_example_core_argument.sacm.xmi', 'oasc-ja.sacm.xmi', 'safety-case-sanitized-strict.sacm.xmi']) {
    const runs: number[] = [];
    for (let run = 0; run < 3; run += 1) {
      const before = await page.evaluate(() => ({ at: performance.now(), revision: window.__assuranceForge?.revision ?? 0 }));
      await page.getByTestId('open-file').setInputFiles(`../testdata/sacm/${sample}`);
      // The revision counts opens, so the next one names this file's.
      await expect.poll(() => page.evaluate(() => window.__assuranceForge?.revision ?? 0)).toBeGreaterThan(before.revision ?? 0);
      await expect.poll(() => page.evaluate(() => window.__assuranceForge?.drawn.nodes ?? 0)).toBeGreaterThan(0);
      const after = await page.evaluate(() => new Promise<number>((done) => requestAnimationFrame(() => done(performance.now()))));
      runs.push(after - before.at);
    }
    runs.sort((a, b) => a - b);
    timing[sample] = { openToFirstPaintMs: Math.round(runs[1]), runsMs: runs.map(Math.round) };
  }
  // Panning the large case: wheel events for about a second, paints counted.
  const box = (await canvas(page).boundingBox())!;
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  const frames = await page.evaluate(
    () =>
      new Promise<{ frames: number; meanMs: number; worstMs: number; displayMs: number }>((done) => {
        // When a new paint was first seen, and every display frame.
        const stamps: number[] = [];
        const display: number[] = [];
        let lastPaints = -1;
        const canvasElement = document.querySelector('canvas')!;
        const start = performance.now();
        const tick = (now: number) => {
          display.push(now);
          canvasElement.dispatchEvent(new WheelEvent('wheel', { deltaX: 12, deltaY: 6, bubbles: true, cancelable: true, clientX: 700, clientY: 400 }));
          const paints = window.__assuranceForge?.paints ?? 0;
          if (paints !== lastPaints) stamps.push(now);
          lastPaints = paints;
          if (now - start < 1000) requestAnimationFrame(tick);
          else {
            const gaps = stamps.slice(1).map((at, i) => at - stamps[i]);
            const displayGaps = display.slice(1).map((at, i) => at - display[i]);
            const mean = (xs: number[]) => xs.reduce((a, b) => a + b, 0) / Math.max(1, xs.length);
            done({ frames: stamps.length, meanMs: Math.round(mean(gaps) * 10) / 10, worstMs: Math.round(Math.max(0, ...gaps) * 10) / 10, displayMs: Math.round(mean(displayGaps) * 10) / 10 });
          }
        };
        requestAnimationFrame(tick);
      }),
  );
  timing['safety-case-sanitized-strict.sacm.xmi pan'] = frames;
  writeFileSync(resolve(OUT, 'timing.json'), `${JSON.stringify(timing, null, 2)}\n`);
});
