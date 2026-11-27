// Adding and removing elements from the canvas's context menu, and Edit ▸
// Undo and Redo (milestone 2). The menu lists the GSN kinds that can be
// added there, with no notes, and Move… and Remove for a node.

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

test('a right-click on a goal lists the six kinds, the two challenges, Move and Remove; choosing one adds it, selected, and Undo and Redo take it back and bring it again', async ({ page }) => {
  await opened(page);
  const menu = await menuOf(page, 'cl_top');
  await expect(menu).toHaveAccessibleName('Goal cl_top');
  await expect(menu.getByRole('menuitem')).toHaveText(
    [...KINDS, 'Challenge with a Goal', 'Challenge with a Solution', 'Move…', 'Remove'].map((kind) => new RegExp(`^${kind}`)),
  );
  for (const item of await menu.getByRole('menuitem').all()) await expect(item).not.toHaveAttribute('aria-disabled', 'true');
  // No notes: an add item says only what it adds. Remove names its key.
  for (const kind of KINDS) await expect(menu.getByRole('menuitem', { name: kind })).toHaveAccessibleDescription('');
  await expect(menu.getByRole('menuitem', { name: 'Remove' })).toHaveAccessibleDescription('Delete');
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
  // Its identifier is given, and its name and description are the author's to write.
  await expect(page.getByTestId('element-type')).toHaveText('Goal');
  await expect(page.getByTestId('element-identifier')).toHaveValue('G1');
  await expect(page.getByTestId('element-name')).toHaveValue('');
  await expect(page.getByTestId('element-statement')).toHaveValue('');
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

test('a leaf offers nothing to add, only its challenges, Move and Remove', async ({ page }) => {
  await opened(page);
  const menu = await menuOf(page, 'ev_1');
  await expect(menu.getByRole('menuitem')).toHaveText([/^Challenge with a Goal/, /^Challenge with a Solution/, /^Move…/, /^Remove/]);
  await page.keyboard.press('Escape');
  await expect(menu).toBeHidden();
  expect((await hook(page)).revision).toBe(1);
});

test('a Strategy is added alone, takes only Goals under it, and develops an undeveloped goal', async ({ page }) => {
  await opened(page);
  const undeveloped = (await hook(page)).diagram!.diagram.nodes.find((node) => node.id === 'cl_sub_2')!;
  expect(undeveloped.decorators.some((decorator) => decorator.type === 'undeveloped')).toBe(true);
  let menu = await menuOf(page, 'cl_sub_2');
  await menu.getByRole('menuitem', { name: 'Add Strategy' }).click();
  // The Strategy and its line, and nothing under it.
  await expect.poll(() => counts(page)).toEqual({ nodes: 9, edges: 8 });
  await expect(status(page)).toHaveText('Strategy S1 added.');
  const state = await hook(page);
  const developed = state.diagram!.diagram.nodes.find((node) => node.id === 'cl_sub_2')!;
  expect(developed.decorators.some((decorator) => decorator.type === 'undeveloped')).toBe(false);
  const strategy = state.selected!;
  expect(state.diagram!.diagram.nodes.find((node) => node.id === strategy)!.kind).toBe('strategy');
  expect(state.diagram!.diagram.edges.filter((edge) => edge.from === strategy)).toHaveLength(0);

  // Under the Strategy: a Goal and its context, never a Strategy or a Solution.
  menu = await menuOf(page, strategy);
  await expect(menu.getByRole('menuitem')).toHaveText([
    /^Add Goal/,
    /^Add Context/,
    /^Add Assumption/,
    /^Add Justification/,
    /^Challenge with a Goal/,
    /^Challenge with a Solution/,
    /^Move…/,
    /^Remove/,
  ]);
  await menu.getByRole('menuitem', { name: 'Add Goal' }).click();
  await expect.poll(() => counts(page)).toEqual({ nodes: 10, edges: 9 });
  await expect(status(page)).toHaveText('Goal G1 added.');
  const goal = (await hook(page)).selected!;
  expect((await hook(page)).diagram!.diagram.edges.some((edge) => edge.from === strategy && edge.to === goal)).toBe(true);

  // Each is one undo step; the first takes the Strategy and the development back.
  await page.keyboard.press('Control+z');
  await expect.poll(() => counts(page)).toEqual({ nodes: 9, edges: 8 });
  await page.keyboard.press('Control+z');
  await expect.poll(() => counts(page)).toEqual({ nodes: 8, edges: 7 });
  const again = (await hook(page)).diagram!.diagram.nodes.find((node) => node.id === 'cl_sub_2')!;
  expect(again.decorators.some((decorator) => decorator.type === 'undeveloped')).toBe(true);
  await page.keyboard.press('Control+y');
  await expect.poll(() => counts(page)).toEqual({ nodes: 9, edges: 8 });
});

test('Delete removes a node with nothing under it at once, and selects what it was under', async ({ page }) => {
  await opened(page);
  const point = await nodeCentre(page, 'ev_1');
  await page.mouse.click(point.x, point.y);
  await expect.poll(async () => (await hook(page)).selected).toBe('ev_1');
  await page.keyboard.press('Delete');
  await expect.poll(() => counts(page)).toEqual({ nodes: 7, edges: 6 });
  await expect(page.getByRole('alertdialog')).toHaveCount(0);
  await expect(status(page)).toHaveText('Solution ev_1 removed.');
  expect((await hook(page)).selected).toBe('cl_sub_1');
  await page.keyboard.press('Control+z');
  await expect.poll(() => counts(page)).toEqual({ nodes: 8, edges: 7 });
});

test('removing a node with elements under it lists them and asks first; Cancel keeps them', async ({ page }) => {
  await opened(page);
  // A Context under the top claim, for Ctrl+Z to have something to take back.
  await (await menuOf(page, 'cl_top')).getByRole('menuitem', { name: 'Add Context' }).click();
  await expect.poll(async () => (await hook(page)).revision).toBe(2);
  let menu = await menuOf(page, 'cl_sub_1');
  await menu.getByRole('menuitem', { name: 'Remove' }).click();
  const dialog = page.getByRole('alertdialog');
  await expect(dialog).toBeVisible();
  await expect(dialog).toHaveAccessibleName('Remove Goal cl_sub_1 and what is under it?');
  const rows = page.getByTestId('remove-list').getByRole('listitem');
  await expect(rows).toHaveCount(3);
  await expect(rows.first()).toHaveText(/^Goal cl_sub_1/);
  // While it asks, Edit's keys do nothing underneath it.
  await page.keyboard.press('Control+z');
  await expect(dialog).toBeVisible();
  await dialog.getByRole('button', { name: 'Cancel' }).click();
  await expect(dialog).toBeHidden();
  expect((await hook(page)).revision).toBe(2);
  expect(await counts(page)).toEqual({ nodes: 9, edges: 8 });

  menu = await menuOf(page, 'cl_sub_1');
  await menu.getByRole('menuitem', { name: 'Remove' }).click();
  await page.getByTestId('remove-confirm').click();
  await expect.poll(() => counts(page)).toEqual({ nodes: 6, edges: 5 });
  await expect(status(page)).toHaveText('Goal cl_sub_1 and 2 elements under it removed.');
  // What it was under is selected; one undo brings all three back.
  expect((await hook(page)).selected).toBe('ar_1');
  await page.keyboard.press('Control+z');
  await expect.poll(() => counts(page)).toEqual({ nodes: 9, edges: 8 });
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

test('the context-menu key opens the selected node’s menu, and the browser’s own menu stays off it', async ({ page }) => {
  await opened(page);
  const point = await nodeCentre(page, 'cl_sub_1');
  await page.mouse.click(point.x, point.y);
  await expect.poll(async () => (await hook(page)).selected).toBe('cl_sub_1');
  await page.keyboard.press('ContextMenu');
  const menu = page.getByRole('menu');
  await expect(menu).toBeVisible();
  const first = menu.getByRole('menuitem', { name: 'Add Goal' });
  await expect(first).toBeFocused();
  // On Windows the key's contextmenu event arrives after the menu has taken
  // focus. DATUM's menu cancels it, so the browser shows no menu of its own
  // over it; this keeps that so.
  const cancelled = await first.evaluate((item) => {
    const event = new MouseEvent('contextmenu', { bubbles: true, cancelable: true });
    item.dispatchEvent(event);
    return event.defaultPrevented;
  });
  expect(cancelled).toBe(true);
  await expect(menu).toBeVisible();
  await page.keyboard.press('Escape');
});

test('Shift+F10 with a line selected opens the line’s menu, as a right-click on it does', async ({ page }) => {
  await opened(page);
  const box = (await canvas(page).boundingBox())!;
  const state = await hook(page);
  const edge = state.diagram!.diagram.edges.find((candidate) => candidate.from === 'cl_sub_1' && candidate.to === 'ev_1')!;
  const [a, b] = [edge.points[0], edge.points[1]];
  const view = state.viewport!;
  await page.mouse.click(box.x + ((a.x + b.x) / 2) * view.scale + view.offsetX, box.y + ((a.y + b.y) / 2) * view.scale + view.offsetY);
  // The line is selected: a selection that is no node's id.
  await expect
    .poll(async () => {
      const { selected, diagram } = await hook(page);
      return selected !== null && !diagram!.diagram.nodes.some((node) => node.id === selected);
    })
    .toBe(true);
  await page.keyboard.press('Shift+F10');
  await expect(page.getByRole('menu', { name: 'Line' })).toBeVisible();
});

test('Ctrl+Z held down undoes every add and ends on Undone, not on a refusal', async ({ page }) => {
  await opened(page);
  for (let added = 1; added <= 3; added += 1) {
    const menu = await menuOf(page, 'cl_top');
    await menu.getByRole('menuitem', { name: 'Add Goal' }).click();
    await expect.poll(async () => (await hook(page)).revision).toBe(1 + added);
  }
  await canvas(page).focus();
  // Repeats of a key held down: more than there are adds to undo.
  await page.keyboard.down('Control');
  for (let repeat = 0; repeat < 8; repeat += 1) await page.keyboard.down('z');
  await page.keyboard.up('z');
  await page.keyboard.up('Control');
  await expect.poll(() => counts(page)).toEqual({ nodes: 8, edges: 7 });
  await expect(await editMenu(page, 'Undo')).toHaveAttribute('aria-disabled', 'true');
  await page.keyboard.press('Escape');
  await expect(status(page)).toHaveText('Undone.');
});

test('on the empty canvas only a top-level Goal can be added, and a line has no menu', async ({ page }) => {
  await opened(page);
  const box = (await canvas(page).boundingBox())!;
  await page.mouse.click(box.x + 12, box.y + box.height - 12, { button: 'right' });
  const menu = page.getByRole('menu');
  await expect(menu).toHaveAccessibleName('Add');
  await expect(menu.getByRole('menuitem')).toHaveText([/^Add Goal/]);
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
  await expect(menu.getByRole('menuitem')).toHaveText([/^ゴールで異議を唱える/, /^ソリューションで異議を唱える/, /^移動…/, /^削除/]);
  await page.keyboard.press('Escape');
  await (await menuOf(page, 'cl_top')).getByRole('menuitem', { name: '仮定を追加' }).click();
  await expect(status(page)).toHaveText('仮定 A1 を追加しました。');
  await page.keyboard.press('Control+z');
  await expect(status(page)).toHaveText('元に戻しました。');
});

test('Element Properties changes an element’s ID, name and description; each is one undo step, and a refused ID changes nothing', async ({ page }) => {
  await opened(page);
  const point = await nodeCentre(page, 'cl_top');
  await page.mouse.click(point.x, point.y);
  const id = page.getByTestId('element-identifier');
  const name = page.getByTestId('element-name');
  const statement = page.getByTestId('element-statement');
  await expect(id).toHaveValue('cl_top');
  const nameBefore = await name.inputValue();
  const text = async () => (await hook(page)).diagram!.diagram.nodes.find((node) => node.id === 'cl_top')!.text.map((line) => line.text).join(' ');

  // Enter applies a single line: the diagram and the tree show it.
  await name.fill('Braking is safe');
  await name.press('Enter');
  await expect(status(page)).toHaveText('Name changed.');
  expect((await hook(page)).revision).toBe(2);
  expect(await text()).toContain('Braking is safe');
  await expect(page.getByRole('tree', { name: 'Argument Navigator' }).getByRole('treeitem', { name: /Braking is safe/ })).toBeVisible();

  // Enter in the description is a new line; leaving the field applies it.
  await statement.fill('The braking function');
  await statement.press('Enter');
  await statement.pressSequentially('is acceptably safe.');
  await id.focus();
  await expect(status(page)).toHaveText('Description changed.');
  await expect(statement).toHaveValue('The braking function\nis acceptably safe.');
  expect((await hook(page)).revision).toBe(3);

  await id.fill('G1');
  await id.press('Enter');
  await expect(status(page)).toHaveText('ID changed.');
  expect(await text()).toContain('G1');
  // The element is the same one: the selection is by its SACM id, which stays.
  expect((await hook(page)).selected).toBe('cl_top');

  // An empty ID, and another element's, are refused: the field shows the ID again.
  await id.fill('');
  await id.press('Enter');
  await expect(status(page)).toHaveText('Nothing changed. An ID cannot be empty.');
  await expect(id).toHaveValue('G1');
  await id.fill('cl_sub_2');
  await id.press('Enter');
  await expect(status(page)).toHaveText('Nothing changed. Another element of the file has that ID.');
  await expect(id).toHaveValue('G1');
  // Escape puts back what was there, and the same text again is no change.
  await name.fill('Not kept');
  await name.press('Escape');
  await expect(name).toHaveValue('Braking is safe');
  await name.focus();
  await name.press('Enter');
  expect((await hook(page)).revision).toBe(4);

  // Each change is one undo step.
  await (await editMenu(page, 'Undo')).click();
  await expect(id).toHaveValue('cl_top');
  await (await editMenu(page, 'Undo')).click();
  await (await editMenu(page, 'Undo')).click();
  await expect(name).toHaveValue(nameBefore);
  await expect.poll(async () => (await hook(page)).revision).toBe(7);
  await page.getByRole('menuitem', { name: 'Edit', exact: true }).click();
  await expect(page.getByRole('menu', { name: 'Edit' }).getByRole('menuitem', { name: 'Undo' })).toHaveAttribute('aria-disabled', 'true');
});

test('a field left without typing changes nothing, even when the text has space around it; typing after Enter is kept', async ({ page }) => {
  await page.goto('/');
  // A goal whose name the file holds with a space after it.
  await page.getByTestId('open-file').setInputFiles({
    name: 'spaced.sacm.xmi',
    mimeType: 'application/xml',
    buffer: Buffer.from(`<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:id="case">
  <name content="Case"/>
  <argumentPackage xmi:id="p"><name content="P"/>
    <argumentElement xmi:type="sacm:Claim" xmi:id="g1"><name content="Safe "/></argumentElement>
  </argumentPackage>
</sacm:AssuranceCasePackage>`),
  });
  await expect.poll(async () => (await hook(page))?.revision).toBe(1);
  const point = await nodeCentre(page, 'g1');
  await page.mouse.click(point.x, point.y);
  const id = page.getByTestId('element-identifier');
  const name = page.getByTestId('element-name');
  await expect(name).toHaveValue('Safe ');

  // Through the fields and out, typing nothing: no change, no undo step.
  await id.focus();
  await page.keyboard.press('Tab');
  await page.keyboard.press('Tab');
  await page.keyboard.press('Tab');
  await name.focus();
  await name.press('Enter');
  expect((await hook(page)).revision).toBe(1);
  await expect(name).toHaveValue('Safe ');

  // Enter applies, and what is typed straight after it stays in the field.
  await name.fill('Braking');
  await name.press('Enter');
  await name.pressSequentially(' is safe');
  await expect.poll(async () => (await hook(page)).revision).toBe(2);
  await expect(name).toHaveValue('Braking is safe');
  await name.press('Enter');
  await expect.poll(async () => (await hook(page)).revision).toBe(3);
  await expect(name).toHaveValue('Braking is safe');
});
