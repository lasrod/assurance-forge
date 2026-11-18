// Keyboard shortcuts, in one place, so the label a menu shows and the key it
// listens for cannot disagree.

function isMac(): boolean {
  if (typeof navigator === 'undefined') return false;
  const hinted = (navigator as Navigator & { userAgentData?: { platform?: string } }).userAgentData?.platform;
  return /mac|iphone|ipad/i.test(hinted || navigator.platform || navigator.userAgent);
}

const mac = isMac();

/** `⌘O` on macOS, `Ctrl+O` elsewhere, as the File menu shows it. */
export const OPEN_SHORTCUT = mac ? '⌘O' : 'Ctrl+O';

/** Whether `event` is the open shortcut: ⌘O on macOS, Ctrl+O elsewhere, not while an IME composes. */
export function isOpenShortcut(event: KeyboardEvent): boolean {
  const modifier = mac ? event.metaKey && !event.ctrlKey : event.ctrlKey && !event.metaKey;
  return modifier && !event.shiftKey && !event.altKey && !event.isComposing && event.key.toLowerCase() === 'o';
}
