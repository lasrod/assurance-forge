import { expect, test, type Locator, type Page } from '@playwright/test';

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

// The C++ application's arrangement (plan decision D9): the Case Explorer and
// the Argument Navigator in the start dock, Element Properties at the end,
// Problems at the bottom.
const docks: Array<{
  name: string;
  shown: (page: Page) => Locator;
  hide: string;
  reopen: string;
  grows: 'width' | 'height';
}> = [
  {
    // Named after the view it shows, as the C++ panels were.
    name: 'Case Explorer',
    shown: (page) => page.getByRole('tablist', { name: 'Case Explorer' }),
    hide: 'Hide case explorer',
    reopen: 'Case Explorer',
    grows: 'width',
  },
  {
    name: 'Element Properties',
    shown: (page) => page.getByRole('region', { name: 'Element Properties' }),
    hide: 'Hide element properties',
    reopen: 'Element Properties',
    grows: 'width',
  },
  {
    name: 'Problems',
    shown: (page) => page.getByRole('region', { name: 'Problems' }),
    hide: 'Hide problems',
    reopen: 'Show problems',
    grows: 'height',
  },
];

for (const dock of docks) {
  test(`the ${dock.name} dock closes and reopens, and the canvas fills the centre`, async ({ page }) => {
    await opened(page);
    await expect(dock.shown(page)).toBeVisible();
    const before = await expectCanvasFillsCentre(page);

    await page.getByRole('button', { name: dock.hide, exact: true }).click();
    await expect(dock.shown(page)).toBeHidden();
    await expect.poll(async () => (await page.getByRole('img', { name: 'GSN diagram' }).boundingBox())?.[dock.grows]).toBeGreaterThan(before[dock.grows]);
    await expectCanvasFillsCentre(page);
    // The diagram was drawn again at the new size.
    expect(await page.evaluate(() => window.__assuranceForge?.drawn)).toEqual({ nodes: 8, edges: 7 });

    await page.getByRole('button', { name: dock.reopen, exact: true }).click();
    await expect(dock.shown(page)).toBeVisible();
    await expect.poll(async () => (await page.getByRole('img', { name: 'GSN diagram' }).boundingBox())?.[dock.grows]).toBeCloseTo(before[dock.grows], 0);
    await expectCanvasFillsCentre(page);
  });
}

test('the panels carry the C++ application\'s names, in Japanese too', async ({ page }) => {
  await opened(page, '&lang=ja');
  await expect(page.getByRole('tab', { name: 'ケースエクスプローラー' })).toBeVisible();
  await expect(page.getByRole('tab', { name: '論証ナビゲーター' })).toBeVisible();
  for (const title of ['要素プロパティ', '問題']) {
    await expect(page.getByRole('heading', { name: title, exact: true })).toBeVisible();
  }
  await expect(page.getByRole('button', { name: 'ファイル', exact: true })).toBeVisible();
  await expect(page.getByTestId('status')).toContainText('ノード 8 個、エッジ 7 本');

  await page.getByRole('button', { name: '表示', exact: true }).click();
  await page.getByRole('menuitemradio', { name: 'English' }).click();
  await expect(page.getByRole('tab', { name: 'Case Explorer' })).toBeVisible();
  await expect(page.getByRole('tab', { name: 'Argument Navigator' })).toBeVisible();
  for (const title of ['Element Properties', 'Problems']) {
    await expect(page.getByRole('heading', { name: title, exact: true })).toBeVisible();
  }
  await expect(page.getByTestId('status')).toContainText('8 nodes, 7 edges');
  await expect.poll(() => page.evaluate(() => document.documentElement.lang)).toBe('en');
});
