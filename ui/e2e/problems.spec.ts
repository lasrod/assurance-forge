// The problem list (milestone 1, step 2): the Problems panel lists what is
// wrong with the case, the status bar counts it, and choosing a problem
// shows its element. The list follows each edit.

import { expect, test, type Page } from '@playwright/test';

const SAMPLE = 'sacm_example_core_argument.sacm.xmi';

const hook = (page: Page) => page.evaluate(() => window.__assuranceForge!);
const counts = (page: Page) => page.getByTestId('problem-counts');
const list = (page: Page, name = 'Problems') => page.getByRole('listbox', { name });

async function opened(page: Page, sample = SAMPLE, query = '') {
  await page.goto(`/?sample=${sample}${query}`);
  await expect.poll(async () => (await hook(page))?.revision).toBeGreaterThanOrEqual(1);
}

test('the status bar counts the problems and opens the panel, which lists them errors first', async ({ page }) => {
  await opened(page);
  // The sample gives an Assumption as an inference's premise (an error) and
  // supports a Goal marked undeveloped (a warning).
  await expect(counts(page)).toHaveText('1 errors, 1 warnings');
  await expect(list(page)).toBeHidden();
  await counts(page).getByRole('button').click();
  await expect(list(page)).toBeVisible();
  await expect(page.getByRole('heading', { name: 'Problems (2)', exact: true })).toBeVisible();
  const rows = list(page).getByRole('option');
  await expect(rows).toHaveCount(2);
  await expect(rows.nth(0)).toContainText('is supported by Assumption cl_assumption_1');
  await expect(rows.nth(0)).toContainText('AF-GSN-004 · GSN3-CORE-015');
  await expect(rows.nth(0).getByRole('img', { name: 'Error' })).toBeVisible();
  await expect(rows.nth(1)).toContainText('Goal cl_sub_2 is marked undeveloped but is supported');
  await expect(rows.nth(1).getByRole('img', { name: 'Warning' })).toBeVisible();
});

test('choosing a problem selects its element, and the keyboard moves through the list', async ({ page }) => {
  await opened(page);
  await counts(page).getByRole('button').click();
  const rows = list(page).getByRole('option');
  await rows.nth(1).click();
  await expect.poll(async () => (await hook(page)).selected).toBe('cl_sub_2');
  await page.keyboard.press('ArrowUp');
  await expect.poll(async () => (await hook(page)).selected).toBe('cl_sub_1');
});

test('the list follows an edit and its undo', async ({ page }) => {
  await opened(page);
  await counts(page).getByRole('button').click();
  // A Strategy added alone supports nothing yet: a third problem.
  const box = (await page.locator('canvas.af-canvas').boundingBox())!;
  const state = await hook(page);
  const bounds = state.diagram!.diagram.nodes.find((node) => node.id === 'cl_top')!.bounds;
  const view = state.viewport!;
  await page.mouse.click(
    box.x + (bounds.x + bounds.width / 2) * view.scale + view.offsetX,
    box.y + (bounds.y + bounds.height / 2) * view.scale + view.offsetY,
    { button: 'right' },
  );
  await page.getByRole('menu').getByRole('menuitem', { name: 'Add Strategy' }).click();
  await expect(counts(page)).toHaveText('1 errors, 2 warnings');
  await expect(list(page).getByRole('option')).toHaveCount(3);
  await expect(list(page).getByRole('option').nth(2)).toContainText('supports nothing: a Strategy needs a Goal under it.');
  await expect(list(page).getByRole('option').nth(2)).toContainText('AF-GSN-008');

  await page.keyboard.press('Control+z');
  await expect(counts(page)).toHaveText('1 errors, 1 warnings');
  await expect(list(page).getByRole('option')).toHaveCount(2);
});

test('a case with no problems says so', async ({ page }) => {
  await opened(page, 'gallery-shared-support.sacm.xmi');
  await expect(counts(page)).toHaveText('0 errors, 0 warnings');
  await counts(page).getByRole('button').click();
  await expect(page.getByText('No problems found.')).toBeVisible();
});

test('the problems are in Japanese', async ({ page }) => {
  await opened(page, SAMPLE, '&lang=ja');
  await expect(counts(page)).toHaveText('エラー 1 件、警告 1 件');
  await counts(page).getByRole('button').click();
  const rows = list(page, '問題').getByRole('option');
  await expect(rows.nth(0)).toContainText('前提 cl_assumption_1 に支持されています');
  await expect(rows.nth(1).getByRole('img', { name: '警告' })).toBeVisible();
});
