// Removing a line (milestone 2, step 8, G1; W37): a support or context
// line's menu offers Remove Line, and Delete removes a selected line. The
// nodes at its ends stay; ACPs that go with it are asked about first; what
// cannot be removed says why; Undo brings it back.

import { readFileSync } from 'node:fs';

import { expect, test, type Page } from '@playwright/test';

const CORE = 'sacm_example_core_argument.sacm.xmi';

const hook = (page: Page) => page.evaluate(() => window.__assuranceForge!);
const canvas = (page: Page) => page.locator('canvas.af-canvas');
const status = (page: Page) => page.getByTestId('status');
const menu = (page: Page) => page.getByRole('menu');

async function opened(page: Page, sample: string, query = '') {
  await page.goto(`/?sample=${sample}${query}`);
  await expect.poll(async () => (await hook(page))?.revision).toBe(1);
  await expect.poll(async () => (await hook(page)).viewport?.scale ?? null).not.toBeNull();
}

/**
 * The middle of a segment of the line `relationship` draws into `to`, in
 * page coordinates: by default the last, the line's own, where a trunk it
 * shares with its siblings opens no menu.
 */
async function onLine(page: Page, relationship: string, to: string, segment: 'first' | 'last' = 'last') {
  const state = await hook(page);
  const box = (await canvas(page).boundingBox())!;
  const view = state.viewport!;
  const edge = state.diagram!.diagram.edges.find((candidate) => candidate.relationship === relationship && candidate.to === to)!;
  const [a, b] = segment === 'last' ? edge.points.slice(-2) : edge.points.slice(0, 2);
  // The first a third of the way along, clear of an ACP's square at its middle.
  const along = segment === 'last' ? 1 / 2 : 1 / 3;
  const point = { x: a.x + (b.x - a.x) * along, y: a.y + (b.y - a.y) * along };
  return { x: box.x + point.x * view.scale + view.offsetX, y: box.y + point.y * view.scale + view.offsetY };
}

const drawn = async (page: Page, relationship: string, to: string) =>
  (await hook(page)).diagram!.diagram.edges.some((edge) => edge.relationship === relationship && edge.to === to);

const nodes = async (page: Page) => (await hook(page)).diagram!.diagram.nodes.map((node) => node.id).sort();

async function lineMenu(page: Page, relationship: string, to: string, segment: 'first' | 'last' = 'last') {
  const at = await onLine(page, relationship, to, segment);
  await page.mouse.click(at.x, at.y, { button: 'right' });
  await expect(menu(page)).toBeVisible();
}

test('a line is removed from its menu, its ends stay, and Undo brings it back', async ({ page }) => {
  await opened(page, CORE);
  const before = await nodes(page);
  // The assumption a support line joins (AF-GSN-004).
  await lineMenu(page, 'inf_2', 'cl_assumption_1');
  // Delete does it too, once the line is selected.
  await expect(menu(page).getByRole('menuitem', { name: 'Remove Line' })).toHaveAccessibleDescription('Delete');
  await menu(page).getByRole('menuitem', { name: 'Remove Line' }).click();

  await expect.poll(async () => (await hook(page)).revision).toBe(2);
  await expect(status(page)).toHaveText('Line from cl_sub_1 to cl_assumption_1 removed.');
  expect(await drawn(page, 'inf_2', 'cl_assumption_1')).toBe(false);
  expect(await nodes(page)).toEqual(before);

  await page.keyboard.press('ControlOrMeta+z');
  await expect.poll(() => drawn(page, 'inf_2', 'cl_assumption_1')).toBe(true);
});

test('Delete removes the selected line', async ({ page }) => {
  await opened(page, CORE);
  const at = await onLine(page, 'inf_2', 'cl_assumption_1');
  await page.mouse.click(at.x, at.y);
  await expect.poll(async () => (await hook(page)).selected).toBe('inf_2 cl_sub_1 cl_assumption_1');
  await canvas(page).focus();
  await page.keyboard.press('Delete');
  await expect(status(page)).toHaveText('Line from cl_sub_1 to cl_assumption_1 removed.');
  expect(await drawn(page, 'inf_2', 'cl_assumption_1')).toBe(false);
});

test('a line that cannot be removed says why, and nothing changes', async ({ page }) => {
  await opened(page, CORE);
  // A Solution is known only by its line.
  await lineMenu(page, 'ae_1', 'ev_1');
  await menu(page).getByRole('menuitem', { name: 'Remove Line' }).click();
  await expect(status(page)).toHaveText(
    'Nothing changed. Without this line, the file would no longer say what the element at its end is: a Solution or a Context is known only by its line. Remove the element instead.',
  );
  // The line from a Goal to its Strategy goes with the Strategy.
  await lineMenu(page, 'inf_1', 'ar_1');
  await menu(page).getByRole('menuitem', { name: 'Remove Line' }).click();
  await expect(status(page)).toHaveText('Nothing changed. The line from a Goal to its Strategy goes only with the Strategy. Remove the Strategy instead.');
  expect((await hook(page)).revision).toBe(1);
});

test('the ACPs that go with a line are asked about first', async ({ page }) => {
  // G3 also supports G2 directly, by a line that carries ACP1's description.
  const sample = readFileSync('../testdata/sacm/acps.sacm.xmi', 'utf8');
  const anchor = '<argumentElement xmi:type="sacm:AssertedEvidence" xmi:id="E2"';
  expect(sample).toContain(anchor);
  const withLine = sample.replace(
    anchor,
    `<argumentElement xmi:type="sacm:AssertedInference" xmi:id="IX" source="G3" target="G2" metaClaim="ACP1"><name content="IX"/></argumentElement>\n    ${anchor}`,
  );
  await page.goto('/');
  await page.getByTestId('open-file').setInputFiles({ name: 'withline.sacm.xmi', mimeType: 'application/xml', buffer: Buffer.from(withLine) });
  await expect.poll(async () => (await hook(page))?.viewport?.scale ?? null).not.toBeNull();
  expect(await drawn(page, 'IX', 'G3')).toBe(true);

  await lineMenu(page, 'IX', 'G3');
  await menu(page).getByRole('menuitem', { name: 'Remove Line' }).click();
  const dialog = page.getByRole('alertdialog', { name: 'Remove the line from G2 to G3?' });
  await expect(dialog.getByTestId('remove-line-list').getByRole('listitem')).toHaveText(['ACP1']);
  // Cancel changes nothing.
  await dialog.getByRole('button', { name: 'Cancel' }).click();
  expect(await drawn(page, 'IX', 'G3')).toBe(true);

  await lineMenu(page, 'IX', 'G3');
  await menu(page).getByRole('menuitem', { name: 'Remove Line' }).click();
  await page.getByRole('alertdialog').getByTestId('remove-line-confirm').click();
  await expect(status(page)).toHaveText('Line from G2 to G3 removed, with ACP1.');
  expect(await drawn(page, 'IX', 'G3')).toBe(false);
  // ACP1 stays on the Strategy's line, which still names it.
  const labels = (await hook(page)).diagram!.diagram.edges.flatMap((edge) => edge.acps.map((acp) => acp.label.text));
  expect(labels).toContain('ACP1');
});

test('a challenge line offers no Remove Line', async ({ page }) => {
  await opened(page, 'challenges.sacm.xmi');
  const state = await hook(page);
  const challenge = state.diagram!.diagram.edges.find((edge) => edge.kind === 'challenge' && !edge.toRelationship)!;
  // From the challenger: its other end is on what it challenges.
  await lineMenu(page, challenge.relationship, challenge.to, 'first');
  await expect(menu(page).getByRole('menuitem', { name: 'Remove Line' })).toHaveCount(0);
});

test('removing a line is worded in Japanese', async ({ page }) => {
  await opened(page, CORE, '&lang=ja');
  await lineMenu(page, 'inf_2', 'cl_assumption_1');
  await menu(page).getByRole('menuitem', { name: '線を削除' }).click();
  await expect(status(page)).toHaveText('cl_sub_1 から cl_assumption_1 への線を削除しました。');
  await lineMenu(page, 'ae_1', 'ev_1');
  await menu(page).getByRole('menuitem', { name: '線を削除' }).click();
  await expect(status(page)).toHaveText(/^変更していません。この線がないと/);
});
