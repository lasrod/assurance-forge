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

test('a shared identifier is renumbered from its problem, and Undo takes it back', async ({ page }) => {
  // a and b are both G1, and b is drawn under a.
  const tagged = (id: string) =>
    `<argumentElement xmi:type="sacm:Claim" xmi:id="${id}"><name content="${id}"/><taggedValue xmi:id="t${id}"><key><value content="assuranceForge.gsn.identifier"/></key><content><value content="G1"/></content></taggedValue></argumentElement>`;
  const xml = `<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:id="case">
  <name content="Case"/>
  <argumentPackage xmi:id="p"><name content="P"/>
    ${tagged('a')}${tagged('b')}
    <argumentElement xmi:type="sacm:AssertedInference" xmi:id="i" source="b" target="a"><name content="i"/></argumentElement>
  </argumentPackage>
</sacm:AssuranceCasePackage>`;
  await page.goto('/');
  await page.getByTestId('open-file').setInputFiles({ name: 'shared.sacm.xmi', mimeType: 'application/xml', buffer: Buffer.from(xml) });
  await expect.poll(async () => (await hook(page))?.viewport?.scale ?? null).not.toBeNull();
  await page.getByTestId('problem-counts').getByRole('button').click();
  const shared = rows(page).filter({ hasText: 'AF-GSN-009' });
  await shared.click();
  await expect(offered(page)).toContainText('Keep G1 on the first in the file, and renumber the others as G2.');
  await offered(page).getByRole('button', { name: 'Repair' }).click();

  await expect(status(page)).toHaveText('Renumbered as G2.');
  await expect(shared).toHaveCount(0);
  const identifiers = (await hook(page)).diagram!.diagram.nodes.map((node) => node.text.find((line) => line.role === 'identifier')?.text);
  expect(identifiers.sort()).toEqual(['G1', 'G2']);

  await page.keyboard.press('ControlOrMeta+z');
  await expect(shared).toHaveCount(1);
});

/** A case where the Strategy s is at an end: s supports g, and g2 supports s; with `more`. */
function strategyAtEnd(more = '') {
  const claim = (id: string) => `<argumentElement xmi:type="sacm:Claim" xmi:id="${id}"><name content="${id}"/></argumentElement>`;
  return `<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:id="case">
  <name content="Case"/>
  <argumentPackage xmi:id="p"><name content="P"/>
    ${claim('g')}${claim('g2')}${claim('h')}
    <argumentElement xmi:type="sacm:ArgumentReasoning" xmi:id="s"><name content="s"/></argumentElement>
    <argumentElement xmi:type="sacm:AssertedInference" xmi:id="up" source="s" target="g"><name content="up"/></argumentElement>
    <argumentElement xmi:type="sacm:AssertedInference" xmi:id="down" source="g2" target="s"><name content="down"/></argumentElement>
    ${more}
  </argumentPackage>
</sacm:AssuranceCasePackage>`;
}

async function openXml(page: Page, xml: string, query = '') {
  await page.goto(`/${query}`);
  await page.getByTestId('open-file').setInputFiles({ name: 'strategy.sacm.xmi', mimeType: 'application/xml', buffer: Buffer.from(xml) });
  await expect.poll(async () => (await hook(page))?.viewport?.scale ?? null).not.toBeNull();
  await page.getByTestId('problem-counts').getByRole('button').click();
}

test('a Strategy at an end is made the reasoning between its Goals, and Undo takes it back', async ({ page }) => {
  await openXml(page, strategyAtEnd());
  const atEnd = rows(page).filter({ hasText: 'AF-GSN-007' });
  await expect(atEnd).toHaveCount(2);
  await atEnd.first().click();
  await expect(offered(page)).toContainText('Make s the reasoning between g and the Goals under it.');
  await offered(page).getByRole('button', { name: 'Repair' }).click();

  await expect(status(page)).toHaveText('s is now the reasoning under g.');
  await expect(atEnd).toHaveCount(0);
  const lines = (await hook(page)).diagram!.diagram.edges.map((edge) => `${edge.from}>${edge.to}`).sort();
  expect(lines).toEqual(['g>s', 's>g2']);
  expect((await hook(page)).selected).toBe('s');

  await page.keyboard.press('ControlOrMeta+z');
  await expect(atEnd).toHaveCount(2);
});

test('a Strategy that supports two Goals says why it is not made the reasoning', async ({ page }) => {
  await openXml(
    page,
    strategyAtEnd('<argumentElement xmi:type="sacm:AssertedInference" xmi:id="up2" source="s" target="h"><name content="up2"/></argumentElement>'),
  );
  await rows(page).filter({ hasText: 'AF-GSN-007' }).first().click();
  await expect(offered(page)).toHaveCount(0);
  await expect(page.getByTestId('problem-repair-refused')).toHaveText(
    'It cannot be repaired here. This Strategy supports no Goal, several, or one together with something else, so there is no one line above it to make it the reasoning of.',
  );
});

test('making a Strategy the reasoning is worded in Japanese', async ({ page }) => {
  await openXml(page, strategyAtEnd(), '?lang=ja');
  await page.getByRole('option').filter({ hasText: 'AF-GSN-007' }).first().click();
  await expect(offered(page)).toContainText('s を、g とその下のゴールのあいだの推論にします。');
  await offered(page).getByRole('button', { name: '修復' }).click();
  await expect(status(page)).toHaveText('s を g の下の推論にしました。');
});
