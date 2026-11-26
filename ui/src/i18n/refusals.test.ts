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

  it('shows counts and leaves out libSACM\'s English sentences', () => {
    const refusal = {
      code: 'SACM-SEC-002',
      template: 'SACM-SEC-002/unheld-element',
      arguments: [
        { name: 'element', value: { type: 'text' as const, value: 'x' } },
        { name: 'error', value: { type: 'text' as const, value: 'the element could not be held' } },
        { name: 'unit', value: { type: 'count' as const, value: 55296 } },
      ],
      line: 1,
      column: 2,
    };
    expect(wordRefusal(refusal, 'ja')).toBe('ファイルがこのバージョンで扱える大きさや深さを超えています。 (element: x, unit: 55296) [SACM-SEC-002 1 行 2 列]');
  });

  it('words the refusals libSACM 0.4.0 added', () => {
    const limit = { code: 'SACM-SEC-003', template: 'SACM-SEC-003/depth', arguments: [], line: 2, column: 1 };
    expect(wordRefusal(limit, 'en')).toMatch(/^The file passes a limit of what this version reads/);
    const revision = { code: 'SACM-XMI-008', template: 'SACM-XMI-008/other-revision-root', arguments: [], line: null, column: null };
    expect(wordRefusal(revision, 'ja')).toBe('ファイルが別の SACM 版の名前空間にあります。このバージョンが読むのは SACM 2.3 です。 [SACM-XMI-008]');
  });

  it('falls back for a code it does not word, and leaves out what has no place', () => {
    const unknown = { code: 'SACM-NEW-001', template: 'SACM-NEW-001/x', arguments: [], line: null, column: null };
    expect(wordRefusal(unknown, 'en')).toBe('The file was not read. [SACM-NEW-001]');
  });
});
