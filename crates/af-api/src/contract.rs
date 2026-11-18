//! The types that cross to the frontend. They serialize with serde as JSON in
//! camelCase, and `bindings/api.ts` declares the same shapes in TypeScript.
//!
//! The core sends codes and their arguments, never sentences: the frontend
//! words them in the interface language.

use serde::Serialize;

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
        /// What libSACM reported while reading and validating it.
        diagnostics: DiagnosticCounts,
        nodes: u32,
        edges: u32,
        /// The view's codes and the elements it did not place.
        view_codes: u32,
    },
    /// libSACM refused the file. Nothing is open.
    Refused(Refusal),
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
    pub revision: u32,
    pub diagram: Diagram,
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
    pub identifier: String,
    pub name: Option<String>,
    pub statement: Option<String>,
    /// The view's codes about this element.
    pub codes: Vec<ViewCode>,
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
    Neutral,
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
    pub to: String,
    pub kind: EdgeKind,
    pub dashed: bool,
    /// From the `from` node's outline to the arrowhead.
    pub points: Vec<Point>,
    pub arrowhead: Arrowhead,
    /// The tip, on the `to` node's outline, then the other two corners.
    pub head: Vec<Point>,
}

#[derive(Clone, Debug, PartialEq, Serialize)]
#[cfg_attr(feature = "typescript", derive(TS))]
#[serde(rename_all = "camelCase")]
pub struct Diagram {
    pub nodes: Vec<DiagramNode>,
    pub edges: Vec<DiagramEdge>,
    pub bounds: Rect,
}

/// The TypeScript declarations of every type above, in one module, as
/// `bindings/api.ts` holds them.
#[cfg(feature = "typescript")]
pub fn typescript() -> String {
    let cfg = ts_rs::Config::new();
    let declarations = [
        OpenResult::decl(&cfg),
        DiagnosticCounts::decl(&cfg),
        Refusal::decl(&cfg),
        MessageArgument::decl(&cfg),
        ArgumentValue::decl(&cfg),
        DiagramAt::decl(&cfg),
        ElementDetails::decl(&cfg),
        ViewCode::decl(&cfg),
        NodeKind::decl(&cfg),
        Point::decl(&cfg),
        Rect::decl(&cfg),
        Shape::decl(&cfg),
        TextRole::decl(&cfg),
        TextLine::decl(&cfg),
        Decorator::decl(&cfg),
        DiagramNode::decl(&cfg),
        EdgeKind::decl(&cfg),
        Arrowhead::decl(&cfg),
        DiagramEdge::decl(&cfg),
        Diagram::decl(&cfg),
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
