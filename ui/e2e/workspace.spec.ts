import { expect, test, type Page } from '@playwright/test';

async function opened(page: Page, query = '') {
  await page.goto(`/?sample=sacm_example_core_argument.sacm.xmi${query}`);
  await expect.poll(() => page.evaluate(() => window.__assuranceForge?.drawn)).toEqual({ nodes: 8, edges: 7 });
}

const canvas = (page: Page) => page.getByRole('img', { name: 'GSN diagram' });

/** The canvas fills the centre's surface exactly: the centre, under its zoom bar. */
async function expectCanvasFillsCentre(page: Page) {
  const centre = await page.getByRole('region', { name: 'Diagram' }).locator('.datum-workspace-surface').boundingBox();
  const box = await canvas(page).boundingBox();
  expect(box).not.toBeNull();
  expect(box).toEqual(centre);
  return box!;
}

// The C++ application's arrangement (plan decision D9): the Case Explorer
// stacked above the Argument Navigator, Element Properties at the end,
// Problems at the bottom.
for (const dock of [
  { name: 'Element Properties', hide: 'Hide element properties', reopen: 'Element Properties', grows: 'width' as const, startsClosed: false },
  // Problems starts closed while it has no problem list (src/layout.ts).
  { name: 'Problems', hide: 'Hide problems', reopen: 'Show problems', grows: 'height' as const, startsClosed: true },
]) {
  test(`the ${dock.name} dock closes and reopens, and the canvas fills the centre`, async ({ page }) => {
    await opened(page);
    const region = page.getByRole('region', { name: dock.name });
    if (dock.startsClosed) {
      await expect(region).toBeHidden();
      await page.getByRole('button', { name: dock.reopen, exact: true }).click();
    }
    await expect(region).toBeVisible();
    const before = await expectCanvasFillsCentre(page);

    await page.getByRole('button', { name: dock.hide, exact: true }).click();
    await expect(region).toBeHidden();
    await expect.poll(async () => (await canvas(page).boundingBox())?.[dock.grows]).toBeGreaterThan(before[dock.grows]);
    await expectCanvasFillsCentre(page);
    // The diagram was drawn again at the new size.
    expect(await page.evaluate(() => window.__assuranceForge?.drawn)).toEqual({ nodes: 8, edges: 7 });

    await page.getByRole('button', { name: dock.reopen, exact: true }).click();
    await expect(region).toBeVisible();
    await expect.poll(async () => (await canvas(page).boundingBox())?.[dock.grows]).toBeCloseTo(before[dock.grows], 0);
    await expectCanvasFillsCentre(page);
  });
}

test('the Case Explorer is stacked above the Argument Navigator, and each hides on its own', async ({ page }) => {
  await opened(page);
  const explorer = page.getByRole('region', { name: 'Case Explorer' });
  const navigator = page.getByRole('region', { name: 'Argument Navigator' });
  const [above, below] = [await explorer.boundingBox(), await navigator.boundingBox()];
  expect(above && below && above.y + above.height <= below.y && Math.abs(above.x - below.x) < 1).toBe(true);
  const before = await expectCanvasFillsCentre(page);

  // Hiding one: the other takes the dock, and the canvas keeps its size.
  await page.getByRole('button', { name: 'Hide case explorer', exact: true }).click();
  await expect(explorer).toBeHidden();
  await expect(navigator).toBeVisible();
  // Polled a while, so a dock that wrongly closed or resized after the click would show.
  await expect
    .poll(async () => (await canvas(page).boundingBox())?.width, { intervals: [100, 100, 200], timeout: 1000 })
    .toBeCloseTo(before.width, 0);
  await page.waitForTimeout(300);
  expect((await canvas(page).boundingBox())?.width).toBeCloseTo(before.width, 0);

  // Hiding the last one closes the dock, and the canvas grows.
  await page.getByRole('button', { name: 'Hide argument navigator', exact: true }).click();
  await expect(navigator).toBeHidden();
  await expect.poll(async () => (await canvas(page).boundingBox())?.width).toBeGreaterThan(before.width);
  await expectCanvasFillsCentre(page);

  // The rail opens the dock on the view chosen, and it is kept.
  await page.getByRole('button', { name: 'Argument Navigator', exact: true }).click();
  await expect(navigator).toBeVisible();
  await expectCanvasFillsCentre(page);
  await page.reload();
  await expect(page.getByRole('region', { name: 'Argument Navigator' })).toBeVisible();
  await expect(page.getByRole('region', { name: 'Case Explorer' })).toBeHidden();
});

test('at the medium width the side panels are tabs in one dock, and the canvas fills the centre', async ({ page }) => {
  // 56rem: DATUM's medium tier merges the side docks, and a stacked dock's views become tabs.
  await page.setViewportSize({ width: 900, height: 800 });
  await opened(page);
  for (const tab of ['Element Properties', 'Case Explorer', 'Argument Navigator']) {
    await expect(page.getByRole('tab', { name: tab })).toBeVisible();
  }
  await page.getByRole('tab', { name: 'Argument Navigator' }).click();
  await expect(page.getByRole('tab', { name: 'Argument Navigator' })).toHaveAttribute('aria-selected', 'true');
  await expectCanvasFillsCentre(page);
});

test('the Problems panel starts closed with its rail showing, and stays open once opened', async ({ page }) => {
  await opened(page);
  const problems = page.getByRole('region', { name: 'Problems' });
  await expect(problems).toBeHidden();
  await page.getByRole('button', { name: 'Show problems', exact: true }).click();
  await expect(problems).toBeVisible();
  await expectCanvasFillsCentre(page);
  // The layout is kept: the user's choice holds after a reload.
  await opened(page);
  await expect(problems).toBeVisible();
});

test('the panels carry the C++ application\'s names, in Japanese too', async ({ page }) => {
  // Every panel open, Problems too, so each shows its title.
  await page.addInitScript(() =>
    localStorage.setItem('assurance-forge.workspace.v3', JSON.stringify({ open: { start: true, end: true, bottom: true }, size: {} })),
  );
  await opened(page, '&lang=ja');
  for (const title of ['ケースエクスプローラー', '論証ナビゲーター', '要素プロパティ', '問題']) {
    await expect(page.getByRole('heading', { name: title, exact: true })).toBeVisible();
  }
  await expect(page.getByRole('menuitem', { name: 'ファイル', exact: true })).toBeVisible();
  await expect(page.getByTestId('status')).toContainText('ノード 8 個、エッジ 7 本');

  await page.getByRole('menuitem', { name: '表示', exact: true }).click();
  await page.getByRole('menuitem', { name: '外観' }).click();
  await page.getByRole('menuitem', { name: '言語' }).click();
  await page.getByRole('menuitemradio', { name: 'English' }).click();
  for (const title of ['Case Explorer', 'Argument Navigator', 'Element Properties', 'Problems']) {
    await expect(page.getByRole('heading', { name: title, exact: true })).toBeVisible();
  }
  await expect(page.getByTestId('status')).toContainText('8 nodes, 7 edges');
  await expect.poll(() => page.evaluate(() => document.documentElement.lang)).toBe('en');
});
