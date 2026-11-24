// One editable text of the selected element: a single line (the identifier,
// the name) or prose (the description). What is typed is a draft until it is
// applied: on leaving the field, on Enter in a single line, on Ctrl+Enter
// (⌘Enter) in prose. Escape puts back the element's text. What the draft does
// is decided in textDraft.ts.

import { useEffect, useId, useRef, useState, type KeyboardEvent } from 'react';
import { afterAnswer, followValue, toSend, type TextOutcome } from './textDraft';

export interface TextPropertyProps {
  label: string;
  /** The element's text, as the core gave it. */
  value: string;
  /** Applies the text, and answers what the core made of it. */
  onCommit: (text: string) => Promise<TextOutcome>;
  /** How it is applied and cancelled, for a screen reader. */
  hint: string;
  /** Prose, on several lines. */
  multiline?: boolean;
  /** An identifier: monospace. */
  mono?: boolean;
  'data-testid'?: string;
}

export function TextProperty({ label, value, onCommit, hint, multiline = false, mono, 'data-testid': testId }: TextPropertyProps) {
  const id = useId();
  const [draft, setDraft] = useState(value);
  // The element's text the draft started from, and a text sent and not yet answered.
  const base = useRef(value);
  const pending = useRef<string | null>(null);
  // Prose the core refused: it stays in the field, marked, until it is changed.
  const [refused, setRefused] = useState(false);

  // The element's text changed (an applied edit, an undo, another language):
  // the field follows, unless that would lose what the user is typing.
  useEffect(() => {
    setDraft((current) => followValue(current, base.current, pending.current, value));
    base.current = value;
  }, [value]);
  const latest = useRef(value);
  latest.current = value;
  // Escape leaves the field without applying what was typed.
  const cancelled = useRef(false);

  const commit = () => {
    if (cancelled.current) {
      cancelled.current = false;
      return;
    }
    const text = toSend(draft, value, pending.current, multiline);
    if (text === null) return;
    // A single line shows what is sent, without the space around it.
    if (!multiline) setDraft(text);
    pending.current = text;
    const answered = (outcome: TextOutcome) => {
      if (pending.current === text) pending.current = null;
      setDraft((current) => afterAnswer(current, text, outcome, latest.current, multiline));
      if (multiline) setRefused(outcome === 'refused');
    };
    onCommit(text).then(answered, () => answered('refused'));
  };

  const onKeyDown = (event: KeyboardEvent<HTMLInputElement | HTMLTextAreaElement>) => {
    // While an input method is composing, Enter and Escape are its own.
    if (event.nativeEvent.isComposing) return;
    if (event.key === 'Escape') {
      event.preventDefault();
      event.stopPropagation();
      cancelled.current = true;
      setDraft(value);
      setRefused(false);
      event.currentTarget.blur();
    } else if (event.key === 'Enter' && (!multiline || event.ctrlKey || event.metaKey)) {
      event.preventDefault();
      commit();
    }
  };

  const onChange = (text: string) => {
    setDraft(text);
    setRefused(false);
  };

  const shared = {
    id,
    className: 'datum-input af-text-input',
    value: draft,
    spellCheck: multiline,
    'aria-describedby': `${id}-hint`,
    'aria-invalid': refused || undefined,
    'data-mono': mono ? '' : undefined,
    'data-testid': testId,
    onBlur: commit,
    onKeyDown,
  };
  return (
    <div className="af-text-property" data-multiline={multiline ? '' : undefined}>
      <label className="af-text-label" htmlFor={id}>
        {label}
      </label>
      {multiline ? (
        <textarea {...shared} rows={3} onChange={(event) => onChange(event.target.value)} />
      ) : (
        <input {...shared} type="text" autoComplete="off" onChange={(event) => onChange(event.target.value)} />
      )}
      <span id={`${id}-hint`} hidden>
        {hint}
      </span>
    </div>
  );
}
