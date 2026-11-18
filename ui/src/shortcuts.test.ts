import { describe, expect, it } from 'vitest';
import { isOpenShortcut, OPEN_SHORTCUT } from './shortcuts';

// The tests run outside macOS, where the shortcut is Ctrl+O.
const key = (event: Partial<KeyboardEvent>) =>
  ({ ctrlKey: false, metaKey: false, shiftKey: false, altKey: false, isComposing: false, repeat: false, key: '', code: '', ...event }) as KeyboardEvent;

describe('isOpenShortcut', () => {
  it('is Ctrl+O, as the menu says', () => {
    expect(OPEN_SHORTCUT).toBe('Ctrl+O');
    expect(isOpenShortcut(key({ ctrlKey: true, key: 'o', code: 'KeyO' }))).toBe(true);
    expect(isOpenShortcut(key({ ctrlKey: true, key: 'O', code: 'KeyO' }))).toBe(true);
    expect(isOpenShortcut(key({ key: 'o', code: 'KeyO' }))).toBe(false);
    expect(isOpenShortcut(key({ ctrlKey: true, shiftKey: true, key: 'O', code: 'KeyO' }))).toBe(false);
  });

  it('finds O by its place on a layout that is not Latin', () => {
    expect(isOpenShortcut(key({ ctrlKey: true, key: 'щ', code: 'KeyO' }))).toBe(true);
    // On a Latin layout the letter decides, wherever it is (AZERTY, Dvorak).
    expect(isOpenShortcut(key({ ctrlKey: true, key: 'r', code: 'KeyO' }))).toBe(false);
  });

  it('ignores a held key and an IME that is composing', () => {
    expect(isOpenShortcut(key({ ctrlKey: true, key: 'o', code: 'KeyO', repeat: true }))).toBe(false);
    expect(isOpenShortcut(key({ ctrlKey: true, key: 'o', code: 'KeyO', isComposing: true }))).toBe(false);
  });
});
