import { describe, expect, it } from 'vitest';
import { DEFAULT_LAYOUT, readLayout } from './layout';

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

  it('reads the earlier version, which kept the layout under `layout`', () => {
    expect(readLayout({ layout: { open: { start: false, end: true, bottom: true }, size: {} }, startView: 'x' }).open.start).toBe(false);
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
