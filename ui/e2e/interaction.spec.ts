// Goal G8: one test per row of the plan's Interaction table. The viewport and
// the selection are read through the hook that only test builds have. The
// Theme row is in drawing.spec.ts ("switching the theme draws the canvas
// again in its colours").

import { expect, test, type Page } from '@playwright/test';

const SAMPLE = 'sacm_example_core_argument.sacm.xmi';

async function opened(page: Page, query = '') {
  await page.setViewportSize({ width: 1280, height: 800 });
  await page.goto(`/?sample=${SAMPLE}${query}`);
  await expect.poll(() => page.evaluate(() => window.__assuranceForge?.drawn)).toEqual({ nodes: 8, edges: 7 });
}

const canvas = (page: Page) => page.getByRole('img', { name: 'GSN diagram' });

async function hook(page: Page) {
  const state = await page.evaluate(() => window.__assuranceForge);
  if (!state?.viewport || !state.diagram) throw new Error('nothing drawn');
  return { viewport: state.viewport, diagram: state.diagram.diagram, selected: state.selected, hovered: state.hovered };
}

const viewport = async (page: Page) => (await hook(page)).viewport;

/** A node's centre, in page coordinates. */
async function nodeCentre(page: Page, id: string) {
  const { viewport: view, diagram } = await hook(page);
  const box = (await canvas(page).boundingBox())!;
  const node = diagram.nodes.find((candidate) => candidate.id === id)!;
  const { x, y, width, height } = node.shape.rect;
  return { x: box.x + (x + width / 2) * view.scale + view.offsetX, y: box.y + (y + height / 2) * view.scale + view.offsetY };
}

/** A point on empty canvas: inside the fit margin, near the top left corner. */
async function emptyPoint(page: Page) {
  const box = (await canvas(page).boundingBox())!;
  return { x: box.x + 6, y: box.y + 6 };
}

/** The diagram point at a canvas point (CSS px from the canvas's corner). */
async function diagramAt(page: Page, x: number, y: number) {
  const view = await viewport(page);
  return { x: (x - view.offsetX) / view.scale, y: (y - view.offsetY) / view.scale };
}

async function canvasCentre(page: Page) {
  const box = (await canvas(page).boundingBox())!;
  return { x: box.width / 2, y: box.height / 2 };
}

/** The viewport that fits the whole diagram, by the plan's rule (16 px margin, at most 100%). */
async function fitted(page: Page) {
  const { diagram } = await hook(page);
  const box = (await canvas(page).boundingBox())!;
  const b = diagram.bounds;
  const scale = Math.max(Math.min((box.width - 32) / b.width, (box.height - 32) / b.height, 1), 0.1);
  return { scale, offsetX: (box.width - b.width * scale) / 2 - b.x * scale, offsetY: (box.height - b.height * scale) / 2 - b.y * scale };
}

function expectViewport(actual: { scale: number; offsetX: number; offsetY: number }, expected: typeof actual) {
  expect(actual.scale).toBeCloseTo(expected.scale, 5);
  expect(actual.offsetX).toBeCloseTo(expected.offsetX, 3);
  expect(actual.offsetY).toBeCloseTo(expected.offsetY, 3);
}

test('Pan: a drag on empty canvas, a middle-button drag and the wheel move the view, and select nothing', async ({ page }) => {
  await opened(page);
  const start = await viewport(page);
  const from = await emptyPoint(page);
  await page.mouse.move(from.x, from.y);
  await page.mouse.down();
  await page.mouse.move(from.x + 60, from.y + 40, { steps: 5 });
  await page.mouse.up();
  const dragged = await viewport(page);
  expect(dragged.offsetX - start.offsetX).toBeCloseTo(60, 3);
  expect(dragged.offsetY - start.offsetY).toBeCloseTo(40, 3);
  expect(dragged.scale).toBe(start.scale);
  expect((await hook(page)).selected).toBeNull();

  // A drag that starts on a node pans too, and does not select it.
  const node = await nodeCentre(page, 'cl_top');
  await page.mouse.move(node.x, node.y);
  await page.mouse.down();
  await page.mouse.move(node.x - 30, node.y + 20, { steps: 5 });
  await page.mouse.up();
  expect((await viewport(page)).offsetX - dragged.offsetX).toBeCloseTo(-30, 3);
  expect((await hook(page)).selected).toBeNull();

  // The middle button pans from the first move.
  const middle = await viewport(page);
  await page.mouse.move(from.x, from.y);
  await page.mouse.down({ button: 'middle' });
  await page.mouse.move(from.x + 2, from.y - 3);
  await page.mouse.up({ button: 'middle' });
  expect((await viewport(page)).offsetX - middle.offsetX).toBeCloseTo(2, 3);
  expect((await viewport(page)).offsetY - middle.offsetY).toBeCloseTo(-3, 3);

  // The wheel pans on both axes.
  const wheeled = await viewport(page);
  await page.mouse.wheel(25, 50);
  await expect.poll(async () => (await viewport(page)).offsetY).toBeCloseTo(wheeled.offsetY - 50, 3);
  expect((await viewport(page)).offsetX).toBeCloseTo(wheeled.offsetX - 25, 3);
  expect((await hook(page)).selected).toBeNull();
});

test('Zoom: Ctrl and the wheel zoom about the pointer, + and − about the centre, from 10% to 400%, shown in the status bar', async ({ page }) => {
  await opened(page);
  const box = (await canvas(page).boundingBox())!;
  const pointer = { x: 200, y: 150 };
  const under = await diagramAt(page, pointer.x, pointer.y);
  const before = await viewport(page);
  await page.mouse.move(box.x + pointer.x, box.y + pointer.y);
  await page.keyboard.down('Control');
  await page.mouse.wheel(0, -40);
  await page.keyboard.up('Control');
  await expect.poll(async () => (await viewport(page)).scale).toBeGreaterThan(before.scale);
  const after = await diagramAt(page, pointer.x, pointer.y);
  expect(after.x).toBeCloseTo(under.x, 3);
  expect(after.y).toBeCloseTo(under.y, 3);

  // + and − zoom about the centre, a step of 1.25.
  await canvas(page).focus();
  const centre = await canvasCentre(page);
  const middle = await diagramAt(page, centre.x, centre.y);
  const stepped = (await viewport(page)).scale;
  await page.keyboard.press('+');
  expect((await viewport(page)).scale).toBeCloseTo(stepped * 1.25, 5);
  const still = await diagramAt(page, centre.x, centre.y);
  expect(still.x).toBeCloseTo(middle.x, 3);
  expect(still.y).toBeCloseTo(middle.y, 3);
  await page.keyboard.press('-');
  expect((await viewport(page)).scale).toBeCloseTo(stepped, 5);

  // The range, and the status bar.
  for (let press = 0; press < 12; press += 1) await page.keyboard.press('+');
  expect((await viewport(page)).scale).toBe(4);
  await expect(page.getByTestId('zoom')).toHaveText('Zoom 400%');
  for (let press = 0; press < 25; press += 1) await page.keyboard.press('-');
  expect((await viewport(page)).scale).toBeCloseTo(0.1, 5);
  await expect(page.getByTestId('zoom')).toHaveText('Zoom 10%');

  // The toolbar's buttons do the same as the keys.
  await page.getByRole('button', { name: 'Zoom in' }).click();
  expect((await viewport(page)).scale).toBeCloseTo(0.125, 5);
  await page.getByRole('button', { name: 'Zoom out' }).click();
  expect((await viewport(page)).scale).toBeCloseTo(0.1, 5);
});

test('Fit: the diagram is fitted on open, and again by Shift+1 and by the toolbar', async ({ page }) => {
  await opened(page);
  expectViewport(await viewport(page), await fitted(page));

  await canvas(page).focus();
  await page.keyboard.press('+');
  await page.mouse.wheel(40, 40);
  await page.keyboard.press('Shift+Digit1');
  expectViewport(await viewport(page), await fitted(page));

  await page.keyboard.press('+');
  await page.getByRole('button', { name: 'Fit to view' }).click();
  expectViewport(await viewport(page), await fitted(page));
});

test('Fit selection: Shift+2 fits the selected node, and does nothing with nothing selected', async ({ page }) => {
  await opened(page);
  await canvas(page).focus();
  // Away from the fit, so a Shift+2 that fitted all would show.
  await page.keyboard.press('+');
  await page.mouse.wheel(30, 30);
  await expect.poll(async () => (await viewport(page)).offsetX).not.toBeCloseTo((await fitted(page)).offsetX, 0);
  const before = await viewport(page);
  await page.keyboard.press('Shift+Digit2');
  expectViewport(await viewport(page), before);

  const node = await nodeCentre(page, 'ar_1');
  await page.mouse.click(node.x, node.y);
  await page.keyboard.press('Shift+Digit2');
  const { diagram, viewport: view } = await hook(page);
  const bounds = diagram.nodes.find((candidate) => candidate.id === 'ar_1')!.bounds;
  const centre = await canvasCentre(page);
  // Centred, at the largest zoom that fits, which is at most 100%.
  expect(view.scale).toBe(1);
  expect((bounds.x + bounds.width / 2) * view.scale + view.offsetX).toBeCloseTo(centre.x, 3);
  expect((bounds.y + bounds.height / 2) * view.scale + view.offsetY).toBeCloseTo(centre.y, 3);
});

test('Actual size: Shift+0 returns to 100% about the centre', async ({ page }) => {
  await opened(page);
  await canvas(page).focus();
  await page.keyboard.press('-');
  const centre = await canvasCentre(page);
  const middle = await diagramAt(page, centre.x, centre.y);
  expect((await viewport(page)).scale).toBeLessThan(1);
  await page.keyboard.press('Shift+Digit0');
  expect((await viewport(page)).scale).toBe(1);
  await expect(page.getByTestId('zoom')).toHaveText('Zoom 100%');
  const still = await diagramAt(page, centre.x, centre.y);
  expect(still.x).toBeCloseTo(middle.x, 3);
  expect(still.y).toBeCloseTo(middle.y, 3);
});

/** The canvas pixel at a page point, as r + g + b. */
async function pixel(page: Page, at: { x: number; y: number }) {
  return canvas(page).evaluate((element: HTMLCanvasElement, point) => {
    const rect = element.getBoundingClientRect();
    const scale = element.width / rect.width;
    const data = element.getContext('2d')!.getImageData(Math.round((point.x - rect.x) * scale), Math.round((point.y - rect.y) * scale), 1, 1).data;
    return data[0] + data[1] + data[2];
  }, at);
}

test('Select: a click outlines the node, and Element Properties shows what it is', async ({ page }) => {
  await opened(page, '&lang=en&theme=light');
  const properties = page.getByRole('region', { name: 'Element Properties' });
  await expect(properties).toContainText('Nothing is selected.');
  const node = await nodeCentre(page, 'cl_sub_2');
  // A point inside the node, clear of its text: just inside its top left corner.
  const { diagram, viewport: view } = await hook(page);
  const rect = diagram.nodes.find((candidate) => candidate.id === 'cl_sub_2')!.shape.rect;
  const box = (await canvas(page).boundingBox())!;
  const inside = { x: box.x + (rect.x + 3) * view.scale + view.offsetX, y: box.y + (rect.y + 3) * view.scale + view.offsetY };
  const unselected = await pixel(page, inside);

  await page.mouse.click(node.x, node.y);
  await expect.poll(async () => (await hook(page)).selected).toBe('cl_sub_2');
  // Off the node, so what shows is the selection and not the hover.
  const empty = await emptyPoint(page);
  await page.mouse.move(empty.x, empty.y);
  await expect.poll(async () => (await hook(page)).hovered).toBeNull();
  await expect.poll(() => pixel(page, inside)).not.toBe(unselected);

  const grid = page.getByTestId('element-properties');
  await expect(grid).toContainText('Goal, undeveloped');
  await expect(grid).toContainText('cl_sub_2');
  await expect(grid).toContainText('SubClaim_Verification');
  await expect(grid).toContainText('Verification activities demonstrate braking requirements are satisfied.');
  await expect(grid).toContainText('SACM ID');

  // The panel's words follow the language.
  await page.getByRole('menuitem', { name: 'View', exact: true }).click();
  await page.getByRole('menuitem', { name: 'Appearance' }).click();
  await page.getByRole('menuitem', { name: 'Language' }).click();
  await page.getByRole('menuitemradio', { name: '日本語' }).click();
  await expect(grid).toContainText('ゴール（未展開）');
  expect((await hook(page)).selected).toBe('cl_sub_2');
});

test('Clear: a click on empty canvas or Esc clears the selection', async ({ page }) => {
  await opened(page);
  const properties = page.getByRole('region', { name: 'Element Properties' });
  const node = await nodeCentre(page, 'cl_top');
  await page.mouse.click(node.x, node.y);
  await expect(page.getByTestId('element-properties')).toContainText('cl_top');

  const empty = await emptyPoint(page);
  await page.mouse.click(empty.x, empty.y);
  await expect.poll(async () => (await hook(page)).selected).toBeNull();
  await expect(properties).toContainText('Nothing is selected.');

  await page.mouse.click(node.x, node.y);
  await expect.poll(async () => (await hook(page)).selected).toBe('cl_top');
  await page.keyboard.press('Escape');
  await expect.poll(async () => (await hook(page)).selected).toBeNull();
  await expect(properties).toContainText('Nothing is selected.');
});

test('Hover: the node under the pointer is highlighted and the cursor changes', async ({ page }) => {
  await opened(page, '&theme=light');
  const node = await nodeCentre(page, 'ev_1');
  const { diagram, viewport: view } = await hook(page);
  const rect = diagram.nodes.find((candidate) => candidate.id === 'ev_1')!.shape.rect;
  const box = (await canvas(page).boundingBox())!;
  // Inside the circle, clear of its text: a little below its top.
  const inside = { x: box.x + (rect.x + rect.width / 2) * view.scale + view.offsetX, y: box.y + (rect.y + 4) * view.scale + view.offsetY };
  const plain = await pixel(page, inside);

  await page.mouse.move(node.x, node.y);
  await expect.poll(async () => (await hook(page)).hovered).toBe('ev_1');
  await expect(canvas(page)).toHaveCSS('cursor', 'pointer');
  await expect.poll(() => pixel(page, inside)).not.toBe(plain);
  // Hovering selects nothing.
  expect((await hook(page)).selected).toBeNull();

  const empty = await emptyPoint(page);
  await page.mouse.move(empty.x, empty.y);
  await expect.poll(async () => (await hook(page)).hovered).toBeNull();
  await expect(canvas(page)).toHaveCSS('cursor', 'default');
  await expect.poll(() => pixel(page, inside)).toBe(plain);
});

test('Resize: when a panel closes and reopens, the canvas keeps its view centre', async ({ page }) => {
  await opened(page);
  const centreBefore = await canvasCentre(page);
  const middle = await diagramAt(page, centreBefore.x, centreBefore.y);

  await page.getByRole('button', { name: 'Hide element properties', exact: true }).click();
  await expect.poll(async () => (await canvasCentre(page)).x).toBeGreaterThan(centreBefore.x);
  await expect
    .poll(async () => {
      const centre = await canvasCentre(page);
      return (await diagramAt(page, centre.x, centre.y)).x;
    })
    .toBeCloseTo(middle.x, 3);

  await page.getByRole('button', { name: 'Element Properties', exact: true }).click();
  await expect.poll(async () => (await canvasCentre(page)).x).toBeCloseTo(centreBefore.x, 0);
  const centre = await canvasCentre(page);
  const still = await diagramAt(page, centre.x, centre.y);
  expect(still.x).toBeCloseTo(middle.x, 3);
  expect(still.y).toBeCloseTo(middle.y, 3);
});

test('Keys: a click gives the canvas focus without a ring, so its keys work; Tab shows the ring', async ({ page }) => {
  await opened(page);
  const node = await nodeCentre(page, 'cl_top');
  await page.mouse.click(node.x, node.y);
  const focus = () => canvas(page).evaluate((element) => ({ focused: document.activeElement === element, ring: element.matches(':focus-visible') }));
  expect(await focus()).toEqual({ focused: true, ring: false });

  await page.keyboard.press('Shift+Tab');
  await page.keyboard.press('Tab');
  expect(await focus()).toEqual({ focused: true, ring: true });
});
