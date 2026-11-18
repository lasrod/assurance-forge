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

/** The viewport that fits `bounds` in a `width` × `height` canvas, centred, at most 100%. */
export function fit(bounds: Rect, width: number, height: number): Viewport {
  const room = (size: number) => Math.max(size - 2 * FIT_MARGIN, 1);
  const scale = Math.min(Math.max(Math.min(room(width) / bounds.width, room(height) / bounds.height, 1), MIN_SCALE), MAX_SCALE);
  return {
    scale,
    offsetX: (width - bounds.width * scale) / 2 - bounds.x * scale,
    offsetY: (height - bounds.height * scale) / 2 - bounds.y * scale,
  };
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

function edgeBounds(edge: DiagramEdge): Rect {
  const points = [...edge.points, ...edge.head];
  const xs = points.map((point) => point.x);
  const ys = points.map((point) => point.y);
  const x = Math.min(...xs);
  const y = Math.min(...ys);
  return { x, y, width: Math.max(...xs) - x, height: Math.max(...ys) - y };
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
  fontFamily: string,
  size: { width: number; height: number; dpr: number },
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
    if (!overlaps(edgeBounds(edge), visible)) continue;
    drawEdge(context, edge, palette, scale);
    drawn.edges += 1;
  }
  for (const node of diagram.nodes) {
    if (!overlaps(node.bounds, visible)) continue;
    drawNode(context, node, palette, fontFamily, scale);
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

function drawNode(context: CanvasRenderingContext2D, node: DiagramNode, palette: Palette, fontFamily: string, scale: number) {
  shapePath(context, node);
  context.fillStyle = palette.ground;
  context.fill();
  context.strokeStyle = palette.line;
  context.setLineDash(node.shape.type === 'neutral' ? [4 / scale, 3 / scale] : []);
  context.stroke();
  context.setLineDash([]);

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
      context.font = `600 ${decorator.size}px ${fontFamily}`;
      context.fillText(decorator.text, decorator.x, decorator.baseline);
    }
  }

  if (far) {
    drawFarIdentifier(context, node, palette, fontFamily, scale);
    return;
  }
  for (const line of node.text) {
    context.fillStyle = line.role === 'identifier' ? palette.secondary : palette.text;
    context.font = `${line.weight} ${line.size}px ${fontFamily}`;
    context.fillText(line.text, line.x, line.baseline);
  }
}

/** Zoomed far out: the identifier alone, at metadata size on screen, centred, if it fits. */
function drawFarIdentifier(context: CanvasRenderingContext2D, node: DiagramNode, palette: Palette, fontFamily: string, scale: number) {
  const identifier = node.text.find((line) => line.role === 'identifier');
  if (!identifier) return;
  const size = FAR_IDENTIFIER_PX / scale;
  context.font = `400 ${size}px ${fontFamily}`;
  const width = context.measureText(identifier.text).width;
  const rect = node.shape.rect;
  if (width > rect.width * 0.9 || size > rect.height * 0.9) return;
  context.fillStyle = palette.text;
  context.textBaseline = 'middle';
  context.fillText(identifier.text, rect.x + (rect.width - width) / 2, rect.y + rect.height / 2);
  context.textBaseline = 'alphabetic';
}
