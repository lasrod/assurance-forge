import { expect, test, type Page } from '@playwright/test';

async function opened(page: Page, query = '') {
  await page.goto(`/?sample=sacm_example_core_argument.sacm.xmi${query}`);
  await expect.poll(() => page.evaluate(() => window.__assuranceForge?.drawn)).toEqual({ nodes: 8, edges: 7 });
}

/** The canvas fills the centre surface exactly. */
async function expectCanvasFillsCentre(page: Page) {
  const centre = await page.getByRole('region', { name: 'Diagram' }).boundingBox();
  const canvas = await page.getByRole('img', { name: 'GSN diagram' }).boundingBox();
  expect(canvas).not.toBeNull();
  expect(canvas).toEqual(centre);
  return canvas!;
}

const docks = [
  { name: 'Outline', reopen: { role: 'button' as const, name: 'Outline', exact: true }, grows: 'width' as const },
  { name: 'Details', reopen: { role: 'button' as const, name: 'Details', exact: true }, grows: 'width' as const },
  { name: 'Problems', reopen: { role: 'button' as const, name: 'Show problems', exact: true }, grows: 'height' as const },
];

for (const dock of docks) {
  test(`the ${dock.name} dock closes and reopens, and the canvas fills the centre`, async ({ page }) => {
    await opened(page);
    const region = page.getByRole('region', { name: dock.name });
    await expect(region).toBeVisible();
    const before = await expectCanvasFillsCentre(page);

    await page.getByRole('button', { name: `Hide ${dock.name.toLowerCase()}` }).click();
    await expect(region).toBeHidden();
    await expect.poll(async () => (await page.getByRole('img', { name: 'GSN diagram' }).boundingBox())?.[dock.grows]).toBeGreaterThan(before[dock.grows]);
    await expectCanvasFillsCentre(page);
    // The diagram was drawn again at the new size.
    expect(await page.evaluate(() => window.__assuranceForge?.drawn)).toEqual({ nodes: 8, edges: 7 });

    await page.getByRole(dock.reopen.role, { name: dock.reopen.name, exact: dock.reopen.exact }).click();
    await expect(region).toBeVisible();
    await expect.poll(async () => (await page.getByRole('img', { name: 'GSN diagram' }).boundingBox())?.[dock.grows]).toBeCloseTo(before[dock.grows], 0);
    await expectCanvasFillsCentre(page);
  });
}

test('the docks are titled in Japanese, and the language can be switched', async ({ page }) => {
  await opened(page, '&lang=ja');
  for (const title of ['アウトライン', '詳細', '問題']) {
    await expect(page.getByRole('heading', { name: title, exact: true })).toBeVisible();
  }
  await expect(page.getByTestId('status')).toContainText('ノード 8 個、エッジ 7 本');

  await page.getByRole('radio', { name: 'EN' }).check();
  for (const title of ['Outline', 'Details', 'Problems']) {
    await expect(page.getByRole('heading', { name: title, exact: true })).toBeVisible();
  }
  await expect(page.getByTestId('status')).toContainText('8 nodes, 7 edges');
  await expect.poll(() => page.evaluate(() => document.documentElement.lang)).toBe('en');
});
