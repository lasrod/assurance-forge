import { expect, test, type Page } from '@playwright/test';

const SAMPLE = 'sacm_example_core_argument.sacm.xmi';

const hook = (page: Page) => page.evaluate(() => window.__assuranceForge);
const welcome = (page: Page) => page.getByTestId('welcome');
const recent = (page: Page) => page.getByRole('listbox', { name: 'Recent files' });

async function fileMenu(page: Page, item: string | RegExp) {
  await page.getByRole('menuitem', { name: 'File', exact: true }).click();
  await page.getByRole('menuitem', { name: item }).click();
}

test('with nothing open the window shows the welcome page, and no empty panels', async ({ page }) => {
  await page.goto('/');
  await expect(welcome(page)).toBeVisible();
  await expect(page.getByRole('heading', { name: 'Assurance Forge' })).toBeVisible();
  await expect(page.getByRole('button', { name: 'Open a file…' })).toBeVisible();
  await expect(welcome(page)).toContainText('No recent files');
  await expect(page.getByRole('region', { name: 'Diagram' })).toHaveCount(0);
  await expect(page.getByRole('region', { name: 'Case Explorer' })).toHaveCount(0);
});

test('the welcome page opens a file from the chooser', async ({ page }) => {
  await page.goto('/');
  const chooser = page.waitForEvent('filechooser');
  await page.getByRole('button', { name: 'Open a file…' }).click();
  await (await chooser).setFiles(`../testdata/sacm/${SAMPLE}`);
  await expect.poll(async () => (await hook(page))?.drawn).toEqual({ nodes: 8, edges: 7 });
  await expect(welcome(page)).toBeHidden();
  await expect(page).toHaveTitle(`${SAMPLE} – Assurance Forge`);
});

test('the example opens, File ▸ Close returns to the welcome page, and the file is recent', async ({ page }) => {
  await page.goto('/');
  await page.getByRole('button', { name: 'Open the example' }).click();
  await expect.poll(async () => (await hook(page))?.drawn).toEqual({ nodes: 8, edges: 7 });

  await fileMenu(page, 'Close');
  await expect(welcome(page)).toBeVisible();
  await expect.poll(async () => (await hook(page))?.drawn).toEqual({ nodes: 0, edges: 0 });
  await expect(page).toHaveTitle('Assurance Forge');
  await expect(recent(page).getByRole('option')).toHaveCount(1);
  await expect(recent(page).getByRole('option')).toContainText(SAMPLE);
  await expect(recent(page).getByRole('option')).toContainText('8 nodes, 7 edges');
  // The browser keeps a copy, and says so.
  await expect(welcome(page)).toContainText('This browser keeps a copy');
});

test('a recent file opens again after the page is loaded again, most recent first', async ({ page }) => {
  await page.goto('/');
  await page.getByTestId('open-file').setInputFiles('../testdata/sacm/sample.sacm.xmi');
  await expect.poll(async () => (await hook(page))?.revision).toBe(1);
  await page.getByTestId('open-file').setInputFiles(`../testdata/sacm/${SAMPLE}`);
  await expect.poll(async () => (await hook(page))?.drawn).toEqual({ nodes: 8, edges: 7 });

  await page.goto('/');
  await expect(recent(page).getByRole('option')).toHaveCount(2);
  await expect(recent(page).getByRole('option').first()).toContainText(SAMPLE);
  await recent(page).getByRole('option', { name: /sample\.sacm\.xmi/ }).last().click();
  await expect(welcome(page)).toBeHidden();
  await expect(page).toHaveTitle('sample.sacm.xmi – Assurance Forge');

  // File ▸ Open Recent lists them too, the one just opened first.
  await page.getByRole('menuitem', { name: 'File', exact: true }).click();
  await page.getByRole('menuitem', { name: 'Open Recent' }).click();
  await expect(page.getByRole('menu', { name: 'Open Recent' }).getByRole('menuitem')).toHaveText(['sample.sacm.xmi', SAMPLE]);
  await page.getByRole('menuitem', { name: SAMPLE }).click();
  await expect.poll(async () => (await hook(page))?.drawn).toEqual({ nodes: 8, edges: 7 });
});

test('Clear recent files empties the list, and it stays empty', async ({ page }) => {
  await page.goto('/');
  await page.getByRole('button', { name: 'Open the example' }).click();
  await expect.poll(async () => (await hook(page))?.drawn).toEqual({ nodes: 8, edges: 7 });
  await fileMenu(page, 'Close');
  await page.getByRole('button', { name: 'Clear recent files' }).click();
  await expect(welcome(page)).toContainText('No recent files');
  await page.goto('/');
  await expect(welcome(page)).toContainText('No recent files');
});

test('a refused file is not a recent file, and the welcome page shows why it did not open', async ({ page }) => {
  await page.goto('/');
  await expect(welcome(page)).toBeVisible();
  await page.getByTestId('open-file').setInputFiles({
    name: 'broken.sacm.xmi',
    mimeType: 'application/xml',
    buffer: Buffer.from('<?xml version="1.0"?>\n<sacm:AssuranceCasePackage\n  oops'),
  });
  await expect(page.getByTestId('status')).toContainText('SACM-XML-001');
  await expect(welcome(page)).toBeVisible();
  await expect(welcome(page)).toContainText('No recent files');
});

test('the welcome page follows the language', async ({ page }) => {
  await page.goto('/?lang=ja');
  await expect(page.getByRole('button', { name: 'ファイルを開く…' })).toBeVisible();
  await expect(welcome(page)).toContainText('最近使ったファイルはありません');
});
