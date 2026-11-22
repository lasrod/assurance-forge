// Which answer from the core the page shows. Answers come back in any order:
// the desktop's calls cross a process boundary, and a relayout (language,
// text sizes) can be overtaken by another relayout or by a new file.
//
// Two counters tell them apart. The revision names the document's state: it
// changes when a file is opened (and, with editing, on each change). The
// layout generation names one layout of it: it changes with the language,
// the text sizes and the fonts, which change the picture but not the
// argument. An answer is shown only if it is of the revision on screen and
// no older a layout than the one shown.

import type { DiagramAt } from './core/Core';

/**
 * The diagram to show after `next` arrives: `next` if it is of the revision
 * on screen (`revision`) and a newer layout than `current`, else `current`.
 * With no revision on screen, nothing new is shown: an open that failed may
 * leave an earlier document in the core, which must not come back.
 */
export function newerDiagram(current: DiagramAt | null, next: DiagramAt | null, revision: number | null): DiagramAt | null {
  if (!next || revision === null || next.revision !== revision) return current;
  if (current && current.revision === next.revision && next.layout <= current.layout) return current;
  return next;
}
