// Which docks are open, how large, and which tab the start dock shows: the
// viewer's own convenience, kept in this browser only, under one key. The page
// works the same without it.

import type { WorkspaceLayout } from './design';

const KEY = 'assurance-forge.workspace.v2';
/** Before the start dock's tab was kept, the layout alone was stored here. */
const LAYOUT_V1 = 'assurance-forge.workspace.v1';

export type StartView = 'caseExplorer' | 'argumentNavigator';

/** Any id as a start view: the Argument Navigator, or else the Case Explorer. */
export function toStartView(id: unknown): StartView {
  return id === 'argumentNavigator' ? 'argumentNavigator' : 'caseExplorer';
}

export interface WorkspaceState {
  layout: WorkspaceLayout;
  startView: StartView;
}

export const DEFAULT_WORKSPACE: WorkspaceState = {
  layout: { open: { start: true, end: true, bottom: true }, size: {} },
  startView: 'caseExplorer',
};

export function loadWorkspace(): WorkspaceState {
  try {
    let stored = JSON.parse(localStorage.getItem(KEY) ?? 'null') as Partial<WorkspaceState> | null;
    if (!stored) {
      // A layout saved by the earlier version is kept.
      const v1 = JSON.parse(localStorage.getItem(LAYOUT_V1) ?? 'null') as WorkspaceLayout | null;
      if (v1) stored = { layout: v1 };
      localStorage.removeItem(LAYOUT_V1);
    }
    const layout = stored?.layout;
    if (layout && typeof layout.open === 'object' && typeof layout.size === 'object') {
      return {
        layout: { open: { ...DEFAULT_WORKSPACE.layout.open, ...layout.open }, size: { ...layout.size } },
        startView: toStartView(stored?.startView),
      };
    }
  } catch {
    // No storage, or something unreadable in it: the default.
  }
  return DEFAULT_WORKSPACE;
}

export function saveWorkspace(state: WorkspaceState) {
  try {
    localStorage.setItem(KEY, JSON.stringify(state));
  } catch {
    // Storage is a convenience.
  }
}
