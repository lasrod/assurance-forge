// Draws the render model on a Canvas 2D context. The geometry is the core's
// (af-layout); this only paints it. Lines keep 1 CSS px at every zoom, and
// shapes, text and arrowheads scale (DATUM's rule for a diagram canvas).

import type {
  AcpMark, Diagram, DiagramEdge, DiagramNode, Point, Rect } from '../core/Core';

/**
 * The diagram's own colours (`--af-diagram-*` in app.css): a ground apart
 * from the panels' surfaces, with the nodes as paper on it.
 */
export interface Palette {
  /** The ground the diagram is on. */
  ground: string;
  /** The dots of the ground's grid. */
  grid: string;
  /** A node's fill. */
  fill: string;
  /** Outlines, connectors, arrowheads. */
  line: string;
  /** A node's text. */
  text: string;
  /** The identifier. */
  secondary: string;
  /** The fill of the node under the pointer. */
  hover: string;
  /** The selected node's fill. */
  selectedFill: string;
  /** The selected node's outline, and a selected line. */
  selectedLine: string;
  /** Forced colours are on: the selected outline is heavier, since there is no fill to see. */
  forced: boolean;
}

/**
 * What is selected and what is under the pointer: separate states (DATUM).
 * Each is a node's SACM id, or the key of one edge (`edgeKey`).
 */
export interface Marks {
  selected: string | null;
  hovered: string | null;
  /** A move being chosen: the node to move, and the nodes it may go under. The rest is drawn faint. */
  move?: MoveMarks | null;
  /** What follows the pointer while a node is dragged: where the pointer is, in CSS px of the canvas, and what it says. */
  badge?: Badge | null;
}

export interface MoveMarks {
  source: string;
  allowed: ReadonlySet<string>;
  /** For a moved challenge: the lines (`edgeKey`) it may go to. */
  lines?: ReadonlySet<string>;
}

export interface Badge {
  pointer: Point;
  label: string;
}

/** How faint what a move cannot go to is drawn. */
const FAINT = 0.35;

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

/**
 * What names one edge of the diagram: its relationship and its two ends. One
 * SACM relationship may be drawn as several edges (an inference through a
 * Strategy, or with several premises), and each is selected by itself. An XML
 * id holds no space, so the key cannot be a node's id.
 */
export function edgeKey(edge: Pick<DiagramEdge, 'relationship' | 'from' | 'to'>): string {
  return `${edge.relationship} ${edge.from} ${edge.to}`;
}

/** The edge with this key, or null. */
export function findEdge(diagram: Diagram, key: string): DiagramEdge | null {
  return diagram.edges.find((edge) => edgeKey(edge) === key) ?? null;
}

/** How near a line the pointer selects it, in CSS px on either side. */
export const EDGE_REACH = 5;

/** How far the diagram point is from the segment `a` to `b`. */
function segmentDistance(point: Point, a: Point, b: Point): number {
  const dx = b.x - a.x;
  const dy = b.y - a.y;
  const length = dx * dx + dy * dy;
  const t = length === 0 ? 0 : clamp(((point.x - a.x) * dx + (point.y - a.y) * dy) / length, 0, 1);
  return Math.hypot(point.x - (a.x + dx * t), point.y - (a.y + dy * t));
}

/** How far the diagram point is from the edge's line and arrowhead. */
export function edgeDistance(edge: DiagramEdge, point: Point): number {
  const [tip, left, right] = edge.head;
  const line = [...edge.points, tip];
  let nearest = Infinity;
  for (let index = 1; index < line.length; index += 1) {
    nearest = Math.min(nearest, segmentDistance(point, line[index - 1], line[index]));
  }
  return Math.min(nearest, segmentDistance(point, tip, left), segmentDistance(point, tip, right), segmentDistance(point, left, right));
}

/**
 * The edge whose line is within `EDGE_REACH` CSS px of the canvas point
 * (`x`, `y`): the nearest, and of two as near, the one drawn last. The reach
 * is on screen, so a line is as easy to take at any zoom.
 */
export function hitEdge(diagram: Diagram, viewport: Viewport, x: number, y: number): DiagramEdge | null {
  const point = toDiagram(viewport, x, y);
  const reach = EDGE_REACH / viewport.scale;
  let found: DiagramEdge | null = null;
  let nearest = reach;
  for (const edge of diagram.edges) {
    const { bounds } = edge;
    if (point.x < bounds.x - reach || point.x > bounds.x + bounds.width + reach) continue;
    if (point.y < bounds.y - reach || point.y > bounds.y + bounds.height + reach) continue;
    const distance = edgeDistance(edge, point);
    if (distance <= nearest) {
      nearest = distance;
      found = edge;
    }
  }
  return found;
}

/**
 * The one line under the canvas point, for what changes the argument there:
 * where lines of several relationships are as near, as on a trunk sibling
 * lines share, it is not said which the pointer means, and none is.
 * `ambiguous` says so. Lines of one relationship, such as a Strategy's
 * premises, are one relationship to challenge, and the nearest is taken.
 */
export function hitOneEdge(diagram: Diagram, viewport: Viewport, x: number, y: number): { edge: DiagramEdge | null; ambiguous: boolean } {
  const found = hitEdge(diagram, viewport, x, y);
  if (!found) return { edge: null, ambiguous: false };
  const point = toDiagram(viewport, x, y);
  // As near within a pixel on screen.
  const nearest = edgeDistance(found, point) + 1 / viewport.scale;
  const ambiguous = diagram.edges.some((edge) => edge.relationship !== found.relationship && edgeDistance(edge, point) <= nearest);
  return ambiguous ? { edge: null, ambiguous } : { edge: found, ambiguous };
}

/**
 * The assurance claim point under the canvas point: on its square, with a
 * margin of 2 px on screen, or on its label. Null anywhere else.
 */
export function hitAcp(diagram: Diagram, viewport: Viewport, x: number, y: number): AcpMark | null {
  const point = toDiagram(viewport, x, y);
  // A small margin on screen, so a click on the line beside it is the line's.
  const reach = 2 / viewport.scale;
  for (const edge of diagram.edges) {
    for (const acp of edge.acps) {
      const { square, label } = acp;
      const onSquare =
        point.x >= square.x - reach &&
        point.x <= square.x + square.width + reach &&
        point.y >= square.y - reach &&
        point.y <= square.y + square.height + reach;
      const onLabel =
        point.x >= label.x && point.x <= label.x + label.width && point.y >= label.baseline - label.size && point.y <= label.baseline + label.size * 0.3;
      if (onSquare || onLabel) return acp;
    }
  }
  return null;
}

/**
 * The assurance claim points on `edge`: each a solid square on the line, in
 * the line's colour, and its label beside it as an identifier is drawn. Far
 * out, the squares alone.
 */
function drawAcps(context: CanvasRenderingContext2D, edge: DiagramEdge, palette: Palette, families: Families, scale: number, marks: Marks) {
  // As drawEdge strokes the line: marked when selected, or a place.
  const key = edgeKey(edge);
  const marked = marks.selected === key || marks.move?.lines?.has(key) === true;
  for (const acp of edge.acps) {
    const { square, label } = acp;
    context.fillStyle = marked ? palette.selectedLine : palette.line;
    context.fillRect(square.x, square.y, square.width, square.height);
    if (label.size * scale < SMALLEST_TEXT_PX) continue;
    context.fillStyle = palette.secondary;
    context.font = `${label.weight} ${label.size}px ${families.identifier}`;
    context.fillText(label.text, label.x, label.baseline);
  }
}

/**
 * The other argument package whose name is under the canvas point: a node of
 * another package, in a package's diagram, shows it in its `package` lines,
 * and a click there opens that package. Null anywhere else.
 */
export function hitPackage(diagram: Diagram, viewport: Viewport, x: number, y: number): string | null {
  const node = hitTest(diagram, viewport, x, y);
  if (!node?.package) return null;
  const point = toDiagram(viewport, x, y);
  const on = node.text.some(
    (line) =>
      line.role === 'package' &&
      point.x >= line.x &&
      point.x <= line.x + line.width &&
      point.y >= line.baseline - line.size &&
      point.y <= line.baseline + line.size * 0.3,
  );
  return on ? node.package : null;
}

/**
 * What is under the canvas point: the id of a node, which is drawn over the
 * lines, or else the key of the edge whose line is there.
 */
export function hitId(diagram: Diagram, viewport: Viewport, x: number, y: number): string | null {
  const node = hitTest(diagram, viewport, x, y);
  if (node) return node.id;
  const edge = hitEdge(diagram, viewport, x, y);
  return edge ? edgeKey(edge) : null;
}

/** Everything drawn for the node with this id or the edge with this key, or null when it is neither. */
export function boundsOf(diagram: Diagram, id: string): Rect | null {
  const node = diagram.nodes.find((candidate) => candidate.id === id);
  return node ? node.bounds : (findEdge(diagram, id)?.bounds ?? null);
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
/** The smallest it is set at, to fit a narrow node, before it is cut short. */
const FAR_IDENTIFIER_MIN_PX = 9;

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
  drawGrid(context, viewport, palette, size);
  context.setTransform(size.dpr * scale, 0, 0, size.dpr * scale, size.dpr * offsetX, size.dpr * offsetY);
  context.lineWidth = 1 / scale;
  context.lineJoin = 'round';

  // Only what is in view is drawn.
  const visible = visibleArea(viewport, size.width, size.height);
  const drawn = { nodes: 0, edges: 0 };
  // A marked line goes over the others, the selected one last.
  // Drawn last, so over the rest: the selected line, the hovered, and the
  // lines a moved challenge may go to over the faint ones.
  const places = marks.move?.lines;
  const rank = (edge: DiagramEdge) => {
    const key = edgeKey(edge);
    return key === marks.selected ? 3 : key === marks.hovered ? 2 : places?.has(key) ? 1 : 0;
  };
  const edges = marks.selected || marks.hovered || places ? [...diagram.edges].sort((a, b) => rank(a) - rank(b)) : diagram.edges;
  const move = marks.move ?? null;
  if (move) context.globalAlpha = FAINT;
  for (const edge of edges) {
    if (!overlaps(edge.bounds, visible)) continue;
    // A line a moved challenge may go to is drawn in full.
    if (move) context.globalAlpha = move.lines?.has(edgeKey(edge)) ? 1 : FAINT;
    drawEdge(context, edge, palette, scale, marks);
    drawAcps(context, edge, palette, families, scale, marks);
    drawn.edges += 1;
  }
  for (const node of diagram.nodes) {
    if (!overlaps(node.bounds, visible)) continue;
    // While a move is chosen, only the node moved and where it may go are
    // drawn in full.
    if (move) context.globalAlpha = node.id === move.source || move.allowed.has(node.id) ? 1 : FAINT;
    drawNode(context, node, palette, families, scale, marks);
    drawn.nodes += 1;
  }
  context.globalAlpha = 1;
  if (marks.badge) drawBadge(context, marks.badge, palette, families, size.dpr);
  return drawn;
}

/** Beside the pointer, not under it: the place below stays readable. */
const BADGE_OFFSET = 14;
const BADGE_HEIGHT = 24;
const BADGE_PAD = 8;

/**
 * The mark that follows the pointer while a node is dragged: its identifier
 * and how many elements come with it, in CSS px whatever the zoom.
 */
function drawBadge(context: CanvasRenderingContext2D, badge: Badge, palette: Palette, families: Families, dpr: number) {
  context.save();
  context.setTransform(dpr, 0, 0, dpr, 0, 0);
  context.font = `600 12px ${families.text}`;
  const width = context.measureText(badge.label).width + 2 * BADGE_PAD;
  const [x, y] = [badge.pointer.x + BADGE_OFFSET, badge.pointer.y + BADGE_OFFSET];
  context.fillStyle = palette.selectedFill;
  context.strokeStyle = palette.selectedLine;
  context.lineWidth = palette.forced ? 2 : 1;
  context.beginPath();
  context.rect(x, y, width, BADGE_HEIGHT);
  context.fill();
  context.stroke();
  context.fillStyle = palette.text;
  context.textBaseline = 'middle';
  context.fillText(badge.label, x + BADGE_PAD, y + BADGE_HEIGHT / 2);
  context.restore();
}

/** The grid's pitch, in diagram units. */
export const GRID = 24;
/** Dots closer than this, in CSS px, are not drawn: they would be a tint, not a grid. */
const SMALLEST_GRID_PX = 12;

/** One tile of the grid, made once for each size and colour, for each context. */
const tiles = new WeakMap<CanvasRenderingContext2D, { key: string; pattern: CanvasPattern | null }>();

/**
 * A pattern of one dot of `dot` device px in a cell of `cell`, or null where
 * there is no document to make it in (a test's context).
 */
function gridTile(context: CanvasRenderingContext2D, cell: number, dot: number, colour: string): CanvasPattern | null {
  const key = `${cell} ${dot} ${colour}`;
  const held = tiles.get(context);
  if (held?.key === key) return held.pattern;
  let pattern: CanvasPattern | null = null;
  if (typeof document !== 'undefined') {
    const tile = document.createElement('canvas');
    tile.width = cell;
    tile.height = cell;
    const drawn = tile.getContext('2d');
    if (drawn) {
      drawn.fillStyle = colour;
      drawn.fillRect(0, 0, dot, dot);
      pattern = context.createPattern(tile, 'repeat');
    }
  }
  tiles.set(context, { key, pattern });
  return pattern;
}

/**
 * The ground's dots: one each `GRID` diagram units, so they move and scale
 * with the diagram and say where it is. Each is one CSS px at every zoom. It
 * is one fill with a pattern, whatever the number of dots.
 */
function drawGrid(context: CanvasRenderingContext2D, viewport: Viewport, palette: Palette, size: { width: number; height: number; dpr: number }) {
  const pitch = GRID * viewport.scale;
  if (palette.forced || pitch < SMALLEST_GRID_PX) return;
  // The tile is a whole number of device px, scaled to the exact pitch so the
  // dots stay on the diagram's grid however far it reaches.
  const exact = pitch * size.dpr;
  const cell = Math.round(exact);
  const pattern = gridTile(context, cell, Math.max(1, Math.round(size.dpr)), palette.grid);
  if (!pattern) return;
  const first = (offset: number) => ((offset % pitch) + pitch) % pitch;
  pattern.setTransform(
    new DOMMatrix().translateSelf(first(viewport.offsetX) * size.dpr, first(viewport.offsetY) * size.dpr).scaleSelf(exact / cell),
  );
  context.setTransform(1, 0, 0, 1, 0, 0);
  context.fillStyle = pattern;
  context.fillRect(0, 0, size.width * size.dpr, size.height * size.dpr);
}

function polyline(context: CanvasRenderingContext2D, points: Point[]) {
  context.beginPath();
  points.forEach((point, index) => (index === 0 ? context.moveTo(point.x, point.y) : context.lineTo(point.x, point.y)));
}

/** The dashes of each line style, in CSS px: a challenge is dashed, a neutral edge dotted. */
const DASHES = { solid: [], dashed: [6, 4], dotted: [1.5, 3.5] } as const;
/** A line's width in CSS px: plain, under the pointer, and selected. */
const EDGE_WIDTH = { plain: 1, hovered: 2, selected: 2.5, dropOn: 4 } as const;

function drawEdge(context: CanvasRenderingContext2D, edge: DiagramEdge, palette: Palette, scale: number, marks: Marks) {
  // Selected and hovered are told apart by weight as well as colour.
  const hovered = marks.hovered === edgeKey(edge);
  // Every line a moved challenge may go to is marked as a place, and the
  // one under the pointer, where it would be dropped, the more.
  const place = marks.move?.lines?.has(edgeKey(edge)) === true;
  const selected = marks.selected === edgeKey(edge) || place;
  const stroke = selected ? palette.selectedLine : palette.line;
  context.strokeStyle = stroke;
  const width = place && hovered ? EDGE_WIDTH.dropOn : selected ? EDGE_WIDTH.selected : hovered ? EDGE_WIDTH.hovered : EDGE_WIDTH.plain;
  context.lineWidth = width / scale;
  context.lineCap = edge.line === 'dotted' ? 'round' : 'butt';
  context.setLineDash(DASHES[edge.line].map((length) => length / scale));
  polyline(context, edge.points);
  context.stroke();
  context.setLineDash([]);
  context.lineCap = 'butt';
  const [tip, left, right] = edge.head;
  if (edge.arrowhead === 'open') {
    polyline(context, [left, tip, right]);
    context.stroke();
  } else {
    polyline(context, [tip, left, right]);
    context.closePath();
    context.fillStyle = edge.arrowhead === 'filled' ? stroke : palette.ground;
    context.fill();
    context.stroke();
  }
  context.lineWidth = 1 / scale;
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
  // While a move is chosen the node moved is the marked one, and a place it
  // may go has the heavier outline: by weight, not by colour alone.
  const target = marks.move?.allowed.has(node.id) ?? false;
  const selected = marks.move ? marks.move.source === node.id : marks.selected === node.id;
  const hovered = marks.hovered === node.id && (!marks.move || target);
  shapePath(context, node);
  context.fillStyle = selected ? palette.selectedFill : hovered ? palette.hover : palette.fill;
  context.fill();
  context.strokeStyle = selected || target ? palette.selectedLine : palette.line;
  context.lineWidth = (target ? (hovered ? 3 : 2) : selected && palette.forced ? 2 : 1) / scale;
  context.setLineDash(node.shape.type === 'neutral' ? [4 / scale, 3 / scale] : []);
  context.stroke();
  context.setLineDash([]);
  context.lineWidth = 1 / scale;

  // Zoomed far out the text is not drawn, the A and J included: only the
  // identifier, at metadata size (DATUM's canvas rule 5).
  const sized = node.text.find((line) => line.role !== 'identifier' && line.role !== 'package') ?? node.text[0];
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
    const metadata = line.role === 'identifier' || line.role === 'package';
    context.fillStyle = metadata ? palette.secondary : palette.text;
    context.font = `${line.weight} ${line.size}px ${metadata ? families.identifier : families.text}`;
    context.fillText(line.text, line.x, line.baseline);
    // The other package's name opens it: drawn as a link is.
    if (line.role === 'package') {
      context.fillRect(line.x, line.baseline + 2 / scale, line.width, 1 / scale);
    }
  }
}

/**
 * The identifier as it fits `room` px: whole, or its start and an ellipsis.
 * Only what is drawn is cut; the identifier itself is not changed.
 */
export function fitIdentifier(text: string, room: number, measure: (text: string) => number): string {
  if (measure(text) <= room) return text;
  const characters = Array.from(text);
  let [low, high] = [0, characters.length];
  while (low < high) {
    const middle = Math.ceil((low + high) / 2);
    if (measure(`${characters.slice(0, middle).join('')}…`) <= room) low = middle;
    else high = middle - 1;
  }
  return low === 0 ? '' : `${characters.slice(0, low).join('')}…`;
}

/**
 * Zoomed far out: the identifier alone, centred, at metadata size on screen;
 * smaller, down to 9 px, in a node too narrow for it; and past that, its
 * start and an ellipsis. A node is never left blank while its identifier's
 * first characters fit.
 */
function drawFarIdentifier(context: CanvasRenderingContext2D, node: DiagramNode, palette: Palette, families: Families, scale: number) {
  const identifier = node.text.find((line) => line.role === 'identifier');
  if (!identifier) return;
  const rect = node.shape.rect;
  const room = rect.width * 0.9;
  const fontAt = (px: number) => `400 ${px / scale}px ${families.identifier}`;
  context.font = fontAt(FAR_IDENTIFIER_PX);
  const whole = context.measureText(identifier.text).width;
  const px = whole > room ? Math.max(FAR_IDENTIFIER_MIN_PX, (FAR_IDENTIFIER_PX * room) / whole) : FAR_IDENTIFIER_PX;
  if (px / scale > rect.height * 0.9) return;
  context.font = fontAt(px);
  const text = fitIdentifier(identifier.text, room, (candidate) => context.measureText(candidate).width);
  if (!text) return;
  const width = context.measureText(text).width;
  context.fillStyle = palette.text;
  context.textBaseline = 'middle';
  context.fillText(text, rect.x + (rect.width - width) / 2, rect.y + rect.height / 2);
  context.textBaseline = 'alphabetic';
}

/**
 * The view moved so that `bounds` (diagram units) is wholly in a canvas of
 * `width` × `height` CSS px, centred, or `viewport` itself when it already
 * is. The zoom stays.
 */
export function reveal(viewport: Viewport, bounds: Rect, width: number, height: number): Viewport {
  const left = bounds.x * viewport.scale + viewport.offsetX;
  const top = bounds.y * viewport.scale + viewport.offsetY;
  const [shownWidth, shownHeight] = [bounds.width * viewport.scale, bounds.height * viewport.scale];
  if (left >= 0 && top >= 0 && left + shownWidth <= width && top + shownHeight <= height) return viewport;
  return {
    ...viewport,
    offsetX: viewport.offsetX + (width - shownWidth) / 2 - left,
    offsetY: viewport.offsetY + (height - shownHeight) / 2 - top,
  };
}

/**
 * The view moved so that a node laid out again from `before` to `after`
 * (diagram units) stays where it was on the screen. The zoom stays.
 */
export function keepInPlace(viewport: Viewport, before: Rect, after: Rect): Viewport {
  return {
    ...viewport,
    offsetX: viewport.offsetX + (before.x - after.x) * viewport.scale,
    offsetY: viewport.offsetY + (before.y - after.y) * viewport.scale,
  };
}
