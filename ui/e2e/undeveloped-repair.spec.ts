// The undeveloped mark of a Goal, set and cleared from Element Properties and
// from the Goal's menu; and the repair, from the Problems panel, of an
// element the file joins by a support line where GSN has context.

import { expect, test, type Page } from '@playwright/test';

const SAMPLE = 'sacm_example_core_argument.sacm.xmi';

const hook = (page: Page) => page.evaluate(() => window.__assuranceForge!);
const canvas = (page: Page) => page.locator('canvas.af-canvas');
const status = (page: Page) => page.getByTestId('status');
const mark = (page: Page) => page.getByTestId('element-undeveloped');

async function opened(page: Page, query = '') {
  await page.goto(`/?sample=${SAMPLE}${query}`);
  await expect.poll(async () => (await hook(page))?.revision).toBe(1);
  await expect.poll(async () => (await hook(page)).viewport?.scale ?? null).not.toBeNull();
}

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

async function select(page: Page, id: string) {
  const point = await nodeCentre(page, id);
  await page.mouse.click(point.x, point.y);
  await expect.poll(async () => (await hook(page)).selected).toBe(id);
}

async function menuOf(page: Page, id: string) {
  const point = await nodeCentre(page, id);
  await page.mouse.click(point.x, point.y, { button: 'right' });
  const menu = page.getByRole('menu');
  await expect(menu).toBeVisible();
  return menu;
}

/** Whether the diagram draws the undeveloped diamond on the node. */
async function marked(page: Page, id: string) {
  const { diagram } = await hook(page);
  return diagram!.diagram.nodes.find((node) => node.id === id)!.decorators.some((decorator) => decorator.type === 'undeveloped');
}

/** How the diagram joins `to` under `from`. */
async function line(page: Page, from: string, to: string) {
  const { diagram } = await hook(page);
  return diagram!.diagram.edges.find((edge) => edge.from === from && edge.to === to)?.kind ?? null;
}

test('a stale mark is cleared in Element Properties, and a supported goal is then not marked', async ({ page }) => {
  await opened(page);
  // The example marks `cl_sub_2` undeveloped though it has evidence.
  expect(await marked(page, 'cl_sub_2')).toBe(true);
  await select(page, 'cl_sub_2');
  const box = mark(page).getByRole('checkbox', { name: 'Undeveloped' });
  await expect(box).toBeChecked();
  await expect(box).toBeEnabled();
  await box.uncheck();
  await expect.poll(async () => (await hook(page)).revision).toBe(2);
  expect(await marked(page, 'cl_sub_2')).toBe(false);
  await expect(status(page)).toHaveText('Undeveloped mark of cl_sub_2 cleared.');
  // Supported: the mark cannot be set, and the panel says why.
  await expect(box).not.toBeChecked();
  await expect(box).toBeDisabled();
  await expect(mark(page)).toContainText('A supported Goal is not undeveloped.');

  // One undo step.
  await canvas(page).focus();
  await page.keyboard.press('ControlOrMeta+z');
  await expect.poll(async () => (await hook(page)).revision).toBe(3);
  expect(await marked(page, 'cl_sub_2')).toBe(true);
});

test('a goal with no support is marked from its menu and from Element Properties; other elements have no mark', async ({ page }) => {
  await opened(page);
  // Not a Goal: no checkbox.
  for (const id of ['ar_1', 'ev_1', 'cl_assumption_1']) {
    await select(page, id);
    await expect(page.getByTestId('element-identifier')).toHaveValue(id);
    await expect(mark(page)).toHaveCount(0);
  }
  // A new Goal has no support.
  let menu = await menuOf(page, 'cl_top');
  // A supported Goal's menu does not offer the mark.
  await expect(menu.getByRole('menuitem', { name: /undeveloped/i })).toHaveCount(0);
  await menu.getByRole('menuitem', { name: 'Add Goal' }).click();
  await expect.poll(async () => (await hook(page)).revision).toBe(2);
  const goal = (await hook(page)).selected!;
  expect(await marked(page, goal)).toBe(false);

  menu = await menuOf(page, goal);
  await menu.getByRole('menuitem', { name: 'Mark as undeveloped' }).click();
  await expect.poll(async () => (await hook(page)).revision).toBe(3);
  expect(await marked(page, goal)).toBe(true);
  await expect(status(page)).toHaveText('G1 marked as undeveloped.');
  await expect(page.getByTestId('element-type')).toHaveText('Goal, undeveloped');

  const box = mark(page).getByRole('checkbox', { name: 'Undeveloped' });
  await expect(box).toBeChecked();
  menu = await menuOf(page, goal);
  await expect(menu.getByRole('menuitem', { name: 'Clear the undeveloped mark' })).toBeVisible();
  await page.keyboard.press('Escape');
  await box.uncheck();
  await expect.poll(async () => (await hook(page)).revision).toBe(4);
  expect(await marked(page, goal)).toBe(false);
});

test('the problem of an assumption joined by a support line offers its repair, which is one undo step', async ({ page }) => {
  await opened(page);
  expect(await line(page, 'cl_sub_1', 'cl_assumption_1')).toBe('supportedBy');
  // The status bar's counts open the panel.
  await page.getByTestId('problem-counts').getByRole('button').click();
  const problems = page.getByRole('listbox', { name: 'Problems' });
  const rows = problems.getByRole('option');
  const before = await rows.count();
  // Nothing is offered until a problem is chosen, and only for one that can be repaired.
  await expect(page.getByTestId('problem-repair')).toHaveCount(0);
  await rows.filter({ hasText: 'AF-GSN-010' }).click();
  await expect(page.getByTestId('problem-repair')).toHaveCount(0);
  await rows.filter({ hasText: 'AF-GSN-004' }).click();
  const repair = page.getByTestId('problem-repair');
  await expect(repair).toContainText('Join cl_assumption_1 as context, in place of the support line.');
  await repair.getByRole('button', { name: 'Repair' }).click();

  await expect.poll(async () => (await hook(page)).revision).toBe(2);
  expect(await line(page, 'cl_sub_1', 'cl_assumption_1')).toBe('inContextOf');
  await expect(status(page)).toHaveText('Assumption cl_assumption_1 joined as context.');
  await expect(rows).toHaveCount(before - 1);
  await expect(rows.filter({ hasText: 'AF-GSN-004' })).toHaveCount(0);
  await expect(page.getByTestId('problem-repair')).toHaveCount(0);
  expect((await hook(page)).selected).toBe('cl_assumption_1');

  await canvas(page).focus();
  await page.keyboard.press('ControlOrMeta+z');
  await expect.poll(async () => (await hook(page)).revision).toBe(3);
  expect(await line(page, 'cl_sub_1', 'cl_assumption_1')).toBe('supportedBy');
  await expect(rows.filter({ hasText: 'AF-GSN-004' })).toHaveCount(1);
});

test('the mark and the repair are worded in Japanese', async ({ page }) => {
  await opened(page, '&lang=ja');
  await select(page, 'cl_sub_2');
  await mark(page).getByRole('checkbox', { name: '未展開' }).uncheck();
  await expect(status(page)).toHaveText('cl_sub_2 の未展開の印を外しました。');
  await page.getByTestId('problem-counts').getByRole('button').click();
  await page.getByRole('option').filter({ hasText: 'AF-GSN-004' }).click();
  await page.getByTestId('problem-repair').getByRole('button', { name: '修復' }).click();
  await expect(status(page)).toContainText('を文脈としてつなぎました。');
});
