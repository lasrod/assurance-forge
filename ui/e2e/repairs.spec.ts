// The repairs a problem offers (milestone 2, step 8, G2; F1 and F4): a
// cycle offers each of its lines, a stale undeveloped mark is cleared, and a
// problem whose repairs cannot be made here says why. Each repair is the
// edit it names, one undo step.

import { expect, test, type Page } from '@playwright/test';

const hook = (page: Page) => page.evaluate(() => window.__assuranceForge!);
const status = (page: Page) => page.getByTestId('status');
const rows = (page: Page) => page.getByRole('listbox', { name: 'Problems' }).getByRole('option');
const offered = (page: Page) => page.getByTestId('problem-repair');

async function opened(page: Page, sample: string, query = '') {
  await page.goto(`/?sample=${sample}${query}`);
  await expect.poll(async () => (await hook(page))?.revision).toBe(1);
  await expect.poll(async () => (await hook(page)).viewport?.scale ?? null).not.toBeNull();
  // The status bar's counts open the panel.
  await page.getByTestId('problem-counts').getByRole('button').click();
}

test('a cycle offers each of its lines, and one removed breaks it', async ({ page }) => {
  await opened(page, 'gallery-roots-and-cycle.sacm.xmi');
  const cycles = rows(page).filter({ hasText: 'AF-GSN-001' });
  await expect(cycles).toHaveCount(2);
  // Ca, Cb and Cc support each other in a circle: one row, and a menu of
  // its lines.
  await cycles.first().click();
  await expect(offered(page)).toContainText('Choose one of 3 repairs.');
  await offered(page).getByRole('button', { name: 'Repair…' }).click();
  const menu = page.getByRole('menu');
  await expect(menu.getByRole('menuitem')).toHaveText([
    'Remove the line from Ca to Cb.',
    'Remove the line from Cb to Cc.',
    'Remove the line from Cc to Ca.',
  ]);
  // The list stays in view.
  await expect(cycles.first()).toBeInViewport();
  await menu.getByRole('menuitem', { name: 'Remove the line from Cb to Cc.' }).click();

  await expect.poll(async () => (await hook(page)).revision).toBe(2);
  await expect(status(page)).toHaveText('Line from Cb to Cc removed.');
  await expect(cycles).toHaveCount(1);

  await page.keyboard.press('ControlOrMeta+z');
  await expect(cycles).toHaveCount(2);
});

test('a stale undeveloped mark is cleared from its problem', async ({ page }) => {
  await opened(page, 'sacm_example_core_argument.sacm.xmi');
  await rows(page).filter({ hasText: 'AF-GSN-010' }).click();
  await expect(offered(page)).toContainText('Clear the undeveloped mark of cl_sub_2.');
  await offered(page).getByRole('button', { name: 'Repair' }).click();
  await expect.poll(async () => (await hook(page)).revision).toBe(2);
  await expect(rows(page).filter({ hasText: 'AF-GSN-010' })).toHaveCount(0);
});

test('a problem whose repair cannot be made here says why', async ({ page }) => {
  // The Goal g is given as context of h (AF-GSN-005), and that line is
  // challenged by k: it is not removed alone.
  const xml = `<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:id="case">
  <name content="Case"/>
  <argumentPackage xmi:id="p"><name content="P"/>
    <argumentElement xmi:type="sacm:Claim" xmi:id="g"><name content="g"/></argumentElement>
    <argumentElement xmi:type="sacm:Claim" xmi:id="h"><name content="h"/></argumentElement>
    <argumentElement xmi:type="sacm:Claim" xmi:id="k"><name content="k"/></argumentElement>
    <argumentElement xmi:type="sacm:AssertedContext" xmi:id="x" source="g" target="h"><name content="x"/></argumentElement>
    <argumentElement xmi:type="sacm:AssertedInference" xmi:id="ch" source="k" target="x" isCounter="true"><name content="ch"/></argumentElement>
  </argumentPackage>
</sacm:AssuranceCasePackage>`;
  await page.goto('/');
  await page.getByTestId('open-file').setInputFiles({ name: 'challenged.sacm.xmi', mimeType: 'application/xml', buffer: Buffer.from(xml) });
  await expect.poll(async () => (await hook(page))?.viewport?.scale ?? null).not.toBeNull();
  await page.getByTestId('problem-counts').getByRole('button').click();
  await rows(page).filter({ hasText: 'AF-GSN-005' }).click();
  await expect(offered(page)).toHaveCount(0);
  await expect(page.getByTestId('problem-repair-refused')).toHaveText(
    'It cannot be repaired here. This line is challenged, so it is not removed on its own. Remove or move the challenge first.',
  );
});

test('the repairs are worded in Japanese', async ({ page }) => {
  await opened(page, 'gallery-roots-and-cycle.sacm.xmi', '&lang=ja');
  // The list is named in Japanese too.
  await page.getByRole('option').filter({ hasText: 'AF-GSN-001' }).first().click();
  await expect(offered(page)).toContainText('3 通りの修復から選んでください。');
  await offered(page).getByRole('button', { name: '修復…' }).click();
  await expect(page.getByRole('menu').getByRole('menuitem').first()).toHaveText('Ca から Cb への線を削除します。');
});
