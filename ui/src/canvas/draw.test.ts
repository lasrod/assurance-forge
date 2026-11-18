import { describe, expect, it } from 'vitest';
import type { Diagram, DiagramNode } from '../core/Core';
import { draw, fit, MAX_SCALE, MIN_SCALE, type Palette } from './draw';

describe('fit', () => {
  it('centres a small diagram at 100%', () => {
    const viewport = fit({ x: 0, y: 0, width: 200, height: 100 }, 1000, 800);
    expect(viewport.scale).toBe(1);
    expect(viewport.offsetX).toBe(400);
    expect(viewport.offsetY).toBe(350);
  });

  it('shrinks a large diagram to fit, keeping its aspect', () => {
    const viewport = fit({ x: 20, y: 20, width: 4000, height: 1000 }, 1000, 800);
    expect(viewport.scale).toBeCloseTo((1000 - 32) / 4000);
    // The diagram's left edge lands on the margin.
    expect(20 * viewport.scale + viewport.offsetX).toBeCloseTo(16);
  });

  it('stays within the zoom range', () => {
    expect(fit({ x: 0, y: 0, width: 1e7, height: 1e7 }, 800, 600).scale).toBe(MIN_SCALE);
    expect(fit({ x: 0, y: 0, width: 1, height: 1 }, 800, 600).scale).toBeLessThanOrEqual(MAX_SCALE);
  });
});


/** A context that records the text drawn and measures 0.5 em per character. */
function recorder() {
  const texts: string[] = [];
  let font = '10px x';
  const context = new Proxy(
    {},
    {
      get: (_, name) => {
        if (name === 'fillText') return (text: string) => texts.push(text);
        if (name === 'measureText') return (text: string) => ({ width: text.length * 0.5 * parseFloat(font.split(' ')[1]) });
        if (name === 'font') return font;
        return () => undefined;
      },
      set: (_, name, value) => {
        if (name === 'font') font = value as string;
        return true;
      },
    },
  ) as unknown as CanvasRenderingContext2D;
  return { context, texts };
}

const palette: Palette = { ground: '#fff', line: '#000', text: '#000', secondary: '#666' };

function node(id: string, x: number): DiagramNode {
  const rect = { x, y: 0, width: 200, height: 80 };
  return {
    id,
    kind: 'goal',
    shape: { type: 'rectangle', rect },
    text: [
      { text: id, role: 'identifier', x, baseline: 12, width: 20, size: 12, weight: 400 },
      { text: `Statement of ${id}`, role: 'statement', x, baseline: 40, width: 100, size: 14, weight: 400 },
    ],
    decorators: [],
    bounds: rect,
  };
}

const diagram: Diagram = {
  nodes: [node('G1', 0), node('G2', 5000)],
  edges: [],
  bounds: { x: 0, y: 0, width: 5200, height: 80 },
};

describe('draw', () => {
  it('draws only what is in view', () => {
    const { context } = recorder();
    const drawn = draw(context, diagram, { scale: 1, offsetX: 0, offsetY: 0 }, palette, 'x', { width: 800, height: 600, dpr: 1 });
    expect(drawn).toEqual({ nodes: 1, edges: 0 });
  });

  it('draws only identifiers when the text would be under 7 px', () => {
    const near = recorder();
    draw(near.context, diagram, { scale: 1, offsetX: 0, offsetY: 0 }, palette, 'x', { width: 800, height: 600, dpr: 1 });
    expect(near.texts).toEqual(['G1', 'Statement of G1']);

    const far = recorder();
    draw(far.context, diagram, { scale: 0.4, offsetX: 0, offsetY: 0 }, palette, 'x', { width: 800, height: 600, dpr: 1 });
    expect(far.texts).toEqual(['G1']);
  });

  it('draws no letter decorator far out, and treats a node with only an identifier the same', () => {
    const assumption: DiagramNode = {
      ...node('A1', 0),
      kind: 'assumption',
      decorators: [{ type: 'letter', text: 'A', x: 190, baseline: 90, size: 12 }],
    };
    const bare: DiagramNode = { ...node('G9', 300), text: [node('G9', 300).text[0]] };
    const far = recorder();
    draw(far.context, { nodes: [assumption, bare], edges: [], bounds: diagram.bounds }, { scale: 0.4, offsetX: 0, offsetY: 0 }, palette, 'x', {
      width: 800,
      height: 600,
      dpr: 1,
    });
    expect(far.texts).toEqual(['A1', 'G9']);
  });
});
