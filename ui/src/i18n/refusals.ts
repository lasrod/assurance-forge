// Why a file was not opened, worded from libSACM's refusal code and its
// arguments. A per-template catalogue (libSACM's docs/sacm/diagnostics.md
// lists about 200 templates) is a later step; until then a refusal is worded
// by its code, with the arguments that name what is wrong.

import type { Refusal } from '../core/Core';
import type { Language } from './messages';

type Reasons = Record<string, string>;

const en: Reasons = {
  'SACM-XML-001': 'The file is not well-formed XML.',
  'SACM-XML-002': 'The file is in an encoding this version does not read. Save it as UTF-8.',
  'SACM-SEC-001': 'The file has a DOCTYPE, which is refused before anything is read.',
  'SACM-SEC-002': 'The file is larger or deeper than this version can hold.',
  'SACM-SEC-003': 'The file passes a limit of what this version reads: its size, its depth, or how many elements, namespaces or attributes it has.',
  'SACM-XMI-001': 'The file does not start with a SACM 2.3 package.',
  'SACM-XMI-002': 'The file is not in the SACM 2.3 namespace.',
  'SACM-XMI-003': 'The file has content SACM 2.3 does not allow where it stands.',
  'SACM-XMI-005': 'An element has no type where SACM needs one.',
  'SACM-XMI-008': 'The file is in the namespace of another SACM revision. This version reads SACM 2.3.',
  'SACM-XMI-012': 'The file uses a shape of the C++ application, which a strict read refuses.',
  'SACM-XMI-015': 'An XMI element is not in the XMI namespace.',
  'SACM-ID-001': 'An id is used for more than one element.',
  'SACM-ID-002': 'An id is not a valid XML name.',
};

const ja: Reasons = {
  'SACM-XML-001': 'ファイルが整形式の XML ではありません。',
  'SACM-XML-002': 'このバージョンでは読めない文字コードです。UTF-8 で保存してください。',
  'SACM-SEC-001': 'ファイルに DOCTYPE があるため、読み込む前に拒否しました。',
  'SACM-SEC-002': 'ファイルがこのバージョンで扱える大きさや深さを超えています。',
  'SACM-SEC-003': 'ファイルがこのバージョンで読める上限（大きさ、深さ、要素・名前空間・属性の数）を超えています。',
  'SACM-XMI-001': 'ファイルが SACM 2.3 のパッケージで始まっていません。',
  'SACM-XMI-002': 'ファイルが SACM 2.3 の名前空間にありません。',
  'SACM-XMI-003': 'SACM 2.3 がその位置で認めない内容があります。',
  'SACM-XMI-005': 'SACM が型を必要とする要素に型がありません。',
  'SACM-XMI-008': 'ファイルが別の SACM 版の名前空間にあります。このバージョンが読むのは SACM 2.3 です。',
  'SACM-XMI-012': 'C++ アプリケーションの形式が使われており、厳密な読み込みでは拒否されます。',
  'SACM-XMI-015': 'XMI 要素が XMI の名前空間にありません。',
  'SACM-ID-001': '同じ id が複数の要素に使われています。',
  'SACM-ID-002': 'id が XML の名前として正しくありません。',
};

const fallback: Record<Language, string> = {
  en: 'The file was not read.',
  ja: 'ファイルを読み込めませんでした。',
};

const place: Record<Language, (line: number, column: number) => string> = {
  en: (line, column) => `line ${line}, column ${column}`,
  ja: (line, column) => `${line} 行 ${column} 列`,
};

/** Arguments whose values are libSACM's English prose, not names. */
const PROSE = new Set(['error', 'message', 'reason', 'detail', 'label']);

/** The refusal as a sentence: why, what it names, where, and the code. */
export function wordRefusal(refusal: Refusal, language: Language): string {
  const reason = (language === 'ja' ? ja : en)[refusal.code] ?? fallback[language];
  // What names the problem: ids, names, counts. Arguments that carry
  // libSACM's own English sentences are left out, as is anything long.
  const named = refusal.arguments
    .filter((argument) => !PROSE.has(argument.name))
    .filter((argument) => argument.value.type === 'count' || argument.value.value.length <= 80)
    .map((argument) => `${argument.name}: ${String(argument.value.value)}`);
  const parts = [reason];
  if (named.length > 0) parts.push(`(${named.join(', ')})`);
  const where = refusal.line !== null && refusal.column !== null ? ` ${place[language](refusal.line, refusal.column)}` : '';
  return `${parts.join(' ')} [${refusal.code}${where}]`;
}
