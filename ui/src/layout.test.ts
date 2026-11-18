import { describe, expect, it } from 'vitest';
import { DEFAULT_WORKSPACE, togglePanel } from './layout';

const open = DEFAULT_WORKSPACE.layout;

describe('togglePanel', () => {
  it('hides a stacked view on its own, and closes the dock with the last, as DATUM\'s Hide does', () => {
    const one = togglePanel(open, 'caseExplorer', false, 'start');
    expect(one.open.start).toBe(true);
    expect(one.hidden).toEqual(['caseExplorer']);
    const both = togglePanel(one, 'argumentNavigator', false, 'start');
    expect(both.open.start).toBe(false);
    // What was hidden stays hidden: the rail reopens the dock on the last view.
    expect(both.hidden).toEqual(['caseExplorer']);
  });

  it('shows a view by taking it out of hidden, and opens the dock', () => {
    const closed = { ...open, open: { ...open.open, start: false }, hidden: ['caseExplorer'] };
    const shown = togglePanel(closed, 'caseExplorer', true, 'start');
    expect(shown.open.start).toBe(true);
    expect(shown.hidden).toEqual([]);
  });

  it('below the wide tier a start view goes with the dock that holds it', () => {
    // At the medium tier the start views are tabs in the end dock.
    const closed = togglePanel(open, 'caseExplorer', false, 'end');
    expect(closed.open.end).toBe(false);
    expect(closed.hidden ?? []).toEqual([]);
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
