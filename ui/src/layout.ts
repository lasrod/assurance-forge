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
 * In the stacked start dock (the wide tier) it does what DATUM's own Hide and
 * rail do: hiding a view adds it to `hidden`, and hiding the last one shown
 * closes the dock instead, keeping `hidden` as it was; showing a view takes it
 * out of `hidden` and opens the dock. Anywhere else the panel goes with its
 * dock: below the wide tier DATUM shows the start views as tabs and does not
 * apply `hidden`.
 */
export function togglePanel(layout: WorkspaceLayout, id: string, show: boolean, dock: WorkspaceDockId): WorkspaceLayout {
  const stacked = dock === 'start' && (START_VIEWS as readonly string[]).includes(id);
  if (!stacked) return { ...layout, open: { ...layout.open, [dock]: show } };
  const hidden = new Set(layout.hidden ?? []);
  if (show) {
    hidden.delete(id);
    return { ...layout, open: { ...layout.open, start: true }, hidden: [...hidden] };
  }
  const othersShown = START_VIEWS.some((view) => view !== id && !hidden.has(view));
  if (layout.open.start && othersShown) {
    hidden.add(id);
    return { ...layout, hidden: [...hidden] };
  }
  return { ...layout, open: { ...layout.open, start: false } };
}
