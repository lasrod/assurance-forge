import { describe, expect, it } from 'vitest';
import type { AddChoice, ElementKind } from './core/Core';
import { addItems, refusalText } from './addMenu';
import { translate } from './i18n/messages';

const en = (key: Parameters<typeof translate>[1], values?: Record<string, string | number>) => translate('en', key, values);
const ja = (key: Parameters<typeof translate>[1], values?: Record<string, string | number>) => translate('ja', key, values);
const KINDS: ElementKind[] = ['goal', 'strategy', 'solution', 'context', 'assumption', 'justification'];
const choices = (refusal: string | null, develops = false): AddChoice[] =>
  KINDS.map((kind) => ({ kind, refusal, develops: develops && ['goal', 'strategy', 'solution'].includes(kind) }));

describe('the add menu', () => {
  it('lists the six kinds, and a leaf greys each out with the reason, naming the leaf', () => {
    const items = addItems(choices('AF-EDIT-001'), 'solution', en);
    expect(items.map((item) => item.label)).toEqual([
      'Add Goal',
      'Add Strategy',
      'Add Solution',
      'Add Context',
      'Add Assumption',
      'Add Justification',
    ]);
    expect(items.every((item) => item.unavailable)).toBe(true);
    expect(items[0].note).toBe('A Solution is a leaf in GSN: nothing goes under it.');
  });

  it('says a Strategy comes with its first sub-goal, and that support develops an undeveloped goal', () => {
    const items = addItems(choices(null, true), 'goal', en);
    expect(items.every((item) => !item.unavailable)).toBe(true);
    expect(items[0].note).toBe('Develops this goal.');
    expect(items[1].note).toBe('With its first sub-goal. Develops this goal.');
    expect(items[3].note).toBeNull();
  });

  it('words every code in both languages, and leaves an unknown one as it is', () => {
    for (let n = 1; n <= 8; n += 1) {
      const code = `AF-EDIT-00${n}`;
      expect(refusalText(code, 'goal', en)).not.toBe(code);
      expect(refusalText(code, 'goal', ja)).not.toBe(code);
    }
    expect(refusalText('AF-EDIT-099', null, en)).toBe('AF-EDIT-099');
    expect(addItems(choices(null), 'goal', ja)[0].label).toBe('ゴールを追加');
  });
});
