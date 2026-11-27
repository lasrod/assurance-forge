import { describe, expect, it } from 'vitest';
import type { Diagram, DiagramEdge, DiagramNode } from '../core/Core';
import { badgeLabel, comesWith, EDGE, EDGE_SPEED, edgePan, readableAt } from './drag';
import { SMALLEST_TEXT_PX } from './draw';

const node = (id: string, size = 14): DiagramNode =>
  ({
    id,
    text: [
      { text: id.toUpperCase(), role: 'identifier', size: 12 },
      { text: 'A statement', role: 'statement', size },
    ],
  }) as unknown as DiagramNode;

const edge = (from: string, to: string, kind = 'supportedBy'): DiagramEdge => ({ from, to, kind }) as unknown as DiagramEdge;

const diagram = (edges: DiagramEdge[]): Diagram => ({ nodes: [], edges }) as unknown as Diagram;

describe('edgePan', () => {
  const size = { width: 800, height: 600 };

  it('does nothing away from the edges', () => {
    expect(edgePan({ x: 400, y: 300 }, size)).toEqual({ dx: 0, dy: 0 });
    expect(edgePan({ x: EDGE, y: 600 - EDGE }, size)).toEqual({ dx: 0, dy: 0 });
  });

  it('pans towards what is beyond the edge the pointer is near, faster the nearer it is', () => {
    // Near the left edge the diagram moves right, bringing in what is left of the view.
    const near = edgePan({ x: EDGE / 2, y: 300 }, size);
    const nearer = edgePan({ x: EDGE / 4, y: 300 }, size);
    expect(near.dx).toBeGreaterThan(0);
    expect(nearer.dx).toBeGreaterThan(near.dx);
    expect(near.dy).toBe(0);
    expect(edgePan({ x: 800 - EDGE / 2, y: 300 }, size).dx).toBe(-near.dx);
    expect(edgePan({ x: 400, y: 600 - EDGE / 2 }, size).dy).toBeLessThan(0);
  });

  it('is at full speed at the edge and beyond it', () => {
    expect(edgePan({ x: 0, y: 300 }, size).dx).toBe(EDGE_SPEED);
    expect(edgePan({ x: -40, y: 300 }, size).dx).toBe(EDGE_SPEED);
    expect(edgePan({ x: 400, y: 700 }, size).dy).toBe(-EDGE_SPEED);
  });
});

describe('comesWith', () => {
  const shown = diagram([edge('g', 's'), edge('s', 'a'), edge('s', 'b'), edge('a', 'sn'), edge('g', 'c', 'inContextOf'), edge('k', 'a', 'challenge')]);

  it('counts every node drawn under the node, at any depth', () => {
    // `k` challenges `a`, so it is drawn under it.
    expect(comesWith(shown, 'g')).toBe(6);
    expect(comesWith(shown, 'a')).toBe(2);
    expect(comesWith(shown, 'sn')).toBe(0);
  });

  it('counts a challenge of a line under the node the line hangs from', () => {
    // `k` challenges the line from `g` to `d`; `k2` challenges that challenge.
    const line = { ...edge('g', 'd'), relationship: 'id' } as DiagramEdge;
    const challenge = { ...edge('k', 'id', 'challenge'), relationship: 'ch', toRelationship: true } as DiagramEdge;
    const again = { ...edge('k2', 'ch', 'challenge'), relationship: 'ch2', toRelationship: true } as DiagramEdge;
    const shownLines = diagram([line, challenge, again]);
    expect(comesWith(shownLines, 'g')).toBe(3);
    expect(comesWith(shownLines, 'd')).toBe(0);
  });

  it('counts a node under two parents once, and ends in a cycle', () => {
    expect(comesWith(diagram([edge('g', 'a'), edge('g', 'b'), edge('a', 'x'), edge('b', 'x'), edge('x', 'g')]), 'g')).toBe(3);
  });
});

describe('badgeLabel', () => {
  it('is the identifier, and how many come with it', () => {
    expect(badgeLabel(node('g3'), 0)).toBe('G3');
    expect(badgeLabel(node('g3'), 4)).toBe('G3 + 4');
  });

  it('is the SACM id when the node shows no identifier', () => {
    expect(badgeLabel({ id: 'n1', text: [] } as unknown as DiagramNode, 0)).toBe('n1');
  });
});

describe('readableAt', () => {
  it('is whether the node’s text is drawn at the zoom', () => {
    const shown = node('g', 14);
    expect(readableAt(shown, 1)).toBe(true);
    expect(readableAt(shown, SMALLEST_TEXT_PX / 14)).toBe(true);
    expect(readableAt(shown, 0.3)).toBe(false);
  });
});
