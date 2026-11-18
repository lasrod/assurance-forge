import { expect, test, type Page } from '@playwright/test';

async function drawn(page: Page) {
  return page.evaluate(() => window.__assuranceForge);
}

test('the core sample draws 8 nodes and 7 edges', async ({ page }) => {
  await page.goto('/?sample=sacm_example_core_argument.sacm.xmi');
  await expect.poll(async () => (await drawn(page))?.drawn).toEqual({ nodes: 8, edges: 7 });
  expect((await drawn(page))?.revision).toBe(1);
  await expect(page.getByTestId('status')).toContainText('8 nodes, 7 edges');
});

test('a file chosen in the picker opens', async ({ page }) => {
  await page.goto('/');
  await expect(page.getByTestId('status')).toContainText('Open a SACM 2.3 file');
  await page.getByTestId('open-file').setInputFiles('../testdata/sacm/safety-case-sanitized-strict.sacm.xmi');
  // 133 Claims, 29 ArgumentReasonings and 109 ArtifactReferences.
  await expect.poll(async () => (await drawn(page))?.drawn.nodes).toBe(271);
});

test('a refused file shows its code and place, and no diagram', async ({ page }) => {
  await page.goto('/');
  await expect(page.getByTestId('status')).toContainText('Open a SACM 2.3 file');
  await page.getByTestId('open-file').setInputFiles({
    name: 'broken.sacm.xmi',
    mimeType: 'application/xml',
    buffer: Buffer.from('<?xml version="1.0"?>\n<sacm:AssuranceCasePackage\n  oops'),
  });
  await expect(page.getByTestId('status')).toContainText(/Not opened: SACM-[A-Z]+-\d+ at line \d+, column \d+/);
  await expect.poll(async () => (await drawn(page))?.drawn).toEqual({ nodes: 0, edges: 0 });
});
