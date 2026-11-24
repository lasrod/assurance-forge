// From the core's depth-first rows to the nodes DATUM's Tree takes. A row's
// parent is the nearest row before it with a smaller level.

import { createElement, type ReactNode } from 'react';

import type { OutlineRow, Package } from './core/Core';
import type { TreeNode } from './design';

interface Levelled {
  id: string;
  level: number;
}

interface Built {
  nodes: TreeNode<string>[];
  /** Each row's parent id, or null for a root. */
  parents: Map<string, string | null>;
  /** The ids of the rows that have children. */
  branches: string[];
}

/** Nests depth-first rows by their level. `node` gives a row's own part of its tree node. */
export function nest<R extends Levelled>(rows: readonly R[], node: (row: R) => Omit<TreeNode<string>, 'value' | 'children'>): Built {
  const nodes: TreeNode<string>[] = [];
  const parents = new Map<string, string | null>();
  const branches: string[] = [];
  // The open ancestors of the row being read, outermost first.
  const path: Array<{ id: string; level: number; children: TreeNode<string>[] }> = [];
  for (const row of rows) {
    while (path.length > 0 && path[path.length - 1]!.level >= row.level) path.pop();
    const parent = path[path.length - 1];
    const children: TreeNode<string>[] = [];
    const made: TreeNode<string> = { ...node(row), value: row.id, children };
    (parent ? parent.children : nodes).push(made);
    parents.set(row.id, parent ? parent.id : null);
    if (parent && parent.children.length === 1) branches.push(parent.id);
    path.push({ id: row.id, level: row.level, children });
  }
  return { nodes, parents, branches };
}

/**
 * A row's trailing text, kept on one line: an identifier is never broken, and a
 * kind in Japanese would otherwise shrink to one character a line. The name
 * beside it wraps instead.
 */
const trailing = (text: string): ReactNode => createElement('span', { className: 'af-tree-meta' }, text);

/**
 * A row's name, which the tree keeps on one line: the whole of it is its
 * tooltip, for when the dock cuts it short.
 */
const named = (text: string): ReactNode => createElement('span', { title: text }, text);

/** What a node is called in a list: its name, else its statement, else its identifier. */
export function outlineLabel(row: Pick<OutlineRow, 'name' | 'statement' | 'identifier'>): string {
  return row.name || row.statement || row.identifier;
}

/** The argument's tree: each node by its name, with its GSN identifier after it. */
export function outlineTree(rows: readonly OutlineRow[]): Built {
  return nest(rows, (row) => {
    const label = outlineLabel(row);
    // An element with neither name nor statement is called by its identifier, once.
    return { label: named(label), textValue: `${row.identifier} ${label}`, meta: label === row.identifier ? undefined : trailing(row.identifier) };
  });
}

/** The file's packages: each by its name, with its kind in words after it. */
export function packageTree(packages: readonly Package[], kind: (sacmClass: string) => string): Built {
  return nest(packages, (item) => {
    const label = item.name || item.id;
    return { label: named(label), textValue: label, meta: trailing(kind(item.kind)) };
  });
}

/** The ids from `id`'s parent up to its root, nearest first. */
export function ancestors(parents: ReadonlyMap<string, string | null>, id: string): string[] {
  const found: string[] = [];
  let at = parents.get(id) ?? null;
  while (at !== null && !found.includes(at)) {
    found.push(at);
    at = parents.get(at) ?? null;
  }
  return found;
}
