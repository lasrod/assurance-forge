import { describe, expect, it } from 'vitest';
import type { OutlineRow } from './core/Core';
import { ancestors, nest, outlineLabel, outlineTree, packageTree } from './outline';

const row = (id: string, level: number, more: Partial<OutlineRow> = {}): OutlineRow => ({
  id,
  level,
  kind: 'goal',
  undeveloped: false,
  identifier: id.toUpperCase(),
  name: `Name of ${id}`,
  statement: null,
  ...more,
});

describe('nest', () => {
  it('nests depth-first rows by level, and returns to an outer level', () => {
    const { nodes, parents, branches } = nest(
      [row('g1', 1), row('c1', 2), row('s1', 2), row('g2', 3), row('sn1', 4), row('g3', 3), row('g9', 1)],
      (r) => ({ label: r.id }),
    );
    const shape = (list: typeof nodes): unknown => list.map((n) => (n.children?.length ? [n.value, shape([...n.children])] : n.value));
    expect(shape(nodes)).toEqual([['g1', ['c1', ['s1', [['g2', ['sn1']], 'g3']]]], 'g9']);
    expect(parents.get('sn1')).toBe('g2');
    expect(parents.get('g9')).toBeNull();
    expect(branches).toEqual(['g1', 's1', 'g2']);
    expect(ancestors(parents, 'sn1')).toEqual(['g2', 's1', 'g1']);
    expect(ancestors(parents, 'g1')).toEqual([]);
  });

  it('gives nothing for no rows', () => {
    expect(nest([], () => ({ label: '' }))).toEqual({ nodes: [], parents: new Map(), branches: [] });
  });
});

describe('outlineTree', () => {
  it('calls a node by its name, else its statement, else its identifier', () => {
    expect(outlineLabel({ name: 'Top', statement: 'The system is safe', identifier: 'G1' })).toBe('Top');
    expect(outlineLabel({ name: null, statement: 'The system is safe', identifier: 'G1' })).toBe('The system is safe');
    expect(outlineLabel({ name: null, statement: null, identifier: 'G1' })).toBe('G1');
    expect(outlineLabel({ name: '', statement: '', identifier: 'G1' })).toBe('G1');
  });

  it('shows the identifier after the name, and typeahead finds either', () => {
    const [node] = outlineTree([row('g1', 1)]).nodes;
    expect(node).toMatchObject({ value: 'g1', label: 'Name of g1', textValue: 'G1 Name of g1' });
    expect(node?.meta).toMatchObject({ props: { children: 'G1' } });
    const [bare] = outlineTree([row('g1', 1, { name: null })]).nodes;
    expect(bare).toMatchObject({ label: 'G1', meta: undefined });
  });
});

describe('packageTree', () => {
  it('calls a package by its name, else its id, with its kind in words', () => {
    const { nodes } = packageTree(
      [
        { id: 'case', level: 1, kind: 'AssuranceCasePackage', name: 'Braking', elements: 1 },
        { id: 'arg', level: 2, kind: 'ArgumentPackage', name: null, elements: 23 },
      ],
      (kind) => `<${kind}>`,
    );
    expect(nodes[0]).toMatchObject({ label: 'Braking', meta: { props: { children: '<AssuranceCasePackage>' } } });
    expect(nodes[0]?.children?.[0]).toMatchObject({ label: 'arg', meta: { props: { children: '<ArgumentPackage>' } } });
  });
});
