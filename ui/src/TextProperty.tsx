// One editable text of the selected element: a single line (the identifier,
// the name) or prose (the description). What is typed is a draft until it is
// applied: on leaving the field, on Enter in a single line, on Ctrl+Enter
// (⌘Enter) in prose. Escape puts back the element's text.

import { useEffect, useId, useRef, useState, type KeyboardEvent } from 'react';

export interface TextPropertyProps {
  label: string;
  /** The element's text, as the core gave it. */
  value: string;
  /**
   * Applies the text. Answers whether the element's text is now that: false
   * when the core refused it, and the field then shows the element's text again.
   */
  onCommit: (text: string) => Promise<boolean>;
  /** How it is applied and cancelled, for a screen reader. */
  hint: string;
  /** Prose, on several lines. */
  multiline?: boolean;
  /** An identifier: monospace. */
  mono?: boolean;
  'data-testid'?: string;
}

export function TextProperty({ label, value, onCommit, hint, multiline, mono, 'data-testid': testId }: TextPropertyProps) {
  const id = useId();
  const [draft, setDraft] = useState(value);
  // The element's text changed (an undo, another language): the field follows.
  useEffect(() => setDraft(value), [value]);
  const latest = useRef(value);
  latest.current = value;
  // Escape leaves the field without applying what was typed.
  const cancelled = useRef(false);

  const commit = () => {
    if (cancelled.current) {
      cancelled.current = false;
      return;
    }
    // A single line has no meaning in the space around it.
    const text = multiline ? draft : draft.trim();
    if (text === value) {
      setDraft(value);
      return;
    }
    void onCommit(text).then(
      (applied) => {
        if (!applied) setDraft(latest.current);
      },
      () => setDraft(latest.current),
    );
  };

  const onKeyDown = (event: KeyboardEvent<HTMLInputElement | HTMLTextAreaElement>) => {
    // While an input method is composing, Enter and Escape are its own.
    if (event.nativeEvent.isComposing) return;
    if (event.key === 'Escape') {
      event.preventDefault();
      event.stopPropagation();
      cancelled.current = true;
      setDraft(value);
      event.currentTarget.blur();
    } else if (event.key === 'Enter' && (!multiline || event.ctrlKey || event.metaKey)) {
      event.preventDefault();
      commit();
    }
  };

  const shared = {
    id,
    className: 'datum-input af-text-input',
    value: draft,
    spellCheck: Boolean(multiline),
    'aria-describedby': `${id}-hint`,
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
        <textarea {...shared} rows={3} onChange={(event) => setDraft(event.target.value)} />
      ) : (
        <input {...shared} type="text" autoComplete="off" onChange={(event) => setDraft(event.target.value)} />
      )}
      <span id={`${id}-hint`} hidden>
        {hint}
      </span>
    </div>
  );
}
