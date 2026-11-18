import { describe, expect, it } from 'vitest';
import { DEFAULT_LAYOUT, readLayout } from './layout';

// The earlier version's `{ layout }` wrapper is unwrapped by loadLayout, by its key.

describe('readLayout', () => {
  it('reads a stored layout, hidden views and their sizes included', () => {
    const layout = {
      open: { start: true, end: false, bottom: true },
      size: { start: 20, end: null },
      hidden: ['caseExplorer'],
      viewSize: { argumentNavigator: 12 },
    };
    expect(readLayout(layout)).toEqual(layout);
  });

  it('drops what is not a size, and is the default for what is not a layout', () => {
    const read = readLayout({ open: { start: 'yes' }, size: { start: 'abc', end: -3, bottom: 9 }, viewSize: { a: {} } });
    expect(read.open).toEqual(DEFAULT_LAYOUT.open);
    expect(read.size).toEqual({ bottom: 9 });
    expect(read.viewSize).toEqual({});
    expect(readLayout('nonsense')).toEqual(DEFAULT_LAYOUT);
    expect(readLayout(null)).toEqual(DEFAULT_LAYOUT);
  });
});
