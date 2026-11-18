import { describe, expect, it } from 'vitest';
import { wordRefusal } from './refusals';

describe('wordRefusal', () => {
  const duplicate = {
    code: 'SACM-ID-001',
    template: 'SACM-ID-001/duplicate-id',
    arguments: [{ name: 'id', value: { type: 'text' as const, value: 'twice' } }],
    line: 6,
    column: 5,
  };

  it('says why, what it names, where, and the code', () => {
    expect(wordRefusal(duplicate, 'en')).toBe('An id is used for more than one element. (id: twice) [SACM-ID-001 line 6, column 5]');
    expect(wordRefusal(duplicate, 'ja')).toBe('同じ id が複数の要素に使われています。 (id: twice) [SACM-ID-001 6 行 5 列]');
  });

  it('falls back for a code it does not word, and leaves out what has no place', () => {
    const unknown = { code: 'SACM-NEW-001', template: 'SACM-NEW-001/x', arguments: [], line: null, column: null };
    expect(wordRefusal(unknown, 'en')).toBe('The file was not read. [SACM-NEW-001]');
  });
});
