// Adding elements from the canvas's context menu, and Edit ▸ Undo and Redo
// (milestone 2, step 1). The menu lists the six GSN kinds on every node;
// those GSN does not allow there are unavailable and say why.

import { expect, test, type Page } from '@playwright/test';

const SAMPLE = 'sacm_example_core_argument.sacm.xmi';
const KINDS = ['Add Goal', 'Add Strategy', 'Add Solution', 'Add Context', 'Add Assumption', 'Add Justification'];

const hook = (page: Page) => page.evaluate(() => window.__assuranceForge!);
// Found by its class: its name follows the language.
const canvas = (page: Page) => page.locator('canvas.af-canvas');
/** What the diagram holds, in view or not (the hook's `drawn` counts only what is in view). */
const counts = async (page: Page) => {
  const { diagram } = await hook(page);
  return { nodes: diagram!.diagram.nodes.length, edges: diagram!.diagram.edges.length };
};
const status = (page: Page) => page.getByTestId('status');

async function opened(page: Page, query = '') {
  await page.goto(`/?sample=${SAMPLE}${query}`);
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

/** Opens the add menu of a node by a right-click on it. */
async function menuOf(page: Page, id: string) {
  const point = await nodeCentre(page, id);
  await page.mouse.click(point.x, point.y, { button: 'right' });
  const menu = page.getByRole('menu');
  await expect(menu).toBeVisible();
  return menu;
}

async function editMenu(page: Page, item: 'Undo' | 'Redo') {
  await page.getByRole('menuitem', { name: 'Edit', exact: true }).click();
  return page.getByRole('menu', { name: 'Edit' }).getByRole('menuitem', { name: item });
}

test('a right-click on a goal lists the six kinds; choosing one adds it, selected, and Undo and Redo take it back and bring it again', async ({ page }) => {
  await opened(page);
  const menu = await menuOf(page, 'cl_top');
  await expect(menu).toHaveAccessibleName('Goal cl_top');
  await expect(menu.getByRole('menuitem')).toHaveText(KINDS.map((kind) => new RegExp(`^${kind}`)));
  for (const item of await menu.getByRole('menuitem').all()) await expect(item).not.toHaveAttribute('aria-disabled', 'true');
  // The right-clicked node is selected.
  expect((await hook(page)).selected).toBe('cl_top');
  const topBefore = await nodeCentre(page, 'cl_top');

  // A Goal under the top claim moves the top claim in the layout, over its
  // new children: the view follows, so it stays where it was on the screen.
  await menu.getByRole('menuitem', { name: 'Add Goal' }).click();
  await expect(menu).toBeHidden();
  await expect.poll(() => counts(page)).toEqual({ nodes: 9, edges: 8 });
  const added = (await hook(page)).selected!;
  expect(added).not.toBe('cl_top');
  await expect(status(page)).toHaveText('Goal G1 added.');
  await expect(page.getByTestId('element-properties')).toContainText('Goal');
  const state = await hook(page);
  expect(state.revision).toBe(2);
  expect(state.diagram!.diagram.edges.some((edge) => edge.from === 'cl_top' && edge.to === added && edge.kind === 'supportedBy')).toBe(true);
  const topAfter = await nodeCentre(page, 'cl_top');
  expect(Math.abs(topAfter.x - topBefore.x)).toBeLessThan(1);
  expect(Math.abs(topAfter.y - topBefore.y)).toBeLessThan(1);

  await (await editMenu(page, 'Undo')).click();
  await expect.poll(() => counts(page)).toEqual({ nodes: 8, edges: 7 });
  await expect(status(page)).toHaveText('Undone.');
  expect((await hook(page)).selected).toBeNull();
  const redo = await editMenu(page, 'Redo');
  await expect(redo).not.toHaveAttribute('aria-disabled', 'true');
  await redo.click();
  await expect.poll(() => counts(page)).toEqual({ nodes: 9, edges: 8 });
  await expect(status(page)).toHaveText('Redone.');
  // Nothing more to redo.
  await expect(await editMenu(page, 'Redo')).toHaveAttribute('aria-disabled', 'true');
  await page.keyboard.press('Escape');
});

test('a leaf lists the six kinds, each unavailable and saying why, and choosing one adds nothing', async ({ page }) => {
  await opened(page);
  const menu = await menuOf(page, 'ev_1');
  const items = menu.getByRole('menuitem');
  await expect(items).toHaveCount(6);
  for (const item of await items.all()) {
    await expect(item).toHaveAttribute('aria-disabled', 'true');
    await expect(item).toHaveAccessibleDescription('A Solution is a leaf in GSN: nothing goes under it.');
  }
  // Chosen from the keyboard, as a pointer cannot choose it.
  await page.keyboard.press('ArrowDown');
  await expect(items.first()).toBeFocused();
  await page.keyboard.press('Enter');
  await page.keyboard.press('Escape');
  await expect(menu).toBeHidden();
  expect((await hook(page)).revision).toBe(1);
  expect(await counts(page)).toEqual({ nodes: 8, edges: 7 });
});

test('a Strategy comes with its first sub-goal, and support under an undeveloped goal develops it', async ({ page }) => {
  await opened(page);
  // cl_sub_2 is undeveloped: its support items say so.
  const undeveloped = (await hook(page)).diagram!.diagram.nodes.find((node) => node.id === 'cl_sub_2')!;
  expect(undeveloped.decorators.some((decorator) => decorator.type === 'undeveloped')).toBe(true);
  let menu = await menuOf(page, 'cl_sub_2');
  await expect(menu.getByRole('menuitem', { name: 'Add Goal' })).toHaveAccessibleDescription('Develops this goal.');
  await expect(menu.getByRole('menuitem', { name: 'Add Strategy' })).toHaveAccessibleDescription('With its first sub-goal. Develops this goal.');
  await expect(menu.getByRole('menuitem', { name: 'Add Context' })).toHaveAccessibleDescription('');
  await menu.getByRole('menuitem', { name: 'Add Strategy' }).click();
  await expect.poll(() => counts(page)).toEqual({ nodes: 10, edges: 9 });
  await expect(status(page)).toHaveText('Strategy S1 added.');
  const state = await hook(page);
  const developed = state.diagram!.diagram.nodes.find((node) => node.id === 'cl_sub_2')!;
  expect(developed.decorators.some((decorator) => decorator.type === 'undeveloped')).toBe(false);
  const strategy = state.selected!;
  expect(state.diagram!.diagram.nodes.find((node) => node.id === strategy)!.kind).toBe('strategy');
  expect(state.diagram!.diagram.edges.filter((edge) => edge.from === strategy && edge.kind === 'supportedBy')).toHaveLength(1);

  // Under the Strategy: a Goal, but not a Solution or a Strategy, whose SACM form is not decided.
  menu = await menuOf(page, strategy);
  await expect(menu.getByRole('menuitem', { name: 'Add Goal' })).not.toHaveAttribute('aria-disabled', 'true');
  for (const name of ['Add Strategy', 'Add Solution']) {
    await expect(menu.getByRole('menuitem', { name })).toHaveAttribute('aria-disabled', 'true');
    await expect(menu.getByRole('menuitem', { name })).toHaveAccessibleDescription('GSN allows it, but how SACM holds it is not decided yet.');
  }
  await page.keyboard.press('Escape');

  // One undo takes back the Strategy, its sub-goal and the development.
  await page.keyboard.press('Control+z');
  await expect.poll(() => counts(page)).toEqual({ nodes: 8, edges: 7 });
  const again = (await hook(page)).diagram!.diagram.nodes.find((node) => node.id === 'cl_sub_2')!;
  expect(again.decorators.some((decorator) => decorator.type === 'undeveloped')).toBe(true);
  await page.keyboard.press('Control+y');
  await expect.poll(() => counts(page)).toEqual({ nodes: 10, edges: 9 });
});

test('Shift+F10 opens the selected node’s menu from the keyboard, and Enter adds the first kind', async ({ page }) => {
  await opened(page);
  const point = await nodeCentre(page, 'cl_sub_1');
  await page.mouse.click(point.x, point.y);
  await expect.poll(async () => (await hook(page)).selected).toBe('cl_sub_1');
  await page.keyboard.press('Shift+F10');
  const menu = page.getByRole('menu');
  await expect(menu).toBeVisible();
  await expect(menu.getByRole('menuitem', { name: 'Add Goal' })).toBeFocused();
  await page.keyboard.press('Enter');
  await expect.poll(() => counts(page)).toEqual({ nodes: 9, edges: 8 });
  await expect(status(page)).toHaveText('Goal G1 added.');
  // Escape returned focus to the canvas: its keys work again.
  await expect(canvas(page)).toBeFocused();
});

test('on the empty canvas only a top-level Goal can be added, and a line has no menu', async ({ page }) => {
  await opened(page);
  const box = (await canvas(page).boundingBox())!;
  await page.mouse.click(box.x + 12, box.y + box.height - 12, { button: 'right' });
  const menu = page.getByRole('menu');
  await expect(menu).toHaveAccessibleName('Add');
  await expect(menu.getByRole('menuitem', { name: 'Add Goal' })).not.toHaveAttribute('aria-disabled', 'true');
  await expect(menu.getByRole('menuitem', { name: 'Add Solution' })).toHaveAccessibleDescription('Only a Goal can be added at the top level.');
  await menu.getByRole('menuitem', { name: 'Add Goal' }).click();
  await expect.poll(() => counts(page)).toEqual({ nodes: 9, edges: 7 });
  const added = (await hook(page)).selected!;
  expect((await hook(page)).diagram!.diagram.edges.some((edge) => edge.to === added)).toBe(false);

  // The line from cl_sub_1 down to ev_1, at its middle.
  const state = await hook(page);
  const edge = state.diagram!.diagram.edges.find((candidate) => candidate.from === 'cl_sub_1' && candidate.to === 'ev_1')!;
  const [a, b] = [edge.points[0], edge.points[1]];
  const view = state.viewport!;
  await page.mouse.click(
    box.x + ((a.x + b.x) / 2) * view.scale + view.offsetX,
    box.y + ((a.y + b.y) / 2) * view.scale + view.offsetY,
    { button: 'right' },
  );
  await expect(page.getByRole('menu')).toHaveCount(0);
});

test('an edit keeps the Argument Navigator’s closed branches closed', async ({ page }) => {
  await opened(page);
  const navigator = page.getByRole('tree', { name: 'Argument Navigator' });
  const branch = navigator.getByRole('treeitem', { name: /Reasoning_ArchitectureAndVerification/ });
  await branch.focus();
  await page.keyboard.press('ArrowLeft');
  await expect(navigator.getByRole('treeitem')).toHaveCount(3);
  const menu = await menuOf(page, 'ctx_1');
  await page.keyboard.press('Escape');
  await expect(menu).toBeHidden();
  // A Context under the top claim adds a row; the closed branch stays closed.
  await (await menuOf(page, 'cl_top')).getByRole('menuitem', { name: 'Add Context' }).click();
  await expect(navigator.getByRole('treeitem')).toHaveCount(4);
  await expect(branch).toHaveAttribute('aria-expanded', 'false');
});

test('the menu and the edits are worded in Japanese', async ({ page }) => {
  await opened(page, '&lang=ja');
  const menu = await menuOf(page, 'ev_1');
  await expect(menu.getByRole('menuitem').first()).toHaveText(/^ゴールを追加/);
  await expect(menu.getByRole('menuitem').first()).toHaveAccessibleDescription('GSN ではソリューションは末端です。下には何も置けません。');
  await page.keyboard.press('Escape');
  await (await menuOf(page, 'cl_top')).getByRole('menuitem', { name: '仮定を追加' }).click();
  await expect(status(page)).toHaveText('仮定 A1 を追加しました。');
  await page.keyboard.press('Control+z');
  await expect(status(page)).toHaveText('元に戻しました。');
});
