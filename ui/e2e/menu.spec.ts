import { expect, test, type Page } from '@playwright/test';

async function opened(page: Page) {
  await page.goto('/?sample=sacm_example_core_argument.sacm.xmi');
  await expect.poll(() => page.evaluate(() => window.__assuranceForge?.revision)).toBe(1);
}

async function viewMenu(page: Page) {
  await page.getByRole('button', { name: 'View', exact: true }).click();
  await expect(page.getByRole('menu', { name: 'View' })).toBeVisible();
}

/** A checkbox item keeps its menu open, as a menu of options should; Escape closes it. */
async function closeMenu(page: Page) {
  await page.keyboard.press('Escape');
  await expect(page.getByRole('menu')).toBeHidden();
}

test('File ▸ Open… opens the file chooser, and the file opens', async ({ page }) => {
  await opened(page);
  await page.getByRole('button', { name: 'File', exact: true }).click();
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

test('View toggles each panel', async ({ page }) => {
  await opened(page);
  const properties = page.getByRole('region', { name: 'Element Properties' });
  const problems = page.getByRole('region', { name: 'Problems' });
  const explorer = page.getByRole('tablist', { name: 'Case Explorer and Argument Navigator' });

  for (const [item, panel] of [
    ['Element Properties', properties],
    ['Problems', problems],
    ['Case Explorer', explorer],
  ] as const) {
    await viewMenu(page);
    await expect(page.getByRole('menuitemcheckbox', { name: item })).toBeChecked();
    await page.getByRole('menuitemcheckbox', { name: item }).click();
    await closeMenu(page);
    await expect(panel).toBeHidden();
    await viewMenu(page);
    await expect(page.getByRole('menuitemcheckbox', { name: item })).not.toBeChecked();
    await page.getByRole('menuitemcheckbox', { name: item }).click();
    await closeMenu(page);
    await expect(panel).toBeVisible();
  }

  // The Argument Navigator shares the start dock: both are checked while it is
  // open, unchecking it closes the dock, and checking it opens it on its tab.
  await viewMenu(page);
  await expect(page.getByRole('menuitemcheckbox', { name: 'Argument Navigator' })).toBeChecked();
  await page.getByRole('menuitemcheckbox', { name: 'Argument Navigator' }).click();
  await closeMenu(page);
  await expect(explorer).toBeHidden();
  await viewMenu(page);
  await page.getByRole('menuitemcheckbox', { name: 'Argument Navigator' }).click();
  await closeMenu(page);
  await expect(page.getByRole('tab', { name: 'Argument Navigator' })).toHaveAttribute('aria-selected', 'true');
});

test('at the medium width the side panels share one dock, and View follows it', async ({ page }) => {
  // 56rem: DATUM's medium tier puts the side docks' views under the end dock.
  await page.setViewportSize({ width: 900, height: 800 });
  await opened(page);
  await viewMenu(page);
  await page.getByRole('menuitemcheckbox', { name: 'Element Properties' }).click();
  await closeMenu(page);
  await expect(page.getByRole('tab', { name: 'Case Explorer' })).toBeHidden();
  await viewMenu(page);
  for (const item of ['Case Explorer', 'Argument Navigator', 'Element Properties']) {
    await expect(page.getByRole('menuitemcheckbox', { name: item })).not.toBeChecked();
  }
  await expect(page.getByRole('menuitemcheckbox', { name: 'Problems' })).toBeChecked();
});

test('View ▸ Appearance switches the theme and the language', async ({ page }) => {
  await opened(page);
  await viewMenu(page);
  await page.getByRole('menuitemradio', { name: 'Dark' }).click();
  await expect.poll(() => page.locator('[data-datum-theme]').first().getAttribute('data-datum-theme')).toBe('dark');
  await viewMenu(page);
  await page.getByRole('menuitemradio', { name: '日本語' }).click();
  await expect(page.getByRole('button', { name: '表示', exact: true })).toBeVisible();
  await expect(page.getByTestId('status')).toContainText('ノード 8 個');
});
