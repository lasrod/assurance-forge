import { expect, test, type Page } from '@playwright/test';

async function opened(page: Page, sample: string, query = '') {
  await page.goto(`/?sample=${sample}${query}`);
  await expect.poll(() => page.evaluate(() => window.__assuranceForge?.revision)).toBe(1);
}

/** Every line of node text measured by the browser against the core's measurement. */
async function textErrors(page: Page) {
  return page.evaluate(() => {
    const state = window.__assuranceForge;
    const context = document.createElement('canvas').getContext('2d');
    if (!state?.diagram || !context) return null;
    const family = "'DATUM Measured Sans', 'DATUM Measured Sans JP'";
    let largest = 0;
    let worst = '';
    let lines = 0;
    for (const node of state.diagram.diagram.nodes) {
      for (const line of node.text) {
        context.font = `${line.weight} ${line.size}px ${family}`;
        const error = Math.abs(context.measureText(line.text).width - line.width);
        lines += 1;
        if (error > largest) {
          largest = error;
          worst = line.text;
        }
      }
    }
    return { largest, worst, lines };
  });
}

// The plan's budget: the browser's width within 1.5 px of the core's, for every line.
for (const [name, sample, query] of [
  ['English', 'sacm_example_core_argument.sacm.xmi', ''],
  ['English, the large case', 'safety-case-sanitized-strict.sacm.xmi', ''],
  ['Japanese', 'oasc-ja.sacm.xmi', '&lang=ja'],
]) {
  test(`text widths match the browser's: ${name}`, async ({ page }) => {
    await opened(page, sample, query);
    const result = await textErrors(page);
    expect(result).not.toBeNull();
    expect(result!.lines).toBeGreaterThan(10);
    expect(result!.largest, `worst line: ${result!.worst}`).toBeLessThanOrEqual(1.5);
  });
}

for (const theme of ['light', 'dark']) {
  test(`the core sample is drawn as before, ${theme}`, async ({ page, browserName }) => {
    test.skip(browserName !== 'chromium', 'screenshots are compared in one browser');
    await page.setViewportSize({ width: 1280, height: 800 });
    await opened(page, 'sacm_example_core_argument.sacm.xmi', `&theme=${theme}&lang=en`);
    await expect(page.getByRole('img', { name: 'GSN diagram' })).toHaveScreenshot(`core-${theme}.png`, { maxDiffPixels: 20 });
  });
}

test('switching the theme draws the canvas again in its colours', async ({ page }) => {
  await opened(page, 'sacm_example_core_argument.sacm.xmi', '&theme=light');
  const ground = () =>
    page.getByRole('img', { name: 'GSN diagram' }).evaluate((canvas: HTMLCanvasElement) => {
      const pixel = canvas.getContext('2d')!.getImageData(2, 2, 1, 1).data;
      return pixel[0] + pixel[1] + pixel[2];
    });
  const light = await ground();
  const paints = await page.evaluate(() => window.__assuranceForge?.paints ?? 0);
  await page.getByRole('button', { name: 'View', exact: true }).click();
  await page.getByRole('menuitemradio', { name: 'Dark' }).click();
  await expect.poll(() => page.evaluate(() => window.__assuranceForge?.paints ?? 0)).toBeGreaterThan(paints);
  await expect.poll(ground).toBeLessThan(light - 200);
});

test('the diagram text follows the user\'s text size', async ({ page }) => {
  // A larger browser text size: DATUM's type tokens are in rem.
  await page.addInitScript(() => {
    document.addEventListener('DOMContentLoaded', () => {
      document.documentElement.style.fontSize = '20px';
    });
  });
  await opened(page, 'sacm_example_core_argument.sacm.xmi', '&lang=en');
  const size = () =>
    page.evaluate(() => window.__assuranceForge?.diagram?.diagram.nodes[0].text.find((line) => line.role === 'statement')?.size);
  // type.control is 0.875rem: 17.5 px at a 20 px root.
  await expect.poll(size).toBe(17.5);
});
