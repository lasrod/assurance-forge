// Moving a node by dragging it (milestone 2, step 4): a drag that begins on
// a node moves it, with what is under it, to the place it is dropped on; a
// drag that begins anywhere else pans.

import { expect, test, type Page } from '@playwright/test';

const SAMPLE = 'sacm_example_core_argument.sacm.xmi';

const hook = (page: Page) => page.evaluate(() => window.__assuranceForge!);
const canvas = (page: Page) => page.locator('canvas.af-canvas');
const status = (page: Page) => page.getByTestId('status');

async function opened(page: Page, sample = SAMPLE) {
  await page.goto(`/?sample=${sample}`);
  await expect.poll(async () => (await hook(page))?.revision).toBe(1);
  await expect.poll(async () => (await hook(page)).viewport?.scale ?? null).not.toBeNull();
}

/** A node's centre on the page, from the diagram as drawn. */
async function nodeCentre(page: Page, id: string) {
  const box = (await canvas(page).boundingBox())!;
  const state = await hook(page);
  const bounds = state.diagram!.diagram.nodes.find((node) => node.id === id)!.bounds;
  const view = state.viewport!;
  return {
    x: box.x + (bounds.x + bounds.width / 2) * view.scale + view.offsetX,
    y: box.y + (bounds.y + bounds.height / 2) * view.scale + view.offsetY,
  };
}

/** The nodes the diagram draws `id` under. */
async function parents(page: Page, id: string) {
  const { diagram } = await hook(page);
  return diagram!.diagram.edges.filter((edge) => edge.to === id).map((edge) => edge.from);
}

type Point = { x: number; y: number };

/** Presses on the node `from` and moves to `to`, a node or a point, without letting go. */
async function dragTo(page: Page, from: string, to: string | Point) {
  const start = await nodeCentre(page, from);
  const end = typeof to === 'string' ? await nodeCentre(page, to) : to;
  await page.mouse.move(start.x, start.y);
  await page.mouse.down();
  await page.mouse.move(start.x + 8, start.y + 8, { steps: 2 });
  await expect.poll(async () => (await hook(page)).dragging).toBe(from);
  await page.mouse.move(end.x, end.y, { steps: 8 });
}

test('a node dragged onto a goal moves under it, and Undo takes it back', async ({ page }) => {
  await opened(page);
  expect(await parents(page, 'ev_2')).toEqual(['cl_sub_2']);
  const view = (await hook(page)).viewport!;
  await dragTo(page, 'ev_2', 'cl_sub_1');
  // The place under the pointer is marked, and the view did not pan.
  await expect.poll(async () => (await hook(page)).hovered).toBe('cl_sub_1');
  expect((await hook(page)).viewport).toEqual(view);
  // No prompt: the drag is the choice.
  await expect(page.locator('.af-move-banner')).toBeHidden();
  await page.mouse.up();

  await expect.poll(async () => (await hook(page)).revision).toBe(2);
  expect(await parents(page, 'ev_2')).toEqual(['cl_sub_1']);
  await expect(status(page)).toHaveText('Solution ev_2 moved under cl_sub_1.');
  expect((await hook(page)).selected).toBe('ev_2');
  expect((await hook(page)).dragging).toBeNull();

  await page.keyboard.press('ControlOrMeta+z');
  await expect.poll(async () => (await hook(page)).revision).toBe(3);
  expect(await parents(page, 'ev_2')).toEqual(['cl_sub_2']);
});

test('a branch moves with what is under it', async ({ page }) => {
  await opened(page);
  await dragTo(page, 'cl_sub_2', 'cl_top');
  await page.mouse.up();
  await expect.poll(async () => (await hook(page)).revision).toBe(2);
  expect(await parents(page, 'cl_sub_2')).toEqual(['cl_top']);
  expect(await parents(page, 'ev_2')).toEqual(['cl_sub_2']);
});

test('a drop on a place the node cannot go says why, and the drag is over', async ({ page }) => {
  await opened(page);
  await dragTo(page, 'ev_2', 'ar_1');
  await page.mouse.up();
  await expect(status(page)).toHaveText('Nothing changed. In GSN a Strategy is supported only by Goals.');
  expect((await hook(page)).revision).toBe(1);
  expect((await hook(page)).dragging).toBeNull();
  // The choice did not stay open: a click selects again.
  const point = await nodeCentre(page, 'cl_sub_1');
  await page.mouse.click(point.x, point.y);
  await expect.poll(async () => (await hook(page)).selected).toBe('cl_sub_1');
});

test('a drop on nothing, or back on the node, changes nothing', async ({ page }) => {
  await opened(page);
  const box = (await canvas(page).boundingBox())!;
  // A Goal could go to the top level, but not by a drop that missed.
  await dragTo(page, 'cl_sub_2', { x: box.x + box.width / 2, y: box.y + box.height - 80 });
  await page.mouse.up();
  await expect.poll(async () => (await hook(page)).dragging).toBeNull();
  expect((await hook(page)).revision).toBe(1);

  await dragTo(page, 'cl_sub_2', 'cl_sub_2');
  await page.mouse.up();
  await expect.poll(async () => (await hook(page)).dragging).toBeNull();
  expect((await hook(page)).revision).toBe(1);
  expect(await parents(page, 'cl_sub_2')).toEqual(['ar_1']);
});

test('Escape gives the drag up, and the rest of the press does nothing', async ({ page }) => {
  await opened(page);
  await canvas(page).focus();
  const view = (await hook(page)).viewport!;
  const end = await nodeCentre(page, 'cl_sub_1');
  await dragTo(page, 'ev_2', 'cl_sub_1');
  await page.keyboard.press('Escape');
  await expect.poll(async () => (await hook(page)).dragging).toBeNull();
  // Moved on, the pointer pans nothing; let go on a node, it is not selected.
  await page.mouse.move(end.x - 200, end.y + 100, { steps: 6 });
  await page.mouse.move(end.x, end.y, { steps: 6 });
  await page.mouse.up();
  expect((await hook(page)).selected).toBe('ev_2');
  expect((await hook(page)).revision).toBe(1);
  expect((await hook(page)).viewport).toEqual(view);
  await expect(page.locator('.af-move-banner')).toBeHidden();
});

test('a drag that begins on the empty canvas pans, and moves nothing', async ({ page }) => {
  await opened(page);
  const box = (await canvas(page).boundingBox())!;
  const before = (await hook(page)).viewport!;
  await page.mouse.move(box.x + 40, box.y + box.height - 40);
  await page.mouse.down();
  await page.mouse.move(box.x + 140, box.y + box.height - 90, { steps: 5 });
  await page.mouse.up();
  const after = (await hook(page)).viewport!;
  expect(after.offsetX - before.offsetX).toBeCloseTo(100, 0);
  expect(after.offsetY - before.offsetY).toBeCloseTo(-50, 0);
  expect((await hook(page)).revision).toBe(1);
});

test('a node that cannot be moved says why at once, and nothing follows the pointer', async ({ page }) => {
  // `shared` is under two goals: there is no one line to move.
  await opened(page, 'gallery-shared-support.sacm.xmi');
  const { diagram } = await hook(page);
  const counts = new Map<string, number>();
  for (const edge of diagram!.diagram.edges) counts.set(edge.to, (counts.get(edge.to) ?? 0) + 1);
  const shared = [...counts].find(([, count]) => count > 1)![0];
  const start = await nodeCentre(page, shared);
  await page.mouse.move(start.x, start.y);
  await page.mouse.down();
  await page.mouse.move(start.x + 30, start.y + 30, { steps: 4 });
  await expect(status(page)).toHaveText('Nothing changed. This element is under more than one element, so there is no single line to move.');
  await expect.poll(async () => (await hook(page)).dragging).toBeNull();
  // The rest of the press pans, and looks it.
  const view = (await hook(page)).viewport!;
  await page.mouse.move(start.x + 200, start.y + 120, { steps: 6 });
  await expect.poll(async () => (await hook(page)).viewport!.offsetX - view.offsetX).toBeGreaterThan(100);
  await expect(canvas(page)).toHaveCSS('cursor', 'grabbing');
  await page.mouse.up();
  expect((await hook(page)).revision).toBe(1);

  // A Goal's menu still opens.
  const goal = diagram!.diagram.nodes.find((node) => node.kind === 'goal')!;
  const at = await nodeCentre(page, goal.id);
  await page.mouse.click(at.x, at.y, { button: 'right' });
  await expect(page.getByRole('menu')).toBeVisible();
});

test('a long press opens no menu while a node is dragged, and ends a press held still', async ({ page }) => {
  // A touch held on a node arms its drag before the browser's long press
  // opens a menu: the menu event is sent as that long press sends it.
  await opened(page);
  const longPress = (at: Point) =>
    canvas(page).dispatchEvent('contextmenu', { clientX: at.x, clientY: at.y, button: 2, bubbles: true, cancelable: true });
  const end = await nodeCentre(page, 'cl_sub_1');
  await dragTo(page, 'ev_2', 'cl_sub_1');
  await longPress(end);
  await expect(page.getByRole('menu')).toBeHidden();
  expect((await hook(page)).dragging).toBe('ev_2');
  await page.mouse.up();
  await expect.poll(async () => (await hook(page)).revision).toBe(2);

  // Held still on a node: the menu opens, and no drag follows.
  const start = await nodeCentre(page, 'cl_sub_2');
  await page.mouse.move(start.x, start.y);
  await page.mouse.down();
  await longPress(start);
  await expect(page.getByRole('menu')).toBeVisible();
  await page.mouse.move(start.x + 40, start.y + 40, { steps: 4 });
  expect((await hook(page)).dragging ?? null).toBeNull();
  await page.mouse.up();
  expect((await hook(page)).revision).toBe(2);
});

test('the view pans when a dragged node nears an edge', async ({ page }) => {
  await opened(page);
  const box = (await canvas(page).boundingBox())!;
  const before = (await hook(page)).viewport!;
  await dragTo(page, 'ev_2', { x: box.x + 10, y: box.y + box.height / 2 });
  // Held at the left edge, the diagram keeps moving right.
  await expect.poll(async () => (await hook(page)).viewport!.offsetX - before.offsetX).toBeGreaterThan(60);
  await page.keyboard.press('Escape');
  await page.mouse.up();
  expect((await hook(page)).revision).toBe(1);
});

test('zoomed out too far to read a node, a drag on it pans', async ({ page }) => {
  await opened(page, 'safety-case-sanitized-strict.sacm.xmi');
  const { diagram, viewport } = await hook(page);
  // The large case at fit: no text is drawn.
  expect(viewport!.scale).toBeLessThan(0.3);
  const box = (await canvas(page).boundingBox())!;
  // A node in view.
  const node = diagram!.diagram.nodes.find((candidate) => {
    const x = (candidate.bounds.x + candidate.bounds.width / 2) * viewport!.scale + viewport!.offsetX;
    const y = (candidate.bounds.y + candidate.bounds.height / 2) * viewport!.scale + viewport!.offsetY;
    return x > 100 && x < box.width - 100 && y > 60 && y < box.height - 60;
  })!;
  const start = await nodeCentre(page, node.id);
  await page.mouse.move(start.x, start.y);
  await page.mouse.down();
  await page.mouse.move(start.x + 60, start.y + 30, { steps: 5 });
  expect((await hook(page)).dragging).toBeNull();
  await page.mouse.up();
  expect((await hook(page)).viewport!.offsetX - viewport!.offsetX).toBeCloseTo(60, 0);
  expect((await hook(page)).revision).toBe(1);
});

test('a touch on a node pans, and moves it only once it has been held', async ({ page, browserName }) => {
  // Touches are sent through Chromium's debugging protocol.
  test.skip(browserName !== 'chromium', 'touch input is driven over CDP');
  await opened(page);
  const client = await page.context().newCDPSession(page);
  const touch = async (type: 'touchStart' | 'touchMove' | 'touchEnd', point?: Point) =>
    client.send('Input.dispatchTouchEvent', { type, touchPoints: point ? [{ x: Math.round(point.x), y: Math.round(point.y) }] : [] });
  const slide = async (from: Point, to: Point) => {
    for (let step = 1; step <= 8; step += 1) {
      await touch('touchMove', { x: from.x + ((to.x - from.x) * step) / 8, y: from.y + ((to.y - from.y) * step) / 8 });
    }
  };

  // At once: the view pans, and nothing moves.
  const before = (await hook(page)).viewport!;
  let start = await nodeCentre(page, 'ev_2');
  await touch('touchStart', start);
  await slide(start, { x: start.x - 80, y: start.y - 40 });
  await touch('touchEnd');
  await expect.poll(async () => (await hook(page)).viewport!.offsetX - before.offsetX).toBeCloseTo(-80, 0);
  expect((await hook(page)).dragging ?? null).toBeNull();
  expect((await hook(page)).revision).toBe(1);

  // Held first: the node is dragged, and dropped on a goal it moves there.
  start = await nodeCentre(page, 'ev_2');
  const end = await nodeCentre(page, 'cl_sub_1');
  const held = (await hook(page)).viewport!;
  await touch('touchStart', start);
  await page.waitForTimeout(450);
  await slide(start, end);
  await expect.poll(async () => (await hook(page)).dragging).toBe('ev_2');
  expect((await hook(page)).viewport).toEqual(held);
  await touch('touchEnd');
  await expect.poll(async () => (await hook(page)).revision).toBe(2);
  expect(await parents(page, 'ev_2')).toEqual(['cl_sub_1']);
});

test('a second finger during a touch drag gives the drag up', async ({ page, browserName }) => {
  test.skip(browserName !== 'chromium', 'touch input is driven over CDP');
  await opened(page);
  const client = await page.context().newCDPSession(page);
  const touch = async (type: 'touchStart' | 'touchMove' | 'touchEnd', points: Point[]) =>
    client.send('Input.dispatchTouchEvent', {
      type,
      touchPoints: points.map((point, id) => ({ x: Math.round(point.x), y: Math.round(point.y), id })),
    });
  const start = await nodeCentre(page, 'ev_2');
  await touch('touchStart', [start]);
  await page.waitForTimeout(450);
  for (let step = 1; step <= 4; step += 1) await touch('touchMove', [{ x: start.x + step * 10, y: start.y + step * 10 }]);
  await expect.poll(async () => (await hook(page)).dragging).toBe('ev_2');
  const first = { x: start.x + 40, y: start.y + 40 };
  await touch('touchStart', [first, { x: first.x + 120, y: first.y }]);
  await touch('touchMove', [first, { x: first.x + 160, y: first.y }]);
  await touch('touchEnd', []);
  await expect.poll(async () => (await hook(page)).dragging ?? null).toBeNull();

  // No move is left under way: a click on a Goal selects it and moves nothing.
  const goal = await nodeCentre(page, 'cl_sub_1');
  await page.mouse.click(goal.x, goal.y);
  await expect.poll(async () => (await hook(page)).selected).toBe('cl_sub_1');
  expect((await hook(page)).revision).toBe(1);
});
