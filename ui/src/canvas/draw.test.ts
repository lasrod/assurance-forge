import { describe, expect, it } from 'vitest';
import type { Diagram, DiagramEdge, DiagramNode } from '../core/Core';
import {
  boundsOf,
  keepInPlace,
  reveal,
  draw,
  fitIdentifier,
  EDGE_REACH,
  edgeDistance,
  edgeKey,
  findEdge,
  fit,
  hitEdge,
  hitId,
  hitTest,
  insideShape,
  MAX_SCALE,
  MIN_SCALE,
  pan,
  toDiagram,
  zoomAbout,
  type Families,
  type Palette,
} from './draw';

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
  /** The line width at each stroke, and every dash pattern set. */
  const widths: number[] = [];
  const dashes: number[][] = [];
  let lineWidth = 1;
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
        if (name === 'stroke')
          return () => {
            strokes.push(strokeStyle);
            widths.push(lineWidth);
          };
        if (name === 'setLineDash') return (pattern: number[]) => dashes.push(pattern);
        return () => undefined;
      },
      set: (_, name, value) => {
        if (name === 'font') font = value as string;
        if (name === 'fillStyle') fillStyle = value as string;
        if (name === 'strokeStyle') strokeStyle = value as string;
        if (name === 'lineWidth') lineWidth = value as number;
        return true;
      },
    },
  ) as unknown as CanvasRenderingContext2D;
  return { context, texts, fonts, fills, strokes, widths, dashes };
}

const palette: Palette = {
  ground: '#fff',
  grid: '#ddd',
  fill: '#fefefe',
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

  it('far out, sets an identifier too wide for its node smaller, and cuts it short only past 9 px', () => {
    // The recorder measures a character as half the font size. A node is 200
    // wide, so 180 is the room; at 40% the 12 px identifier is a 30 px font.
    const named = (id: string): DiagramNode => ({ ...node('G1', 0), text: [{ ...node('G1', 0).text[0], text: id }] });
    const view = { scale: 0.4, offsetX: 0, offsetY: 0 };
    const at = { width: 800, height: 600, dpr: 1 };
    // 14 characters: 210 at 12 px, so set at 12 × 180 / 210 ≈ 10.3 px, whole.
    const smaller = recorder();
    draw(smaller.context, { nodes: [named('c_definition12')], edges: [], bounds: diagram.bounds }, view, palette, families, at);
    expect(smaller.texts).toEqual(['c_definition12']);
    expect(parseFloat(smaller.fonts[0].split(' ')[1])).toBeCloseTo((12 * 180) / 210 / 0.4, 3);
    // 30 characters: past 9 px, so its start and an ellipsis, never blank.
    const cut = recorder();
    draw(cut.context, { nodes: [named('REQ_SYS_BRAKE_CONTROLLER_0001')], edges: [], bounds: diagram.bounds }, view, palette, families, at);
    expect(cut.texts).toHaveLength(1);
    expect(cut.texts[0]).toMatch(/^REQ_SYS_BRAKE_.*…$/);
    expect(parseFloat(cut.fonts[0].split(' ')[1])).toBeCloseTo(9 / 0.4, 3);
  });

  it('cuts an identifier short only as far as it must', () => {
    const measure = (text: string) => Array.from(text).length;
    expect(fitIdentifier('G1', 5, measure)).toBe('G1');
    expect(fitIdentifier('ABCDEFGH', 5, measure)).toBe('ABCD…');
    expect(fitIdentifier('ABCDEFGH', 1, measure)).toBe('');
    // By character, not by UTF-16 unit.
    expect(fitIdentifier('論証ナビゲーター', 4, measure)).toBe('論証ナ…');
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
    // A node is filled with its own colour, not the ground's.
    expect(plain.fills).toEqual(['#fefefe']);

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

/** An edge along `points` to `tip`, its arrowhead pointing down. */
function edge(relationship: string, points: { x: number; y: number }[], tip: { x: number; y: number }, line: DiagramEdge['line'] = 'solid'): DiagramEdge {
  const all = [...points, tip];
  const xs = all.map((point) => point.x);
  const ys = all.map((point) => point.y);
  return {
    relationship,
    from: 'G1',
    to: 'G2',
    toRelationship: false,
    kind: line === 'dashed' ? 'challenge' : 'supportedBy',
    line,
    points,
    arrowhead: line === 'solid' ? 'filled' : 'open',
    head: [tip, { x: tip.x - 5, y: tip.y - 10 }, { x: tip.x + 5, y: tip.y - 10 }],
    bounds: { x: Math.min(...xs) - 5, y: Math.min(...ys), width: Math.max(...xs) - Math.min(...xs) + 10, height: Math.max(...ys) - Math.min(...ys) },
  };
}

describe('relationships', () => {
  // A bent line from (100, 80) down, across and down to (300, 200); and a second relationship beside it.
  const bent = edge('i1', [{ x: 100, y: 80 }, { x: 100, y: 140 }, { x: 300, y: 140 }, { x: 300, y: 190 }], { x: 300, y: 200 });
  const other = edge('k1', [{ x: 400, y: 80 }, { x: 400, y: 190 }], { x: 400, y: 200 }, 'dashed');
  const withEdges: Diagram = { nodes: [node('G1', 0)], edges: [bent, other], bounds: { x: 0, y: 0, width: 500, height: 200 } };
  const unit = { scale: 1, offsetX: 0, offsetY: 0 };

  it('measures the distance to any part of a line, and to its arrowhead', () => {
    expect(edgeDistance(bent, { x: 200, y: 143 })).toBeCloseTo(3);
    expect(edgeDistance(bent, { x: 100, y: 100 })).toBe(0);
    expect(edgeDistance(bent, { x: 300, y: 200 })).toBe(0);
    expect(edgeDistance(bent, { x: 200, y: 100 })).toBeCloseTo(40);
  });

  it('takes the line within reach of the pointer, on screen', () => {
    expect(hitEdge(withEdges, unit, 200, 140 + EDGE_REACH)?.relationship).toBe('i1');
    expect(hitEdge(withEdges, unit, 200, 141 + EDGE_REACH)).toBeNull();
    // Zoomed out to 25% the reach is still 5 px on screen: 20 in the diagram.
    const far = { scale: 0.25, offsetX: 0, offsetY: 0 };
    expect(hitEdge(withEdges, far, 50, 35 + 4)?.relationship).toBe('i1');
    expect(hitEdge(withEdges, far, 50, 35 + 7)).toBeNull();
  });

  it('takes the nearer of two lines', () => {
    const close = edge('i2', [{ x: 104, y: 80 }, { x: 104, y: 190 }], { x: 104, y: 200 });
    const both: Diagram = { ...withEdges, edges: [bent, close] };
    expect(hitEdge(both, unit, 101, 100)?.relationship).toBe('i1');
    expect(hitEdge(both, unit, 103, 100)?.relationship).toBe('i2');
  });

  it('puts a node before the line under it', () => {
    // (100, 79) is on G1 and within reach of the line that leaves its bottom edge.
    expect(hitId(withEdges, unit, 100, 79)).toBe('G1');
    expect(hitId(withEdges, unit, 100, 100)).toBe('i1 G1 G2');
    expect(hitId(withEdges, unit, 250, 300)).toBeNull();
  });

  // A second line of the same relationship, to another node.
  const second = { ...edge('i1', [{ x: 100, y: 80 }, { x: 100, y: 290 }], { x: 100, y: 300 }), to: 'G3' };
  const two: Diagram = { ...withEdges, edges: [bent, second] };

  it('names each line of a relationship apart, and bounds a node or one line', () => {
    expect(edgeKey(bent)).toBe('i1 G1 G2');
    expect(edgeKey(second)).toBe('i1 G1 G3');
    expect(findEdge(two, 'i1 G1 G3')).toBe(second);
    expect(findEdge(two, 'i1')).toBeNull();
    expect(boundsOf(two, 'G1')).toEqual({ x: 0, y: 0, width: 200, height: 80 });
    expect(boundsOf(two, 'i1 G1 G2')).toEqual(bent.bounds);
    expect(boundsOf(two, 'i1 G1 G3')).toEqual(second.bounds);
    expect(boundsOf(two, 'nothing')).toBeNull();
  });

  it('marks only the selected line of a relationship with several', () => {
    const drawn = recorder();
    draw(drawn.context, { ...two, nodes: [] }, unit, palette, families, { width: 800, height: 600, dpr: 1 }, { selected: 'i1 G1 G3', hovered: null });
    // The other line of i1 and its arrowhead stay plain; the selected one is drawn last.
    expect(drawn.strokes).toEqual(['#000', '#000', '#00f', '#00f']);
    expect(drawn.widths).toEqual([1, 1, 2.5, 2.5]);
  });

  it('draws a challenge dashed and a neutral edge dotted', () => {
    const dotted = edge('n1', [{ x: 450, y: 80 }, { x: 450, y: 190 }], { x: 450, y: 200 }, 'dotted');
    const drawn = recorder();
    draw(drawn.context, { ...withEdges, nodes: [], edges: [bent, other, dotted] }, unit, palette, families, { width: 800, height: 600, dpr: 1 });
    // Each edge sets its pattern, then clears it for the arrowhead.
    expect(drawn.dashes).toEqual([[], [], [6, 4], [], [1.5, 3.5], []]);
  });

  it('marks the selected line by colour and weight, and the hovered one by weight', () => {
    const drawn = recorder();
    draw(drawn.context, { ...withEdges, nodes: [] }, unit, palette, families, { width: 800, height: 600, dpr: 1 }, {
      selected: 'k1 G1 G2',
      hovered: 'i1 G1 G2',
    });
    // The hovered line and its arrowhead, then the selected ones, which are drawn last.
    expect(drawn.strokes).toEqual(['#000', '#000', '#00f', '#00f']);
    expect(drawn.widths).toEqual([2, 2, 2.5, 2.5]);

    const plain = recorder();
    draw(plain.context, { ...withEdges, nodes: [] }, unit, palette, families, { width: 800, height: 600, dpr: 1 });
    expect(plain.widths).toEqual([1, 1, 1, 1]);
  });
});

describe('reveal and keepInPlace', () => {
  const view = { scale: 2, offsetX: 10, offsetY: 20 };
  it('leaves a view that shows the bounds alone, and centres them otherwise', () => {
    expect(reveal(view, { x: 0, y: 0, width: 10, height: 10 }, 100, 100)).toBe(view);
    const moved = reveal(view, { x: 100, y: 0, width: 10, height: 10 }, 100, 100);
    expect(moved.scale).toBe(2);
    expect(100 * moved.scale + moved.offsetX).toBeCloseTo((100 - 20) / 2);
    expect(0 * moved.scale + moved.offsetY).toBeCloseTo((100 - 20) / 2);
  });
  it('keeps a node that moved in the layout where it was on the screen', () => {
    const kept = keepInPlace(view, { x: 5, y: 5, width: 1, height: 1 }, { x: 25, y: -5, width: 1, height: 1 });
    expect(25 * kept.scale + kept.offsetX).toBe(5 * view.scale + view.offsetX);
    expect(-5 * kept.scale + kept.offsetY).toBe(5 * view.scale + view.offsetY);
    expect(kept.scale).toBe(2);
  });
});
