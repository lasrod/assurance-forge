import { expect, test, type Page } from '@playwright/test';

const hook = (page: Page) => page.evaluate(() => window.__assuranceForge!);

async function opened(page: Page, sample = 'sacm_example_core_argument.sacm.xmi', query = '') {
  await page.goto(`/?sample=${sample}${query}`);
  await expect.poll(async () => (await hook(page))?.revision).toBe(1);
}

const navigator = (page: Page) => page.getByRole('tree', { name: 'Argument Navigator' });
const explorer = (page: Page) => page.getByRole('tree', { name: 'Case Explorer' });

/** A node's centre on the page, from the diagram as drawn. */
async function nodeCentre(page: Page, id: string) {
  const box = (await page.getByRole('img', { name: 'GSN diagram' }).boundingBox())!;
  const state = await hook(page);
  const bounds = state.diagram!.diagram.nodes.find((node) => node.id === id)!.bounds;
  const view = state.viewport!;
  return {
    x: box.x + (bounds.x + bounds.width / 2) * view.scale + view.offsetX,
    y: box.y + (bounds.y + bounds.height / 2) * view.scale + view.offsetY,
  };
}

test('the Argument Navigator shows the argument as the diagram places it', async ({ page }) => {
  await opened(page);
  const rows = navigator(page).getByRole('treeitem');
  await expect(rows).toHaveCount(8);
  // Depth first: the context beside the top claim, then what supports it.
  await expect(rows.nth(0)).toContainText('TopClaim');
  await expect(rows.nth(1)).toContainText('OperationalDesignDomainContext');
  await expect(rows.nth(2)).toContainText('Reasoning_ArchitectureAndVerification');
  await expect(rows.nth(0)).toHaveAttribute('aria-level', '1');
  await expect(rows.nth(2)).toHaveAttribute('aria-level', '2');
  await expect(rows.nth(3)).toHaveAttribute('aria-level', '3');
  // Each row carries its GSN identifier.
  await expect(rows.nth(0)).toContainText('cl_top');
});

test('a row chosen in the tree is selected on the canvas and shown in Element Properties', async ({ page }) => {
  await opened(page);
  await navigator(page).getByRole('treeitem', { name: /SubClaim_Verification/ }).click();
  await expect.poll(async () => (await hook(page)).selected).toBe('cl_sub_2');
  const properties = page.getByTestId('element-properties');
  await expect(properties).toContainText('Goal, undeveloped');
  await expect(page.getByTestId('element-statement')).toHaveText('Verification activities demonstrate braking requirements are satisfied.');
});

test('a node chosen on the canvas is the selected row of the tree, with its branch open', async ({ page }) => {
  await opened(page);
  // Close the branch that holds the node first.
  const branch = navigator(page).getByRole('treeitem', { name: /Reasoning_ArchitectureAndVerification/ });
  await branch.focus();
  await page.keyboard.press('ArrowLeft');
  await expect(navigator(page).getByRole('treeitem')).toHaveCount(3);

  const node = await nodeCentre(page, 'ev_2');
  await page.mouse.click(node.x, node.y);
  await expect.poll(async () => (await hook(page)).selected).toBe('ev_2');
  const row = navigator(page).getByRole('treeitem', { name: /BrakeVerificationReportReference/ });
  await expect(row).toBeVisible();
  await expect(row).toHaveAttribute('aria-selected', 'true');
});

test('a row chosen in the tree comes into view when the canvas does not show it', async ({ page }) => {
  await opened(page, 'safety-case-sanitized-strict.sacm.xmi');
  const size = (await page.getByRole('img', { name: 'GSN diagram' }).boundingBox())!;
  const rows = navigator(page).getByRole('treeitem');
  const last = rows.last();
  await last.scrollIntoViewIfNeeded();
  await last.click();
  const state = await hook(page);
  expect(state.selected).not.toBeNull();
  const bounds = state.diagram!.diagram.nodes.find((node) => node.id === state.selected)!.bounds;
  await expect
    .poll(async () => {
      const view = (await hook(page)).viewport!;
      const left = bounds.x * view.scale + view.offsetX;
      const top = bounds.y * view.scale + view.offsetY;
      return left >= 0 && top >= 0 && left + bounds.width * view.scale <= size.width && top + bounds.height * view.scale <= size.height;
    })
    .toBe(true);
});

test('the Case Explorer shows the file and its packages, in words', async ({ page }) => {
  await opened(page);
  const rows = explorer(page).getByRole('treeitem');
  await expect(rows).toHaveCount(3);
  await expect(rows.nth(0)).toContainText('sacm_example_core_argument.sacm.xmi');
  await expect(rows.nth(1)).toContainText('Assurance case');
  await expect(rows.nth(2)).toContainText('BrakingArgumentPackage');
  await expect(rows.nth(2)).toContainText('Argument');
  await expect(rows.nth(2)).toHaveAttribute('aria-level', '3');
});

test('the trees follow the language', async ({ page }) => {
  await opened(page, 'oasc-ja.sacm.xmi', '&lang=ja');
  const tree = page.getByRole('tree', { name: '論証ナビゲーター' });
  const rows = await tree.getByRole('treeitem').count();
  expect(rows).toBeGreaterThan(8);
  await expect(page.getByRole('tree', { name: 'ケースエクスプローラー' }).getByRole('treeitem').nth(1)).toContainText('アシュアランスケース');
  await page.getByRole('menuitem', { name: '表示', exact: true }).click();
  await page.getByRole('menuitem', { name: '外観' }).click();
  await page.getByRole('menuitem', { name: '言語' }).click();
  await page.getByRole('menuitemradio', { name: 'English' }).click();
  // The same tree, named and worded in English.
  await expect(navigator(page).getByRole('treeitem')).toHaveCount(rows);
  await expect(explorer(page).getByRole('treeitem').nth(1)).toContainText('Assurance case');
});

test('the zoom field above the canvas shows the zoom and sets it', async ({ page }) => {
  await opened(page);
  const field = page.getByRole('region', { name: 'Diagram' }).getByLabel('Zoom', { exact: true });
  const fitted = Math.round((await hook(page)).viewport!.scale * 100);
  await expect(field).toHaveValue(String(fitted));
  await field.fill('200');
  await field.press('Enter');
  await expect(page.getByTestId('zoom')).toHaveText('Zoom 200%');
  expect((await hook(page)).viewport!.scale).toBeCloseTo(2, 5);
});

test('View ▸ Panels hides and shows a panel', async ({ page }) => {
  await opened(page);
  const problems = page.getByRole('region', { name: 'Problems' });
  await expect(problems).toBeVisible();
  await page.getByRole('menuitem', { name: 'View', exact: true }).click();
  const item = page.getByRole('menuitemcheckbox', { name: 'Problems' });
  await expect(item).toHaveAttribute('aria-checked', 'true');
  await item.click();
  await expect(problems).toBeHidden();
  // A menu of options stays open, and says what shows now.
  await expect(item).toHaveAttribute('aria-checked', 'false');
  await item.click();
  await expect(problems).toBeVisible();
  await expect(item).toHaveAttribute('aria-checked', 'true');
});
