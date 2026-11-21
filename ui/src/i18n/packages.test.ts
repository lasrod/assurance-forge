import { describe, expect, it } from 'vitest';
import { translate, type MessageKey } from './messages';
import { packageKind } from './packages';

const en = (key: MessageKey, values?: Record<string, string | number>) => translate('en', key, values);
const ja = (key: MessageKey, values?: Record<string, string | number>) => translate('ja', key, values);

describe('packageKind', () => {
  it('words a SACM package class, its interface and its binding', () => {
    expect(packageKind('AssuranceCasePackage', en)).toBe('Assurance case');
    expect(packageKind('ArgumentPackage', en)).toBe('Argument');
    expect(packageKind('ArgumentPackageInterface', en)).toBe('Argument interface');
    expect(packageKind('TerminologyPackageBinding', en)).toBe('Terminology binding');
    expect(packageKind('ArtifactPackage', ja)).toBe('成果物');
    expect(packageKind('ArgumentPackageInterface', ja)).toBe('論証インターフェース');
  });

  it('shows a class it does not know by its name', () => {
    expect(packageKind('SomethingNew', en)).toBe('SomethingNew');
    expect(packageKind('Interface', en)).toBe('Interface');
  });
});
