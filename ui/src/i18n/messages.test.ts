import { describe, expect, it } from 'vitest';
import { initialLanguage, translate } from './messages';

describe('messages', () => {
  it('fills the values in either language', () => {
    expect(translate('en', 'opened', { nodes: 8, edges: 7 })).toBe('8 nodes, 7 edges');
    expect(translate('ja', 'opened', { nodes: 8, edges: 7 })).toBe('ノード 8 個、エッジ 7 本');
  });

  it('chooses the language from the address, then the browser', () => {
    expect(initialLanguage('?lang=ja', ['en-GB'])).toBe('ja');
    expect(initialLanguage('', ['ja-JP', 'en'])).toBe('ja');
    expect(initialLanguage('', ['sv'])).toBe('en');
    expect(initialLanguage('?lang=fr', [])).toBe('en');
  });
});
