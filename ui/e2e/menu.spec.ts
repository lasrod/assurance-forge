import { expect, test, type Page } from '@playwright/test';

async function opened(page: Page) {
  await page.goto('/?sample=sacm_example_core_argument.sacm.xmi');
  await expect.poll(() => page.evaluate(() => window.__assuranceForge?.revision)).toBe(1);
}

async function viewMenu(page: Page) {
  await page.getByRole('menuitem', { name: 'View', exact: true }).click();
  await expect(page.getByRole('menu', { name: 'View' })).toBeVisible();
}

test('the menu bar is a menu bar: File and View, moved between by the arrow keys', async ({ page }) => {
  await opened(page);
  const bar = page.getByRole('menubar', { name: 'Menu' });
  await expect(bar.getByRole('menuitem')).toHaveText(['File', 'View']);
  await bar.getByRole('menuitem', { name: 'File' }).focus();
  await page.keyboard.press('ArrowRight');
  await expect(bar.getByRole('menuitem', { name: 'View' })).toBeFocused();
});

test('File ▸ Open… opens the file chooser, and the file opens', async ({ page }) => {
  await opened(page);
  await page.getByRole('menuitem', { name: 'File', exact: true }).click();
  const chooser = page.waitForEvent('filechooser');
  // The name includes the shortcut text, which DATUM's MenuItem renders inside the item.
  await page.getByRole('menuitem', { name: /^Open…/ }).click();
  await (await chooser).setFiles('../testdata/sacm/safety-case-sanitized-strict.sacm.xmi');
  await expect(page.getByTestId('status')).toContainText('271 nodes');
});

test('Ctrl+O opens the file chooser', async ({ page }) => {
  await opened(page);
  const chooser = page.waitForEvent('filechooser');
  await page.keyboard.press('ControlOrMeta+o');
  expect(await chooser).toBeTruthy();
});

test("View holds Appearance, as the C++ application’s did, and no panel toggles", async ({ page }) => {
  await opened(page);
  await viewMenu(page);
  await expect(page.getByRole('menu', { name: 'View' }).getByRole('menuitem')).toHaveText(['Appearance']);
});

test('View ▸ Appearance ▸ Theme and Language switch them', async ({ page }) => {
  await opened(page);
  await viewMenu(page);
  await page.getByRole('menuitem', { name: 'Appearance' }).click();
  await page.getByRole('menuitem', { name: 'Theme' }).click();
  await page.getByRole('menuitemradio', { name: 'Dark' }).click();
  await expect.poll(() => page.locator('[data-datum-theme]').first().getAttribute('data-datum-theme')).toBe('dark');
  await viewMenu(page);
  await page.getByRole('menuitem', { name: 'Appearance' }).click();
  await page.getByRole('menuitem', { name: 'Language' }).click();
  await page.getByRole('menuitemradio', { name: '日本語' }).click();
  await expect(page.getByRole('menuitem', { name: '表示', exact: true })).toBeVisible();
  await expect(page.getByTestId('status')).toContainText('ノード 8 個');
});
