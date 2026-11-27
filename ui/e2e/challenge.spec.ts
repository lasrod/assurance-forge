// Adding a challenge (milestone 2, step 6): a node's menu and a line's menu
// offer Add Counter Goal and Add Counter Evidence. The new
// challenger is drawn beside what it challenges, with a challenge line to it,
// is selected, and is one undo step.

import { expect, test, type Page } from '@playwright/test';

const SAMPLE = 'sacm_example_core_argument.sacm.xmi';

const hook = (page: Page) => page.evaluate(() => window.__assuranceForge!);
const canvas = (page: Page) => page.locator('canvas.af-canvas');
const status = (page: Page) => page.getByTestId('status');

async function opened(page: Page, query = '') {
  await page.goto(`/?sample=${SAMPLE}${query}`);
  await expect.poll(async () => (await hook(page))?.revision).toBe(1);
  await expect.poll(async () => (await hook(page)).viewport?.scale ?? null).not.toBeNull();
}

/** A node's centre on the page. */
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

/**
 * A point on the line, not a challenge's, that ends at `to`: the middle of
 * its first segment, or of its last, which is its own where lines share a
 * trunk.
 */
async function onLineTo(page: Page, to: string, segment: 'first' | 'last' = 'first') {
  const box = (await canvas(page).boundingBox())!;
  const state = await hook(page);
  const edge = state.diagram!.diagram.edges.find((candidate) => candidate.to === to && candidate.kind !== 'challenge')!;
  const view = state.viewport!;
  const [a, b] = segment === 'first' ? edge.points : edge.points.slice(-2);
  return {
    edge,
    at: { x: box.x + ((a.x + b.x) / 2) * view.scale + view.offsetX, y: box.y + ((a.y + b.y) / 2) * view.scale + view.offsetY },
  };
}

/** The challenge lines of the diagram: from the challenger to what it challenges. */
async function challenges(page: Page) {
  const { diagram } = await hook(page);
  return diagram!.diagram.edges.filter((edge) => edge.kind === 'challenge').map((edge) => ({ from: edge.from, to: edge.to }));
}

test('a node is challenged by a Goal from its menu, and Undo takes it back', async ({ page }) => {
  await opened(page);
  const at = await nodeCentre(page, 'cl_sub_1');
  await page.mouse.click(at.x, at.y, { button: 'right' });
  const menu = page.getByRole('menu');
  await expect(menu.getByRole('menuitem', { name: 'Add Counter Goal' })).toBeVisible();
  await expect(menu.getByRole('menuitem', { name: 'Add Counter Evidence' })).toBeVisible();
  await menu.getByRole('menuitem', { name: 'Add Counter Goal' }).click();

  await expect.poll(async () => (await hook(page)).revision).toBe(2);
  const challenger = (await hook(page)).selected!;
  expect(await challenges(page)).toEqual([{ from: challenger, to: 'cl_sub_1' }]);
  const node = (await hook(page)).diagram!.diagram.nodes.find((candidate) => candidate.id === challenger)!;
  expect(node.kind).toBe('goal');
  expect(node.text.find((line) => line.role === 'identifier')?.text).toBe('CG1');
  await expect(status(page)).toHaveText('Counter goal CG1 added.');

  await canvas(page).focus();
  await page.keyboard.press('ControlOrMeta+z');
  await expect.poll(async () => (await hook(page)).revision).toBe(3);
  expect(await challenges(page)).toEqual([]);
});

test('a line is challenged by a Solution from its menu, by a right-click or the menu key', async ({ page }) => {
  await opened(page);
  // The line of evidence into ev_2: a relationship of its own.
  const { edge, at } = await onLineTo(page, 'ev_2');
  await page.mouse.click(at.x, at.y, { button: 'right' });
  const menu = page.getByRole('menu', { name: 'Line' });
  await expect(menu).toBeVisible();
  // A line's menu only challenges it: nothing is added under a line.
  await expect(menu.getByRole('menuitem')).toHaveText(['Add Counter Goal', 'Add Counter Evidence']);
  expect((await hook(page)).selected).toBe(`${edge.relationship} ${edge.from} ${edge.to}`);
  await menu.getByRole('menuitem', { name: 'Add Counter Evidence' }).click();

  await expect.poll(async () => (await hook(page)).revision).toBe(2);
  const challenger = (await hook(page)).selected!;
  const drawn = (await hook(page)).diagram!.diagram;
  expect(drawn.nodes.find((candidate) => candidate.id === challenger)!.kind).toBe('solution');
  expect(drawn.edges.some((candidate) => candidate.kind === 'challenge' && candidate.from === challenger && candidate.to === edge.relationship && candidate.toRelationship)).toBe(true);

  // The menu key opens the selected line's menu.
  await canvas(page).focus();
  await page.keyboard.press('Shift+1');
  const again = await onLineTo(page, 'ev_2');
  await page.mouse.click(again.at.x, again.at.y);
  await page.keyboard.press('Shift+F10');
  await expect(page.getByRole('menu', { name: 'Line' })).toBeVisible();
  await page.keyboard.press('Escape');
});

test('a premise line under a Strategy says why it is not challenged on its own', async ({ page }) => {
  await opened(page);
  // The inference from cl_sub_1 and cl_sub_2 to cl_top through ar_1 is one
  // relationship, challenged on its first line, into the Strategy.
  const { at } = await onLineTo(page, 'cl_sub_2');
  await page.mouse.click(at.x, at.y, { button: 'right' });
  await expect(status(page)).toHaveText(/^Nothing changed\. This line has no relationship of its own/);
  await expect(page.getByRole('menu')).toHaveCount(0);
  const first = await onLineTo(page, 'ar_1');
  await page.mouse.click(first.at.x, first.at.y, { button: 'right' });
  await expect(page.getByRole('menu', { name: 'Line' })).toBeVisible();
});

test('the challenge items are worded in Japanese', async ({ page }) => {
  await opened(page, '&lang=ja');
  const at = await nodeCentre(page, 'cl_sub_1');
  await page.mouse.click(at.x, at.y, { button: 'right' });
  const menu = page.getByRole('menu');
  await expect(menu.getByRole('menuitem', { name: '反論ゴールを追加' })).toBeVisible();
  await expect(menu.getByRole('menuitem', { name: '反証を追加' })).toBeVisible();
});

/** Adds a counter goal to `at` from its menu; its SACM id. */
async function counterGoal(page: Page, at: string) {
  const centre = await nodeCentre(page, at);
  await page.mouse.click(centre.x, centre.y, { button: 'right' });
  await page.getByRole('menu').getByRole('menuitem', { name: 'Add Counter Goal' }).click();
  await expect.poll(async () => (await hook(page)).revision).toBe(2);
  return (await hook(page)).selected!;
}

/** Presses on the node `from` and moves to `to` without letting go. */
async function dragTo(page: Page, from: string, to: { x: number; y: number }) {
  const start = await nodeCentre(page, from);
  await page.mouse.move(start.x, start.y);
  await page.mouse.down();
  await page.mouse.move(start.x + 8, start.y + 8, { steps: 2 });
  await expect.poll(async () => (await hook(page)).dragging).toBe(from);
  await page.mouse.move(to.x, to.y, { steps: 8 });
}

test('a new counter goal has its name ready to type', async ({ page }) => {
  await opened(page);
  const challenger = await counterGoal(page, 'cl_sub_1');
  const name = page.getByTestId('element-name');
  await expect(name).toBeFocused();
  await page.keyboard.type('Sensor fails silently');
  await page.keyboard.press('Enter');
  await expect.poll(async () => (await hook(page)).revision).toBe(3);
  expect((await hook(page)).selected).toBe(challenger);
});

// The view follows the new counter goal, beside cl_sub_1: the places it is
// dragged to are those near it, in view on a larger page.
test.describe('moving a challenge by dragging its challenger', () => {
  test.use({ viewport: { width: 1600, height: 1000 } });

  test('a counter goal dragged onto a node challenges it instead, and Undo takes it back', async ({ page }) => {
    await opened(page);
    const challenger = await counterGoal(page, 'cl_sub_1');
    await dragTo(page, challenger, await nodeCentre(page, 'ev_1'));
    await expect.poll(async () => (await hook(page)).hovered).toBe('ev_1');
    await page.mouse.up();

    await expect.poll(async () => (await hook(page)).revision).toBe(3);
    expect(await challenges(page)).toEqual([{ from: challenger, to: 'ev_1' }]);
    await expect(status(page)).toHaveText(/^CG1 now challenges \S+\.$/);
    // Still a counter goal: nothing else about it changed.
    const nodes = (await hook(page)).diagram!.diagram.nodes;
    expect(nodes.find((node) => node.id === challenger)!.kind).toBe('goal');

    await canvas(page).focus();
    await page.keyboard.press('ControlOrMeta+z');
    await expect.poll(async () => (await hook(page)).revision).toBe(4);
    expect(await challenges(page)).toEqual([{ from: challenger, to: 'cl_sub_1' }]);
  });

  test('a counter goal dragged onto a line challenges that line, and every line it may go to is marked', async ({ page }) => {
    await opened(page);
    const challenger = await counterGoal(page, 'cl_sub_1');
    const { edge, at } = await onLineTo(page, 'ev_1');
    await dragTo(page, challenger, at);
    await expect.poll(async () => (await hook(page)).hovered).toBe(`${edge.relationship} ${edge.from} ${edge.to}`);
    await page.mouse.up();

    await expect.poll(async () => (await hook(page)).revision).toBe(3);
    const drawn = (await hook(page)).diagram!.diagram;
    expect(drawn.edges.filter((candidate) => candidate.kind === 'challenge')).toEqual([
      expect.objectContaining({ from: challenger, to: edge.relationship, toRelationship: true }),
    ]);
    await expect(status(page)).toHaveText(/^CG1 now challenges the line from \S+ to \S+\.$/);
  });

  test('Move… then a click on a line moves the challenge there, without a drag', async ({ page }) => {
    await opened(page);
    const challenger = await counterGoal(page, 'cl_sub_1');
    const centre = await nodeCentre(page, challenger);
    await page.mouse.click(centre.x, centre.y, { button: 'right' });
    await page.getByRole('menu').getByRole('menuitem', { name: 'Move…' }).click();
    // A challenge stays a challenge: there is no top level to move it to.
    await expect(page.locator('.af-move-banner').getByRole('button', { name: /top level/ })).toHaveCount(0);
    const { edge, at } = await onLineTo(page, 'ev_1');
    await page.mouse.click(at.x, at.y);

    await expect.poll(async () => (await hook(page)).revision).toBe(3);
    const drawn = (await hook(page)).diagram!.diagram;
    expect(drawn.edges.filter((candidate) => candidate.kind === 'challenge')).toEqual([
      expect.objectContaining({ from: challenger, to: edge.relationship, toRelationship: true }),
    ]);
  });

  test("Move… then a line's menu moves the challenge there, and offers no move onto a line that does not take it", async ({ page }) => {
    await opened(page);
    const challenger = await counterGoal(page, 'cl_sub_1');
    const centre = await nodeCentre(page, challenger);
    await page.mouse.click(centre.x, centre.y, { button: 'right' });
    await page.getByRole('menu').getByRole('menuitem', { name: 'Move…' }).click();
    // A premise's line under a Strategy: only Cancel.
    const premise = await onLineTo(page, 'cl_sub_1', 'last');
    await page.mouse.click(premise.at.x, premise.at.y, { button: 'right' });
    await expect(page.getByRole('menu').getByRole('menuitem')).toHaveText(['Cancel move']);
    await page.keyboard.press('Escape');
    // The line into ev_1 takes it.
    const { edge, at } = await onLineTo(page, 'ev_1');
    await page.mouse.click(at.x, at.y, { button: 'right' });
    const menu = page.getByRole('menu');
    await expect(menu.getByRole('menuitem')).toHaveText(['Move CG1 here', 'Cancel move']);
    await menu.getByRole('menuitem', { name: 'Move CG1 here' }).click();

    await expect.poll(async () => (await hook(page)).revision).toBe(3);
    const drawn = (await hook(page)).diagram!.diagram;
    expect(drawn.edges.filter((candidate) => candidate.kind === 'challenge')).toEqual([
      expect.objectContaining({ from: challenger, to: edge.relationship, toRelationship: true }),
    ]);
  });

  test('while another node is moved, a line opens no menu', async ({ page }) => {
    await opened(page);
    const centre = await nodeCentre(page, 'ev_2');
    await page.mouse.click(centre.x, centre.y, { button: 'right' });
    await page.getByRole('menu').getByRole('menuitem', { name: 'Move…' }).click();
    await expect(page.getByRole('menu')).toHaveCount(0);
    const { at } = await onLineTo(page, 'ev_1');
    await page.mouse.click(at.x, at.y, { button: 'right' });
    // Given time to open, had it been going to.
    await page.waitForTimeout(300);
    await expect(page.getByRole('menu')).toHaveCount(0);
  });

  test('a counter goal dropped on a premise line under a Strategy says why it stays', async ({ page }) => {
    await opened(page);
    const challenger = await counterGoal(page, 'cl_sub_1');
    const { at } = await onLineTo(page, 'cl_sub_1', 'last');
    await dragTo(page, challenger, at);
    await page.mouse.up();
    await expect(status(page)).toHaveText(/^Nothing changed\./);
    expect((await hook(page)).revision).toBe(2);
    expect(await challenges(page)).toEqual([{ from: challenger, to: 'cl_sub_1' }]);
  });
});
