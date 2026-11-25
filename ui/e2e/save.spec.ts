// Saving (milestone 2, step 2): File ▸ Save downloads the case in the
// browser, the title and the status bar say when changes are not saved, and
// nothing that would lose them does so without asking.

import { readFileSync } from 'node:fs';
import { expect, test, type Page } from '@playwright/test';

const SAMPLE = 'sacm_example_core_argument.sacm.xmi';

const hook = (page: Page) => page.evaluate(() => window.__assuranceForge!);
const canvas = (page: Page) => page.locator('canvas.af-canvas');
const status = (page: Page) => page.getByTestId('status');
const modified = (page: Page) => page.getByTestId('modified');

async function opened(page: Page, query = '') {
  await page.goto(`/?sample=${SAMPLE}${query}`);
  await expect.poll(async () => (await hook(page))?.revision).toBe(1);
}

/** Adds a Goal under the top claim from its menu: a change to save. */
async function change(page: Page) {
  const box = (await canvas(page).boundingBox())!;
  const state = await hook(page);
  const bounds = state.diagram!.diagram.nodes.find((node) => node.id === 'cl_top')!.bounds;
  const view = state.viewport!;
  await page.mouse.click(
    box.x + (bounds.x + bounds.width / 2) * view.scale + view.offsetX,
    box.y + (bounds.y + bounds.height / 2) * view.scale + view.offsetY,
    { button: 'right' },
  );
  await page.getByRole('menu').getByRole('menuitem', { name: 'Add Goal' }).click();
  await expect(status(page)).toHaveText('Goal G1 added.');
}

async function fileMenu(page: Page) {
  await page.getByRole('menuitem', { name: 'File', exact: true }).click();
  return page.getByRole('menu', { name: 'File' });
}

/** What the next download holds, and its name, once `act` has started it. */
async function downloaded(page: Page, act: () => Promise<void>) {
  const [download] = await Promise.all([page.waitForEvent('download'), act()]);
  return { name: download.suggestedFilename(), text: readFileSync((await download.path())!, 'utf8') };
}

test('a case opened is saved; a change marks the title and the status bar, and File ▸ Save downloads it and clears the mark', async ({ page }) => {
  await opened(page);
  await expect(modified(page)).toBeHidden();
  await expect(page).toHaveTitle(`${SAMPLE} – Assurance Forge`);
  // The browser downloads: it has Save and no Save As….
  const menu = await fileMenu(page);
  await expect(menu.getByRole('menuitem', { name: /^Save/ })).toHaveText([/^Save/]);
  await page.keyboard.press('Escape');

  await change(page);
  await expect(modified(page)).toHaveText('Not saved');
  await expect(page).toHaveTitle(`● ${SAMPLE} – Assurance Forge`);

  const file = await downloaded(page, async () => {
    await (await fileMenu(page)).getByRole('menuitem', { name: 'Save' }).click();
  });
  expect(file.name).toBe(SAMPLE);
  // The goal added is in what was saved, with its identifier.
  expect(file.text).toContain('assuranceForge.gsn.identifier');
  expect(file.text).toContain('G1');
  await expect(status(page)).toHaveText(`Downloaded ${SAMPLE}. The browser keeps it with your downloads.`);
  await expect(modified(page)).toBeHidden();
  await expect(page).toHaveTitle(`${SAMPLE} – Assurance Forge`);

  // Undo leaves the saved state, and Redo comes back to it.
  await page.keyboard.press('Control+z');
  await expect(modified(page)).toBeVisible();
  await page.keyboard.press('Control+y');
  await expect(status(page)).toHaveText('Redone.');
  await expect(modified(page)).toBeHidden();
});

test('what was saved opens again as the case it was', async ({ page }) => {
  await opened(page);
  await change(page);
  const file = await downloaded(page, () => page.keyboard.press('Control+s'));
  // Opened again from the bytes downloaded: the goal added is there.
  await page.getByTestId('open-file').setInputFiles({ name: 'again.sacm.xmi', mimeType: 'application/xml', buffer: Buffer.from(file.text) });
  await expect(page).toHaveTitle('again.sacm.xmi – Assurance Forge');
  await expect(status(page)).toHaveText(/^9 nodes, 8 edges/);
  await expect(modified(page)).toBeHidden();
});

test('Ctrl+S in a field saves what the field holds', async ({ page }) => {
  await opened(page);
  const name = page.getByTestId('element-name');
  // The top claim, selected from the Argument Navigator.
  await page.getByRole('tree', { name: 'Argument Navigator' }).getByRole('treeitem').first().click();
  await expect(page.getByTestId('element-identifier')).toHaveValue('cl_top');
  await name.fill('Saved from the field');
  const file = await downloaded(page, () => name.press('Control+s'));
  expect(file.text).toContain('Saved from the field');
  await expect(modified(page)).toBeHidden();
});

test('closing with changes not saved asks first: Cancel keeps the case, Don’t save closes it, Save saves and closes', async ({ page }) => {
  await opened(page);
  await change(page);
  const close = async () => (await fileMenu(page)).getByRole('menuitem', { name: 'Close' }).click();
  const dialog = page.getByRole('alertdialog');

  await close();
  await expect(dialog).toHaveAccessibleName(`Save the changes to ${SAMPLE}?`);
  // While it asks, Ctrl+S does nothing underneath it.
  await page.keyboard.press('Control+s');
  await dialog.getByRole('button', { name: 'Cancel' }).click();
  await expect(dialog).toBeHidden();
  await expect(canvas(page)).toBeVisible();
  await expect(modified(page)).toBeVisible();

  await close();
  await dialog.getByRole('button', { name: 'Don’t save' }).click();
  await expect(page.getByTestId('welcome')).toBeVisible();

  await opened(page);
  await change(page);
  await close();
  const file = await downloaded(page, () => dialog.getByRole('button', { name: 'Save', exact: true }).click());
  expect(file.name).toBe(SAMPLE);
  await expect(page.getByTestId('welcome')).toBeVisible();
});

test('a case with no changes closes without asking', async ({ page }) => {
  await opened(page);
  await (await fileMenu(page)).getByRole('menuitem', { name: 'Close' }).click();
  await expect(page.getByRole('alertdialog')).toBeHidden();
  await expect(page.getByTestId('welcome')).toBeVisible();
});

test('File ▸ Save and the question are in Japanese', async ({ page }) => {
  await opened(page, '&lang=ja');
  await page.getByRole('menuitem', { name: 'ファイル', exact: true }).click();
  await expect(page.getByRole('menu', { name: 'ファイル' }).getByRole('menuitem', { name: '保存' })).toBeVisible();
  await page.keyboard.press('Escape');
});
