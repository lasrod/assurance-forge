import { describe, expect, it } from 'vitest';
import { isOpenShortcut, openShortcut } from './shortcuts';

const key = (event: Partial<KeyboardEvent>) =>
  ({ ctrlKey: false, metaKey: false, shiftKey: false, altKey: false, isComposing: false, repeat: false, key: '', code: '', ...event }) as KeyboardEvent;

describe('the open shortcut', () => {
  it('is Ctrl+O on Windows and Linux, as the menu says', () => {
    expect(openShortcut(false)).toBe('Ctrl+O');
    expect(isOpenShortcut(key({ ctrlKey: true, key: 'o', code: 'KeyO' }), false)).toBe(true);
    expect(isOpenShortcut(key({ ctrlKey: true, key: 'O', code: 'KeyO' }), false)).toBe(true);
    expect(isOpenShortcut(key({ metaKey: true, key: 'o', code: 'KeyO' }), false)).toBe(false);
    expect(isOpenShortcut(key({ key: 'o', code: 'KeyO' }), false)).toBe(false);
    expect(isOpenShortcut(key({ ctrlKey: true, shiftKey: true, key: 'O', code: 'KeyO' }), false)).toBe(false);
  });

  it('is ⌘O on macOS, as the menu says', () => {
    expect(openShortcut(true)).toBe('⌘O');
    expect(isOpenShortcut(key({ metaKey: true, key: 'o', code: 'KeyO' }), true)).toBe(true);
    expect(isOpenShortcut(key({ ctrlKey: true, key: 'o', code: 'KeyO' }), true)).toBe(false);
  });

  it('finds O by its place on a layout that is not Latin', () => {
    expect(isOpenShortcut(key({ ctrlKey: true, key: 'щ', code: 'KeyO' }), false)).toBe(true);
    // On a Latin layout the letter decides, wherever it is (AZERTY, Dvorak).
    expect(isOpenShortcut(key({ ctrlKey: true, key: 'r', code: 'KeyO' }), false)).toBe(false);
  });

  it('ignores a held key and an IME that is composing', () => {
    expect(isOpenShortcut(key({ ctrlKey: true, key: 'o', code: 'KeyO', repeat: true }), false)).toBe(false);
    expect(isOpenShortcut(key({ ctrlKey: true, key: 'o', code: 'KeyO', isComposing: true }), false)).toBe(false);
  });
});
