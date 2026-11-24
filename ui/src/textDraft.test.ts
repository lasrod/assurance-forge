import { describe, expect, it } from 'vitest';
import { afterAnswer, followValue, toSend } from './textDraft';

describe('toSend', () => {
  it('sends nothing for a draft the user has not changed, though a single line would trim it', () => {
    expect(toSend('Safe ', 'Safe ', null, false)).toBeNull();
    expect(toSend('  ', '  ', null, false)).toBeNull();
    expect(toSend('Safe', 'Safe', null, true)).toBeNull();
  });

  it('sends a single line without the space around it, and prose as typed', () => {
    expect(toSend(' G1 ', 'G0', null, false)).toBe('G1');
    expect(toSend('Safe ', 'Safe', null, false)).toBeNull();
    expect(toSend('Two\nlines ', 'One', null, true)).toBe('Two\nlines ');
  });

  it('does not send again a text that is on its way', () => {
    expect(toSend('Braking', 'Old', 'Braking', false)).toBeNull();
    expect(toSend('Braking is safe', 'Old', 'Braking', false)).toBe('Braking is safe');
  });
});

describe('followValue', () => {
  it('follows the element when the draft is what it started from, or what was sent', () => {
    expect(followValue('Old', 'Old', null, 'Undone')).toBe('Undone');
    expect(followValue('Braking', 'Old', 'Braking', 'Braking')).toBe('Braking');
  });

  it('keeps what was typed after the text was sent', () => {
    expect(followValue('Braking is safe', 'Old', 'Braking', 'Braking')).toBe('Braking is safe');
  });
});

describe('afterAnswer', () => {
  it('keeps the draft when the text was applied, or was that already', () => {
    expect(afterAnswer('G1', 'G1', 'applied', 'G1', false)).toBe('G1');
    expect(afterAnswer('G1', 'G1', 'unchanged', 'G0', false)).toBe('G1');
  });

  it('puts back a refused single line, unless the user has typed since', () => {
    expect(afterAnswer('', '', 'refused', 'G1', false)).toBe('G1');
    expect(afterAnswer('G2', 'G', 'refused', 'G1', false)).toBe('G2');
  });

  it('keeps refused prose, which would otherwise be lost', () => {
    expect(afterAnswer('Three paragraphs', 'Three paragraphs', 'refused', 'Old', true)).toBe('Three paragraphs');
  });
});
