// Which docks are open, how large, and which tab the start dock shows: the
// viewer's own convenience, kept in this browser only, under one key. The page
// works the same without it.

import type { WorkspaceLayout } from './design';

const KEY = 'assurance-forge.workspace.v2';

export type StartView = 'caseExplorer' | 'argumentNavigator';

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
    const stored = JSON.parse(localStorage.getItem(KEY) ?? 'null') as Partial<WorkspaceState> | null;
    const layout = stored?.layout;
    if (layout && typeof layout.open === 'object' && typeof layout.size === 'object') {
      return {
        layout: { open: { ...DEFAULT_WORKSPACE.layout.open, ...layout.open }, size: { ...layout.size } },
        startView: stored?.startView === 'argumentNavigator' ? 'argumentNavigator' : 'caseExplorer',
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
