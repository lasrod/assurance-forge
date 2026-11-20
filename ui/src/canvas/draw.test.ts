import { describe, expect, it } from 'vitest';
import type { Diagram, DiagramNode } from '../core/Core';
import { draw, fit, hitTest, insideShape, MAX_SCALE, MIN_SCALE, pan, toDiagram, zoomAbout, type Families, type Palette } from './draw';

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


/** A context that records the text drawn, and its font, and measures 0.5 em per character. */
function recorder() {
  const texts: string[] = [];
  const fonts: string[] = [];
  /** The fill style at each fill, and the stroke style at each stroke. */
  const fills: string[] = [];
  const strokes: string[] = [];
  let font = '10px x';
  let fillStyle = '';
  let strokeStyle = '';
  const context = new Proxy(
    {},
    {
      get: (_, name) => {
        if (name === 'fillText')
          return (text: string) => {
            texts.push(text);
            fonts.push(font);
          };
        if (name === 'measureText') return (text: string) => ({ width: text.length * 0.5 * parseFloat(font.split(' ')[1]) });
        if (name === 'font') return font;
        if (name === 'fill') return () => fills.push(fillStyle);
        if (name === 'stroke') return () => strokes.push(strokeStyle);
        return () => undefined;
      },
      set: (_, name, value) => {
        if (name === 'font') font = value as string;
        if (name === 'fillStyle') fillStyle = value as string;
        if (name === 'strokeStyle') strokeStyle = value as string;
        return true;
      },
    },
  ) as unknown as CanvasRenderingContext2D;
  return { context, texts, fonts, fills, strokes };
}

const palette: Palette = {
  ground: '#fff',
  line: '#000',
  text: '#000',
  secondary: '#666',
  hover: '#eee',
  selectedFill: '#def',
  selectedLine: '#00f',
  forced: false,
};
const families: Families = { text: 'sans', identifier: 'mono' };

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
    const drawn = draw(context, diagram, { scale: 1, offsetX: 0, offsetY: 0 }, palette, families, { width: 800, height: 600, dpr: 1 });
    expect(drawn).toEqual({ nodes: 1, edges: 0 });
  });

  it('draws only identifiers when the text would be under 7 px', () => {
    const near = recorder();
    draw(near.context, diagram, { scale: 1, offsetX: 0, offsetY: 0 }, palette, families, { width: 800, height: 600, dpr: 1 });
    expect(near.texts).toEqual(['G1', 'Statement of G1']);

    const far = recorder();
    draw(far.context, diagram, { scale: 0.4, offsetX: 0, offsetY: 0 }, palette, families, { width: 800, height: 600, dpr: 1 });
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
    draw(far.context, { nodes: [assumption, bare], edges: [], bounds: diagram.bounds }, { scale: 0.4, offsetX: 0, offsetY: 0 }, palette, families, {
      width: 800,
      height: 600,
      dpr: 1,
    });
    expect(far.texts).toEqual(['A1', 'G9']);
  });

  it('draws identifiers in the identifier family, near and far, and the rest in the text family', () => {
    const near = recorder();
    draw(near.context, diagram, { scale: 1, offsetX: 0, offsetY: 0 }, palette, families, { width: 800, height: 600, dpr: 1 });
    expect(near.fonts).toEqual(['400 12px mono', '400 14px sans']);

    const far = recorder();
    draw(far.context, diagram, { scale: 0.4, offsetX: 0, offsetY: 0 }, palette, families, { width: 800, height: 600, dpr: 1 });
    expect(far.fonts).toEqual(['400 30px mono']);
  });
});

describe('the view', () => {
  it('zooms about a point, which stays where it is', () => {
    const before = { scale: 1, offsetX: 10, offsetY: 20 };
    const after = zoomAbout(before, 2, 300, 200);
    expect(after.scale).toBe(2);
    expect(toDiagram(after, 300, 200)).toEqual(toDiagram(before, 300, 200));
  });

  it('keeps the zoom between 10% and 400%', () => {
    expect(zoomAbout({ scale: 3, offsetX: 0, offsetY: 0 }, 10, 0, 0).scale).toBe(MAX_SCALE);
    expect(zoomAbout({ scale: 0.2, offsetX: 0, offsetY: 0 }, 0.01, 0, 0).scale).toBe(MIN_SCALE);
  });

  it('pans by screen pixels', () => {
    expect(pan({ scale: 2, offsetX: 10, offsetY: 20 }, 5, -5)).toEqual({ scale: 2, offsetX: 15, offsetY: 15 });
  });
});

describe('hit testing', () => {
  const rect = { x: 0, y: 0, width: 100, height: 50 };
  const shaped = (shape: DiagramNode['shape']): DiagramNode => ({ ...node('N', 0), shape, bounds: rect });

  it('follows the shape, not its box', () => {
    const ellipse = shaped({ type: 'ellipse', rect });
    expect(insideShape(ellipse, { x: 50, y: 25 })).toBe(true);
    expect(insideShape(ellipse, { x: 3, y: 3 })).toBe(false);
    const parallelogram = shaped({ type: 'parallelogram', rect, skew: 20 });
    // The top left corner is outside the slanted edge, the bottom left inside.
    expect(insideShape(parallelogram, { x: 5, y: 2 })).toBe(false);
    expect(insideShape(parallelogram, { x: 5, y: 48 })).toBe(true);
    expect(insideShape(parallelogram, { x: 95, y: 48 })).toBe(false);
    expect(insideShape(shaped({ type: 'rectangle', rect }), { x: 101, y: 25 })).toBe(false);
  });

  it('finds the node under a canvas point through the viewport', () => {
    const viewport = { scale: 0.5, offsetX: 100, offsetY: 0 };
    // G1 spans 0 to 200 by 0 to 80 in the diagram: 100 to 200 by 0 to 40 on the canvas.
    expect(hitTest(diagram, viewport, 150, 20)?.id).toBe('G1');
    expect(hitTest(diagram, viewport, 50, 20)).toBeNull();
  });
});

describe('selected and hovered', () => {
  it('fill and outline the node, each its own way', () => {
    const plain = recorder();
    draw(plain.context, diagram, { scale: 1, offsetX: 0, offsetY: 0 }, palette, families, { width: 800, height: 600, dpr: 1 });
    expect(plain.fills).toEqual(['#fff']);

    const selected = recorder();
    draw(selected.context, diagram, { scale: 1, offsetX: 0, offsetY: 0 }, palette, families, { width: 800, height: 600, dpr: 1 }, {
      selected: 'G1',
      hovered: null,
    });
    expect(selected.fills).toEqual(['#def']);
    expect(selected.strokes).toEqual(['#00f']);

    const hovered = recorder();
    draw(hovered.context, diagram, { scale: 1, offsetX: 0, offsetY: 0 }, palette, families, { width: 800, height: 600, dpr: 1 }, {
      selected: null,
      hovered: 'G1',
    });
    expect(hovered.fills).toEqual(['#eee']);
    expect(hovered.strokes).toEqual(['#000']);
  });
});
