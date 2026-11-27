import { describe, expect, it } from 'vitest';
import type { AddChoice, ElementKind } from './core/Core';
import { addItems, refusalText } from './addMenu';
import { translate } from './i18n/messages';

const en = (key: Parameters<typeof translate>[1], values?: Record<string, string | number>) => translate('en', key, values);
const ja = (key: Parameters<typeof translate>[1], values?: Record<string, string | number>) => translate('ja', key, values);
const KINDS: ElementKind[] = ['goal', 'strategy', 'solution', 'context', 'assumption', 'justification'];
const choices = (refusal: (kind: ElementKind) => string | null): AddChoice[] =>
  KINDS.map((kind) => ({ kind, refusal: refusal(kind), develops: false }));

describe('the add menu', () => {
  it('lists the kinds that can be added, in the standard’s order, with no notes', () => {
    const items = addItems(choices(() => null), en);
    expect(items.map((item) => item.label)).toEqual([
      'Add Goal',
      'Add Strategy',
      'Add Solution',
      'Add Context',
      'Add Assumption',
      'Add Justification',
    ]);
    expect(items.every((item) => Object.keys(item).sort().join() === 'kind,label')).toBe(true);
  });

  it('leaves out what cannot be added: under a Strategy, a Strategy and a Solution', () => {
    const items = addItems(
      choices((kind) => (kind === 'strategy' || kind === 'solution' ? 'AF-EDIT-003' : null)),
      en,
    );
    expect(items.map((item) => item.kind)).toEqual(['goal', 'context', 'assumption', 'justification']);
    expect(addItems(choices(() => 'AF-EDIT-001'), en)).toEqual([]);
  });

  it('words every code in both languages, and leaves an unknown one as it is', () => {
    // To the last code the core sends (AF-EDIT-028, W32).
    for (let n = 1; n <= 28; n += 1) {
      const code = `AF-EDIT-${String(n).padStart(3, '0')}`;
      expect(refusalText(code, 'goal', en)).not.toBe(code);
      expect(refusalText(code, 'goal', ja)).not.toBe(code);
    }
    expect(refusalText('AF-EDIT-099', null, en)).toBe('AF-EDIT-099');
    expect(addItems(choices(() => null), ja)[0].label).toBe('ゴールを追加');
  });
});
