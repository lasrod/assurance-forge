// Relationships on the canvas: a line is hovered and selected like a node,
// one line at a time; Element Properties shows the line and the SACM
// relationship it is drawn for; and a challenge is drawn at what it
// challenges. The sample has every case: context on both sides of a goal,
// and a challenge of a claim, of a context, of a line of support, of a line
// of context and of another challenge.

import { expect, test, type Page } from '@playwright/test';

const SAMPLE = 'challenges.sacm.xmi';

async function opened(page: Page, query = '') {
  await page.setViewportSize({ width: 1280, height: 800 });
  await page.goto(`/?sample=${SAMPLE}${query}`);
  await expect.poll(() => page.evaluate(() => window.__assuranceForge?.drawn)).toEqual({ nodes: 16, edges: 15 });
}

const canvas = (page: Page) => page.getByRole('img', { name: 'GSN diagram' });

async function hook(page: Page) {
  const state = await page.evaluate(() => window.__assuranceForge);
  if (!state?.viewport || !state.diagram) throw new Error('nothing drawn');
  return { viewport: state.viewport, diagram: state.diagram.diagram, selected: state.selected, hovered: state.hovered };
}

/**
 * A point on a line of a relationship, in page coordinates: the middle of
 * its first segment. The line is the relationship's first, or the one that
 * ends at `to`.
 */
async function onLine(page: Page, relationship: string, to?: string) {
  const { viewport: view, diagram } = await hook(page);
  const box = (await canvas(page).boundingBox())!;
  const edge = diagram.edges.find((candidate) => candidate.relationship === relationship && (to === undefined || candidate.to === to))!;
  const [a, b] = edge.points;
  return {
    x: box.x + ((a.x + b.x) / 2) * view.scale + view.offsetX,
    y: box.y + ((a.y + b.y) / 2) * view.scale + view.offsetY,
  };
}

async function emptyPoint(page: Page) {
  const box = (await canvas(page).boundingBox())!;
  return { x: box.x + 6, y: box.y + 6 };
}

/** The canvas pixel at a page point, as r + g + b. */
async function pixel(page: Page, at: { x: number; y: number }) {
  return canvas(page).evaluate((element: HTMLCanvasElement, point) => {
    const rect = element.getBoundingClientRect();
    const scale = element.width / rect.width;
    const data = element.getContext('2d')!.getImageData(Math.round((point.x - rect.x) * scale), Math.round((point.y - rect.y) * scale), 1, 1).data;
    return data[0] + data[1] + data[2];
  }, at);
}

test('a click on a line selects that line, and Element Properties shows it and its relationship', async ({ page }, testInfo) => {
  await opened(page, '&lang=en&theme=light');
  const properties = page.getByRole('region', { name: 'Element Properties' });
  await expect(properties).toContainText('Nothing is selected.');

  // The evidence under g_trap: a line of support.
  const support = await onLine(page, 'e_trials');
  const plain = await pixel(page, support);
  await page.mouse.click(support.x, support.y);
  await expect.poll(async () => (await hook(page)).selected).toBe('e_trials g_trap sn_trials');
  const empty = await emptyPoint(page);
  await page.mouse.move(empty.x, empty.y);
  await expect.poll(async () => (await hook(page)).hovered).toBeNull();
  await expect.poll(() => pixel(page, support)).not.toBe(plain);

  const grid = page.getByTestId('relationship-properties');
  await expect(grid).toContainText('Supported by');
  await expect(grid).toContainText('g_trap');
  await expect(grid).toContainText('sn_trials');
  await expect(grid).toContainText('AssertedEvidence');
  await page.screenshot({ path: testInfo.outputPath('selected-support.png') });

  // A challenge of that line: its arrow runs from what challenges to the relationship.
  const challenge = await onLine(page, 'k_sample');
  await page.mouse.click(challenge.x, challenge.y);
  await expect.poll(async () => (await hook(page)).selected).toBe('k_sample cg_sample e_trials');
  await expect(grid).toContainText('Challenges');
  await expect(grid).toContainText('cg_sample');
  await expect(grid).toContainText('relationship e_trials');

  // An inference with a Strategy is one SACM relationship drawn as three
  // lines. A click selects the one line under it; the panel shows that line,
  // and the whole relationship below it.
  const inference = await onLine(page, 'i_hazards', 'g_moving');
  const sibling = await onLine(page, 'i_hazards', 's_hazards');
  const siblingPlain = await pixel(page, sibling);
  await page.mouse.click(inference.x, inference.y);
  await expect.poll(async () => (await hook(page)).selected).toBe('i_hazards s_hazards g_moving');
  await page.mouse.move(empty.x, empty.y);
  await expect.poll(async () => (await hook(page)).hovered).toBeNull();
  // The line into the Strategy belongs to the same relationship and is not marked.
  expect(await pixel(page, sibling)).toBe(siblingPlain);
  const connection = page.getByTestId('connection');
  await expect(connection).toContainText('s_hazards');
  await expect(connection).toContainText('g_moving');
  await expect(connection).not.toContainText('g_trap');
  const relationship = page.getByTestId('sacm-relationship');
  await expect(relationship).toContainText('AssertedInference');
  await expect(relationship).toContainText('g_top');
  await expect(relationship).toContainText('g_trap, g_moving');
  await page.screenshot({ path: testInfo.outputPath('selected-inference.png') });

  // The panel's words follow the language.
  await page.getByRole('menuitem', { name: 'View', exact: true }).click();
  await page.getByRole('menuitem', { name: 'Appearance' }).click();
  await page.getByRole('menuitem', { name: 'Language' }).click();
  await page.getByRole('menuitemradio', { name: '日本語' }).click();
  await expect(grid).toContainText('サポート関係');
  expect((await hook(page)).selected).toBe('i_hazards s_hazards g_moving');
});

test('Esc clears a selected relationship', async ({ page }) => {
  await opened(page);
  // A challenge of a context: the dashed line up into it.
  const line = await onLine(page, 'k_audit');
  await page.mouse.click(line.x, line.y);
  await expect.poll(async () => (await hook(page)).selected).toBe('k_audit ce_audit c_definition');
  await page.keyboard.press('Escape');
  await expect.poll(async () => (await hook(page)).selected).toBeNull();
  await expect(page.getByRole('region', { name: 'Element Properties' })).toContainText('Nothing is selected.');
});

test('a line under the pointer is marked, a node over it wins, and a click beside it clears', async ({ page }) => {
  await opened(page, '&theme=light');
  const line = await onLine(page, 'x_definition');
  await page.mouse.move(line.x, line.y);
  await expect.poll(async () => (await hook(page)).hovered).toBe('x_definition g_top c_definition');
  await expect(canvas(page)).toHaveCSS('cursor', 'pointer');
  expect((await hook(page)).selected).toBeNull();

  // Within reach of the line but not on it.
  await page.mouse.move(line.x, line.y + 3);
  await expect.poll(async () => (await hook(page)).hovered).toBe('x_definition g_top c_definition');
  await page.mouse.click(line.x, line.y + 3);
  await expect.poll(async () => (await hook(page)).selected).toBe('x_definition g_top c_definition');

  // Out of reach: empty canvas, which clears.
  await page.mouse.click(line.x, line.y + 12);
  await expect.poll(async () => (await hook(page)).selected).toBeNull();

  // A node selects itself, not the line that ends on it.
  const { viewport: view, diagram } = await hook(page);
  const box = (await canvas(page).boundingBox())!;
  const rect = diagram.nodes.find((candidate) => candidate.id === 'g_top')!.shape.rect;
  // Three CSS px inside its edge, whatever the zoom: within the line's reach,
  // and a whole pixel inside the node in a browser that rounds the pointer.
  await page.mouse.click(box.x + rect.x * view.scale + view.offsetX + 3, box.y + (rect.y + rect.height / 2) * view.scale + view.offsetY);
  await expect.poll(async () => (await hook(page)).selected).toBe('g_top');
});

test('Fit selection fits a selected relationship', async ({ page }) => {
  await opened(page);
  const line = await onLine(page, 'e_trials');
  await page.mouse.click(line.x, line.y);
  await expect.poll(async () => (await hook(page)).selected).toBe('e_trials g_trap sn_trials');
  const before = (await hook(page)).viewport;
  await page.keyboard.press('Shift+Digit2');
  await expect.poll(async () => (await hook(page)).viewport.scale).toBeGreaterThan(before.scale);
});
