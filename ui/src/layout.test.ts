import { describe, expect, it } from 'vitest';
import { DEFAULT_WORKSPACE, togglePanel } from './layout';

const open = DEFAULT_WORKSPACE.layout;

describe('togglePanel', () => {
  it('hides a stacked view on its own, and closes the dock with the last', () => {
    const one = togglePanel(open, 'caseExplorer', false, 'start');
    expect(one.open.start).toBe(true);
    expect(one.hidden).toEqual(['caseExplorer']);
    const both = togglePanel(one, 'argumentNavigator', false, 'start');
    expect(both.open.start).toBe(false);
    // Shown again the next time the dock opens.
    expect(both.hidden).toEqual([]);
  });

  it('opens a closed dock on the chosen view alone', () => {
    const closed = { ...open, open: { ...open.open, start: false } };
    const shown = togglePanel(closed, 'argumentNavigator', true, 'start');
    expect(shown.open.start).toBe(true);
    expect(shown.hidden).toEqual(['caseExplorer']);
  });

  it('shows a hidden view in an open dock beside the other', () => {
    const one = togglePanel(open, 'caseExplorer', false, 'start');
    expect(togglePanel(one, 'caseExplorer', true, 'start').hidden).toEqual([]);
  });

  it('opens and closes a dock of one panel', () => {
    expect(togglePanel(open, 'problems', false, 'bottom').open.bottom).toBe(false);
    expect(togglePanel(open, 'elementProperties', false, 'end').open.end).toBe(false);
  });
});
