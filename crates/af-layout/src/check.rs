//! Checks of a laid-out diagram's geometry: what the layout promises, measured
//! on the render model itself.
//!
//! The tests, the gallery and anything else that judges a layout use this one
//! definition. It reads only the [`Diagram`], so it judges any geometry,
//! including geometry built by hand to be wrong.
//!
//! An edge that passes through a node it does not join is a defect the layout
//! must not produce. Two edges that cross are not: some arguments cannot be
//! drawn without crossings, so they are counted, not refused.

use crate::model::{Diagram, DiagramEdge, DiagramNode, Point, Rect};

/// A line drawn through a node that is not one of its ends.
#[derive(Clone, Debug, PartialEq)]
pub struct Intersection {
    /// The SACM id of the relationship the line draws.
    pub relationship: String,
    /// The SACM id of the node it passes through.
    pub node: String,
}

/// A line of a node's text that reaches outside the node's shape.
#[derive(Clone, Debug, PartialEq)]
pub struct TextEscape {
    pub node: String,
    /// The line's index in [`DiagramNode::text`].
    pub line: usize,
}

/// What [`check`] found.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Report {
    pub nodes: usize,
    pub edges: usize,
    /// Pairs of nodes whose drawn bounds share area, by SACM id.
    pub node_overlaps: Vec<(String, String)>,
    /// Lines through nodes that are not their ends.
    pub edge_node_intersections: Vec<Intersection>,
    /// Pairs of edges whose lines cross, each pair once. Not a defect by itself.
    pub edge_crossings: usize,
    pub text_escapes: Vec<TextEscape>,
    /// Edges the layout placed nothing by ([`crate::Placement::None`]).
    pub unplaced_edges: usize,
    /// The total length of every edge's line, in CSS px.
    pub edge_length: f32,
    pub width: f32,
    pub height: f32,
}

impl Report {
    /// No overlap, no line through a node, no text outside its shape.
    pub fn is_valid(&self) -> bool {
        self.node_overlaps.is_empty()
            && self.edge_node_intersections.is_empty()
            && self.text_escapes.is_empty()
    }
}

/// Measures `diagram`'s geometry.
pub fn check(diagram: &Diagram) -> Report {
    let mut report = Report {
        nodes: diagram.nodes.len(),
        edges: diagram.edges.len(),
        width: diagram.bounds.width,
        height: diagram.bounds.height,
        ..Report::default()
    };

    for (i, a) in diagram.nodes.iter().enumerate() {
        for b in &diagram.nodes[i + 1..] {
            if a.bounds.overlaps(&b.bounds) {
                report.node_overlaps.push((a.id.clone(), b.id.clone()));
            }
        }
    }

    for node in &diagram.nodes {
        for (index, line) in node.text.iter().enumerate() {
            if !text_inside(node, line) {
                report.text_escapes.push(TextEscape {
                    node: node.id.clone(),
                    line: index,
                });
            }
        }
    }

    let lines: Vec<Vec<Point>> = diagram.edges.iter().map(line).collect();
    for (edge, points) in diagram.edges.iter().zip(&lines) {
        if edge.placement == crate::Placement::None {
            report.unplaced_edges += 1;
        }
        report.edge_length += points
            .windows(2)
            .map(|pair| (pair[1].x - pair[0].x).hypot(pair[1].y - pair[0].y))
            .sum::<f32>();
        for node in &diagram.nodes {
            if node.id == edge.from || (!edge.to_relationship && node.id == edge.to) {
                continue;
            }
            if passes_through(points, &edge.bounds, node) {
                report.edge_node_intersections.push(Intersection {
                    relationship: edge.relationship.clone(),
                    node: node.id.clone(),
                });
            }
        }
    }

    for i in 0..lines.len() {
        for j in i + 1..lines.len() {
            if diagram.edges[i].bounds.overlaps(&diagram.edges[j].bounds)
                && lines_cross(&lines[i], &lines[j])
            {
                report.edge_crossings += 1;
            }
        }
    }
    report
}

/// An edge's whole line: its points, and on to the arrowhead's tip.
fn line(edge: &DiagramEdge) -> Vec<Point> {
    let mut line = edge.points.clone();
    if line.last() != Some(&edge.head[0]) {
        line.push(edge.head[0]);
    }
    line
}

/// Whether the point is inside the node's shape, by the ray from its centre,
/// with `slack` px allowed outside (negative: required inside).
fn within(node: &DiagramNode, point: Point, slack: f32) -> bool {
    let c = node.shape.rect.centre();
    let (dx, dy) = (point.x - c.x, point.y - c.y);
    let on = node.shape.outline_point(dx, dy);
    dx.hypot(dy) <= (on.x - c.x).hypot(on.y - c.y) + slack
}

/// The line's box: from a little above the cap height to below the baseline,
/// as the tests have always measured it.
fn text_inside(node: &DiagramNode, line: &crate::TextLine) -> bool {
    let top = line.baseline - line.size;
    let bottom = line.baseline + line.size * 0.25;
    [
        Point { x: line.x, y: top },
        Point {
            x: line.x + line.width,
            y: top,
        },
        Point {
            x: line.x,
            y: bottom,
        },
        Point {
            x: line.x + line.width,
            y: bottom,
        },
    ]
    .into_iter()
    .all(|corner| within(node, corner, 0.01))
}

/// Whether the line runs inside the node by more than a pixel anywhere.
fn passes_through(points: &[Point], bounds: &Rect, node: &DiagramNode) -> bool {
    let r = node.shape.rect;
    let inner = Rect {
        x: r.x + 1.0,
        y: r.y + 1.0,
        width: (r.width - 2.0).max(0.0),
        height: (r.height - 2.0).max(0.0),
    };
    if !bounds.overlaps(&inner) {
        return false;
    }
    for pair in points.windows(2) {
        let (a, b) = (pair[0], pair[1]);
        let segment = Rect {
            x: a.x.min(b.x),
            y: a.y.min(b.y),
            width: (a.x - b.x).abs(),
            height: (a.y - b.y).abs(),
        };
        // A level or upright segment has an empty box, which `overlaps`
        // never sees; widen it to a hair.
        let segment = Rect {
            x: segment.x - 0.01,
            y: segment.y - 0.01,
            width: segment.width + 0.02,
            height: segment.height + 0.02,
        };
        if !segment.overlaps(&inner) {
            continue;
        }
        let steps = (b.x - a.x).hypot(b.y - a.y).ceil().max(1.0) as usize;
        for step in 0..=steps {
            let t = step as f32 / steps as f32;
            let point = Point {
                x: a.x + (b.x - a.x) * t,
                y: a.y + (b.y - a.y) * t,
            };
            let inside_box = point.x > inner.x
                && point.x < inner.right()
                && point.y > inner.y
                && point.y < inner.bottom();
            if inside_box && within(node, point, -1.0) {
                return true;
            }
        }
    }
    false
}

/// Whether any segment of one line properly crosses a segment of the other:
/// each passes from one side of the other to the other side. Lines that only
/// touch, or run along each other, do not cross.
fn lines_cross(a: &[Point], b: &[Point]) -> bool {
    a.windows(2)
        .any(|s| b.windows(2).any(|t| segments_cross(s[0], s[1], t[0], t[1])))
}

fn segments_cross(p1: Point, p2: Point, q1: Point, q2: Point) -> bool {
    // A cross product this small is taken as touching.
    const EPSILON: f32 = 1e-3;
    let orient =
        |a: Point, b: Point, c: Point| (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    let (d1, d2) = (orient(q1, q2, p1), orient(q1, q2, p2));
    let (d3, d4) = (orient(p1, p2, q1), orient(p1, p2, q2));
    ((d1 > EPSILON && d2 < -EPSILON) || (d1 < -EPSILON && d2 > EPSILON))
        && ((d3 > EPSILON && d4 < -EPSILON) || (d3 < -EPSILON && d4 > EPSILON))
}
