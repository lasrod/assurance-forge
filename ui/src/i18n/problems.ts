// A problem in words (plan decision P5, docs/roadmap/milestone-1-problems.md).
// The core sends a code, its arguments and the elements it is about; this
// words it in the interface language. A GSN code names its elements by kind
// and identifier, as the diagram shows them. libSACM's codes are worded by
// code, as a refusal is, with the arguments that name what is wrong.

import type { NodeKind, Problem, ProblemElement } from '../core/Core';
import type { Language } from './messages';

type Words = Record<string, string>;

const en: Words = {
  'AF-GSN-001': 'Circular support: {all} support each other, so none of them is established.',
  'AF-GSN-001-self': 'Circular support: {a} supports itself, so it establishes nothing.',
  'AF-GSN-002': '{a} is supported by {b}, but in GSN nothing goes under a Solution, Context, Assumption or Justification.',
  'AF-GSN-003': '{a} has the context {b}, but in GSN nothing is joined to a Solution, Context, Assumption or Justification.',
  'AF-GSN-004': '{a} is supported by {b}, but in GSN a Context, Assumption or Justification is joined by InContextOf, not SupportedBy.',
  'AF-GSN-005': '{a} has {b} as its context, but in GSN only a Context, Assumption or Justification is context.',
  'AF-GSN-006': '{a} is supported by {b}, but in GSN a Strategy is supported only by Goals.',
  'AF-GSN-007': '{all} is a premise or conclusion of a relationship, but in GSN a Strategy is the reasoning between a Goal and its sub-goals.',
  'AF-GSN-007-many': '{all} are premises or conclusions of a relationship, but in GSN a Strategy is the reasoning between a Goal and its sub-goals.',
  'AF-GSN-008': '{a} supports nothing: a Strategy needs a Goal under it.',
  'AF-GSN-009': '{all} share the ID {identifier}: in GSN each element has its own.',
  'AF-GSN-010': '{a} is marked undeveloped but is supported: the mark or the support is out of date.',
  'AF-VIEW-001': '{a}: the GSN type the file records for it does not agree with what it is, so it is not used.',
  'AF-VIEW-002': '{a} is declared defeated or as cited, which the diagram does not draw yet: it is drawn neutral.',
  'AF-VIEW-003': '{a} is neither plainly evidence nor plainly context: it is drawn neutral.',
  'AF-VIEW-004': '{a} is a citation, which the diagram draws neutral until modules are drawn.',
  'AF-VIEW-005': '{a} is abstract (uninstantiated), which the diagram does not mark yet.',
  'AF-VIEW-006': '{a} is of a kind the diagram does not draw.',
  'AF-VIEW-008': 'The declaration of the relationship {a} is not drawn.',
  'AF-VIEW-009': 'An end of the relationship {a} cannot be placed in the diagram.',
  'AF-VIEW-009-end': 'The end {end} of the relationship {a} cannot be placed in the diagram.',
  'AF-VIEW-010': 'The relationship {a} is between artifacts, which the diagram does not draw.',
  'AF-VIEW-011': 'The relationship {a} is defeated: it is drawn neutral.',
  'AF-VIEW-012': 'The meta-claims of {a} are not drawn.',
  'AF-VIEW-013': 'The structure of the Strategy {a} is not drawn.',
  'SACM-ABS-001': 'An element is abstract where SACM 2.3 does not allow it, or the other way round.',
  'SACM-CITE-001': 'An element cites another but is not marked as a citation.',
  'SACM-CITE-002': 'An assertion is declared as cited but does not cite an assertion.',
  'SACM-EXPR-001': 'A text has both an expression and a content, and SACM 2.3 says it has one or the other.',
  'SACM-ID-002': 'An id is not a valid XML name.',
  'SACM-ID-003': 'Elements share a gid, which must be unique.',
  'SACM-ID-004': 'An id has white space around it, which is read and saved without it.',
  'SACM-MULT-001': 'An element has more or fewer of something than SACM 2.3 allows.',
  'SACM-PKG-001': 'A package holds an element SACM 2.3 does not allow in it.',
  'SACM-PKG-002': 'An interface is not inside the package it implements.',
  'SACM-REF-001': 'A reference names no element of the file.',
  'SACM-REF-002': 'A reference names an element of the wrong kind.',
  'SACM-REF-003': 'A reference names content the file keeps but this version does not read, so it is not checked.',
  'SACM-XMI-004': 'An element has no id: it is given one, which a save writes.',
};

const ja: Words = {
  'AF-GSN-001': '循環した支持：{all} が互いを支持しているため、どれも立証されません。',
  'AF-GSN-001-self': '循環した支持：{a} が自分自身を支持しているため、何も立証しません。',
  'AF-GSN-002': '{a} は {b} に支持されていますが、GSN では{kindA}は末端です。下には何も置けません。',
  'AF-GSN-003': '{a} にはコンテキスト {b} がありますが、GSN では{kindA}は末端です。何も結び付けられません。',
  'AF-GSN-004': '{a} は {b} に支持されていますが、{kindB}はコンテキストを与えるものです。GSN では SupportedBy ではなく InContextOf で結びます。',
  'AF-GSN-005': '{a} のコンテキストが {b} ですが、GSN でコンテキストになるのはコンテキスト、前提、正当化だけです。',
  'AF-GSN-006': '{a} は {b} に支持されていますが、GSN では戦略を支えられるのはゴールだけです。',
  'AF-GSN-007': '{all} が関係の前提または結論になっていますが、GSN では戦略はゴールとサブゴールの間の推論です。',
  'AF-GSN-007-many': '{all} が関係の前提または結論になっていますが、GSN では戦略はゴールとサブゴールの間の推論です。',
  'AF-GSN-008': '{a} は何も支持していません。戦略の下にはゴールが必要です。',
  'AF-GSN-009': '{all} が ID {identifier} を共有しています。GSN では要素ごとに固有の ID を持ちます。',
  'AF-GSN-010': '{a} は未展開の印が付いていますが、支持されています。印か支持のどちらかが古くなっています。',
  'AF-VIEW-001': '{a}：ファイルに記録された GSN の種類が要素と一致しないため、使っていません。',
  'AF-VIEW-002': '{a} は反駁済みまたは引用として宣言されており、図ではまだ描けないため中立の形で描いています。',
  'AF-VIEW-003': '{a} は証拠ともコンテキストとも明確でないため、中立の形で描いています。',
  'AF-VIEW-004': '{a} は引用です。モジュールを描けるようになるまで中立の形で描いています。',
  'AF-VIEW-005': '{a} は抽象（未具体化）ですが、図ではまだ印を付けていません。',
  'AF-VIEW-006': '{a} は図で描かない種類の要素です。',
  'AF-VIEW-008': '関係 {a} の宣言は描いていません。',
  'AF-VIEW-009': '関係 {a} の端を図に置けません。',
  'AF-VIEW-009-end': '関係 {a} の端 {end} を図に置けません。',
  'AF-VIEW-010': '関係 {a} は成果物どうしの関係で、図では描きません。',
  'AF-VIEW-011': '関係 {a} は反駁済みのため、中立の形で描いています。',
  'AF-VIEW-012': '{a} のメタ主張は描いていません。',
  'AF-VIEW-013': '戦略 {a} の構造は描いていません。',
  'SACM-ABS-001': 'SACM 2.3 が認めない箇所で要素が抽象になっている、またはその逆です。',
  'SACM-CITE-001': '要素が別の要素を引用していますが、引用として示されていません。',
  'SACM-CITE-002': '主張が引用として宣言されていますが、主張を引用していません。',
  'SACM-EXPR-001': '文に式と内容の両方がありますが、SACM 2.3 ではどちらか一方です。',
  'SACM-ID-002': 'ID が有効な XML 名ではありません。',
  'SACM-ID-003': '要素が gid を共有していますが、gid は一意でなければなりません。',
  'SACM-ID-004': 'ID の前後に空白があり、空白を除いて読み込み、保存します。',
  'SACM-MULT-001': '要素が持つものの数が SACM 2.3 の認める数と合いません。',
  'SACM-PKG-001': 'パッケージに SACM 2.3 が認めない要素があります。',
  'SACM-PKG-002': 'インターフェースが実装するパッケージの中にありません。',
  'SACM-REF-001': '参照がファイル内のどの要素も指していません。',
  'SACM-REF-002': '参照が種類の異なる要素を指しています。',
  'SACM-REF-003': '参照が、ファイルには残っているがこのバージョンでは読めない内容を指しているため、確認していません。',
  'SACM-XMI-004': 'ID のない要素があります。ID を付けて読み込み、保存時に書き出します。',
};

const fallback: Record<Language, string> = {
  en: 'libSACM reported a finding about the file.',
  ja: 'libSACM がファイルについて指摘しました。',
};

const KINDS: Record<Language, Record<NodeKind, string>> = {
  en: {
    goal: 'Goal',
    strategy: 'Strategy',
    solution: 'Solution',
    context: 'Context',
    assumption: 'Assumption',
    justification: 'Justification',
    neutral: 'element',
  },
  ja: {
    goal: 'ゴール',
    strategy: '戦略',
    solution: 'ソリューション',
    context: 'コンテキスト',
    assumption: '前提',
    justification: '正当化',
    neutral: '要素',
  },
};

const LIST: Record<Language, string> = { en: ', ', ja: '、' };

/** Arguments whose values are libSACM's English prose, not names. */
const PROSE = new Set(['error', 'message', 'reason', 'detail', 'label']);

/** An element as the diagram shows it: its kind and identifier, or its SACM id when it is not drawn. */
export function elementName(element: ProblemElement, language: Language): string {
  if (element.identifier === null) return element.id;
  return element.kind ? `${KINDS[language][element.kind]} ${element.identifier}` : element.identifier;
}

/** The problem in words, without its code: what the Problems panel shows. */
export function wordProblem(problem: Problem, language: Language): string {
  const words = language === 'ja' ? ja : en;
  const [a, b] = problem.elements;
  const end = problem.arguments.find((argument) => argument.name === 'end')?.value.value;
  const key = variant(problem, end !== undefined);
  const known = words[key];
  if (known !== undefined) {
    const values: Record<string, string> = {
      a: a ? elementName(a, language) : '',
      b: b ? elementName(b, language) : '',
      kindA: KINDS[language][a?.kind ?? 'neutral'],
      kindB: KINDS[language][b?.kind ?? 'neutral'],
      all: problem.elements.map((element) => elementName(element, language)).join(LIST[language]),
      identifier: a?.identifier ?? a?.id ?? '',
      end: end === undefined ? '' : String(end),
    };
    const sentence = known.replace(/\{(\w+)\}/g, (_, name: string) => values[name] ?? `{${name}}`);
    // libSACM's own codes are general; the arguments say what is wrong.
    return problem.code.startsWith('SACM-') ? withArguments(sentence, problem) : sentence;
  }
  return withArguments(fallback[language], problem);
}

/** Which form of the code's words fits: one element or several, an end named or not. */
function variant(problem: Problem, hasEnd: boolean): string {
  const many = problem.elements.length > 1;
  if (problem.code === 'AF-GSN-001' && !many) return 'AF-GSN-001-self';
  if (problem.code === 'AF-GSN-007' && many) return 'AF-GSN-007-many';
  if (problem.code === 'AF-VIEW-009' && hasEnd) return 'AF-VIEW-009-end';
  return problem.code;
}

/** `sentence`, then the arguments that name what is wrong: ids, names, counts. */
function withArguments(sentence: string, problem: Problem): string {
  const named = problem.arguments
    .filter((argument) => !PROSE.has(argument.name))
    .filter((argument) => argument.value.type === 'count' || argument.value.value.length <= 80)
    .map((argument) => `${argument.name}: ${String(argument.value.value)}`);
  return named.length > 0 ? `${sentence} (${named.join(', ')})` : sentence;
}

/** The code, its requirement and, for what was read, where in the file: the row's meta. */
export function problemMeta(problem: Problem, language: Language): string {
  const parts = [problem.code];
  if (problem.requirement) parts.push(problem.requirement);
  if (problem.line !== null) parts.push(language === 'ja' ? `${problem.line} 行` : `line ${problem.line}`);
  return parts.join(' · ');
}
