// Keyboard shortcuts, in one place, so the label a menu shows and the key it
// listens for cannot disagree. The platform is a parameter, defaulting to
// the one the page runs on, so tests can name either.

export function isMac(): boolean {
  if (typeof navigator === 'undefined') return false;
  const hinted = (navigator as Navigator & { userAgentData?: { platform?: string } }).userAgentData?.platform;
  return /mac|iphone|ipad/i.test(hinted || navigator.platform || navigator.userAgent);
}

/** The platform the page runs on, read once. */
const MAC = isMac();

/** `⌘O` on macOS, `Ctrl+O` elsewhere, as the File menu shows it. */
export function openShortcut(mac = MAC): string {
  return mac ? '⌘O' : 'Ctrl+O';
}

/**
 * Whether `event` is the open shortcut: ⌘O on macOS, Ctrl+O elsewhere. Not
 * while an IME composes, and not a held key's repeats. The key is O by its
 * letter, or by its place when the layout is not Latin (Ctrl+щ on Russian).
 */
export function isOpenShortcut(event: KeyboardEvent, mac = MAC): boolean {
  const modifier = mac ? event.metaKey && !event.ctrlKey : event.ctrlKey && !event.metaKey;
  const latin = /^[\x20-\x7e]$/.test(event.key);
  const o = latin ? event.key.toLowerCase() === 'o' : event.code === 'KeyO';
  return modifier && o && !event.shiftKey && !event.altKey && !event.isComposing && !event.repeat;
}
