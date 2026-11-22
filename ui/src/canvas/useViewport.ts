// The canvas's view and selection: pan, zoom, fit, the centre's size, and
// what is selected. They stay in the frontend (ARCHITECTURE.md); the core
// knows nothing of them.

import { useCallback, useLayoutEffect, useRef, useState } from 'react';
import type { DiagramAt, DiagramNode } from '../core/Core';
import { findEdge, fit, keepInPlace, reveal, zoomAbout, type Viewport } from './draw';

export interface ViewState {
  /** Diagram coordinates to canvas px; null until a new file is fitted. */
  viewport: Viewport | null;
  /** The selected node's SACM id, or a line's key. */
  selected: string | null;
  setSelected: (id: string | null) => void;
  /** The centre surface's size, in CSS px. */
  centreSize: { width: number; height: number };
  /** A view the user chose: zoom, pan, a node brought into view. */
  moveView: (next: Viewport | ((view: Viewport | null) => Viewport | null)) => void;
  onCentreSize: (size: { inline: number; block: number }) => void;
  fitAll: () => void;
  zoomBy: (factor: number) => void;
  /** Selects a node and brings it into view if it is not wholly in it, at the same zoom. */
  selectAndShow: (id: string) => void;
  /** After a relayout for other text sizes: a view that is still the fit made for the earlier sizes is made again. */
  refitIfFitted: () => void;
  /**
   * After an edit: the view moves so that a node on screen before stays where
   * it was (`anchor` first, then the selected node, then any node in both),
   * and the added element is selected and brought into view. Without one, a
   * selection that is gone is cleared.
   */
  followEdit: (before: DiagramAt | null, next: DiagramAt, anchor: string | null, added: string | null) => void;
}

/**
 * `opened` counts the files opened: a new one, or none, starts the view and
 * the selection again; a new revision of the same file (an edit) keeps them.
 */
export function useViewport(diagram: DiagramAt | null, opened: number): ViewState {
  // Pan and zoom, and the selection. A new file starts fitted (null until the
  // canvas has a size) with nothing selected.
  const [viewport, setViewport] = useState<Viewport | null>(null);
  const [selected, setSelected] = useState<string | null>(null);
  // A new file on screen, or none: the view and the selection start again.
  // Done while rendering, so the fit below never sees the old view.
  const file = diagram ? opened : null;
  const [viewFor, setViewFor] = useState<number | null>(file);
  if (viewFor !== file) {
    setViewFor(file);
    setViewport(null);
    setSelected(null);
  }
  const selectedRef = useRef(selected);
  selectedRef.current = selected;

  // The view is a fit the application made (a new file's, or Fit), which the
  // user has not moved since: it is made again when the diagram is laid out
  // again for other text sizes. A resize keeps its centre, and keeps this.
  const fitted = useRef(false);
  const moveView = useCallback((next: Viewport | ((view: Viewport | null) => Viewport | null)) => {
    fitted.current = false;
    setViewport(next);
  }, []);

  // The canvas takes its size from the workspace's centre surface (D5). When
  // it changes, the point at the centre of the view stays at its centre.
  const [centreSize, setCentreSize] = useState({ width: 0, height: 0 });
  const onCentreSize = useCallback((size: { inline: number; block: number }) => {
    setCentreSize((before) => {
      const after = { width: size.inline, height: size.block };
      if (before.width > 0 && before.height > 0 && after.width > 0 && after.height > 0) {
        const dx = (after.width - before.width) / 2;
        const dy = (after.height - before.height) / 2;
        setViewport((view) => view && { ...view, offsetX: view.offsetX + dx, offsetY: view.offsetY + dy });
      }
      return after;
    });
  }, []);

  // A new file is fitted once the canvas has a size, before it is painted.
  useLayoutEffect(() => {
    if (!viewport && diagram && centreSize.width > 0 && centreSize.height > 0) {
      fitted.current = true;
      setViewport(fit(diagram.diagram.bounds, centreSize.width, centreSize.height));
    }
  }, [viewport, diagram, centreSize]);

  const fitAll = () => {
    if (!diagram) return;
    fitted.current = true;
    setViewport(fit(diagram.diagram.bounds, centreSize.width, centreSize.height));
  };
  const zoomBy = (factor: number) => moveView((view) => view && zoomAbout(view, factor, centreSize.width / 2, centreSize.height / 2));

  const selectAndShow = (id: string) => {
    setSelected(id);
    const node = diagram?.diagram.nodes.find((candidate) => candidate.id === id);
    if (!node || !viewport || centreSize.width <= 0 || centreSize.height <= 0) return;
    const left = node.bounds.x * viewport.scale + viewport.offsetX;
    const top = node.bounds.y * viewport.scale + viewport.offsetY;
    const [width, height] = [node.bounds.width * viewport.scale, node.bounds.height * viewport.scale];
    if (left >= 0 && top >= 0 && left + width <= centreSize.width && top + height <= centreSize.height) return;
    moveView({
      ...viewport,
      offsetX: viewport.offsetX + (centreSize.width - width) / 2 - left,
      offsetY: viewport.offsetY + (centreSize.height - height) / 2 - top,
    });
  };

  const refitIfFitted = useCallback(() => {
    if (fitted.current) setViewport(null);
  }, []);

  const followEdit = useCallback(
    (before: DiagramAt | null, next: DiagramAt, anchor: string | null, added: string | null) => {
      // The view is moved now, not a fit: a relayout keeps it.
      fitted.current = false;
      const nodes = before?.diagram.nodes ?? [];
      const find = (list: readonly DiagramNode[], id: string | null) => (id ? list.find((node) => node.id === id) : undefined);
      const kept =
        find(nodes, anchor) ??
        find(nodes, selectedRef.current) ??
        nodes.find((node) => next.diagram.nodes.some((candidate) => candidate.id === node.id));
      const keptAfter = kept && find(next.diagram.nodes, kept.id);
      const addedNode = find(next.diagram.nodes, added);
      setViewport((view) => {
        if (!view) return view;
        const placed = kept && keptAfter ? keepInPlace(view, kept.bounds, keptAfter.bounds) : view;
        return addedNode ? reveal(placed, addedNode.bounds, centreSize.width, centreSize.height) : placed;
      });
      if (added) setSelected(added);
      else {
        const stays = (id: string) => next.diagram.nodes.some((node) => node.id === id) || findEdge(next.diagram, id) !== null;
        setSelected((current) => (current && stays(current) ? current : null));
      }
    },
    [centreSize],
  );

  return { viewport, selected, setSelected, centreSize, moveView, onCentreSize, fitAll, zoomBy, selectAndShow, refitIfFitted, followEdit };
}
