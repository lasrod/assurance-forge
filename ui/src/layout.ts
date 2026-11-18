// Which docks are open and how large: the viewer's own convenience, kept in
// this browser only. The page works the same without it.

import type { WorkspaceLayout } from './design';

const KEY = 'assurance-forge.workspace.v1';
const START_VIEW_KEY = 'assurance-forge.workspace.start-view.v1';

export type StartView = 'caseExplorer' | 'argumentNavigator';

/** The start dock's tab, kept with the layout. */
export function loadStartView(): StartView {
  try {
    return localStorage.getItem(START_VIEW_KEY) === 'argumentNavigator' ? 'argumentNavigator' : 'caseExplorer';
  } catch {
    return 'caseExplorer';
  }
}

export function saveStartView(view: StartView) {
  try {
    localStorage.setItem(START_VIEW_KEY, view);
  } catch {
    // Storage is a convenience.
  }
}

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
