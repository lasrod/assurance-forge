import { describe, expect, it } from 'vitest';
import type { Problem } from '../core/Core';
import { problemMeta, wordProblem } from './problems';

const element = (id: string, identifier: string | null, kind: Problem['elements'][number]['kind']) => ({ id, identifier, kind });

const problem = (fields: Partial<Problem>): Problem => ({
  source: 'gsn',
  severity: 'error',
  code: 'AF-GSN-004',
  requirement: 'GSN3-CORE-015',
  template: null,
  arguments: [],
  elements: [],
  relationship: null,
  line: null,
  column: null,
  repairs: [],
  ...fields,
});

describe('problem words', () => {
  it('names the elements of a GSN problem by kind and identifier, in either language', () => {
    const contextual = problem({
      elements: [element('g', 'G1', 'goal'), element('a', 'A1', 'assumption')],
    });
    expect(wordProblem(contextual, 'en')).toBe(
      'Goal G1 is supported by Assumption A1, but in GSN a Context, Assumption or Justification is joined by InContextOf, not SupportedBy.',
    );
    expect(wordProblem(contextual, 'ja')).toContain('ゴール G1 は 前提 A1 に支持されています');
  });

  it('words a cycle of one and of several', () => {
    const one = problem({ code: 'AF-GSN-001', elements: [element('g', 'G1', 'goal')] });
    expect(wordProblem(one, 'en')).toBe('Circular support: Goal G1 supports itself, so it establishes nothing.');
    const three = problem({
      code: 'AF-GSN-001',
      elements: [element('a', 'G1', 'goal'), element('s', 'S1', 'strategy'), element('b', 'G2', 'goal')],
    });
    expect(wordProblem(three, 'en')).toBe('Circular support: Goal G1, Strategy S1, Goal G2 support each other, so none of them is established.');
  });

  it('names an element the diagram does not draw by its SACM id', () => {
    const unplaced = problem({ code: 'AF-VIEW-003', source: 'diagram', severity: 'info', elements: [element('ref_9', null, null)] });
    expect(wordProblem(unplaced, 'en')).toBe('ref_9 is neither plainly evidence nor plainly context: it is drawn neutral.');
  });

  it('words a libSACM code with the arguments that name what is wrong, and an unknown one by a general sentence', () => {
    const known = problem({
      code: 'SACM-MULT-001',
      source: 'sacm',
      severity: 'warning',
      template: 'SACM-MULT-001/too-many',
      arguments: [
        { name: 'id', value: { type: 'text', value: 'cl_1' } },
        { name: 'count', value: { type: 'count', value: 2 } },
      ],
    });
    expect(wordProblem(known, 'en')).toBe('An element has more or fewer of something than SACM 2.3 allows. (id: cl_1, count: 2)');
    const unknown = problem({ code: 'SACM-XYZ-999', source: 'read', severity: 'warning' });
    expect(wordProblem(unknown, 'ja')).toBe('libSACM がファイルについて指摘しました。');
  });

  it('names the end that cannot be placed, and words several Strategies in the plural', () => {
    const end = problem({
      code: 'AF-VIEW-009',
      source: 'diagram',
      severity: 'info',
      elements: [element('r1', null, null)],
      arguments: [{ name: 'end', value: { type: 'text', value: 'missing_1' } }],
    });
    expect(wordProblem(end, 'en')).toBe('The end missing_1 of the relationship r1 cannot be placed in the diagram.');
    const both = problem({ code: 'AF-GSN-007', elements: [element('s1', 'S1', 'strategy'), element('s2', 'S2', 'strategy')] });
    expect(wordProblem(both, 'en')).toMatch(/^Strategy S1, Strategy S2 are premises or conclusions/);
    const one = problem({ code: 'AF-GSN-007', elements: [element('s1', 'S1', 'strategy')] });
    expect(wordProblem(one, 'en')).toMatch(/^Strategy S1 is a premise or conclusion/);
  });

  it('shows the code, the requirement and the line as meta', () => {
    expect(problemMeta(problem({}), 'en')).toBe('AF-GSN-004 · GSN3-CORE-015');
    expect(problemMeta(problem({ code: 'SACM-XMI-004', requirement: null, line: 5 }), 'ja')).toBe('SACM-XMI-004 · 5 行');
  });

  it('has words for every GSN rule in both languages', () => {
    for (let n = 1; n <= 10; n += 1) {
      const code = `AF-GSN-${String(n).padStart(3, '0')}`;
      const twoElements = problem({ code, elements: [element('x', 'X1', 'goal'), element('y', 'Y1', 'goal')] });
      for (const language of ['en', 'ja'] as const) {
        const words = wordProblem(twoElements, language);
        expect(words, `${code} ${language}`).not.toMatch(/\{\w+\}/);
        expect(words, `${code} ${language}`).not.toBe(wordProblem(problem({ code: 'NONE' }), language));
      }
    }
  });
});
