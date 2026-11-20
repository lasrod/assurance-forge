// A SACM package class in words. The core sends the class's name, such as
// `ArgumentPackageInterface`; a class this does not know is shown by that name.

import type { MessageKey } from './messages';

const BASES: Record<string, MessageKey> = {
  AssuranceCasePackage: 'pkgAssuranceCase',
  ArgumentPackage: 'pkgArgument',
  ArtifactPackage: 'pkgArtifact',
  TerminologyPackage: 'pkgTerminology',
};

type T = (key: MessageKey, values?: Record<string, string | number>) => string;

export function packageKind(sacmClass: string, t: T): string {
  for (const [suffix, form] of [
    ['Interface', 'pkgInterface'],
    ['Binding', 'pkgBinding'],
    ['', null],
  ] as const) {
    if (!sacmClass.endsWith(suffix)) continue;
    const base = BASES[sacmClass.slice(0, sacmClass.length - suffix.length)];
    if (base) return form ? t(form, { kind: t(base) }) : t(base);
  }
  return sacmClass;
}
