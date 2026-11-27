// Assurance claim points (milestone 2, step 7): a line's menu adds one with a
// description or with a confidence argument in a package of its own; a click
// on one shows its argument; a Solution or a Context says why not yet.

import { expect, test, type Page } from '@playwright/test';

const SAMPLE = 'acps.sacm.xmi';

const hook = (page: Page) => page.evaluate(() => window.__assuranceForge!);
const canvas = (page: Page) => page.locator('canvas.af-canvas');
const status = (page: Page) => page.getByTestId('status');
const explorer = (page: Page) => page.getByRole('tree', { name: 'Case Explorer' });

async function opened(page: Page, query = '') {
  await page.goto(`/?sample=${SAMPLE}${query}`);
  await expect.poll(async () => (await hook(page))?.revision).toBe(1);
  await expect.poll(async () => (await hook(page)).viewport?.scale ?? null).not.toBeNull();
}

const shown = async (page: Page) => (await hook(page)).diagram?.package ?? null;

async function toPage(page: Page, point: { x: number; y: number }) {
  const box = (await canvas(page).boundingBox())!;
  const view = (await hook(page)).viewport!;
  return { x: box.x + point.x * view.scale + view.offsetX, y: box.y + point.y * view.scale + view.offsetY };
}

/** The middle of the first segment of the line `relationship` draws into `to`. */
async function onLine(page: Page, relationship: string, to: string) {
  const edge = (await hook(page)).diagram!.diagram.edges.find((candidate) => candidate.relationship === relationship && candidate.to === to)!;
  const [a, b] = edge.points;
  // A third of the way, clear of an ACP's square at its middle.
  return toPage(page, { x: a.x + (b.x - a.x) / 3, y: a.y + (b.y - a.y) / 3 });
}

/** The centre of the ACP of `claim`'s square. */
async function onAcp(page: Page, claim: string) {
  const edges = (await hook(page)).diagram!.diagram.edges;
  const acp = edges.flatMap((edge) => edge.acps).find((candidate) => candidate.claim === claim)!;
  return toPage(page, { x: acp.square.x + acp.square.width / 2, y: acp.square.y + acp.square.height / 2 });
}

const acpsOn = async (page: Page, relationship: string) =>
  (await hook(page)).diagram!.diagram.edges.find((edge) => edge.relationship === relationship)!.acps.map((acp) => acp.label.text);

test('a line takes an ACP with a description, whose text is ready to type, and Undo takes it back', async ({ page }) => {
  await opened(page);
  const at = await onLine(page, 'E2', 'Sn2');
  await page.mouse.click(at.x, at.y, { button: 'right' });
  const menu = page.getByRole('menu', { name: 'Line' });
  await expect(menu.getByRole('menuitem')).toHaveText([
    'Add Counter Goal',
    'Add Counter Evidence',
    'Add ACP with Description',
    'Add ACP with Confidence Argument',
  ]);
  await menu.getByRole('menuitem', { name: 'Add ACP with Description' }).click();

  await expect.poll(async () => (await hook(page)).revision).toBe(2);
  await expect(status(page)).toHaveText('ACP5 added.');
  expect(await acpsOn(page, 'E2')).toEqual(['ACP5']);
  // Not a node: its text is the ACP's, ready to type.
  const description = page.getByTestId('element-statement');
  await expect(description).toBeFocused();
  await page.keyboard.type('The test rig was calibrated before each run.');
  await canvas(page).focus();
  await expect.poll(async () => (await hook(page)).revision).toBe(3);

  await page.keyboard.press('ControlOrMeta+z');
  await page.keyboard.press('ControlOrMeta+z');
  await expect.poll(async () => (await hook(page)).revision).toBe(5);
  expect(await acpsOn(page, 'E2')).toEqual([]);
});

test('a confidence argument is a package of its own, shown with its goal named', async ({ page }) => {
  await opened(page);
  const at = await onLine(page, 'E2', 'Sn2');
  await page.mouse.click(at.x, at.y, { button: 'right' });
  await page.getByRole('menu', { name: 'Line' }).getByRole('menuitem', { name: 'Add ACP with Confidence Argument' }).click();

  await expect.poll(async () => (await hook(page)).revision).toBe(2);
  await expect.poll(() => shown(page)).not.toBeNull();
  await expect(explorer(page).getByRole('treeitem', { name: /Confidence ACP5/ })).toHaveAttribute('aria-selected', 'true');
  const goal = (await hook(page)).diagram!.diagram.nodes;
  expect(goal).toHaveLength(1);
  await expect.poll(async () => (await hook(page)).selected).toBe(goal[0].id);
  await expect(page.getByTestId('element-name')).toBeFocused();

  // The whole case labels the ACP with the package.
  await explorer(page).getByRole('treeitem', { name: /acps\.sacm\.xmi/ }).click();
  await expect.poll(() => shown(page)).toBeNull();
  expect(await acpsOn(page, 'E2')).toEqual(['ACP5[Confidence ACP5]']);
});

test('a click on an ACP shows its confidence argument, or its description', async ({ page }) => {
  await opened(page);
  const elsewhere = await onAcp(page, 'ACP2');
  await page.mouse.click(elsewhere.x, elsewhere.y);
  await expect.poll(() => shown(page)).toBe('argpkg_confidence');
  await expect.poll(async () => (await hook(page)).selected).toBe('ACP2');

  await explorer(page).getByRole('treeitem', { name: /acps\.sacm\.xmi/ }).click();
  await expect.poll(() => shown(page)).toBeNull();
  await expect.poll(async () => (await hook(page)).viewport?.scale ?? null).not.toBeNull();
  const described = await onAcp(page, 'ACP1');
  await page.mouse.click(described.x, described.y);
  await expect.poll(async () => (await hook(page)).selected).toBe('ACP1');
  await expect(page.getByTestId('element-statement')).toHaveValue(/HAZOP of the final design/);
});

test("a Solution's Add ACP says it comes with SACM 2.4", async ({ page }) => {
  await opened(page);
  const node = (await hook(page)).diagram!.diagram.nodes.find((candidate) => candidate.id === 'Sn1')!;
  const at = await toPage(page, { x: node.bounds.x + node.bounds.width / 2, y: node.bounds.y + node.bounds.height / 2 });
  await page.mouse.click(at.x, at.y, { button: 'right' });
  await page.getByRole('menu').getByRole('menuitem', { name: 'Add ACP' }).click();
  await expect(status(page)).toHaveText(/^Nothing changed\. An ACP on a Solution or a Context comes with SACM 2\.4/);
  expect((await hook(page)).revision).toBe(1);
});

test('the ACP items are worded in Japanese', async ({ page }) => {
  await opened(page, '&lang=ja');
  const at = await onLine(page, 'E2', 'Sn2');
  await page.mouse.click(at.x, at.y, { button: 'right' });
  const menu = page.getByRole('menu');
  await expect(menu.getByRole('menuitem', { name: '説明付きの ACP を追加' })).toBeVisible();
  await expect(menu.getByRole('menuitem', { name: '確信論証付きの ACP を追加' })).toBeVisible();
});
