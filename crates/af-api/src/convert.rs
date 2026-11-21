//! From the view's and the layout's types to the contract's.

use af_layout as layout;

use crate::contract::{
    Arrowhead, Decorator, Diagram, DiagramEdge, DiagramNode, EdgeKind, LineStyle, NodeKind, Point,
    Rect, Shape, TextLine, TextRole,
};

pub(crate) fn node_kind(kind: af_gsn::view::NodeKind) -> NodeKind {
    use af_gsn::view::NodeKind as K;
    match kind {
        K::Goal => NodeKind::Goal,
        K::Strategy => NodeKind::Strategy,
        K::Solution => NodeKind::Solution,
        K::Context => NodeKind::Context,
        K::Assumption => NodeKind::Assumption,
        K::Justification => NodeKind::Justification,
        K::Neutral => NodeKind::Neutral,
    }
}

pub(crate) fn edge_kind(kind: af_gsn::view::EdgeKind) -> EdgeKind {
    use af_gsn::view::EdgeKind as K;
    match kind {
        K::SupportedBy => EdgeKind::SupportedBy,
        K::InContextOf => EdgeKind::InContextOf,
        K::Challenge => EdgeKind::Challenge,
        K::Neutral => EdgeKind::Neutral,
    }
}

fn point(point: layout::Point) -> Point {
    Point {
        x: point.x,
        y: point.y,
    }
}

fn rect(rect: layout::Rect) -> Rect {
    Rect {
        x: rect.x,
        y: rect.y,
        width: rect.width,
        height: rect.height,
    }
}

fn shape(shape: layout::Shape) -> Shape {
    let r = rect(shape.rect);
    match shape.kind {
        layout::ShapeKind::Rectangle => Shape::Rectangle { rect: r },
        layout::ShapeKind::Parallelogram { skew } => Shape::Parallelogram { rect: r, skew },
        layout::ShapeKind::RoundedRectangle { radius } => {
            Shape::RoundedRectangle { rect: r, radius }
        }
        layout::ShapeKind::Ellipse => Shape::Ellipse { rect: r },
        layout::ShapeKind::Circle => Shape::Circle { rect: r },
        layout::ShapeKind::Neutral => Shape::Neutral { rect: r },
    }
}

fn text_line(line: &layout::TextLine) -> TextLine {
    TextLine {
        text: line.text.clone(),
        role: match line.role {
            layout::TextRole::Identifier => TextRole::Identifier,
            layout::TextRole::Name => TextRole::Name,
            layout::TextRole::Statement => TextRole::Statement,
        },
        x: line.x,
        baseline: line.baseline,
        width: line.width,
        size: line.size,
        weight: match line.weight {
            layout::Weight::Regular => 400,
            layout::Weight::SemiBold => 600,
        },
    }
}

fn decorator(decorator: &layout::Decorator) -> Decorator {
    match *decorator {
        layout::Decorator::Undeveloped { centre, half } => Decorator::Undeveloped {
            centre: point(centre),
            half,
        },
        layout::Decorator::Letter {
            text,
            x,
            baseline,
            size,
        } => Decorator::Letter {
            text: text.to_string(),
            x,
            baseline,
            size,
        },
    }
}

fn edge(edge: &layout::DiagramEdge) -> DiagramEdge {
    DiagramEdge {
        relationship: edge.relationship.clone(),
        from: edge.from.clone(),
        to: edge.to.clone(),
        to_relationship: edge.to_relationship,
        kind: edge_kind(edge.kind),
        line: match edge.style {
            layout::LineStyle::Solid => LineStyle::Solid,
            layout::LineStyle::Dashed => LineStyle::Dashed,
            layout::LineStyle::Dotted => LineStyle::Dotted,
        },
        points: edge.points.iter().copied().map(point).collect(),
        arrowhead: match edge.arrowhead {
            layout::Arrowhead::Filled => Arrowhead::Filled,
            layout::Arrowhead::Hollow => Arrowhead::Hollow,
            layout::Arrowhead::Open => Arrowhead::Open,
        },
        head: edge.head.iter().copied().map(point).collect(),
        bounds: rect(edge.bounds),
    }
}

pub(crate) fn diagram(diagram: &layout::Diagram) -> Diagram {
    Diagram {
        nodes: diagram
            .nodes
            .iter()
            .map(|node| DiagramNode {
                id: node.id.clone(),
                kind: node_kind(node.kind),
                shape: shape(node.shape),
                text: node.text.iter().map(text_line).collect(),
                decorators: node.decorators.iter().map(decorator).collect(),
                bounds: rect(node.bounds),
            })
            .collect(),
        edges: diagram.edges.iter().map(edge).collect(),
        bounds: rect(diagram.bounds),
    }
}
