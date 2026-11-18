// Which docks are open, how large, and which stacked views are hidden: the
// viewer's own convenience, kept in this browser only, under one key. The page
// works the same without it.

import type { WorkspaceDockId, WorkspaceLayout } from './design';

const KEY = 'assurance-forge.workspace.v2';
/** Before the start dock's tab was kept, the layout alone was stored here. */
const LAYOUT_V1 = 'assurance-forge.workspace.v1';

/** The views stacked in the start dock: the Case Explorer above the Argument Navigator. */
export const START_VIEWS = ['caseExplorer', 'argumentNavigator'] as const;

export interface WorkspaceState {
  layout: WorkspaceLayout;
}

export const DEFAULT_WORKSPACE: WorkspaceState = {
  layout: { open: { start: true, end: true, bottom: true }, size: {} },
};

export function loadWorkspace(): WorkspaceState {
  try {
    let stored = JSON.parse(localStorage.getItem(KEY) ?? 'null') as Partial<WorkspaceState> | null;
    if (!stored) {
      // A layout saved by the earlier version is kept.
      const v1 = JSON.parse(localStorage.getItem(LAYOUT_V1) ?? 'null') as WorkspaceLayout | null;
      if (v1) stored = { layout: v1 };
    }
    const layout = stored?.layout;
    if (layout && typeof layout.open === 'object' && typeof layout.size === 'object') {
      return {
        layout: {
          open: { ...DEFAULT_WORKSPACE.layout.open, ...layout.open },
          size: { ...layout.size },
          ...(Array.isArray(layout.hidden) ? { hidden: layout.hidden.filter((id) => typeof id === 'string') } : {}),
          ...(layout.viewSize && typeof layout.viewSize === 'object' ? { viewSize: { ...layout.viewSize } } : {}),
        },
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
    // The earlier version's layout goes only once this one is saved.
    localStorage.removeItem(LAYOUT_V1);
  } catch {
    // Storage is a convenience.
  }
}

/**
 * The layout with one panel shown or hidden, in the dock that holds it.
 *
 * A stacked view is hidden on its own. Hiding the last one closes the dock,
 * as DATUM's own Hide does, and the views are shown again the next time it
 * opens. Showing a view of a closed dock opens the dock on that view alone.
 */
export function togglePanel(layout: WorkspaceLayout, id: string, show: boolean, dock: WorkspaceDockId): WorkspaceLayout {
  const stacked = (START_VIEWS as readonly string[]).includes(id);
  if (!stacked) return { ...layout, open: { ...layout.open, [dock]: show } };
  const hidden = new Set(layout.hidden ?? []);
  if (show) {
    if (!layout.open[dock]) {
      // The dock opens on the chosen view alone.
      START_VIEWS.forEach((view) => hidden.add(view));
    }
    hidden.delete(id);
    return { ...layout, open: { ...layout.open, [dock]: true }, hidden: [...hidden] };
  }
  hidden.add(id);
  if (START_VIEWS.every((view) => hidden.has(view))) {
    START_VIEWS.forEach((view) => hidden.delete(view));
    return { ...layout, open: { ...layout.open, [dock]: false }, hidden: [...hidden] };
  }
  return { ...layout, hidden: [...hidden] };
}
