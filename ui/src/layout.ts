// Which docks are open, how large, and which stacked views are hidden: the
// viewer's own convenience, kept in this browser only. The page works the
// same without it. DATUM's Workspace changes it; the page only keeps it.

import type { WorkspaceDockId, WorkspaceLayout } from './design';

const KEY = 'assurance-forge.workspace.v3';
/** Earlier versions: the layout alone (v1), and the layout with a tab (v2). */
const EARLIER = ['assurance-forge.workspace.v2', 'assurance-forge.workspace.v1'];

const DOCKS: readonly WorkspaceDockId[] = ['start', 'end', 'bottom'];

export const DEFAULT_LAYOUT: WorkspaceLayout = { open: { start: true, end: true, bottom: true }, size: {} };

/** A size as DATUM takes it: rem above 0, or null for the default. Anything else is dropped. */
function sizes(value: unknown): Record<string, number | null> {
  if (!value || typeof value !== 'object') return {};
  return Object.fromEntries(
    Object.entries(value).filter(([, size]) => size === null || (typeof size === 'number' && Number.isFinite(size) && size > 0)),
  ) as Record<string, number | null>;
}

/** A stored layout, checked: what is not a layout is the default. */
export function readLayout(value: unknown): WorkspaceLayout {
  // v2 stored `{ layout }`, v1 and v3 the layout itself.
  const stored = (value && typeof value === 'object' && 'layout' in value ? (value as { layout: unknown }).layout : value) as
    | Partial<WorkspaceLayout>
    | null;
  if (!stored || typeof stored.open !== 'object' || stored.open === null) return DEFAULT_LAYOUT;
  const open = Object.fromEntries(
    DOCKS.map((dock) => [dock, typeof stored.open?.[dock] === 'boolean' ? stored.open[dock] : DEFAULT_LAYOUT.open[dock]]),
  ) as WorkspaceLayout['open'];
  const hidden = Array.isArray(stored.hidden) ? stored.hidden.filter((id): id is string => typeof id === 'string') : undefined;
  return {
    open,
    size: sizes(stored.size) as WorkspaceLayout['size'],
    ...(hidden ? { hidden } : {}),
    ...(stored.viewSize ? { viewSize: sizes(stored.viewSize) } : {}),
  };
}

export function loadLayout(): WorkspaceLayout {
  try {
    for (const key of [KEY, ...EARLIER]) {
      const stored = localStorage.getItem(key);
      if (stored !== null) return readLayout(JSON.parse(stored));
    }
  } catch {
    // No storage, or something unreadable in it: the default.
  }
  return DEFAULT_LAYOUT;
}

export function saveLayout(layout: WorkspaceLayout) {
  try {
    localStorage.setItem(KEY, JSON.stringify(layout));
    // Earlier versions' entries go only once this one is saved.
    EARLIER.forEach((key) => localStorage.removeItem(key));
  } catch {
    // Storage is a convenience.
  }
}
