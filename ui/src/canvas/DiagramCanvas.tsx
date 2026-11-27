import { useCallback, useEffect, useMemo, useRef, useState, type KeyboardEvent, type MouseEvent, type PointerEvent } from 'react';
import type { DiagramAt, DiagramEdge, TextStyle } from '../core/Core';
import type { ContextMenuSource } from '../design';
import { measuredFamilies, watchCanvasStyle, type CanvasStyle } from '../design';
import { report } from '../testHook';
import { badgeLabel, comesWith, edgePan, HOLD_MS, readableAt, type DragEnd } from './drag';
import { boundsOf, draw, edgeKey, findEdge, fit, hitId, hitOneEdge, hitTest, pan, zoomAbout, type Badge, type MoveMarks, type Palette, type Viewport } from './draw';

/**
 * The diagram's colours, as app.css gives them to the canvas for the theme it
 * is in (`--af-diagram-*`). `style` is read again when the theme changes, and
 * the palette with it.
 */
function palette(element: Element, style: CanvasStyle): Palette {
  const declared = getComputedStyle(element);
  // Forced colours: the system's, as DATUM's canvas rule says.
  const color = (name: string, system: string) =>
    style.forced ? system : declared.getPropertyValue(`--af-diagram-${name}`).trim() || system;
  return {
    ground: color('ground', 'Canvas'),
    grid: color('grid', 'CanvasText'),
    fill: color('fill', 'Canvas'),
    line: color('line', 'CanvasText'),
    text: color('text', 'CanvasText'),
    secondary: color('secondary', 'CanvasText'),
    hover: color('hover', 'Canvas'),
    selectedFill: color('selected-fill', 'Canvas'),
    selectedLine: color('selected-line', 'Highlight'),
    forced: style.forced,
  };
}

/** The diagram's text sizes, as DATUM's type tokens resolve here. */
function textStyle(style: CanvasStyle): TextStyle {
  return {
    textSize: style.px('type.control.size'),
    textLineHeight: style.px('type.control.line-height'),
    identifierSize: style.px('type.metadata.size'),
    identifierLineHeight: style.px('type.metadata.line-height'),
  };
}

/** A press that moves less than this, in CSS px, is a click. */
const DRAG_SLOP = 4;
/** One press of + or −. */
export const ZOOM_STEP = 1.25;

/** Whether `event` opens the context menu from the keyboard: the context-menu key, or Shift+F10. */
export function isMenuKey(event: Pick<globalThis.KeyboardEvent, 'key' | 'shiftKey' | 'ctrlKey' | 'metaKey' | 'altKey'>): boolean {
  if (event.ctrlKey || event.metaKey || event.altKey) return false;
  return event.key === 'ContextMenu' || (event.shiftKey && event.key === 'F10');
}

/** The view's keys while the canvas has focus (DATUM decision 0023), or null for any other key. */
export function viewKey(event: Pick<globalThis.KeyboardEvent, 'key' | 'code' | 'shiftKey' | 'ctrlKey' | 'metaKey' | 'altKey'>) {
  // Ctrl or ⌘ with + − 0 stays the browser's zoom.
  if (event.ctrlKey || event.metaKey || event.altKey) return null;
  // Shift with 1, 2 or 0, by key position, so it is the same on every layout.
  if (event.shiftKey && event.code === 'Digit1') return 'fit';
  if (event.shiftKey && event.code === 'Digit2') return 'fitSelection';
  if (event.shiftKey && event.code === 'Digit0') return 'actualSize';
  if (event.key === '+' || event.key === '=') return 'zoomIn';
  if (event.key === '-') return 'zoomOut';
  if (event.key === 'Escape') return 'clear';
  return null;
}

type Gesture = {
  start: { x: number; y: number };
  view: Viewport;
  panning: boolean;
  pinch?: { distance: number; middle: { x: number; y: number } };
  /** The node the press began on, when a drag of it would move it. */
  node?: string;
  /** The drag would move it: at once with a mouse or a pen, once held with a touch. */
  armed?: boolean;
  /** The node is being dragged. */
  dragging?: boolean;
  /** The timer of a touch being held. */
  hold?: number;
  /** The drag was given up, or refused: the rest of the press does nothing. */
  over?: boolean;
};

/**
 * The diagram on a canvas that fills the workspace's centre surface, `size`
 * CSS px. The viewport and the selection are the application's: the canvas
 * asks to change them. `onTextStyle` hears the text sizes DATUM's tokens
 * resolve to, for the layout. `onContextMenu` asks for the add menu of a
 * node, or of the empty canvas (`null`), at a point in client coordinates:
 * from a right-click, or from the context-menu key or Shift+F10 on the
 * selected node. `onRemove` asks to remove the selected node: Delete, or
 * Backspace, which is the key macOS calls delete.
 *
 * While a move is being chosen (`move`), the canvas draws where the node may
 * go; a click on a node says so (`onMoveTo`) in place of selecting it, and
 * Escape gives the move up (`onMoveCancel`).
 *
 * A drag that begins on a node moves it (`onDragStart`, which answers
 * whether the node can be moved, then `onDragEnd` with where it was
 * dropped); a drag that begins anywhere else pans. A touch moves a node only
 * once it has been held, since a touch on a node is also how the view is
 * panned. Zoomed out too far to read a node, a drag on it pans.
 */
export function DiagramCanvas({
  diagram,
  label,
  size,
  viewport,
  onViewport,
  selected,
  onSelect,
  onTextStyle,
  onContextMenu,
  onLineMenu,
  onRemove,
  move = null,
  onMoveTo,
  onMoveToLine,
  onMoveCancel,
  onDragStart,
  onDragEnd,
}: {
  diagram: DiagramAt | null;
  label: string;
  size: { width: number; height: number };
  viewport: Viewport | null;
  onViewport: (next: Viewport) => void;
  /** The selected node's SACM id, or the selected edge's key (`edgeKey`). */
  selected: string | null;
  onSelect: (id: string | null) => void;
  onTextStyle: (style: TextStyle) => void;
  onContextMenu: (id: string | null, at: { x: number; y: number }, source: ContextMenuSource) => void;
  /** The menu of a line: it is selected, and the menu asked for at `at`. */
  onLineMenu?: (edge: DiagramEdge, at: { x: number; y: number }, source: ContextMenuSource) => void;
  onRemove: (id: string) => void;
  move?: MoveMarks | null;
  /** A node was clicked while a move is chosen: one it may go under, or not. */
  onMoveTo?: (id: string) => void;
  /** A line was clicked while a challenge's move is chosen. */
  onMoveToLine?: (edge: DiagramEdge) => void;
  onMoveCancel?: () => void;
  /** A drag of a node began: answers whether the node can be moved, having asked where. */
  onDragStart?: (id: string) => Promise<boolean>;
  onDragEnd?: (end: DragEnd) => void;
}) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const [style, setStyle] = useState<CanvasStyle | null>(null);
  // Read once for each style: a new one comes with each change of theme.
  // The canvas is there by the time a style is: the style is read from it.
  const colours = useMemo(() => (canvas.current && style ? palette(canvas.current, style) : null), [style]);
  const [hovered, setHovered] = useState<string | null>(null);
  const [panning, setPanning] = useState(false);
  // The node being dragged, and where the pointer is (CSS px of the canvas).
  const [drag, setDrag] = useState<{ id: string; pointer: { x: number; y: number } } | null>(null);
  // What follows the pointer: the node's identifier, and how many come with
  // it. Worked out once a drag, not at each move of the pointer.
  const dragId = drag?.id ?? null;
  const dragLabel = useMemo(() => {
    const node = dragId && diagram ? diagram.diagram.nodes.find((candidate) => candidate.id === dragId) : undefined;
    return node && diagram ? badgeLabel(node, comesWith(diagram.diagram, node.id)) : null;
  }, [diagram, dragId]);
  const [armed, setArmed] = useState(false);
  const dragPointer = useRef<{ x: number; y: number } | null>(null);

  useEffect(() => {
    const element = canvas.current;
    if (!element) return;
    const watch = watchCanvasStyle(element, (next) => {
      setStyle(next);
      onTextStyle(textStyle(next));
    });
    setStyle(watch.style);
    onTextStyle(textStyle(watch.style));
    return () => watch.dispose();
  }, [onTextStyle]);

  // What is gone, after a new file, is no longer under the pointer.
  const shownHover = diagram && hovered && boundsOf(diagram.diagram, hovered) ? hovered : null;

  useEffect(() => {
    const element = canvas.current;
    const context = element?.getContext('2d');
    if (!element || !context || !style || !colours || size.width === 0 || size.height === 0) return;
    const dpr = style.dpr;
    element.width = Math.round(size.width * dpr);
    element.height = Math.round(size.height * dpr);
    if (!diagram || !viewport) {
      context.setTransform(1, 0, 0, 1, 0, 0);
      context.fillStyle = colours.ground;
      context.fillRect(0, 0, element.width, element.height);
      report({ revision: null, drawn: { nodes: 0, edges: 0 }, diagram: null, viewport: null, selected, hovered: null });
      return;
    }
    const badge: Badge | null = drag && dragLabel ? { pointer: drag.pointer, label: dragLabel } : null;
    const drawn = draw(context, diagram.diagram, viewport, colours, measuredFamilies, { ...size, dpr }, {
      selected,
      hovered: shownHover,
      move,
      badge,
    });
    report({ revision: diagram.revision, drawn, diagram, viewport, selected, hovered: shownHover, dragging: drag?.id ?? null });
  }, [diagram, size, style, colours, viewport, selected, shownHover, move, drag, dragLabel]);

  // While a node is dragged, the view pans when the pointer nears an edge,
  // and the place under the pointer follows the view.
  const isDragging = drag !== null;
  useEffect(() => {
    if (!isDragging) return;
    let frame = 0;
    const tick = () => {
      const { diagram: shown, viewport: view, size: area, onViewport: change } = latest.current;
      const point = dragPointer.current;
      if (shown && view && point) {
        const { dx, dy } = edgePan(point, area);
        if (dx !== 0 || dy !== 0) {
          const next = pan(view, dx, dy);
          change(next);
          setHovered(hitTest(shown.diagram, next, point.x, point.y)?.id ?? null);
        }
      }
      frame = requestAnimationFrame(tick);
    };
    frame = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(frame);
  }, [isDragging]);

  // The latest values, for the handlers below, which outlive a render.
  const latest = useRef({ diagram, viewport, size, onViewport });
  latest.current = { diagram, viewport, size, onViewport };

  // Scroll pans on both axes; Ctrl or ⌘ with the wheel, and a touchpad pinch
  // (which the browser sends as Ctrl and the wheel), zoom about the pointer.
  // Not passive, so the page neither scrolls nor zooms.
  useEffect(() => {
    const element = canvas.current;
    if (!element) return;
    const onWheel = (event: WheelEvent) => {
      const { viewport: view, onViewport: change } = latest.current;
      if (!view) return;
      event.preventDefault();
      const unit = event.deltaMode === 1 ? 16 : event.deltaMode === 2 ? element.clientHeight : 1;
      const rect = element.getBoundingClientRect();
      if (event.ctrlKey || event.metaKey) {
        const dy = Math.max(-50, Math.min(50, event.deltaY * unit));
        change(zoomAbout(view, Math.exp(-dy * 0.01), event.clientX - rect.left, event.clientY - rect.top));
        return;
      }
      let dx = event.deltaX * unit;
      let dy = event.deltaY * unit;
      // Shift and a wheel with one axis scrolls sideways, as a page does.
      if (event.shiftKey && dx === 0) [dx, dy] = [dy, 0];
      change(pan(view, -dx, -dy));
    };
    element.addEventListener('wheel', onWheel, { passive: false });
    return () => element.removeEventListener('wheel', onWheel);
  }, []);

  const pointers = useRef(new Map<number, { x: number; y: number }>());
  const gesture = useRef<Gesture | null>(null);
  const local = (event: PointerEvent<HTMLCanvasElement> | MouseEvent<HTMLCanvasElement>) => {
    const rect = event.currentTarget.getBoundingClientRect();
    return { x: event.clientX - rect.left, y: event.clientY - rect.top };
  };
  const twoPointers = () => {
    const [a, b] = [...pointers.current.values()];
    return { distance: Math.hypot(a.x - b.x, a.y - b.y), middle: { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 } };
  };

  const onPointerDown = (event: PointerEvent<HTMLCanvasElement>) => {
    if (!viewport || (event.button !== 0 && event.button !== 1)) return;
    // The middle button would start the browser's autoscroll.
    if (event.button === 1) event.preventDefault();
    event.currentTarget.setPointerCapture(event.pointerId);
    const point = local(event);
    pointers.current.set(event.pointerId, point);
    if (pointers.current.size === 2) {
      // A second finger makes a pinch of the press: a drag under way is
      // given up, as a cancelled pointer gives it up.
      const before = gesture.current;
      endPress(before);
      if (before?.dragging) onDragEnd?.({ on: 'cancel' });
      gesture.current = { start: point, view: viewport, panning: true, pinch: twoPointers() };
    } else {
      gesture.current = { start: point, view: viewport, panning: event.button === 1 };
      // A press on a node may move it. Not while a move is being chosen
      // from the menu, and not on a node too small to read.
      const wanted = event.button === 0 && !move && onDragStart !== undefined;
      const node = wanted && diagram ? hitTest(diagram.diagram, viewport, point.x, point.y) : null;
      if (node && readableAt(node, viewport.scale)) {
        const current = gesture.current;
        current.node = node.id;
        // A touch on a node pans, as a touch anywhere does, unless it is held first.
        const held = event.pointerType === 'touch';
        current.armed = !held;
        if (held) {
          current.hold = window.setTimeout(() => {
            if (gesture.current === current && !current.panning) {
              current.armed = true;
              setArmed(true);
            }
          }, HOLD_MS);
        }
      }
    }
    setPanning(gesture.current.panning);
  };

  /** Ends a press that might have moved a node. */
  const endPress = (current: Gesture | null) => {
    if (current?.hold !== undefined) window.clearTimeout(current.hold);
    setArmed(false);
    setDrag(null);
    dragPointer.current = null;
  };

  const onPointerMove = (event: PointerEvent<HTMLCanvasElement>) => {
    const point = local(event);
    const current = gesture.current;
    if (!current) {
      if (diagram && viewport) {
        // While a challenge's move is chosen, the line marked is the one a
        // click would take: none on a trunk several relationships share.
        const node = move?.lines ? hitTest(diagram.diagram, viewport, point.x, point.y) : null;
        const line = move?.lines && !node ? hitOneEdge(diagram.diagram, viewport, point.x, point.y).edge : null;
        setHovered(move?.lines ? (node?.id ?? (line ? edgeKey(line) : null)) : hitId(diagram.diagram, viewport, point.x, point.y));
      }
      return;
    }
    pointers.current.set(event.pointerId, point);
    if (current.over) return;
    if (current.pinch && pointers.current.size >= 2) {
      const now = twoPointers();
      const zoomed = zoomAbout(current.view, now.distance / current.pinch.distance, current.pinch.middle.x, current.pinch.middle.y);
      onViewport(pan(zoomed, now.middle.x - current.pinch.middle.x, now.middle.y - current.pinch.middle.y));
      return;
    }
    if (current.dragging) {
      // The badge follows the pointer, and the place under it is marked.
      dragPointer.current = point;
      setDrag((held) => (held ? { ...held, pointer: point } : held));
      if (diagram && viewport) {
        const node = hitTest(diagram.diagram, viewport, point.x, point.y);
        // A line, for a challenge that may be dropped on one.
        // Only one line: on a trunk several share, none is marked.
        const line = !node && move?.lines ? hitOneEdge(diagram.diagram, viewport, point.x, point.y).edge : null;
        setHovered(node?.id ?? (line ? edgeKey(line) : null));
      }
      return;
    }
    if (!current.panning && Math.hypot(point.x - current.start.x, point.y - current.start.y) > DRAG_SLOP) {
      const node = current.node && current.armed ? diagram?.diagram.nodes.find((candidate) => candidate.id === current.node) : undefined;
      if (node) {
        current.dragging = true;
        dragPointer.current = point;
        setDrag({ id: node.id, pointer: point });
        // A node that cannot be moved: the drag is over at once, and the
        // status says why. The rest of the press pans, as a press on the
        // canvas does, so a view a node fills can still be moved.
        void onDragStart?.(node.id).then((movable) => {
          if (movable || gesture.current !== current || !current.dragging) return;
          current.dragging = false;
          current.panning = true;
          current.view = latest.current.viewport ?? current.view;
          current.start = dragPointer.current ?? current.start;
          endPress(current);
          setPanning(true);
          setHovered(null);
        });
        return;
      }
      if (current.hold !== undefined) window.clearTimeout(current.hold);
      current.panning = true;
      setPanning(true);
      setHovered(null);
    }
    if (current.panning) onViewport(pan(current.view, point.x - current.start.x, point.y - current.start.y));
  };

  const onPointerUp = (event: PointerEvent<HTMLCanvasElement>) => {
    const current = gesture.current;
    pointers.current.delete(event.pointerId);
    if (pointers.current.size > 0) return;
    gesture.current = null;
    setPanning(false);
    endPress(current);
    if (current?.dragging) {
      // Dropped on a node, on itself (given up), or on nothing.
      const point = local(event);
      const node = diagram && viewport ? hitTest(diagram.diagram, viewport, point.x, point.y) : null;
      // A line, for a challenge, or before the core has said what is moved:
      // the places it answers decide then.
      const line = !node && (move === null || move.lines) && diagram && viewport ? hitOneEdge(diagram.diagram, viewport, point.x, point.y).edge : null;
      onDragEnd?.(
        node ? (node.id === current.node ? { on: 'cancel' } : { on: 'node', id: node.id }) : line ? { on: 'line', edge: line } : { on: 'canvas' },
      );
      return;
    }
    if (!current || current.over || current.panning || current.pinch || event.button !== 0 || !diagram || !viewport) return;
    // A press that did not move is a click: it selects the node under it, or
    // the line under it, or clears the selection on empty canvas.
    const point = local(event);
    if (move) {
      // A move is being chosen: a click on a node is where it goes, and a
      // click on a line or on the empty canvas changes nothing.
      const node = hitTest(diagram.diagram, viewport, point.x, point.y);
      if (node && node.id !== move.source) onMoveTo?.(node.id);
      const line = !node && move.lines ? hitOneEdge(diagram.diagram, viewport, point.x, point.y).edge : null;
      if (line) onMoveToLine?.(line);
      return;
    }
    onSelect(hitId(diagram.diagram, viewport, point.x, point.y));
  };

  const onPointerCancel = (event: PointerEvent<HTMLCanvasElement>) => {
    pointers.current.delete(event.pointerId);
    if (pointers.current.size === 0) {
      const current = gesture.current;
      gesture.current = null;
      setPanning(false);
      endPress(current);
      if (current?.dragging) onDragEnd?.({ on: 'cancel' });
    }
  };

  const fitTo = useCallback(
    (id: string | null) => {
      const { diagram: shown, size: area, onViewport: change } = latest.current;
      if (!shown) return;
      const bounds = id === null ? shown.diagram.bounds : boundsOf(shown.diagram, id);
      if (!bounds) return;
      change(fit(bounds, area.width, area.height));
    },
    [],
  );

  // A right-click on a node, on a line, or on the empty canvas. A line's
  // menu challenges it, or, while a challenge's move is chosen, holds the
  // move.
  const onContext = (event: MouseEvent<HTMLCanvasElement>) => {
    event.preventDefault();
    if (!diagram || !viewport) return;
    // A touch held on a node arms its drag before the browser's long press
    // opens a menu. While the node is dragged no menu opens; held still, the
    // menu opens and the press is over, so no drag follows it.
    const press = gesture.current;
    if (press?.dragging) return;
    if (press?.node) {
      press.over = true;
      endPress(press);
    }
    // The canvas takes focus, so closing the menu returns it here.
    event.currentTarget.focus();
    const point = local(event);
    const node = hitTest(diagram.diagram, viewport, point.x, point.y);
    // A line's menu challenges it: on a trunk several lines share, it is not
    // said which, and no menu opens.
    const hit = node ? null : hitOneEdge(diagram.diagram, viewport, point.x, point.y);
    if (hit?.ambiguous) return;
    const line = hit?.edge ?? null;
    if (line) {
      onLineMenu?.(line, { x: event.clientX, y: event.clientY }, 'pointer');
      return;
    }
    onContextMenu(node?.id ?? null, { x: event.clientX, y: event.clientY }, 'pointer');
  };

  const onKeyDown = (event: KeyboardEvent<HTMLCanvasElement>) => {
    // Escape gives a drag up: the rest of the press does nothing.
    if (event.key === 'Escape' && gesture.current?.dragging) {
      event.preventDefault();
      const current = gesture.current;
      current.dragging = false;
      current.over = true;
      endPress(current);
      setHovered(null);
      onDragEnd?.({ on: 'cancel' });
      return;
    }
    if (isMenuKey(event) && diagram && viewport && !event.nativeEvent.isComposing) {
      event.preventDefault();
      // The selected node's menu at its centre, or the selected line's at
      // the middle of its bounds; with nothing selected, the canvas's at the
      // canvas's centre.
      const rect = event.currentTarget.getBoundingClientRect();
      const node = selected ? diagram.diagram.nodes.find((candidate) => candidate.id === selected) : undefined;
      const line = selected && !node ? findEdge(diagram.diagram, selected) : null;
      if (line) {
        const middle = {
          x: (line.bounds.x + line.bounds.width / 2) * viewport.scale + viewport.offsetX,
          y: (line.bounds.y + line.bounds.height / 2) * viewport.scale + viewport.offsetY,
        };
        onLineMenu?.(line, { x: rect.left + middle.x, y: rect.top + middle.y }, 'keyboard');
        return;
      }
      if (selected && !node) return;
      const centre = node
        ? {
            x: (node.bounds.x + node.bounds.width / 2) * viewport.scale + viewport.offsetX,
            y: (node.bounds.y + node.bounds.height / 2) * viewport.scale + viewport.offsetY,
          }
        : { x: size.width / 2, y: size.height / 2 };
      onContextMenu(node?.id ?? null, { x: rect.left + centre.x, y: rect.top + centre.y }, 'keyboard');
      return;
    }
    const plain = !event.ctrlKey && !event.metaKey && !event.altKey && !event.shiftKey;
    if ((event.key === 'Delete' || event.key === 'Backspace') && plain && !event.nativeEvent.isComposing) {
      // Only a node is removed: a selected line is not. Not while a move is chosen.
      if (!move && selected && diagram?.diagram.nodes.some((node) => node.id === selected)) {
        event.preventDefault();
        onRemove(selected);
      }
      return;
    }
    const action = viewKey(event);
    if (!action || !viewport || event.nativeEvent.isComposing) return;
    event.preventDefault();
    const centre = { x: size.width / 2, y: size.height / 2 };
    switch (action) {
      case 'fit':
        fitTo(null);
        break;
      case 'fitSelection':
        // With nothing selected, nothing happens.
        if (selected) fitTo(selected);
        break;
      case 'actualSize':
        onViewport(zoomAbout(viewport, 1 / viewport.scale, centre.x, centre.y));
        break;
      case 'zoomIn':
        onViewport(zoomAbout(viewport, ZOOM_STEP, centre.x, centre.y));
        break;
      case 'zoomOut':
        onViewport(zoomAbout(viewport, 1 / ZOOM_STEP, centre.x, centre.y));
        break;
      case 'clear':
        if (move) onMoveCancel?.();
        else onSelect(null);
        break;
    }
  };

  return (
    <div className="af-canvas-host">
      <canvas
        ref={canvas}
        className="af-canvas"
        role="img"
        aria-label={label}
        aria-keyshortcuts="+ - Shift+1 Shift+2 Shift+0 Escape Shift+F10 ContextMenu Delete"
        tabIndex={0}
        style={{
          cursor:
            panning || (drag && (!shownHover || !move || move.allowed.has(shownHover) || move.lines?.has(shownHover)))
              ? 'grabbing'
              : armed
                ? 'grab'
                : !shownHover
                  ? 'default'
                  : !move || move.allowed.has(shownHover) || move.lines?.has(shownHover)
                    ? 'pointer'
                    : 'not-allowed',
        }}
        onKeyDown={onKeyDown}
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={onPointerUp}
        onPointerCancel={onPointerCancel}
        onPointerLeave={() => setHovered(null)}
        onAuxClick={(event) => event.preventDefault()}
        onContextMenu={onContext}
      />
    </div>
  );
}
