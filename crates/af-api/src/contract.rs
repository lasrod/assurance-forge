//! The types that cross to the frontend. They serialize with serde as JSON in
//! camelCase, and `bindings/api.ts` declares the same shapes in TypeScript.
//!
//! The core sends codes and their arguments, never sentences: the frontend
//! words them in the interface language.

use serde::{Deserialize, Serialize};

#[cfg(feature = "typescript")]
use ts_rs::TS;

/// What opening a file gave.
#[derive(Clone, Debug, PartialEq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(tag = "result", rename_all = "camelCase")]
pub enum OpenResult {
    /// The file was read. Its diagram is at `revision`.
    #[serde(rename_all = "camelCase")]
    Opened {
        revision: u32,
        /// The problem list's counts (`Problems::counts`): what libSACM
        /// reported reading and validating it, the GSN checks, and, among the
        /// information, what the diagram does not draw.
        diagnostics: DiagnosticCounts,
        nodes: u32,
        edges: u32,
        /// The view's codes and the elements it did not place.
        view_codes: u32,
    },
    /// libSACM refused the file. Nothing is open.
    Refused(Refusal),
}

/// A file a shell read itself (named on the desktop application's command
/// line, or chosen in its open dialog): the file's name and what opening it
/// gave.
#[derive(Clone, Debug, PartialEq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct OpenedFile {
    pub name: String,
    pub result: OpenResult,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct DiagnosticCounts {
    pub errors: u32,
    pub warnings: u32,
    pub infos: u32,
}

/// A read refusal: libSACM's code, its sentence form as a stable template id,
/// and the values that fill it.
#[derive(Clone, Debug, PartialEq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct Refusal {
    /// Such as `SACM-XML-001`.
    pub code: String,
    /// Such as `SACM-XML-001/malformed`.
    pub template: String,
    pub arguments: Vec<MessageArgument>,
    /// 1-based, where the refusal points.
    pub line: Option<u32>,
    pub column: Option<u32>,
}

#[derive(Clone, Debug, PartialEq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct MessageArgument {
    pub name: String,
    pub value: ArgumentValue,
}

#[derive(Clone, Debug, PartialEq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(tag = "type", content = "value", rename_all = "camelCase")]
pub enum ArgumentValue {
    Text(String),
    Count(#[cfg_attr(feature = "typescript", ts(type = "number"))] u64),
}

/// The diagram at a revision.
#[derive(Clone, Debug, PartialEq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct DiagramAt {
    /// The document's revision.
    pub revision: u32,
    /// Which layout of it: it changes when the language or the text sizes
    /// do, or another package is shown, which change the diagram but not the
    /// document.
    pub layout: u32,
    /// The SACM id of the argument package the diagram is of, or null for
    /// the whole case.
    pub package: Option<String>,
    pub diagram: Diagram,
}

/// [`DiagramAt`] with the diagram borrowed, for a shell to serialize without
/// copying it. It serializes exactly as `DiagramAt`, which a test checks.
#[derive(Clone, Copy, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DiagramAtRef<'a> {
    pub revision: u32,
    pub layout: u32,
    pub package: Option<&'a str>,
    pub diagram: &'a Diagram,
}

/// What the details panel shows for a node.
#[derive(Clone, Debug, PartialEq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct ElementDetails {
    /// The SACM id.
    pub id: String,
    pub kind: NodeKind,
    pub undeveloped: bool,
    /// Why the undeveloped mark cannot be changed to what it is not
    /// (`AF-EDIT-021`: not a Goal; `AF-EDIT-022`: the Goal is supported), or
    /// null when it can.
    pub undeveloped_refusal: Option<String>,
    /// A `SupportedBy` line leaves the node: with `undeveloped`, the mark
    /// is out of date.
    pub supported: bool,
    pub identifier: String,
    pub name: Option<String>,
    pub statement: Option<String>,
    /// The view's codes about this element.
    pub codes: Vec<ViewCode>,
    /// The SACM id of the argument package it is held in, or null.
    pub package: Option<String>,
    /// A confidence Claim an ACP shows as its description (R17): it is not
    /// drawn as a node, and its text is the ACP's.
    pub acp_description: bool,
}

/// What the details panel shows for a relationship: one the diagram draws an
/// edge for. Its ends are named as SACM names them; the frontend puts them in
/// the direction the arrow points.
#[derive(Clone, Debug, PartialEq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct RelationshipDetails {
    /// The SACM id.
    pub id: String,
    /// The SACM class, such as `AssertedInference`: the frontend words it.
    pub class: String,
    /// How its edges are drawn.
    pub kind: EdgeKind,
    pub name: Option<String>,
    /// SACM's target: the conclusion of support, what context is for, what a
    /// challenge challenges.
    pub target: Option<RelationshipEnd>,
    /// SACM's sources: the premises, the context, what challenges.
    pub sources: Vec<RelationshipEnd>,
    /// The `reasoning`: the Strategy of an inference.
    pub reasoning: Option<RelationshipEnd>,
    /// The view's codes about this relationship.
    pub codes: Vec<ViewCode>,
}

/// One end of a relationship.
#[derive(Clone, Debug, PartialEq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct RelationshipEnd {
    /// The SACM id the relationship names.
    pub id: String,
    /// The GSN identifier, when the end is a node of the diagram.
    pub identifier: Option<String>,
    /// What the node is drawn as, when the end is a node of the diagram.
    pub node: Option<NodeKind>,
    /// The end is a relationship the diagram draws: a challenge may point at
    /// one.
    pub relationship: bool,
}

#[derive(Clone, Debug, PartialEq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct ViewCode {
    /// Such as `AF-VIEW-003`.
    pub code: String,
    /// The id an end or reasoning names, for `AF-VIEW-009`.
    pub end: Option<String>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub enum NodeKind {
    Goal,
    Strategy,
    Solution,
    Context,
    Assumption,
    Justification,
    Neutral,
}

/// The argument as a tree, for the Argument Navigator: the tree the diagram
/// is laid out from, depth first.
#[derive(Clone, Debug, PartialEq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct Outline {
    /// The document's revision.
    pub revision: u32,
    pub rows: Vec<OutlineRow>,
}

/// One node of the outline. A row's parent is the nearest row before it with
/// a smaller level.
#[derive(Clone, Debug, PartialEq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct OutlineRow {
    /// The SACM id, as in the diagram.
    pub id: String,
    /// 1 for a root.
    pub level: u32,
    pub kind: NodeKind,
    pub undeveloped: bool,
    pub identifier: String,
    pub name: Option<String>,
    pub statement: Option<String>,
}

/// A package of the open file, for the Case Explorer. The packages come
/// depth first; a package's parent is the nearest one before it with a
/// smaller level.
#[derive(Clone, Debug, PartialEq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct Package {
    /// The SACM id.
    pub id: String,
    /// 1 for a package at the top of the file.
    pub level: u32,
    /// The SACM class, such as `ArgumentPackage`: the frontend words it.
    pub kind: String,
    pub name: Option<String>,
    /// How many elements it holds directly, utility elements (descriptions,
    /// notes, tagged values) aside.
    pub elements: u32,
}

// ===== The render model =====

#[derive(Clone, Copy, Debug, PartialEq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
pub struct Point {
    pub x: f32,
    pub y: f32,
}

#[derive(Clone, Copy, Debug, PartialEq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
pub struct Rect {
    pub x: f32,
    pub y: f32,
    pub width: f32,
    pub height: f32,
}

/// A GSN shape inside `rect`.
#[derive(Clone, Copy, Debug, PartialEq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(tag = "type", rename_all = "camelCase")]
pub enum Shape {
    Rectangle {
        rect: Rect,
    },
    /// The top edge moved right by `skew`, the bottom edge left.
    Parallelogram {
        rect: Rect,
        skew: f32,
    },
    RoundedRectangle {
        rect: Rect,
        radius: f32,
    },
    Ellipse {
        rect: Rect,
    },
    Circle {
        rect: Rect,
    },
    /// Drawn so it reads as no GSN element: dashed.
    Neutral {
        rect: Rect,
    },
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub enum TextRole {
    Identifier,
    Name,
    Statement,
    /// In a package's diagram, `[name]` of the other package a node belongs
    /// to.
    Package,
    /// An assurance claim point's label.
    Acp,
}

/// One line of text, drawn with the measured faces in their order:
/// `'DATUM Measured Sans', 'DATUM Measured Sans JP'`.
#[derive(Clone, Debug, PartialEq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct TextLine {
    pub text: String,
    pub role: TextRole,
    pub x: f32,
    pub baseline: f32,
    /// The measured width, which `measureText` should match.
    pub width: f32,
    pub size: f32,
    /// 400 or 600.
    pub weight: u16,
}

#[derive(Clone, Debug, PartialEq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(tag = "type", rename_all = "camelCase")]
pub enum Decorator {
    /// A hollow diamond.
    Undeveloped { centre: Point, half: f32 },
    /// The `A` or `J` of an Assumption or Justification.
    Letter {
        text: String,
        x: f32,
        baseline: f32,
        size: f32,
    },
}

#[derive(Clone, Debug, PartialEq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct DiagramNode {
    /// The SACM id.
    pub id: String,
    /// In a package's diagram, the SACM id of the other argument package the
    /// node belongs to, whose name its `package` lines show; null otherwise.
    pub package: Option<String>,
    pub kind: NodeKind,
    pub shape: Shape,
    pub text: Vec<TextLine>,
    pub decorators: Vec<Decorator>,
    /// Everything drawn for the node. No two nodes' bounds overlap.
    pub bounds: Rect,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub enum EdgeKind {
    SupportedBy,
    InContextOf,
    /// A counter relationship: it points at what it challenges.
    Challenge,
    /// A defeated or cited relationship, drawn as none of GSN's.
    Neutral,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub enum LineStyle {
    /// SupportedBy and InContextOf.
    Solid,
    /// A challenge.
    Dashed,
    /// A neutral edge.
    Dotted,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub enum Arrowhead {
    Filled,
    Hollow,
    /// Two strokes.
    Open,
}

#[derive(Clone, Debug, PartialEq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct DiagramEdge {
    /// The SACM id of the relationship.
    pub relationship: String,
    pub from: String,
    /// The SACM id of what the arrow points at: a node, or a relationship
    /// when `to_relationship` is set.
    pub to: String,
    /// A challenge of a relationship: the tip is on that relationship's line.
    pub to_relationship: bool,
    pub kind: EdgeKind,
    pub line: LineStyle,
    /// From the `from` node's outline to the arrowhead. A curve is given as
    /// the points along it.
    pub points: Vec<Point>,
    pub arrowhead: Arrowhead,
    /// The tip, on what the arrow points at, then the other two corners.
    pub head: Vec<Point>,
    /// The assurance claim points on the line, each a solid square with its
    /// label beside it; on a relationship's first edge only.
    pub acps: Vec<AcpMark>,
    /// Everything drawn for the edge, for culling.
    pub bounds: Rect,
}

#[derive(Clone, Debug, PartialEq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct Diagram {
    pub nodes: Vec<DiagramNode>,
    pub edges: Vec<DiagramEdge>,
    pub bounds: Rect,
}

/// A GSN element kind that can be added: the six of the core notation.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub enum ElementKind {
    Goal,
    Strategy,
    Solution,
    Context,
    Assumption,
    Justification,
}

/// A text of a node that Element Properties changes.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub enum TextField {
    /// The GSN identifier.
    Identifier,
    Name,
    /// The statement: SACM's description.
    Statement,
}

/// One kind the add menu lists, and whether it can be chosen.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct AddChoice {
    pub kind: ElementKind,
    /// Why it cannot be added there (`AF-EDIT-001` to `AF-EDIT-006`), or
    /// null when it can. The menu leaves out a kind that cannot be added.
    pub refusal: Option<String>,
    /// Adding it also clears the parent Goal's undeveloped decorator (W11),
    /// which the diagram then shows; the menu has no notes.
    pub develops: bool,
}

/// The add menu for a node, or for the empty canvas, at `revision`.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct AddChoices {
    pub revision: u32,
    /// The six kinds, in the order the standard introduces them.
    pub choices: Vec<AddChoice>,
}

/// One node a removal deletes, as the confirmation lists it.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct RemovalNode {
    /// A confidence Claim an ACP shows as its description, which is not
    /// drawn: it goes with the line that carries the ACP.
    pub acp_description: bool,
    /// The SACM id.
    pub id: String,
    pub kind: NodeKind,
    /// The GSN identifier.
    pub identifier: String,
    pub name: Option<String>,
    pub statement: Option<String>,
}

/// What removing a node would delete, at `revision`.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct Removal {
    pub revision: u32,
    /// The nodes it deletes: the node chosen first, then what goes with it.
    /// Empty when it is refused.
    pub nodes: Vec<RemovalNode>,
    /// Why it cannot be removed (`AF-EDIT-011`, `AF-EDIT-015`, or
    /// `AF-EDIT-005` when a Strategy kept has no package to hold the place
    /// of its next Goal), or null when it can. The frontend words it.
    pub refusal: Option<String>,
}

/// Whether one node can take a moved one.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct MoveTarget {
    /// The SACM id of the node.
    pub id: String,
    /// Why the moved node cannot go under it (`AF-EDIT-001` to `AF-EDIT-004`,
    /// `AF-EDIT-018` to `AF-EDIT-020`, `AF-EDIT-025`; and, from the
    /// operations the move would make there, `AF-EDIT-005`, `AF-EDIT-015` or
    /// `AF-EDIT-017`), or null when it can. The frontend
    /// words it.
    pub refusal: Option<String>,
    /// Moving there also clears this Goal's undeveloped decorator.
    pub develops: bool,
}

/// Whether one line can take a moved challenge.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct MoveLine {
    /// The SACM id of the relationship the line is drawn for.
    pub relationship: String,
    /// The nodes the line runs from and to.
    pub from: String,
    pub to: String,
    /// Why the challenge cannot go there (`AF-EDIT-002`, `AF-EDIT-004`,
    /// `AF-EDIT-005`, `AF-EDIT-011`, `AF-EDIT-018` to `AF-EDIT-020`,
    /// `AF-EDIT-027`), or
    /// null when it can.
    pub refusal: Option<String>,
}

/// What an assurance claim point's argument is (A3 of the ACP plan).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub enum AcpKind {
    /// A text, drawn only as the ACP.
    Description,
    /// A confidence argument in a new argument package.
    Argument,
}

/// One ACP a line's menu may offer.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct AcpChoice {
    pub kind: AcpKind,
    /// Why it cannot be added (`AF-EDIT-002`, `AF-EDIT-005`, `AF-EDIT-011`,
    /// `AF-EDIT-027`, `AF-EDIT-029`), or null when it can.
    pub refusal: Option<String>,
}

/// The ACPs a line's menu may offer, at `revision`.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct AcpChoices {
    pub revision: u32,
    /// The identifier the next ACP takes, such as `ACP1`: what the interface
    /// names a confidence argument's package after.
    pub identifier: String,
    pub choices: Vec<AcpChoice>,
}

/// An assurance claim point on an edge (GSN v3 Table 1:5-1).
#[derive(Clone, Debug, PartialEq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct AcpMark {
    /// The SACM id of the confidence Claim.
    pub claim: String,
    /// The SACM id of its argument package when another than the edge's:
    /// what a click opens. Null for a description.
    pub package: Option<String>,
    /// The square, centred on the line.
    pub square: Rect,
    pub label: TextLine,
}

/// Where the node `id` may be moved, at `revision`.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct MoveTargets {
    pub revision: u32,
    /// The SACM id of the node to move.
    pub id: String,
    /// Why the node cannot be moved at all (`AF-EDIT-002`, `AF-EDIT-004`,
    /// `AF-EDIT-005`, `AF-EDIT-011`, `AF-EDIT-016`, `AF-EDIT-017`,
    /// `AF-EDIT-024`), or null when it can. `nodes` is then empty.
    pub refusal: Option<String>,
    /// Why it cannot be moved to the top level (`AF-EDIT-006`,
    /// `AF-EDIT-019`; and, from the operations the move would make,
    /// `AF-EDIT-005`, `AF-EDIT-015` or `AF-EDIT-017`), or null when it can.
    pub canvas: Option<String>,
    /// Every node of the diagram, in its order.
    pub nodes: Vec<MoveTarget>,
    /// For a counter goal or counter evidence, every line of the diagram, in
    /// its order: a challenge may be moved onto a line (W32). Empty for any
    /// other node.
    pub lines: Vec<MoveLine>,
}

/// What a command gave.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(tag = "result", rename_all = "camelCase")]
pub enum EditResult {
    /// The document changed: it is now at `revision`.
    #[serde(rename_all = "camelCase")]
    Applied {
        revision: u32,
        /// The element the interface selects: the one an add made, the node
        /// a removed one was drawn under, or the node moved. Null for undo,
        /// redo, a change of text and the removal of a top-level node.
        element: Option<String>,
        /// The SACM ids the command created, changed and deleted.
        created: Vec<String>,
        changed: Vec<String>,
        deleted: Vec<String>,
    },
    /// Nothing changed. `code` is an `AF-EDIT-*` code: why the add cannot be
    /// made, nothing is open (`AF-EDIT-007`), there is nothing to undo or
    /// redo (`AF-EDIT-008`), the add menu was for another revision
    /// (`AF-EDIT-009`), libSACM refused the operations (`AF-EDIT-010`),
    /// why a text cannot be set (`AF-EDIT-011` to `AF-EDIT-014`), or why a
    /// node cannot be removed (`AF-EDIT-005`, `AF-EDIT-011`, `AF-EDIT-015`),
    /// or why it cannot be moved (those of `MoveTargets`, and `AF-EDIT-015`),
    /// marked undeveloped (`AF-EDIT-021`, `AF-EDIT-022`) or repaired
    /// (`AF-EDIT-015`, `AF-EDIT-017`, `AF-EDIT-023`).
    Refused { code: String },
    /// A text was set to what it already was: the document, its revision and
    /// its history are as they were.
    Unchanged,
}

/// What Edit ▸ Undo and Redo would do, at `revision`.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct History {
    pub revision: u32,
    pub can_undo: bool,
    pub can_redo: bool,
    /// The document has changes that are not saved: it is not in the state
    /// it was opened or last saved in. Undo back to that state is saved
    /// again (plan decision S3).
    pub modified: bool,
}

/// What [`crate::Core::write`] gave: the open document as SACM 2.3 XMI,
/// checked to read back as the document (plan decision S1).
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(tag = "result", rename_all = "camelCase")]
pub enum WriteResult {
    /// The document at `revision`, written. It is not saved until the shell
    /// has put it somewhere and said so (`mark_saved`).
    #[serde(rename_all = "camelCase")]
    Written { revision: u32, xmi: String },
    /// Nothing was written. `code` is an `AF-SAVE-*` code: nothing is open
    /// (`AF-SAVE-001`), libSACM refused to write the document
    /// (`AF-SAVE-002`), or what it wrote does not read back as the document
    /// (`AF-SAVE-003`).
    Refused { code: String },
}

/// What File ▸ Save or Save As… gave, as a shell answers it.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(tag = "result", rename_all = "camelCase")]
pub enum SaveResult {
    /// The document at `revision` is saved, as the file `name`.
    #[serde(rename_all = "camelCase")]
    Saved { revision: u32, name: String },
    /// Nothing was saved, and the file on disk is as it was. `code` is an
    /// `AF-SAVE-*` code: those of [`WriteResult`], the file changed on disk
    /// since it was opened or saved (`AF-SAVE-004`), the system refused to
    /// write it (`AF-SAVE-005`), or the document changed while it was being
    /// saved (`AF-SAVE-006`).
    Refused { code: String },
    /// The user cancelled the save dialog.
    Cancelled,
}

/// What the core gave when the shell asked to keep a recovery copy (plan
/// decisions R1 to R3).
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(tag = "result", rename_all = "camelCase")]
pub enum RecoveryCopy {
    /// The copy of the document at `revision`, which has changes that are
    /// not saved: the bytes the shell keeps, as they are.
    #[serde(rename_all = "camelCase")]
    Keep { revision: u32, copy: String },
    /// The document at `revision` is in its saved state: any copy of it goes.
    #[serde(rename_all = "camelCase")]
    Remove { revision: u32 },
    /// No copy was made. `code` is `AF-RECOVER-001` when nothing is open, or
    /// `AF-RECOVER-004` when the document could not be written.
    Refused { code: String },
}

/// What a recovery copy found beside the file just opened is.
#[derive(Clone, Debug, PartialEq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(tag = "result", rename_all = "camelCase")]
pub enum FoundRecovery {
    /// A copy that can be restored: of the file `name`, written at
    /// `written_at` (milliseconds since 1970). `base_matches` is whether the
    /// file holds what it held when the copy's changes began (R6).
    #[serde(rename_all = "camelCase")]
    Found {
        name: String,
        written_at: f64,
        base_matches: bool,
    },
    /// A copy that cannot be read (R7): not a recovery copy this version
    /// reads (`AF-RECOVER-002`), or SACM a strict read refuses
    /// (`AF-RECOVER-003`); `AF-RECOVER-001` when nothing is open.
    Unreadable { code: String },
}

/// What the shell did when the page asked it to keep a recovery copy.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(tag = "result", rename_all = "camelCase")]
pub enum KeepResult {
    /// The copy is written.
    Kept,
    /// There are no changes that are not saved: the copy is removed, or there
    /// was none.
    Removed,
    /// Nothing was done: the document is no longer at the revision asked
    /// for, or a copy found on open is still to be decided (R5).
    Skipped,
    /// No copy could be written (`AF-RECOVER-004`, R8), or the core made none.
    Refused { code: String },
}

/// The problem list at a revision (plan decisions P1 to P6,
/// `docs/roadmap/milestone-1-problems.md`): errors, then warnings, then
/// information; within each, GSN, SACM, read, diagram.
#[derive(Clone, Debug, PartialEq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct Problems {
    pub revision: u32,
    pub problems: Vec<Problem>,
    /// How many of each severity, what the status bar shows: every problem,
    /// those of the diagram among the information.
    pub counts: DiagnosticCounts,
}

/// One problem: a code, the values its words take, and what it is about. The
/// frontend words it (P5).
#[derive(Clone, Debug, PartialEq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct Problem {
    pub source: ProblemSource,
    pub severity: ProblemSeverity,
    /// Such as `AF-GSN-001`, `SACM-REF-001` or `AF-VIEW-003`.
    pub code: String,
    /// The requirement it enforces: a GSN v3 requirement, such as
    /// `GSN3-CORE-014`, or a row of libSACM's functionality matrix.
    pub requirement: Option<String>,
    /// libSACM's template id, such as `SACM-MULT-001/too-many`: which form
    /// of the code it is, for the frontend's words.
    pub template: Option<String>,
    pub arguments: Vec<MessageArgument>,
    /// What it is about; the first is the one to show.
    pub elements: Vec<ProblemElement>,
    /// The SACM id of the relationship it is about, if any.
    pub relationship: Option<String>,
    /// Where in the file, for what libSACM reported reading it.
    pub line: Option<u32>,
    pub column: Option<u32>,
    /// The repair the editor can make of it, or null: `Core::repair_join`
    /// with these. Only `AF-GSN-004` has one so far.
    pub repair: Option<ProblemRepair>,
}

/// A repair of a problem: the element joined as GSN does not allow, and the
/// relationship that joins it.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct ProblemRepair {
    pub element: String,
    pub relationship: String,
}

/// An element a problem is about.
#[derive(Clone, Debug, PartialEq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct ProblemElement {
    /// The SACM id.
    pub id: String,
    /// Its GSN identifier and kind, when the diagram draws it as a node.
    pub identifier: Option<String>,
    pub kind: Option<NodeKind>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub enum ProblemSeverity {
    Error,
    Warning,
    Info,
}

/// Where a problem comes from (P1).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub enum ProblemSource {
    /// Where the argument breaks GSN's rules.
    Gsn,
    /// What libSACM's validation finds in the document.
    Sacm,
    /// What libSACM reported reading the file.
    Read,
    /// What the diagram does not draw, or draws neutral.
    Diagram,
}

/// The TypeScript declarations of every type above, in one module, as
/// `bindings/api.ts` holds them.
#[cfg(feature = "typescript")]
pub fn typescript() -> String {
    let cfg = ts_rs::Config::new();
    let declarations = [
        OpenResult::decl(&cfg),
        OpenedFile::decl(&cfg),
        DiagnosticCounts::decl(&cfg),
        Refusal::decl(&cfg),
        MessageArgument::decl(&cfg),
        ArgumentValue::decl(&cfg),
        DiagramAt::decl(&cfg),
        ElementDetails::decl(&cfg),
        RelationshipDetails::decl(&cfg),
        RelationshipEnd::decl(&cfg),
        ViewCode::decl(&cfg),
        NodeKind::decl(&cfg),
        Outline::decl(&cfg),
        OutlineRow::decl(&cfg),
        Package::decl(&cfg),
        Point::decl(&cfg),
        Rect::decl(&cfg),
        Shape::decl(&cfg),
        TextRole::decl(&cfg),
        AcpMark::decl(&cfg),
        AcpKind::decl(&cfg),
        AcpChoice::decl(&cfg),
        AcpChoices::decl(&cfg),
        TextLine::decl(&cfg),
        Decorator::decl(&cfg),
        DiagramNode::decl(&cfg),
        EdgeKind::decl(&cfg),
        LineStyle::decl(&cfg),
        Arrowhead::decl(&cfg),
        DiagramEdge::decl(&cfg),
        Diagram::decl(&cfg),
        ElementKind::decl(&cfg),
        TextField::decl(&cfg),
        AddChoice::decl(&cfg),
        AddChoices::decl(&cfg),
        RemovalNode::decl(&cfg),
        Removal::decl(&cfg),
        MoveTarget::decl(&cfg),
        MoveLine::decl(&cfg),
        MoveTargets::decl(&cfg),
        ProblemRepair::decl(&cfg),
        EditResult::decl(&cfg),
        History::decl(&cfg),
        WriteResult::decl(&cfg),
        SaveResult::decl(&cfg),
        RecoveryCopy::decl(&cfg),
        FoundRecovery::decl(&cfg),
        KeepResult::decl(&cfg),
        Problems::decl(&cfg),
        Problem::decl(&cfg),
        ProblemElement::decl(&cfg),
        ProblemSeverity::decl(&cfg),
        ProblemSource::decl(&cfg),
    ];
    let mut out = String::from(
        "// Generated from crates/af-api/src/contract.rs by its tests; do not edit.\n\
         // To regenerate: AF_UPDATE_BINDINGS=1 cargo test -p af-api --test bindings\n",
    );
    for declaration in declarations {
        out.push('\n');
        out.push_str("export ");
        out.push_str(&declaration);
        out.push('\n');
    }
    out
}
