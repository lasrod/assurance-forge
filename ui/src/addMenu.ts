// The add menu's items: the six kinds the core lists for a node or the empty
// canvas, worded. A kind that cannot be added there is shown and unavailable,
// with the reason; one that can says what else it does.

import type { AddChoice, ElementKind, NodeKind } from './core/Core';
import type { MessageKey } from './i18n/messages';

type T = (key: MessageKey, values?: Record<string, string | number>) => string;

const KINDS: Record<ElementKind | NodeKind, MessageKey> = {
  goal: 'kindGoal',
  strategy: 'kindStrategy',
  solution: 'kindSolution',
  context: 'kindContext',
  assumption: 'kindAssumption',
  justification: 'kindJustification',
  neutral: 'kindNeutral',
};

export interface AddItem {
  kind: ElementKind;
  label: string;
  /** Why it cannot be chosen, or what else choosing it does; null for neither. */
  note: string | null;
  unavailable: boolean;
}

/** The words for an `AF-EDIT-*` code, about a node of `kind` (null for the canvas). */
export function refusalText(code: string, kind: NodeKind | null, t: T): string {
  const known = /^AF-EDIT-0(0[1-9]|1[0-4])$/.test(code);
  return known ? t(code as MessageKey, { kind: kind ? t(KINDS[kind]) : '' }) : code;
}

export function kindName(kind: ElementKind | NodeKind, t: T): string {
  return t(KINDS[kind]);
}

/** The menu's items for `choices`, made for a node of `parent` (null for the canvas). */
export function addItems(choices: readonly AddChoice[], parent: NodeKind | null, t: T): AddItem[] {
  return choices.map((choice) => {
    const label = t('addKind', { kind: t(KINDS[choice.kind]) });
    if (choice.refusal !== null) {
      return { kind: choice.kind, label, note: refusalText(choice.refusal, parent, t), unavailable: true };
    }
    const notes: string[] = [];
    // A Strategy is added with its first sub-goal (W2).
    if (choice.kind === 'strategy' && parent !== null) notes.push(t('addWithSubGoal'));
    // Support under an undeveloped Goal develops it (W11).
    if (choice.develops) notes.push(t('addDevelops'));
    return { kind: choice.kind, label, note: notes.length > 0 ? notes.join(' ') : null, unavailable: false };
  });
}
