// Moving a node from its menu (milestone 2, step 4): Move… starts choosing a
// place, the canvas shows where the node may go, and a click on a place, or
// its menu, moves the node with what is under it.

import { expect, test, type Page } from '@playwright/test';

const SAMPLE = 'sacm_example_core_argument.sacm.xmi';

const hook = (page: Page) => page.evaluate(() => window.__assuranceForge!);
const canvas = (page: Page) => page.locator('canvas.af-canvas');
const status = (page: Page) => page.getByTestId('status');
const banner = (page: Page) => page.locator('.af-move-banner');

async function opened(page: Page) {
  await page.goto(`/?sample=${SAMPLE}`);
  await expect.poll(async () => (await hook(page))?.revision).toBe(1);
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

async function menuOf(page: Page, id: string) {
  const point = await nodeCentre(page, id);
  await page.mouse.click(point.x, point.y, { button: 'right' });
  const menu = page.getByRole('menu');
  await expect(menu).toBeVisible();
  return menu;
}

async function click(page: Page, id: string) {
  const point = await nodeCentre(page, id);
  await page.mouse.click(point.x, point.y);
}

/** The nodes the diagram draws `id` under. */
async function parents(page: Page, id: string) {
  const { diagram } = await hook(page);
  return diagram!.diagram.edges.filter((edge) => edge.to === id).map((edge) => edge.from);
}

async function startMove(page: Page, id: string) {
  const menu = await menuOf(page, id);
  await menu.getByRole('menuitem', { name: 'Move…' }).click();
  await expect(banner(page)).toBeVisible();
}

test('Move… then a click on a goal moves the node under it, and Undo takes it back', async ({ page }) => {
  await opened(page);
  expect(await parents(page, 'ev_2')).toEqual(['cl_sub_2']);
  await startMove(page, 'ev_2');
  await expect(banner(page)).toContainText('Moving Solution ev_2');
  // Evidence is not a top-level element: the banner does not offer it.
  await expect(banner(page).getByRole('button')).toHaveText(['Cancel move']);

  await click(page, 'cl_sub_1');
  await expect.poll(async () => (await hook(page)).revision).toBe(2);
  expect(await parents(page, 'ev_2')).toEqual(['cl_sub_1']);
  await expect(status(page)).toHaveText('Solution ev_2 moved under cl_sub_1.');
  await expect(banner(page)).toBeHidden();
  // The node moved is selected.
  expect((await hook(page)).selected).toBe('ev_2');

  await page.keyboard.press('ControlOrMeta+z');
  await expect.poll(async () => (await hook(page)).revision).toBe(3);
  expect(await parents(page, 'ev_2')).toEqual(['cl_sub_2']);
});

test('a click on a place the node cannot go says why and keeps the move', async ({ page }) => {
  await opened(page);
  await startMove(page, 'ev_2');
  // A Strategy is supported only by Goals.
  await click(page, 'ar_1');
  await expect(status(page)).toHaveText('Nothing changed. In GSN a Strategy is supported only by Goals.');
  await expect(banner(page)).toBeVisible();
  // A leaf, named by its kind.
  await click(page, 'ev_1');
  await expect(status(page)).toHaveText('Nothing changed. A Solution is a leaf in GSN: nothing goes under it.');
  // Where it is already.
  await click(page, 'cl_sub_2');
  await expect(status(page)).toHaveText('Nothing changed. It is there already.');
  expect((await hook(page)).revision).toBe(1);
  expect(await parents(page, 'ev_2')).toEqual(['cl_sub_2']);
});

test('Escape and Cancel give the move up, and a click selects again', async ({ page }) => {
  await opened(page);
  await startMove(page, 'ev_2');
  await canvas(page).focus();
  await page.keyboard.press('Escape');
  await expect(banner(page)).toBeHidden();
  // The selection is kept: Escape gave up the move, not the selection.
  expect((await hook(page)).selected).toBe('ev_2');

  await startMove(page, 'ev_2');
  await banner(page).getByRole('button', { name: 'Cancel move' }).click();
  await expect(banner(page)).toBeHidden();
  await click(page, 'cl_sub_1');
  expect((await hook(page)).selected).toBe('cl_sub_1');
  expect((await hook(page)).revision).toBe(1);
});

test('the menu of a place moves the node there; another place offers only to cancel', async ({ page }) => {
  await opened(page);
  await startMove(page, 'ev_2');
  let menu = await menuOf(page, 'ar_1');
  await expect(menu.getByRole('menuitem')).toHaveText([/^Cancel move/]);
  await page.keyboard.press('Escape');
  await expect(banner(page)).toBeVisible();

  menu = await menuOf(page, 'cl_top');
  await expect(menu.getByRole('menuitem')).toHaveText([/^Move ev_2 here/, /^Cancel move/]);
  await menu.getByRole('menuitem', { name: 'Move ev_2 here' }).click();
  await expect.poll(async () => (await hook(page)).revision).toBe(2);
  expect(await parents(page, 'ev_2')).toEqual(['cl_top']);
});

test('a goal moves to the top level from the banner, with what is under it', async ({ page }) => {
  await opened(page);
  await startMove(page, 'cl_sub_2');
  await banner(page).getByRole('button', { name: 'Move cl_sub_2 to the top level' }).click();
  await expect.poll(async () => (await hook(page)).revision).toBe(2);
  expect(await parents(page, 'cl_sub_2')).toEqual([]);
  expect(await parents(page, 'ev_2')).toEqual(['cl_sub_2']);
  await expect(status(page)).toHaveText('Goal cl_sub_2 moved to the top level.');
});

test('the move is worded in Japanese', async ({ page }) => {
  await page.goto(`/?sample=${SAMPLE}&lang=ja`);
  await expect.poll(async () => (await hook(page))?.revision).toBe(1);
  const menu = await menuOf(page, 'ev_2');
  await menu.getByRole('menuitem', { name: '移動…' }).click();
  await expect(banner(page)).toContainText('移動先をクリックしてください');
  await click(page, 'cl_sub_1');
  await expect(status(page)).toContainText('の下へ移動しました');
});
