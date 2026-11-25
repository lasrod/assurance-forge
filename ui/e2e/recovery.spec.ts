// The recovery copy (milestone 2, step 3): while the case has changes that
// are not saved, the page keeps a copy in this browser's IndexedDB; a page
// that ends without closing the file leaves it, and opening the file again
// offers it back before anything else.

import { expect, test, type BrowserContext, type Page } from '@playwright/test';

const SAMPLE = 'sacm_example_core_argument.sacm.xmi';

const hook = (page: Page) => page.evaluate(() => window.__assuranceForge!);
const canvas = (page: Page) => page.locator('canvas.af-canvas');
const status = (page: Page) => page.getByTestId('status');
const modified = (page: Page) => page.getByTestId('modified');
const dialog = (page: Page) => page.getByRole('alertdialog');

async function opened(page: Page, query = '') {
  await page.goto(`/?sample=${SAMPLE}${query}`);
  await expect.poll(async () => (await hook(page))?.revision).toBeGreaterThanOrEqual(1);
}

/** Adds a Goal under the top claim from its menu: a change not saved. */
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
  await expect(status(page)).toHaveText(/^Goal G\d+ added\.$/);
}

/** The names the browser keeps a copy for. */
function copies(page: Page): Promise<string[]> {
  return page.evaluate(
    () =>
      new Promise<string[]>((resolve, reject) => {
        const opening = indexedDB.open('assurance-forge-recovery', 1);
        // Not made yet: no copy. Making it here, without its stores, would
        // keep the page from making it.
        let missing = false;
        opening.onupgradeneeded = () => {
          missing = true;
          opening.transaction?.abort();
        };
        opening.onerror = () => (missing ? resolve([]) : reject(opening.error));
        opening.onsuccess = () => {
          const db = opening.result;
          if (!db.objectStoreNames.contains('copies')) {
            db.close();
            resolve([]);
            return;
          }
          const keys = db.transaction('copies').objectStore('copies').getAllKeys();
          keys.onsuccess = () => {
            db.close();
            resolve(keys.result.map(String));
          };
          keys.onerror = () => reject(keys.error);
        };
      }),
  );
}

/**
 * A change kept, then the page ends without closing the file, as a crash
 * ends it: no `beforeunload`, nothing given up. Answers a new page in the
 * same browser, which shares its IndexedDB.
 */
async function crashAfterAChange(page: Page, context: BrowserContext, query = ''): Promise<Page> {
  await opened(page, query);
  await change(page);
  await expect.poll(() => copies(page)).toEqual([SAMPLE]);
  await page.close({ runBeforeUnload: false });
  return context.newPage();
}

async function fileMenu(page: Page) {
  await page.getByRole('menuitem', { name: 'File', exact: true }).click();
  return page.getByRole('menu', { name: 'File' });
}

test('a copy a crash left is offered on open; Restore makes one change that Undo takes back', async ({ page, context }) => {
  const again = await crashAfterAChange(page, context);
  await opened(again);
  await expect(dialog(again)).toBeVisible();
  await expect(dialog(again)).toContainText(`Restore the changes to ${SAMPLE}?`);
  await expect(dialog(again)).toContainText('were not saved when Assurance Forge last closed');
  await expect(again.getByTestId('recovery-base-changed')).toBeHidden();
  // The file as saved, until it is decided.
  expect((await hook(again)).diagram!.diagram.nodes).toHaveLength(8);
  await again.getByTestId('recovery-restore').click();
  await expect(status(again)).toHaveText('Changes restored. Edit ▸ Undo takes them back.');
  await expect(modified(again)).toBeVisible();
  await expect.poll(async () => (await hook(again)).diagram!.diagram.nodes.length).toBe(9);

  await again.keyboard.press('Control+z');
  await expect(modified(again)).toBeHidden();
  await expect.poll(async () => (await hook(again)).diagram!.diagram.nodes.length).toBe(8);
  // Back in its saved state: the copy goes.
  await expect.poll(() => copies(again)).toEqual([]);
});

test('Escape restores, since Undo takes it back', async ({ page, context }) => {
  const again = await crashAfterAChange(page, context);
  await opened(again);
  await expect(dialog(again)).toBeVisible();
  await again.keyboard.press('Escape');
  await expect(status(again)).toHaveText('Changes restored. Edit ▸ Undo takes them back.');
  await expect.poll(async () => (await hook(again)).diagram!.diagram.nodes.length).toBe(9);
});

test('Discard deletes the copy, and the file opens next time without asking', async ({ page, context }) => {
  const again = await crashAfterAChange(page, context);
  await opened(again);
  await again.getByTestId('recovery-discard').click();
  await expect(status(again)).toHaveText('The copy of the changes was discarded.');
  await expect(modified(again)).toBeHidden();
  expect(await copies(again)).toEqual([]);
  await again.reload();
  await expect.poll(async () => (await hook(again))?.revision).toBeGreaterThanOrEqual(1);
  await expect(dialog(again)).toBeHidden();
});

test('a copy goes with a save, and with changes given up by Close', async ({ page }) => {
  await opened(page);
  await change(page);
  await expect.poll(() => copies(page)).toEqual([SAMPLE]);
  await Promise.all([page.waitForEvent('download'), page.keyboard.press('Control+s')]);
  await expect(modified(page)).toBeHidden();
  await expect.poll(() => copies(page)).toEqual([]);

  await change(page);
  await expect.poll(() => copies(page)).toEqual([SAMPLE]);
  await (await fileMenu(page)).getByRole('menuitem', { name: 'Close' }).click();
  await page.getByTestId('unsaved-discard').click();
  await expect.poll(() => copies(page)).toEqual([]);
});

test('the question is in Japanese', async ({ page, context }) => {
  const again = await crashAfterAChange(page, context);
  await opened(again, '&lang=ja');
  await expect(dialog(again)).toContainText(`${SAMPLE} への変更を復元しますか？`);
  await expect(again.getByTestId('recovery-restore')).toHaveText('復元');
  await expect(again.getByTestId('recovery-discard')).toHaveText('破棄');
});
