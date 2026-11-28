// The add menu's items: of the six kinds the core lists for a node or the
// empty canvas, the ones that can be added there. A kind that cannot is left
// out: GSN does not allow it there, and the menu does not explain what it
// does not offer.

import type {
  AcpChoice,
  AcpGoal,
  AcpKind, AddChoice, ElementKind, NodeKind } from './core/Core';
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
}

/** The words for an `AF-EDIT-*` code, about a node of `kind` (null for the canvas). */
export function refusalText(code: string, kind: NodeKind | null, t: T): string {
  const known = /^AF-EDIT-0(0[1-9]|[1-3][0-9]|4[0-3])$/.test(code);
  return known ? t(code as MessageKey, { kind: kind ? t(KINDS[kind]) : '' }) : code;
}

export function kindName(kind: ElementKind | NodeKind, t: T): string {
  return t(KINDS[kind]);
}

/** The menu's challenge items: the challengers that may challenge there, in the core's order. */
export function challengeItems(choices: readonly AddChoice[], t: T): AddItem[] {
  return choices
    .filter((choice) => choice.refusal === null)
    .map((choice) => ({ kind: choice.kind, label: t(choice.kind === 'solution' ? 'addCounterEvidence' : 'addCounterGoal') }));
}

/** The line menu's ACP items: the kinds of argument an ACP may be added with there. */
export function acpItems(choices: readonly AcpChoice[], t: T): { kind: AcpKind; label: string }[] {
  return choices
    .filter((choice) => choice.refusal === null)
    .map((choice) => ({ kind: choice.kind, label: t(choice.kind === 'description' ? 'addAcpDescription' : 'addAcpArgument') }));
}

/** The line menu's goals an ACP may link to there (W36), in the core's order, labelled as the ACP would be. */
export function acpGoalItems(goals: readonly AcpGoal[]): { claim: string; label: string }[] {
  return goals.filter((goal) => goal.refusal === null).map((goal) => ({ claim: goal.claim, label: goal.label }));
}

/** The menu's items for `choices`: the kinds that can be added, in the core's order. */
export function addItems(choices: readonly AddChoice[], t: T): AddItem[] {
  return choices
    .filter((choice) => choice.refusal === null)
    .map((choice) => ({ kind: choice.kind, label: t('addKind', { kind: t(KINDS[choice.kind]) }) }));
}
