// Generated from crates/af-api/src/contract.rs by its tests; do not edit.
// To regenerate: AF_UPDATE_BINDINGS=1 cargo test -p af-api --test bindings

export type OpenResult = { "result": "opened", revision: number, 
/**
 * What libSACM reported while reading and validating it.
 */
diagnostics: DiagnosticCounts, nodes: number, edges: number, 
/**
 * The view's codes and the elements it did not place.
 */
viewCodes: number, } | { "result": "refused" } & Refusal;

export type OpenedFile = { name: string, result: OpenResult, };

export type DiagnosticCounts = { errors: number, warnings: number, infos: number, };

export type Refusal = { 
/**
 * Such as `SACM-XML-001`.
 */
code: string, 
/**
 * Such as `SACM-XML-001/malformed`.
 */
template: string, arguments: Array<MessageArgument>, 
/**
 * 1-based, where the refusal points.
 */
line: number | null, column: number | null, };

export type MessageArgument = { name: string, value: ArgumentValue, };

export type ArgumentValue = { "type": "text", "value": string } | { "type": "count", "value": number };

export type DiagramAt = { 
/**
 * The document's revision.
 */
revision: number, 
/**
 * Which layout of it: it changes when the language or the text sizes
 * do, which change the diagram but not the document.
 */
layout: number, diagram: Diagram, };

export type ElementDetails = { 
/**
 * The SACM id.
 */
id: string, kind: NodeKind, undeveloped: boolean, identifier: string, name: string | null, statement: string | null, 
/**
 * The view's codes about this element.
 */
codes: Array<ViewCode>, };

export type ViewCode = { 
/**
 * Such as `AF-VIEW-003`.
 */
code: string, 
/**
 * The id an end or reasoning names, for `AF-VIEW-009`.
 */
end: string | null, };

export type NodeKind = "goal" | "strategy" | "solution" | "context" | "assumption" | "justification" | "neutral";

export type Outline = { 
/**
 * The document's revision.
 */
revision: number, rows: Array<OutlineRow>, };

export type OutlineRow = { 
/**
 * The SACM id, as in the diagram.
 */
id: string, 
/**
 * 1 for a root.
 */
level: number, kind: NodeKind, undeveloped: boolean, identifier: string, name: string | null, statement: string | null, };

export type Package = { 
/**
 * The SACM id.
 */
id: string, 
/**
 * 1 for a package at the top of the file.
 */
level: number, 
/**
 * The SACM class, such as `ArgumentPackage`: the frontend words it.
 */
kind: string, name: string | null, 
/**
 * How many elements it holds directly, utility elements (descriptions,
 * notes, tagged values) aside.
 */
elements: number, };

export type Point = { x: number, y: number, };

export type Rect = { x: number, y: number, width: number, height: number, };

export type Shape = { "type": "rectangle", rect: Rect, } | { "type": "parallelogram", rect: Rect, skew: number, } | { "type": "roundedRectangle", rect: Rect, radius: number, } | { "type": "ellipse", rect: Rect, } | { "type": "circle", rect: Rect, } | { "type": "neutral", rect: Rect, };

export type TextRole = "identifier" | "name" | "statement";

export type TextLine = { text: string, role: TextRole, x: number, baseline: number, 
/**
 * The measured width, which `measureText` should match.
 */
width: number, size: number, 
/**
 * 400 or 600.
 */
weight: number, };

export type Decorator = { "type": "undeveloped", centre: Point, half: number, } | { "type": "letter", text: string, x: number, baseline: number, size: number, };

export type DiagramNode = { 
/**
 * The SACM id.
 */
id: string, kind: NodeKind, shape: Shape, text: Array<TextLine>, decorators: Array<Decorator>, 
/**
 * Everything drawn for the node. No two nodes' bounds overlap.
 */
bounds: Rect, };

export type EdgeKind = "supportedBy" | "inContextOf" | "neutral";

export type Arrowhead = "filled" | "hollow" | "open";

export type DiagramEdge = { 
/**
 * The SACM id of the relationship.
 */
relationship: string, from: string, to: string, kind: EdgeKind, dashed: boolean, 
/**
 * From the `from` node's outline to the arrowhead.
 */
points: Array<Point>, arrowhead: Arrowhead, 
/**
 * The tip, on the `to` node's outline, then the other two corners.
 */
head: Array<Point>, 
/**
 * Everything drawn for the edge, for culling.
 */
bounds: Rect, };

export type Diagram = { nodes: Array<DiagramNode>, edges: Array<DiagramEdge>, bounds: Rect, };
