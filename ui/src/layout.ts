// Which docks are open and how large: the viewer's own convenience, kept in
// this browser only. The page works the same without it.

import type { WorkspaceLayout } from './design';

const KEY = 'assurance-forge.workspace.v1';

export const DEFAULT_LAYOUT: WorkspaceLayout = { open: { start: true, end: true, bottom: true }, size: {} };

export function loadLayout(): WorkspaceLayout {
  try {
    const stored = JSON.parse(localStorage.getItem(KEY) ?? 'null') as WorkspaceLayout | null;
    if (stored && typeof stored.open === 'object' && typeof stored.size === 'object') {
      return { open: { ...DEFAULT_LAYOUT.open, ...stored.open }, size: { ...stored.size } };
    }
  } catch {
    // No storage, or something unreadable in it: the default.
  }
  return DEFAULT_LAYOUT;
}

export function saveLayout(layout: WorkspaceLayout) {
  try {
    localStorage.setItem(KEY, JSON.stringify(layout));
  } catch {
    // Storage is a convenience.
  }
}
