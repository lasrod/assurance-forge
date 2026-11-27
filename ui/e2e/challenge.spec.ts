// Adding a challenge (milestone 2, step 6): a node's menu and a line's menu
// offer Challenge with a Goal and Challenge with a Solution. The new
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

/** A point on the line that ends at `to`: the middle of its first segment. */
async function onLineTo(page: Page, to: string) {
  const box = (await canvas(page).boundingBox())!;
  const state = await hook(page);
  const edge = state.diagram!.diagram.edges.find((candidate) => candidate.to === to)!;
  const view = state.viewport!;
  const [a, b] = edge.points;
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
  await expect(menu.getByRole('menuitem', { name: 'Challenge with a Goal' })).toBeVisible();
  await expect(menu.getByRole('menuitem', { name: 'Challenge with a Solution' })).toBeVisible();
  await menu.getByRole('menuitem', { name: 'Challenge with a Goal' }).click();

  await expect.poll(async () => (await hook(page)).revision).toBe(2);
  const challenger = (await hook(page)).selected!;
  expect(await challenges(page)).toEqual([{ from: challenger, to: 'cl_sub_1' }]);
  const node = (await hook(page)).diagram!.diagram.nodes.find((candidate) => candidate.id === challenger)!;
  expect(node.kind).toBe('goal');
  await expect(status(page)).toHaveText(/^Goal G\d+ added\.$/);

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
  await expect(menu.getByRole('menuitem')).toHaveText(['Challenge with a Goal', 'Challenge with a Solution']);
  expect((await hook(page)).selected).toBe(`${edge.relationship} ${edge.from} ${edge.to}`);
  await menu.getByRole('menuitem', { name: 'Challenge with a Solution' }).click();

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
  await expect(menu.getByRole('menuitem', { name: 'ゴールで異議を唱える' })).toBeVisible();
  await expect(menu.getByRole('menuitem', { name: 'ソリューションで異議を唱える' })).toBeVisible();
});
