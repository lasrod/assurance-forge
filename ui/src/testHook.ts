// What the browser tests read about the page. Only a build made with
// `--mode e2e` has it: in any other build the condition is false at build time
// and the code is removed.

import type { DiagramAt } from './core/Core';
import type { Viewport } from './canvas/draw';

export interface TestState {
  revision: number | null;
  drawn: { nodes: number; edges: number };
  /** What was drawn from, to compare the browser's text widths with the core's. */
  diagram: DiagramAt | null;
  /** Diagram coordinates to canvas CSS px, as drawn. */
  viewport: Viewport | null;
  /** The selected node's SACM id, or the selected edge's key: its relationship and two ends, with spaces between. */
  selected: string | null;
  /** The node or the edge under the pointer, named the same way. */
  hovered: string | null;
  /** How many times the canvas was painted. */
  paints: number;
}

declare global {
  interface Window {
    __assuranceForge?: TestState;
  }
}

export function report(state: Omit<TestState, 'paints'>) {
  if (import.meta.env.MODE === 'e2e') {
    window.__assuranceForge = { ...state, paints: (window.__assuranceForge?.paints ?? 0) + 1 };
  }
}
