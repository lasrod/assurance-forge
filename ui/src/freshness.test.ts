import { describe, expect, it } from 'vitest';
import type { DiagramAt } from './core/Core';
import { newerDiagram } from './freshness';

const at = (revision: number, layout: number): DiagramAt => ({
  revision,
  layout,
  package: null,
  diagram: { nodes: [], edges: [], bounds: { x: 0, y: 0, width: 0, height: 0 } },
});

describe('newerDiagram', () => {
  it('shows a newer layout of the revision on screen', () => {
    const shown = at(1, 2);
    const next = at(1, 3);
    expect(newerDiagram(shown, next, 1)).toBe(next);
  });

  it('keeps the newer layout when an older one arrives after it', () => {
    // The text size changed (layout 3), then the language (layout 4); the
    // language's answer came back first.
    const language = at(1, 4);
    const size = at(1, 3);
    expect(newerDiagram(language, size, 1)).toBe(language);
  });

  it('keeps what is shown when the same layout arrives again', () => {
    const shown = at(1, 3);
    expect(newerDiagram(shown, at(1, 3), 1)).toBe(shown);
  });

  it('drops a layout of another revision', () => {
    // A relayout of the file before, answered after a new file opened.
    const shown = at(2, 5);
    expect(newerDiagram(shown, at(1, 6), 2)).toBe(shown);
  });

  it('shows nothing new while no revision is on screen', () => {
    // A read failed: the core may still hold the file before it.
    expect(newerDiagram(null, at(1, 3), null)).toBeNull();
  });

  it('shows the first layout of the revision on screen', () => {
    const next = at(2, 1);
    expect(newerDiagram(null, next, 2)).toBe(next);
    expect(newerDiagram(at(1, 9), next, 2)).toBe(next);
  });
});
