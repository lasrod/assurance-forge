// One diagram per argument package (milestone 2, step 7, A5 and A6): the
// Case Explorer's file row shows the whole case and a package's row that
// package; a node of another package is marked with its name, which opens it.

import { expect, test, type Page } from '@playwright/test';

const SAMPLE = 'oasc-ja.sacm.xmi';

const hook = (page: Page) => page.evaluate(() => window.__assuranceForge!);
const canvas = (page: Page) => page.locator('canvas.af-canvas');
const explorer = (page: Page) => page.getByRole('tree', { name: 'Case Explorer' });
const navigator = (page: Page) => page.getByRole('tree', { name: 'Argument Navigator' });

async function opened(page: Page) {
  await page.goto(`/?sample=${SAMPLE}`);
  await expect.poll(async () => (await hook(page))?.revision).toBe(1);
  await expect.poll(async () => (await hook(page)).viewport?.scale ?? null).not.toBeNull();
}

const shown = async (page: Page) => (await hook(page)).diagram?.package ?? null;

/** The middle of the first line of `id`'s package mark, on the page. */
async function onMark(page: Page, id: string) {
  const box = (await canvas(page).boundingBox())!;
  const state = await hook(page);
  const node = state.diagram!.diagram.nodes.find((candidate) => candidate.id === id)!;
  const line = node.text.find((candidate) => candidate.role === 'package')!;
  const view = state.viewport!;
  return {
    x: box.x + (line.x + line.width / 2) * view.scale + view.offsetX,
    y: box.y + (line.baseline - line.size / 3) * view.scale + view.offsetY,
  };
}

test('a package row shows that package, and the file row the whole case', async ({ page }) => {
  await opened(page);
  expect(await shown(page)).toBeNull();
  const whole = (await hook(page)).diagram!.diagram.nodes.length;

  await explorer(page).getByRole('treeitem', { name: /Vehicle Safety/ }).click();
  await expect.poll(() => shown(page)).toBe('ARG_VehicleSafety');
  const diagram = (await hook(page)).diagram!.diagram;
  expect(diagram.nodes.length).toBeLessThan(whole);
  // G4 of the main argument is drawn, with its package's name.
  const g4 = diagram.nodes.find((node) => node.id === 'G4')!;
  expect(g4.package).toBe('ARG_Main');
  expect(g4.text.filter((line) => line.role === 'package').map((line) => line.text.trim()).join(' ')).toBe(
    '[Open Autonomy Safety Case - Main Argument]',
  );

  await explorer(page).getByRole('treeitem', { name: /oasc-ja/ }).click();
  await expect.poll(() => shown(page)).toBeNull();
  expect((await hook(page)).diagram!.diagram.nodes.length).toBe(whole);
});

test("a click on another package's name opens it, and the Case Explorer follows", async ({ page }) => {
  await opened(page);
  await explorer(page).getByRole('treeitem', { name: /Vehicle Safety/ }).click();
  await expect.poll(() => shown(page)).toBe('ARG_VehicleSafety');
  await expect.poll(async () => (await hook(page)).viewport?.scale ?? null).not.toBeNull();
  const at = await onMark(page, 'G4');
  await page.mouse.click(at.x, at.y);
  await expect.poll(() => shown(page)).toBe('ARG_Main');
  await expect(explorer(page).getByRole('treeitem', { name: /Main Argument/ })).toHaveAttribute('aria-selected', 'true');
});

test('a node the package does not draw, chosen in the Argument Navigator, is shown in the whole case', async ({ page }) => {
  await opened(page);
  await explorer(page).getByRole('treeitem', { name: /Vehicle Safety/ }).click();
  await expect.poll(() => shown(page)).toBe('ARG_VehicleSafety');
  const drawn = new Set((await hook(page)).diagram!.diagram.nodes.map((node) => node.id));
  // G1, the main argument's top goal, is not drawn in Vehicle Safety.
  expect(drawn.has('G1')).toBe(false);
  await navigator(page).getByRole('treeitem').first().click();
  await expect.poll(() => shown(page)).toBeNull();
  await expect.poll(async () => (await hook(page)).selected).toBe('G1');
});

test("an element added under another package's node is shown in that package", async ({ page }) => {
  await opened(page);
  await explorer(page).getByRole('treeitem', { name: /Vehicle Safety/ }).click();
  await expect.poll(() => shown(page)).toBe('ARG_VehicleSafety');
  await expect.poll(async () => (await hook(page)).viewport?.scale ?? null).not.toBeNull();
  // G4 is of the main argument: what is added under it goes there.
  const box = (await canvas(page).boundingBox())!;
  const state = await hook(page);
  const g4 = state.diagram!.diagram.nodes.find((node) => node.id === 'G4')!;
  const view = state.viewport!;
  await page.mouse.click(box.x + (g4.bounds.x + g4.bounds.width / 2) * view.scale + view.offsetX, box.y + (g4.bounds.y + 8) * view.scale + view.offsetY, {
    button: 'right',
  });
  await page.getByRole('menu').getByRole('menuitem', { name: 'Add Solution' }).click();
  await expect.poll(async () => (await hook(page)).revision).toBe(2);
  await expect.poll(() => shown(page)).toBe('ARG_Main');
  const selected = (await hook(page)).selected;
  expect((await hook(page)).diagram!.diagram.nodes.some((node) => node.id === selected)).toBe(true);
});
