// Draws the render model on a Canvas 2D context. The geometry is the core's
// (af-layout); this only paints it. Lines keep 1 CSS px at every zoom, and
// shapes, text and arrowheads scale (DATUM's rule for a diagram canvas).

import type { Diagram, DiagramEdge, DiagramNode, Point, Rect } from '../core/Core';

export interface Palette {
  /** `surface.canvas`: the ground and the node fill. */
  ground: string;
  /** `line.control`: outlines, connectors, arrowheads. */
  line: string;
  /** `text.primary`. */
  text: string;
  /** `text.secondary`: the identifier. */
  secondary: string;
  /** `surface.hover`: the fill of the node under the pointer. */
  hover: string;
  /** `selection.surface`: the selected node's fill. */
  selectedFill: string;
  /** `selection.indicator`: the selected node's outline. */
  selectedLine: string;
  /** Forced colours are on: the selected outline is heavier, since there is no fill to see. */
  forced: boolean;
}

/** Which node is selected and which is under the pointer: separate states (DATUM). */
export interface Marks {
  selected: string | null;
  hovered: string | null;
}

/** Diagram coordinates to CSS pixels: `x * scale + offsetX`. */
export interface Viewport {
  scale: number;
  offsetX: number;
  offsetY: number;
}

export const MIN_SCALE = 0.1;
export const MAX_SCALE = 4;
const FIT_MARGIN = 16;

const clamp = (value: number, low: number, high: number) => Math.min(high, Math.max(low, value));

/** The viewport that fits `bounds` in a `width` × `height` canvas, centred, at most 100%. */
export function fit(bounds: Rect, width: number, height: number): Viewport {
  const room = (size: number) => Math.max(size - 2 * FIT_MARGIN, 1);
  // At most 100%: fitting never enlarges a small diagram.
  const scale = Math.max(Math.min(room(width) / bounds.width, room(height) / bounds.height, 1), MIN_SCALE);
  return {
    scale,
    offsetX: (width - bounds.width * scale) / 2 - bounds.x * scale,
    offsetY: (height - bounds.height * scale) / 2 - bounds.y * scale,
  };
}

/**
 * Zooms by `factor` about the canvas point (`x`, `y`) in CSS px, which stays
 * where it is, within 10% to 400%.
 */
export function zoomAbout(viewport: Viewport, factor: number, x: number, y: number): Viewport {
  const scale = clamp(viewport.scale * factor, MIN_SCALE, MAX_SCALE);
  const k = scale / viewport.scale;
  return { scale, offsetX: x - (x - viewport.offsetX) * k, offsetY: y - (y - viewport.offsetY) * k };
}

/** The viewport moved by (`dx`, `dy`) CSS px. */
export function pan(viewport: Viewport, dx: number, dy: number): Viewport {
  return { ...viewport, offsetX: viewport.offsetX + dx, offsetY: viewport.offsetY + dy };
}

/** A canvas point in CSS px as a diagram point. */
export function toDiagram(viewport: Viewport, x: number, y: number): Point {
  return { x: (x - viewport.offsetX) / viewport.scale, y: (y - viewport.offsetY) / viewport.scale };
}

/** Whether the diagram point lies inside the node's shape, its outline included. */
export function insideShape(node: DiagramNode, point: Point): boolean {
  const shape = node.shape;
  const { x, y, width, height } = shape.rect;
  if (point.x < x || point.x > x + width || point.y < y || point.y > y + height) return false;
  switch (shape.type) {
    case 'ellipse':
    case 'circle': {
      const dx = (point.x - (x + width / 2)) / (width / 2);
      const dy = (point.y - (y + height / 2)) / (height / 2);
      return dx * dx + dy * dy <= 1;
    }
    case 'parallelogram': {
      // The left edge runs from (x + skew, y) down to (x, y + height); the right edge likewise.
      const shift = shape.skew * (1 - (point.y - y) / height);
      return point.x >= x + shift && point.x <= x + width - shape.skew + shift;
    }
    default:
      return true;
  }
}

/** The node under the canvas point (`x`, `y`) in CSS px: the one drawn last, on top. */
export function hitTest(diagram: Diagram, viewport: Viewport, x: number, y: number): DiagramNode | null {
  const point = toDiagram(viewport, x, y);
  for (let index = diagram.nodes.length - 1; index >= 0; index -= 1) {
    const node = diagram.nodes[index];
    if (insideShape(node, point)) return node;
  }
  return null;
}

/** The font families: names and statements, and identifiers. */
export interface Families {
  text: string;
  identifier: string;
}

export interface Drawn {
  nodes: number;
  edges: number;
}

/** Text smaller than this on screen is not drawn: only identifiers are (DATUM's canvas rule 5). */
export const SMALLEST_TEXT_PX = 7;
/** The identifier's size on screen when zoomed far out: `type.metadata`. */
const FAR_IDENTIFIER_PX = 12;

/** The part of the diagram in view, in diagram coordinates. */
export function visibleArea(viewport: Viewport, width: number, height: number): Rect {
  return {
    x: -viewport.offsetX / viewport.scale,
    y: -viewport.offsetY / viewport.scale,
    width: width / viewport.scale,
    height: height / viewport.scale,
  };
}

function overlaps(a: Rect, b: Rect): boolean {
  return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

/**
 * Paints `diagram` on a backing store of `width` × `height` CSS px at `dpr`
 * device pixels each. Returns what was drawn.
 */
export function draw(
  context: CanvasRenderingContext2D,
  diagram: Diagram,
  viewport: Viewport,
  palette: Palette,
  families: Families,
  size: { width: number; height: number; dpr: number },
  marks: Marks = { selected: null, hovered: null },
): Drawn {
  const { scale, offsetX, offsetY } = viewport;
  context.setTransform(size.dpr, 0, 0, size.dpr, 0, 0);
  context.fillStyle = palette.ground;
  context.fillRect(0, 0, size.width, size.height);
  context.setTransform(size.dpr * scale, 0, 0, size.dpr * scale, size.dpr * offsetX, size.dpr * offsetY);
  context.lineWidth = 1 / scale;
  context.lineJoin = 'round';

  // Only what is in view is drawn.
  const visible = visibleArea(viewport, size.width, size.height);
  const drawn = { nodes: 0, edges: 0 };
  for (const edge of diagram.edges) {
    if (!overlaps(edge.bounds, visible)) continue;
    drawEdge(context, edge, palette, scale);
    drawn.edges += 1;
  }
  for (const node of diagram.nodes) {
    if (!overlaps(node.bounds, visible)) continue;
    drawNode(context, node, palette, families, scale, marks);
    drawn.nodes += 1;
  }
  return drawn;
}

function polyline(context: CanvasRenderingContext2D, points: Point[]) {
  context.beginPath();
  points.forEach((point, index) => (index === 0 ? context.moveTo(point.x, point.y) : context.lineTo(point.x, point.y)));
}

function drawEdge(context: CanvasRenderingContext2D, edge: DiagramEdge, palette: Palette, scale: number) {
  context.strokeStyle = palette.line;
  context.setLineDash(edge.dashed ? [4 / scale, 3 / scale] : []);
  polyline(context, edge.points);
  context.stroke();
  context.setLineDash([]);
  const [tip, left, right] = edge.head;
  if (edge.arrowhead === 'open') {
    polyline(context, [left, tip, right]);
    context.stroke();
    return;
  }
  polyline(context, [tip, left, right]);
  context.closePath();
  context.fillStyle = edge.arrowhead === 'filled' ? palette.line : palette.ground;
  context.fill();
  context.stroke();
}

function shapePath(context: CanvasRenderingContext2D, node: DiagramNode) {
  const shape = node.shape;
  const { x, y, width, height } = shape.rect;
  context.beginPath();
  switch (shape.type) {
    case 'rectangle':
    case 'neutral':
      context.rect(x, y, width, height);
      break;
    case 'parallelogram':
      context.moveTo(x + shape.skew, y);
      context.lineTo(x + width, y);
      context.lineTo(x + width - shape.skew, y + height);
      context.lineTo(x, y + height);
      context.closePath();
      break;
    case 'roundedRectangle':
      context.roundRect(x, y, width, height, shape.radius);
      break;
    case 'ellipse':
    case 'circle':
      context.ellipse(x + width / 2, y + height / 2, width / 2, height / 2, 0, 0, Math.PI * 2);
      break;
  }
}

function drawNode(
  context: CanvasRenderingContext2D,
  node: DiagramNode,
  palette: Palette,
  families: Families,
  scale: number,
  marks: Marks,
) {
  // Selected and hovered are told apart by fill and outline, never by the
  // outline's colour alone (DATUM's list treatment).
  const selected = marks.selected === node.id;
  const hovered = marks.hovered === node.id;
  shapePath(context, node);
  context.fillStyle = selected ? palette.selectedFill : hovered ? palette.hover : palette.ground;
  context.fill();
  context.strokeStyle = selected ? palette.selectedLine : palette.line;
  context.lineWidth = (selected && palette.forced ? 2 : 1) / scale;
  context.setLineDash(node.shape.type === 'neutral' ? [4 / scale, 3 / scale] : []);
  context.stroke();
  context.setLineDash([]);
  context.lineWidth = 1 / scale;

  // Zoomed far out the text is not drawn, the A and J included: only the
  // identifier, at metadata size (DATUM's canvas rule 5).
  const sized = node.text.find((line) => line.role !== 'identifier') ?? node.text[0];
  const far = sized !== undefined && sized.size * scale < SMALLEST_TEXT_PX;

  for (const decorator of node.decorators) {
    if (decorator.type === 'undeveloped') {
      const { centre, half } = decorator;
      polyline(context, [
        { x: centre.x, y: centre.y - half },
        { x: centre.x + half, y: centre.y },
        { x: centre.x, y: centre.y + half },
        { x: centre.x - half, y: centre.y },
      ]);
      context.closePath();
      context.fillStyle = palette.ground;
      context.fill();
      context.stroke();
    } else if (!far) {
      context.fillStyle = palette.text;
      context.font = `600 ${decorator.size}px ${families.text}`;
      context.fillText(decorator.text, decorator.x, decorator.baseline);
    }
  }

  if (far) {
    drawFarIdentifier(context, node, palette, families, scale);
    return;
  }
  for (const line of node.text) {
    context.fillStyle = line.role === 'identifier' ? palette.secondary : palette.text;
    context.font = `${line.weight} ${line.size}px ${line.role === 'identifier' ? families.identifier : families.text}`;
    context.fillText(line.text, line.x, line.baseline);
  }
}

/** Zoomed far out: the identifier alone, at metadata size on screen, centred, if it fits. */
function drawFarIdentifier(context: CanvasRenderingContext2D, node: DiagramNode, palette: Palette, families: Families, scale: number) {
  const identifier = node.text.find((line) => line.role === 'identifier');
  if (!identifier) return;
  const size = FAR_IDENTIFIER_PX / scale;
  context.font = `400 ${size}px ${families.identifier}`;
  const width = context.measureText(identifier.text).width;
  const rect = node.shape.rect;
  if (width > rect.width * 0.9 || size > rect.height * 0.9) return;
  context.fillStyle = palette.text;
  context.textBaseline = 'middle';
  context.fillText(identifier.text, rect.x + (rect.width - width) / 2, rect.y + rect.height / 2);
  context.textBaseline = 'alphabetic';
}
