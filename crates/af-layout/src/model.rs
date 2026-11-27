//! The render model: positioned geometry that the canvas, hit testing and the
//! SVG export all draw from. Coordinates are CSS pixels at 100% zoom, with y
//! growing downwards.

use af_gsn::view::{EdgeKind, NodeKind};

use crate::text::Weight;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Point {
    pub x: f32,
    pub y: f32,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Rect {
    pub x: f32,
    pub y: f32,
    pub width: f32,
    pub height: f32,
}

impl Rect {
    pub fn right(&self) -> f32 {
        self.x + self.width
    }

    pub fn bottom(&self) -> f32 {
        self.y + self.height
    }

    pub fn centre(&self) -> Point {
        Point {
            x: self.x + self.width / 2.0,
            y: self.y + self.height / 2.0,
        }
    }

    /// Whether the two rectangles share any area.
    pub fn overlaps(&self, other: &Rect) -> bool {
        self.x < other.right()
            && other.x < self.right()
            && self.y < other.bottom()
            && other.y < self.bottom()
    }

    pub(crate) fn union(&self, other: &Rect) -> Rect {
        let x = self.x.min(other.x);
        let y = self.y.min(other.y);
        Rect {
            x,
            y,
            width: self.right().max(other.right()) - x,
            height: self.bottom().max(other.bottom()) - y,
        }
    }
}

/// A GSN shape, inside `Shape::rect`.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum ShapeKind {
    /// Goal.
    Rectangle,
    /// Strategy: the top edge is moved right by `skew`, the bottom edge left.
    Parallelogram { skew: f32 },
    /// Context.
    RoundedRectangle { radius: f32 },
    /// Assumption and Justification.
    Ellipse,
    /// Solution.
    Circle,
    /// A node the view drew neutral: a rectangle drawn so it reads as neither
    /// a Goal nor anything else (dashed).
    Neutral,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Shape {
    pub kind: ShapeKind,
    pub rect: Rect,
}

impl Shape {
    /// Where the ray from the shape's centre in direction `(dx, dy)` leaves the
    /// outline.
    pub fn outline_point(&self, dx: f32, dy: f32) -> Point {
        let c = self.rect.centre();
        let hw = self.rect.width / 2.0;
        let hh = self.rect.height / 2.0;
        let length = (dx * dx + dy * dy).sqrt();
        if length == 0.0 {
            return c;
        }
        let (ux, uy) = (dx / length, dy / length);
        let t = match self.kind {
            ShapeKind::Rectangle | ShapeKind::Neutral => rect_t(hw, hh, ux, uy),
            ShapeKind::Ellipse => 1.0 / ((ux / hw).powi(2) + (uy / hh).powi(2)).sqrt(),
            ShapeKind::Circle => hw.min(hh),
            ShapeKind::RoundedRectangle { radius } => {
                let t = rect_t(hw, hh, ux, uy);
                let (px, py) = (ux * t, uy * t);
                if px.abs() > hw - radius && py.abs() > hh - radius {
                    // In a corner: the circle there.
                    let cx = (hw - radius) * ux.signum();
                    let cy = (hh - radius) * uy.signum();
                    // |t u - c| = r, the larger root.
                    let b = ux * cx + uy * cy;
                    let cc = cx * cx + cy * cy - radius * radius;
                    b + (b * b - cc).max(0.0).sqrt()
                } else {
                    t
                }
            }
            ShapeKind::Parallelogram { .. } => {
                let polygon = self.polygon();
                let mut best = f32::INFINITY;
                for i in 0..polygon.len() {
                    let a = polygon[i];
                    let b = polygon[(i + 1) % polygon.len()];
                    if let Some(t) = ray_segment(c, ux, uy, a, b) {
                        best = best.min(t);
                    }
                }
                if best.is_finite() { best } else { 0.0 }
            }
        };
        Point {
            x: c.x + ux * t,
            y: c.y + uy * t,
        }
    }

    /// The parallelogram's corners, clockwise from top left; the rectangle's
    /// for other shapes.
    pub fn polygon(&self) -> [Point; 4] {
        let r = self.rect;
        let skew = match self.kind {
            ShapeKind::Parallelogram { skew } => skew,
            _ => 0.0,
        };
        [
            Point {
                x: r.x + skew,
                y: r.y,
            },
            Point {
                x: r.right(),
                y: r.y,
            },
            Point {
                x: r.right() - skew,
                y: r.bottom(),
            },
            Point {
                x: r.x,
                y: r.bottom(),
            },
        ]
    }

    /// How far `point` is from the outline, in pixels: 0 on it. Exact for the
    /// rectangle, circle and parallelogram; along the ray from the centre for
    /// the others.
    pub fn outline_distance(&self, point: Point) -> f32 {
        let c = self.rect.centre();
        let (dx, dy) = (point.x - c.x, point.y - c.y);
        if dx == 0.0 && dy == 0.0 {
            // The centre: as far from the outline as the nearest side.
            return (self.rect.width / 2.0).min(self.rect.height / 2.0);
        }
        let on = self.outline_point(dx, dy);
        ((on.x - c.x).hypot(on.y - c.y) - dx.hypot(dy)).abs()
    }
}

fn rect_t(hw: f32, hh: f32, ux: f32, uy: f32) -> f32 {
    let tx = if ux == 0.0 {
        f32::INFINITY
    } else {
        hw / ux.abs()
    };
    let ty = if uy == 0.0 {
        f32::INFINITY
    } else {
        hh / uy.abs()
    };
    tx.min(ty)
}

/// The distance along the ray from `origin` in direction `(ux, uy)` at which
/// it crosses the segment `a`–`b`.
fn ray_segment(origin: Point, ux: f32, uy: f32, a: Point, b: Point) -> Option<f32> {
    let (ex, ey) = (b.x - a.x, b.y - a.y);
    let denominator = ux * ey - uy * ex;
    if denominator.abs() < 1e-9 {
        return None;
    }
    let (wx, wy) = (a.x - origin.x, a.y - origin.y);
    let t = (wx * ey - wy * ex) / denominator;
    let s = (wx * uy - wy * ux) / denominator;
    (t >= 0.0 && (-1e-4..=1.0 + 1e-4).contains(&s)).then_some(t)
}

/// What a line of a node's text is.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum TextRole {
    /// The GSN identifier, at metadata size.
    Identifier,
    /// The name, in semibold.
    Name,
    /// The statement.
    Statement,
    /// The other argument package the node belongs to, in a package's
    /// diagram, as `[name]` at metadata size.
    Package,
}

/// One line of text, drawn with the measured faces in their order.
#[derive(Clone, Debug, PartialEq)]
pub struct TextLine {
    pub text: String,
    pub role: TextRole,
    /// Left of the line.
    pub x: f32,
    pub baseline: f32,
    /// The measured width, which the browser's `measureText` should match.
    pub width: f32,
    pub size: f32,
    pub weight: Weight,
}

#[derive(Clone, Debug, PartialEq)]
pub enum Decorator {
    /// A hollow diamond, centred below the node.
    Undeveloped { centre: Point, half: f32 },
    /// The `A` or `J` beside an Assumption or Justification.
    Letter {
        text: char,
        x: f32,
        baseline: f32,
        size: f32,
    },
}

#[derive(Clone, Debug, PartialEq)]
pub struct DiagramNode {
    /// The SACM id.
    pub id: String,
    /// In a package's diagram, the SACM id of the other argument package the
    /// node belongs to, whose name its `Package` line shows.
    pub package: Option<String>,
    pub kind: NodeKind,
    pub shape: Shape,
    pub text: Vec<TextLine>,
    pub decorators: Vec<Decorator>,
    /// Everything drawn for the node, decorators included. No two nodes'
    /// bounds overlap.
    pub bounds: Rect,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LineStyle {
    /// SupportedBy and InContextOf.
    Solid,
    /// A challenge.
    Dashed,
    /// A neutral edge, so it reads as none of GSN's relationships.
    Dotted,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Arrowhead {
    /// SupportedBy.
    Filled,
    /// InContextOf.
    Hollow,
    /// A challenge, and a neutral edge: two strokes.
    Open,
}

/// How the layout used an edge to place its end.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Placement {
    /// The `to` node sits in the row below the `from` node.
    Below,
    /// The `to` node sits in a lane beside the `from` node: level with it
    /// when it is the lane's first, lower otherwise.
    Beside,
    /// The `from` node, which challenges, sits by what it challenges: in a
    /// lane beside that node (below it, when that node is itself in a lane),
    /// or beside that relationship's line.
    Challenger,
    /// The edge did not place a node.
    None,
}

#[derive(Clone, Debug, PartialEq)]
pub struct DiagramEdge {
    /// The SACM id of the relationship.
    pub relationship: String,
    pub from: String,
    /// The SACM id of what the arrow points at: a node, or a relationship
    /// when `to_relationship` is set.
    pub to: String,
    /// A challenge of a relationship: the tip is on that relationship's
    /// first edge.
    pub to_relationship: bool,
    pub kind: EdgeKind,
    pub style: LineStyle,
    /// The line, from the `from` node's outline to the arrowhead's base (or
    /// to its tip for an open arrowhead). Two points for a straight edge; a
    /// curve is given as the points along it.
    pub points: Vec<Point>,
    pub arrowhead: Arrowhead,
    /// The arrowhead's tip, on the `to` node's outline or the relationship's
    /// line, then its two other corners.
    pub head: [Point; 3],
    pub placement: Placement,
    /// Everything drawn for the edge: its line and arrowhead.
    pub bounds: Rect,
}

#[derive(Clone, Debug, PartialEq)]
pub struct Diagram {
    pub nodes: Vec<DiagramNode>,
    pub edges: Vec<DiagramEdge>,
    /// Everything drawn, with the margin.
    pub bounds: Rect,
}
