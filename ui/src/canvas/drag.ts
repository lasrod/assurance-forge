// Moving a node by dragging it: what does not need the canvas. A drag on a
// node moves it, with what is under it; DiagramCanvas.tsx follows the pointer
// and App.tsx asks the core where the node may go.

import type { Diagram, DiagramNode } from '../core/Core';
import { SMALLEST_TEXT_PX } from './draw';

/** How long a touch is held on a node before a drag moves it and does not pan, in milliseconds. */
export const HOLD_MS = 300;
/** The pointer this near an edge of the canvas, in CSS px, pans the view while a node is dragged. */
export const EDGE = 56;
/** How far the view pans in a frame, in CSS px, with the pointer at the edge. */
export const EDGE_SPEED = 16;

/** Where a drag of a node ended: on a node, on the empty canvas, or given up. */
export type DragEnd = { on: 'node'; id: string } | { on: 'canvas' } | { on: 'cancel' };

/**
 * How far the view pans in a frame with the pointer at `point` in a canvas of
 * `size`, both in CSS px: nothing away from the edges, more the nearer one
 * is, and the full speed at it or beyond.
 */
export function edgePan(point: { x: number; y: number }, size: { width: number; height: number }): { dx: number; dy: number } {
  const push = (distance: number) => (distance < EDGE ? ((EDGE - Math.max(distance, 0)) / EDGE) * EDGE_SPEED : 0);
  // A positive pan moves the diagram right and down, bringing into view what is left and above.
  return { dx: push(point.x) - push(size.width - point.x), dy: push(point.y) - push(size.height - point.y) };
}

/** How many nodes the diagram draws under the node `id`, at any depth: what a move takes with it. */
export function comesWith(diagram: Diagram, id: string): number {
  const under = new Set([id]);
  for (let grew = true; grew; ) {
    grew = false;
    for (const edge of diagram.edges) {
      // A challenge is drawn under what it challenges.
      const [parent, child] = edge.kind === 'challenge' ? [edge.to, edge.from] : [edge.from, edge.to];
      if (under.has(parent) && !under.has(child)) {
        under.add(child);
        grew = true;
      }
    }
  }
  return under.size - 1;
}

/** What follows the pointer: the node's identifier, and how many come with it. */
export function badgeLabel(node: DiagramNode, more: number): string {
  const identifier = node.text.find((line) => line.role === 'identifier')?.text ?? node.id;
  return more > 0 ? `${identifier} + ${more}` : identifier;
}

/**
 * Whether a node's text is drawn at this zoom. Where it is not, a drag on
 * the node pans: a node that cannot be read is not moved by a slip of the
 * hand.
 */
export function readableAt(node: DiagramNode, scale: number): boolean {
  const sized = node.text.find((line) => line.role !== 'identifier') ?? node.text[0];
  return sized === undefined || sized.size * scale >= SMALLEST_TEXT_PX;
}
