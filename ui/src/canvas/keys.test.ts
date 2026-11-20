import { describe, expect, it } from 'vitest';
import { viewKey } from './DiagramCanvas';

const key = (key: string, code: string, modifiers: Partial<{ shiftKey: boolean; ctrlKey: boolean; metaKey: boolean; altKey: boolean }> = {}) =>
  viewKey({ key, code, shiftKey: false, ctrlKey: false, metaKey: false, altKey: false, ...modifiers });

describe('the canvas keys', () => {
  it('fit, fit the selection and return to 100% with Shift and a digit, by key position', () => {
    expect(key('!', 'Digit1', { shiftKey: true })).toBe('fit');
    // A Japanese or French layout gives other characters on the same keys.
    expect(key('"', 'Digit2', { shiftKey: true })).toBe('fitSelection');
    expect(key('0', 'Digit0', { shiftKey: true })).toBe('actualSize');
    expect(key('1', 'Digit1')).toBeNull();
  });

  it('zoom with + and −, and clear with Esc', () => {
    expect(key('+', 'Equal', { shiftKey: true })).toBe('zoomIn');
    expect(key('=', 'Equal')).toBe('zoomIn');
    expect(key('+', 'NumpadAdd')).toBe('zoomIn');
    expect(key('-', 'Minus')).toBe('zoomOut');
    expect(key('Escape', 'Escape')).toBe('clear');
  });

  it('leave Ctrl or ⌘ with + − 0 to the browser', () => {
    expect(key('+', 'Equal', { ctrlKey: true })).toBeNull();
    expect(key('-', 'Minus', { metaKey: true })).toBeNull();
    expect(key('0', 'Digit0', { ctrlKey: true })).toBeNull();
  });
});
