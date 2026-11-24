// What an editable text field does with its draft: the decisions of
// TextProperty.tsx, apart from React so they can be tested.
//
// The draft is what the field shows. `base` is the element's text the draft
// started from; `pending` is a text sent to the core and not yet answered.

/** What the core made of a text sent to it. */
export type TextOutcome = 'applied' | 'unchanged' | 'refused';

/**
 * The text to send when the field is applied, or null when there is nothing
 * to send. A draft the user has not changed is never sent, even when the
 * element's text has space around it that a single line would trim. A single
 * line is sent without the space around it; prose is sent as typed.
 */
export function toSend(draft: string, value: string, pending: string | null, multiline: boolean): string | null {
  if (draft === value) return null;
  const text = multiline ? draft : draft.trim();
  if (text === value || text === pending) return null;
  return text;
}

/**
 * The draft once the element's text has become `value`: it follows, unless
 * the user has typed since the text it started from and since the one sent,
 * which would be lost.
 */
export function followValue(draft: string, base: string, pending: string | null, value: string): string {
  return draft === base || draft === pending ? value : draft;
}

/**
 * The draft once the core has answered for `sent`. Applied or unchanged, it
 * stays. Refused, a single line shows the element's text again, if the user
 * has not typed since; prose keeps what was written, which would otherwise
 * be lost, for the user to change or cancel with Escape.
 */
export function afterAnswer(draft: string, sent: string, outcome: TextOutcome, value: string, multiline: boolean): string {
  if (outcome !== 'refused' || multiline) return draft;
  return draft === sent ? value : draft;
}
