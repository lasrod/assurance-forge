import { describe, expect, it } from 'vitest';
import type { RecentFile } from './Core';
import { RECENT_LIMIT, withRecent } from './webRecent';

const file = (id: string, openedAt: number): RecentFile => ({ id, name: id, location: null, openedAt, nodes: 1, edges: 0 });

describe('withRecent', () => {
  it('puts the file opened last first, once', () => {
    const { keep, drop } = withRecent([file('a', 1), file('b', 2)], file('a', 3));
    expect(keep.map((kept) => [kept.id, kept.openedAt])).toEqual([
      ['a', 3],
      ['b', 2],
    ]);
    expect(drop).toEqual([]);
  });

  it('keeps the latest ten and names the rest to drop', () => {
    const before = Array.from({ length: RECENT_LIMIT }, (_, n) => file(`f${n}`, n));
    const { keep, drop } = withRecent(before, file('new', 100));
    expect(keep).toHaveLength(RECENT_LIMIT);
    expect(keep[0]?.id).toBe('new');
    expect(drop.map((dropped) => dropped.id)).toEqual(['f0']);
  });
});
