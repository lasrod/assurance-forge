import { describe, expect, it } from 'vitest';
import {
  editShortcut,
  isOpenShortcut,
  openShortcut,
  redoShortcut,
  saveAsShortcut,
  saveShortcut,
  saveShortcutOf,
  undoShortcut,
} from './shortcuts';

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

describe('the undo and redo shortcuts', () => {
  it('are Ctrl+Z and Ctrl+Y on Windows and Linux, and Ctrl+Shift+Z redoes too', () => {
    expect([undoShortcut(false), redoShortcut(false)]).toEqual(['Ctrl+Z', 'Ctrl+Y']);
    expect(editShortcut(key({ ctrlKey: true, key: 'z', code: 'KeyZ' }), false)).toBe('undo');
    expect(editShortcut(key({ ctrlKey: true, key: 'y', code: 'KeyY' }), false)).toBe('redo');
    expect(editShortcut(key({ ctrlKey: true, shiftKey: true, key: 'Z', code: 'KeyZ' }), false)).toBe('redo');
    expect(editShortcut(key({ metaKey: true, key: 'z', code: 'KeyZ' }), false)).toBeNull();
    expect(editShortcut(key({ key: 'z', code: 'KeyZ' }), false)).toBeNull();
  });

  it('are ⌘Z and ⇧⌘Z on macOS', () => {
    expect([undoShortcut(true), redoShortcut(true)]).toEqual(['⌘Z', '⇧⌘Z']);
    expect(editShortcut(key({ metaKey: true, key: 'z', code: 'KeyZ' }), true)).toBe('undo');
    expect(editShortcut(key({ metaKey: true, shiftKey: true, key: 'Z', code: 'KeyZ' }), true)).toBe('redo');
    expect(editShortcut(key({ metaKey: true, key: 'y', code: 'KeyY' }), true)).toBeNull();
    expect(editShortcut(key({ ctrlKey: true, key: 'z', code: 'KeyZ' }), true)).toBeNull();
  });

  it('repeat while held, find Z by its place on a layout that is not Latin, and wait for an IME', () => {
    expect(editShortcut(key({ ctrlKey: true, key: 'z', code: 'KeyZ', repeat: true }), false)).toBe('undo');
    expect(editShortcut(key({ ctrlKey: true, key: 'я', code: 'KeyZ' }), false)).toBe('undo');
    expect(editShortcut(key({ ctrlKey: true, key: 'z', code: 'KeyZ', isComposing: true }), false)).toBeNull();
  });
});

describe('the save shortcuts', () => {
  it('are Ctrl+S and Ctrl+Shift+S on Windows and Linux, as the menu says', () => {
    expect(saveShortcut(false)).toBe('Ctrl+S');
    expect(saveAsShortcut(false)).toBe('Ctrl+Shift+S');
    expect(saveShortcutOf(key({ ctrlKey: true, key: 's', code: 'KeyS' }), false)).toBe('save');
    expect(saveShortcutOf(key({ ctrlKey: true, shiftKey: true, key: 'S', code: 'KeyS' }), false)).toBe('saveAs');
    expect(saveShortcutOf(key({ metaKey: true, key: 's', code: 'KeyS' }), false)).toBeNull();
    expect(saveShortcutOf(key({ ctrlKey: true, altKey: true, key: 's', code: 'KeyS' }), false)).toBeNull();
  });

  it('are ⌘S and ⇧⌘S on macOS, as the menu says', () => {
    expect(saveShortcut(true)).toBe('⌘S');
    expect(saveAsShortcut(true)).toBe('⇧⌘S');
    expect(saveShortcutOf(key({ metaKey: true, key: 's', code: 'KeyS' }), true)).toBe('save');
    expect(saveShortcutOf(key({ metaKey: true, shiftKey: true, key: 'S', code: 'KeyS' }), true)).toBe('saveAs');
    expect(saveShortcutOf(key({ ctrlKey: true, key: 's', code: 'KeyS' }), true)).toBeNull();
  });

  it('find S by its place on a layout that is not Latin, and are a held key too, kept from the browser', () => {
    expect(saveShortcutOf(key({ ctrlKey: true, key: 'ы', code: 'KeyS' }), false)).toBe('save');
    expect(saveShortcutOf(key({ ctrlKey: true, key: 's', code: 'KeyS', repeat: true }), false)).toBe('save');
    expect(saveShortcutOf(key({ ctrlKey: true, key: 's', code: 'KeyS', isComposing: true }), false)).toBeNull();
  });
});
