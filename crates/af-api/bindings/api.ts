// Generated from crates/af-api/src/contract.rs by its tests; do not edit.
// To regenerate: AF_UPDATE_BINDINGS=1 cargo test -p af-api --test bindings

export type OpenResult = { "result": "opened", revision: number, 
/**
 * The problem list's counts (`Problems::counts`): what libSACM
 * reported reading and validating it, the GSN checks, and, among the
 * information, what the diagram does not draw.
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
 * do, or another package is shown, which change the diagram but not the
 * document.
 */
layout: number, 
/**
 * The SACM id of the argument package the diagram is of, or null for
 * the whole case.
 */
package: string | null, diagram: Diagram, };

export type ElementDetails = { 
/**
 * The SACM id.
 */
id: string, kind: NodeKind, undeveloped: boolean, 
/**
 * Why the undeveloped mark cannot be changed to what it is not
 * (`AF-EDIT-021`: not a Goal; `AF-EDIT-022`: the Goal is supported), or
 * null when it can.
 */
undevelopedRefusal: string | null, 
/**
 * A `SupportedBy` line leaves the node: with `undeveloped`, the mark
 * is out of date.
 */
supported: boolean, identifier: string, name: string | null, statement: string | null, 
/**
 * The view's codes about this element.
 */
codes: Array<ViewCode>, };

export type RelationshipDetails = { 
/**
 * The SACM id.
 */
id: string, 
/**
 * The SACM class, such as `AssertedInference`: the frontend words it.
 */
class: string, 
/**
 * How its edges are drawn.
 */
kind: EdgeKind, name: string | null, 
/**
 * SACM's target: the conclusion of support, what context is for, what a
 * challenge challenges.
 */
target: RelationshipEnd | null, 
/**
 * SACM's sources: the premises, the context, what challenges.
 */
sources: Array<RelationshipEnd>, 
/**
 * The `reasoning`: the Strategy of an inference.
 */
reasoning: RelationshipEnd | null, 
/**
 * The view's codes about this relationship.
 */
codes: Array<ViewCode>, };

export type RelationshipEnd = { 
/**
 * The SACM id the relationship names.
 */
id: string, 
/**
 * The GSN identifier, when the end is a node of the diagram.
 */
identifier: string | null, 
/**
 * What the node is drawn as, when the end is a node of the diagram.
 */
node: NodeKind | null, 
/**
 * The end is a relationship the diagram draws: a challenge may point at
 * one.
 */
relationship: boolean, };

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

export type TextRole = "identifier" | "name" | "statement" | "package" | "acp";

export type AcpMark = { 
/**
 * The SACM id of the confidence Claim.
 */
claim: string, 
/**
 * The SACM id of its argument package when another than the edge's:
 * what a click opens. Null for a description.
 */
package: string | null, 
/**
 * The square, centred on the line.
 */
square: Rect, label: TextLine, };

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
id: string, 
/**
 * In a package's diagram, the SACM id of the other argument package the
 * node belongs to, whose name its `package` lines show; null otherwise.
 */
package: string | null, kind: NodeKind, shape: Shape, text: Array<TextLine>, decorators: Array<Decorator>, 
/**
 * Everything drawn for the node. No two nodes' bounds overlap.
 */
bounds: Rect, };

export type EdgeKind = "supportedBy" | "inContextOf" | "challenge" | "neutral";

export type LineStyle = "solid" | "dashed" | "dotted";

export type Arrowhead = "filled" | "hollow" | "open";

export type DiagramEdge = { 
/**
 * The SACM id of the relationship.
 */
relationship: string, from: string, 
/**
 * The SACM id of what the arrow points at: a node, or a relationship
 * when `to_relationship` is set.
 */
to: string, 
/**
 * A challenge of a relationship: the tip is on that relationship's line.
 */
toRelationship: boolean, kind: EdgeKind, line: LineStyle, 
/**
 * From the `from` node's outline to the arrowhead. A curve is given as
 * the points along it.
 */
points: Array<Point>, arrowhead: Arrowhead, 
/**
 * The tip, on what the arrow points at, then the other two corners.
 */
head: Array<Point>, 
/**
 * The assurance claim points on the line, each a solid square with its
 * label beside it; on a relationship's first edge only.
 */
acps: Array<AcpMark>, 
/**
 * Everything drawn for the edge, for culling.
 */
bounds: Rect, };

export type Diagram = { nodes: Array<DiagramNode>, edges: Array<DiagramEdge>, bounds: Rect, };

export type ElementKind = "goal" | "strategy" | "solution" | "context" | "assumption" | "justification";

export type TextField = "identifier" | "name" | "statement";

export type AddChoice = { kind: ElementKind, 
/**
 * Why it cannot be added there (`AF-EDIT-001` to `AF-EDIT-006`), or
 * null when it can. The menu leaves out a kind that cannot be added.
 */
refusal: string | null, 
/**
 * Adding it also clears the parent Goal's undeveloped decorator (W11),
 * which the diagram then shows; the menu has no notes.
 */
develops: boolean, };

export type AddChoices = { revision: number, 
/**
 * The six kinds, in the order the standard introduces them.
 */
choices: Array<AddChoice>, };

export type RemovalNode = { 
/**
 * The SACM id.
 */
id: string, kind: NodeKind, 
/**
 * The GSN identifier.
 */
identifier: string, name: string | null, statement: string | null, };

export type Removal = { revision: number, 
/**
 * The nodes it deletes: the node chosen first, then what goes with it.
 * Empty when it is refused.
 */
nodes: Array<RemovalNode>, 
/**
 * Why it cannot be removed (`AF-EDIT-011`, `AF-EDIT-015`, or
 * `AF-EDIT-005` when a Strategy kept has no package to hold the place
 * of its next Goal), or null when it can. The frontend words it.
 */
refusal: string | null, };

export type MoveTarget = { 
/**
 * The SACM id of the node.
 */
id: string, 
/**
 * Why the moved node cannot go under it (`AF-EDIT-001` to `AF-EDIT-004`,
 * `AF-EDIT-018` to `AF-EDIT-020`, `AF-EDIT-025`; and, from the
 * operations the move would make there, `AF-EDIT-005`, `AF-EDIT-015` or
 * `AF-EDIT-017`), or null when it can. The frontend
 * words it.
 */
refusal: string | null, 
/**
 * Moving there also clears this Goal's undeveloped decorator.
 */
develops: boolean, };

export type MoveLine = { 
/**
 * The SACM id of the relationship the line is drawn for.
 */
relationship: string, 
/**
 * The nodes the line runs from and to.
 */
from: string, to: string, 
/**
 * Why the challenge cannot go there (`AF-EDIT-002`, `AF-EDIT-004`,
 * `AF-EDIT-005`, `AF-EDIT-011`, `AF-EDIT-018` to `AF-EDIT-020`,
 * `AF-EDIT-027`), or
 * null when it can.
 */
refusal: string | null, };

export type MoveTargets = { revision: number, 
/**
 * The SACM id of the node to move.
 */
id: string, 
/**
 * Why the node cannot be moved at all (`AF-EDIT-002`, `AF-EDIT-004`,
 * `AF-EDIT-005`, `AF-EDIT-011`, `AF-EDIT-016`, `AF-EDIT-017`,
 * `AF-EDIT-024`), or null when it can. `nodes` is then empty.
 */
refusal: string | null, 
/**
 * Why it cannot be moved to the top level (`AF-EDIT-006`,
 * `AF-EDIT-019`; and, from the operations the move would make,
 * `AF-EDIT-005`, `AF-EDIT-015` or `AF-EDIT-017`), or null when it can.
 */
canvas: string | null, 
/**
 * Every node of the diagram, in its order.
 */
nodes: Array<MoveTarget>, 
/**
 * For a counter goal or counter evidence, every line of the diagram, in
 * its order: a challenge may be moved onto a line (W32). Empty for any
 * other node.
 */
lines: Array<MoveLine>, };

export type ProblemRepair = { element: string, relationship: string, };

export type EditResult = { "result": "applied", revision: number, 
/**
 * The element the interface selects: the one an add made, the node
 * a removed one was drawn under, or the node moved. Null for undo,
 * redo, a change of text and the removal of a top-level node.
 */
element: string | null, 
/**
 * The SACM ids the command created, changed and deleted.
 */
created: Array<string>, changed: Array<string>, deleted: Array<string>, } | { "result": "refused", code: string, } | { "result": "unchanged" };

export type History = { revision: number, canUndo: boolean, canRedo: boolean, 
/**
 * The document has changes that are not saved: it is not in the state
 * it was opened or last saved in. Undo back to that state is saved
 * again (plan decision S3).
 */
modified: boolean, };

export type WriteResult = { "result": "written", revision: number, xmi: string, } | { "result": "refused", code: string, };

export type SaveResult = { "result": "saved", revision: number, name: string, } | { "result": "refused", code: string, } | { "result": "cancelled" };

export type RecoveryCopy = { "result": "keep", revision: number, copy: string, } | { "result": "remove", revision: number, } | { "result": "refused", code: string, };

export type FoundRecovery = { "result": "found", name: string, writtenAt: number, baseMatches: boolean, } | { "result": "unreadable", code: string, };

export type KeepResult = { "result": "kept" } | { "result": "removed" } | { "result": "skipped" } | { "result": "refused", code: string, };

export type Problems = { revision: number, problems: Array<Problem>, 
/**
 * How many of each severity, what the status bar shows: every problem,
 * those of the diagram among the information.
 */
counts: DiagnosticCounts, };

export type Problem = { source: ProblemSource, severity: ProblemSeverity, 
/**
 * Such as `AF-GSN-001`, `SACM-REF-001` or `AF-VIEW-003`.
 */
code: string, 
/**
 * The requirement it enforces: a GSN v3 requirement, such as
 * `GSN3-CORE-014`, or a row of libSACM's functionality matrix.
 */
requirement: string | null, 
/**
 * libSACM's template id, such as `SACM-MULT-001/too-many`: which form
 * of the code it is, for the frontend's words.
 */
template: string | null, arguments: Array<MessageArgument>, 
/**
 * What it is about; the first is the one to show.
 */
elements: Array<ProblemElement>, 
/**
 * The SACM id of the relationship it is about, if any.
 */
relationship: string | null, 
/**
 * Where in the file, for what libSACM reported reading it.
 */
line: number | null, column: number | null, 
/**
 * The repair the editor can make of it, or null: `Core::repair_join`
 * with these. Only `AF-GSN-004` has one so far.
 */
repair: ProblemRepair | null, };

export type ProblemElement = { 
/**
 * The SACM id.
 */
id: string, 
/**
 * Its GSN identifier and kind, when the diagram draws it as a node.
 */
identifier: string | null, kind: NodeKind | null, };

export type ProblemSeverity = "error" | "warning" | "info";

export type ProblemSource = "gsn" | "sacm" | "read" | "diagram";
