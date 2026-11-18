//! Sizing nodes and placing them as a layered tree (plan decision D7).
//!
//! Spacing follows the C++ application's `gsn_layout.cpp` where it fits; it
//! is a reference, not code that was ported.

use std::collections::HashMap;
use std::f32::consts::SQRT_2;

use af_gsn::view::{EdgeKind, Node, NodeKind, View};

use crate::model::{
    Arrowhead, Decorator, Diagram, DiagramEdge, DiagramNode, LineStyle, Placement, Point, Rect,
    Shape, ShapeKind, TextLine, TextRole,
};
use crate::text::{Line, Shaper, Weight};
use crate::{Cache, Style};

/// The width of a Goal, Strategy, Context or neutral node.
const NODE_WIDTH: f32 = 240.0;
/// Between a shape's edge and its text.
const PADDING: f32 = 10.0;
/// How far a Strategy's top and bottom edges are moved sideways.
const SKEW: f32 = 16.0;
/// A Context's corner radius, at most half its height.
const CONTEXT_RADIUS: f32 = 16.0;
/// The widest an Assumption's or Justification's text is wrapped.
const ELLIPSE_TEXT_WIDTH: f32 = 180.0;
/// The text widths tried for a Solution; the one that gives the smallest
/// circle wins.
const SOLUTION_TEXT_WIDTHS: [f32; 7] = [100.0, 120.0, 140.0, 160.0, 180.0, 200.0, 220.0];
/// The smallest height of any shape, and diameter of a circle.
const MIN_HEIGHT: f32 = 48.0;
/// The smallest width of an ellipse.
const MIN_ELLIPSE_WIDTH: f32 = 96.0;
/// Between the identifier, the name and the statement.
const GROUP_GAP: f32 = 4.0;
/// Half the diagonal of the undeveloped diamond.
const DIAMOND: f32 = 8.0;
/// Between sibling subtrees.
const H_GAP: f32 = 40.0;
/// Between rows.
const V_GAP: f32 = 64.0;
/// Between a node and what is beside it.
const SIDE_GAP: f32 = 32.0;
/// Around the whole diagram.
const MARGIN: f32 = 20.0;
const ARROW_LENGTH: f32 = 10.0;
const ARROW_HALF_WIDTH: f32 = 5.0;
/// How far an edge from a node to itself loops out to the right.
const LOOP: f32 = 24.0;

/// A node's wrapped lines, each with its role.
type Lines = Vec<(TextRole, Line)>;

/// A node sized at the origin: its shape's rectangle starts at (0, 0).
struct Sized {
    shape: Shape,
    text: Vec<TextLine>,
    decorators: Vec<Decorator>,
    /// Everything drawn, relative to the shape's origin.
    footprint: Rect,
}

pub(crate) fn layout(view: &View, shaper: &Shaper<'_>, style: Style, cache: &mut Cache) -> Diagram {
    let sized: Vec<Sized> = view
        .nodes
        .iter()
        .map(|node| size(node, shaper, style, cache))
        .collect();
    let index: HashMap<&str, usize> = view
        .nodes
        .iter()
        .enumerate()
        .map(|(i, node)| (node.id.as_str(), i))
        .collect();
    // Edges whose ends are both nodes. The view gives no other.
    let edges: Vec<(usize, usize, usize)> = view
        .edges
        .iter()
        .enumerate()
        .filter_map(|(e, edge)| {
            Some((
                e,
                *index.get(edge.from.as_str())?,
                *index.get(edge.to.as_str())?,
            ))
        })
        .collect();

    let tree = Tree::new(view, &edges, sized.len());
    let origins = place(&tree, &sized);

    let nodes: Vec<DiagramNode> = view
        .nodes
        .iter()
        .zip(&sized)
        .zip(&origins)
        .map(|((node, sized), origin)| translate(node, sized, *origin))
        .collect();

    let diagram_edges = edges
        .iter()
        .map(|&(e, from, to)| {
            let edge = &view.edges[e];
            let placement = match tree.parent[to] {
                Some((parent, via, placement)) if parent == from && via == e => placement,
                _ => Placement::None,
            };
            edge_geometry(edge, &nodes[from], &nodes[to], placement)
        })
        .collect();

    let bounds = nodes
        .iter()
        .map(|node| node.bounds)
        .reduce(|a, b| a.union(&b))
        .map_or(
            Rect {
                x: 0.0,
                y: 0.0,
                width: 2.0 * MARGIN,
                height: 2.0 * MARGIN,
            },
            |all| Rect {
                x: all.x - MARGIN,
                y: all.y - MARGIN,
                width: all.width + 2.0 * MARGIN,
                height: all.height + 2.0 * MARGIN,
            },
        );

    Diagram {
        nodes,
        edges: diagram_edges,
        bounds,
    }
}

// ===== Sizing =====

/// The texts a node shows, in order: identifier, name, statement (T1 to T3).
/// A statement that repeats the name is shown once.
fn texts(node: &Node) -> Vec<(TextRole, &str)> {
    let mut texts = vec![(TextRole::Identifier, node.identifier.as_str())];
    let name = node.name.as_deref().filter(|name| !name.trim().is_empty());
    if let Some(name) = name {
        texts.push((TextRole::Name, name));
    }
    if let Some(statement) = node.statement.as_deref() {
        if !statement.trim().is_empty() && Some(statement) != name {
            texts.push((TextRole::Statement, statement));
        }
    }
    texts
}

fn role_style(role: TextRole, style: Style) -> (f32, f32, Weight) {
    match role {
        TextRole::Identifier => (
            style.identifier_size,
            style.identifier_line_height,
            Weight::Regular,
        ),
        TextRole::Name => (style.text_size, style.text_line_height, Weight::SemiBold),
        TextRole::Statement => (style.text_size, style.text_line_height, Weight::Regular),
    }
}

/// The node's text wrapped to `width`: each line with its role, the widest
/// line and the block's height.
fn content(
    node: &Node,
    width: f32,
    shaper: &Shaper<'_>,
    style: Style,
    cache: &mut Cache,
) -> (Lines, f32, f32) {
    let mut lines = Vec::new();
    let mut height = 0.0;
    let texts = texts(node);
    for (group, (role, text)) in texts.iter().enumerate() {
        let (size, line_height, weight) = role_style(*role, style);
        let wrapped = cache
            .entry(((*text).to_owned(), *role, width.to_bits()))
            .or_insert_with(|| shaper.wrap(text, size, weight, width));
        if group > 0 {
            height += GROUP_GAP;
        }
        height += line_height * wrapped.len() as f32;
        lines.extend(wrapped.iter().cloned().map(|line| (*role, line)));
    }
    let widest = lines.iter().map(|(_, line)| line.width).fold(0.0, f32::max);
    (lines, widest, height)
}

fn size(node: &Node, shaper: &Shaper<'_>, style: Style, cache: &mut Cache) -> Sized {
    let (kind, width, height, lines, text_height) = match node.kind {
        NodeKind::Goal | NodeKind::Neutral | NodeKind::Strategy | NodeKind::Context => {
            let inset = match node.kind {
                NodeKind::Strategy => 2.0 * SKEW,
                NodeKind::Context => CONTEXT_RADIUS,
                _ => 0.0,
            };
            let (lines, _, text_height) = content(
                node,
                NODE_WIDTH - 2.0 * PADDING - inset,
                shaper,
                style,
                cache,
            );
            let height = (text_height + 2.0 * PADDING).max(MIN_HEIGHT);
            let kind = match node.kind {
                NodeKind::Strategy => ShapeKind::Parallelogram { skew: SKEW },
                NodeKind::Context => ShapeKind::RoundedRectangle {
                    radius: CONTEXT_RADIUS.min(height / 2.0),
                },
                NodeKind::Neutral => ShapeKind::Neutral,
                _ => ShapeKind::Rectangle,
            };
            (kind, NODE_WIDTH, height, lines, text_height)
        }
        NodeKind::Assumption | NodeKind::Justification => {
            // The text fills the rectangle inscribed in the ellipse.
            let (lines, widest, text_height) =
                content(node, ELLIPSE_TEXT_WIDTH, shaper, style, cache);
            let width = ((widest / 2.0 + PADDING) * 2.0 * SQRT_2).max(MIN_ELLIPSE_WIDTH);
            let height = ((text_height / 2.0 + PADDING) * 2.0 * SQRT_2).max(MIN_HEIGHT);
            (ShapeKind::Ellipse, width, height, lines, text_height)
        }
        NodeKind::Solution => {
            // The smallest circle around the text's rectangle.
            let mut best: Option<(f32, Lines, f32)> = None;
            for text_width in SOLUTION_TEXT_WIDTHS {
                let (lines, widest, text_height) = content(node, text_width, shaper, style, cache);
                let diameter =
                    (2.0 * ((widest / 2.0).hypot(text_height / 2.0) + PADDING)).max(MIN_HEIGHT);
                if best.as_ref().is_none_or(|(d, _, _)| diameter < *d) {
                    best = Some((diameter, lines, text_height));
                }
            }
            let (diameter, lines, text_height) = best.unwrap_or((MIN_HEIGHT, Vec::new(), 0.0));
            (ShapeKind::Circle, diameter, diameter, lines, text_height)
        }
    };

    let rect = Rect {
        x: 0.0,
        y: 0.0,
        width,
        height,
    };
    let shape = Shape { kind, rect };
    let (ascent, descent) = shaper.ascent_descent();
    let centre = rect.centre();

    // The text block, centred in the shape.
    let mut text = Vec::with_capacity(lines.len());
    let mut top = centre.y - text_height / 2.0;
    let mut previous: Option<TextRole> = None;
    for (role, line) in lines {
        if previous.is_some_and(|previous| previous != role) {
            top += GROUP_GAP;
        }
        previous = Some(role);
        let (size, line_height, weight) = role_style(role, style);
        let baseline = top + (line_height - (ascent + descent) * size) / 2.0 + ascent * size;
        text.push(TextLine {
            x: centre.x - line.width / 2.0,
            baseline,
            width: line.width,
            text: line.text,
            role,
            size,
            weight,
        });
        top += line_height;
    }

    let mut decorators = Vec::new();
    let mut footprint = rect;
    if node.undeveloped {
        let diamond = Point {
            x: centre.x,
            y: rect.bottom() + DIAMOND,
        };
        decorators.push(Decorator::Undeveloped {
            centre: diamond,
            half: DIAMOND,
        });
        footprint = footprint.union(&Rect {
            x: diamond.x - DIAMOND,
            y: rect.bottom(),
            width: 2.0 * DIAMOND,
            height: 2.0 * DIAMOND,
        });
    }
    let letter = match node.kind {
        NodeKind::Assumption => Some('A'),
        NodeKind::Justification => Some('J'),
        _ => None,
    };
    if let Some(letter) = letter {
        let size = style.identifier_size;
        let letter_width = shaper.width(&letter.to_string(), size, Weight::SemiBold);
        let x = centre.x + width / 2.0 * 0.72;
        let baseline = centre.y + height / 2.0 * 0.72 + size;
        decorators.push(Decorator::Letter {
            text: letter,
            x,
            baseline,
            size,
        });
        footprint = footprint.union(&Rect {
            x,
            y: baseline - ascent * size,
            width: letter_width,
            height: (ascent + descent) * size,
        });
    }

    Sized {
        shape,
        text,
        decorators,
        footprint,
    }
}

fn translate(node: &Node, sized: &Sized, origin: Point) -> DiagramNode {
    let move_rect = |rect: Rect| Rect {
        x: rect.x + origin.x,
        y: rect.y + origin.y,
        ..rect
    };
    DiagramNode {
        id: node.id.clone(),
        kind: node.kind,
        shape: Shape {
            kind: sized.shape.kind,
            rect: move_rect(sized.shape.rect),
        },
        text: sized
            .text
            .iter()
            .map(|line| TextLine {
                x: line.x + origin.x,
                baseline: line.baseline + origin.y,
                ..line.clone()
            })
            .collect(),
        decorators: sized
            .decorators
            .iter()
            .map(|decorator| match *decorator {
                Decorator::Undeveloped { centre, half } => Decorator::Undeveloped {
                    centre: Point {
                        x: centre.x + origin.x,
                        y: centre.y + origin.y,
                    },
                    half,
                },
                Decorator::Letter {
                    text,
                    x,
                    baseline,
                    size,
                } => Decorator::Letter {
                    text,
                    x: x + origin.x,
                    baseline: baseline + origin.y,
                    size,
                },
            })
            .collect(),
        bounds: move_rect(sized.footprint),
    }
}

// ===== The tree =====

struct Tree {
    /// For each node: its tree parent, the edge that placed it, and how.
    parent: Vec<Option<(usize, usize, Placement)>>,
    /// Children below, and attachments beside, in edge order.
    below: Vec<Vec<usize>>,
    beside: Vec<Vec<usize>>,
    roots: Vec<usize>,
    depth: Vec<usize>,
}

impl Tree {
    fn new(view: &View, edges: &[(usize, usize, usize)], count: usize) -> Self {
        let structural = |e: usize| {
            matches!(
                view.edges[e].kind,
                EdgeKind::SupportedBy | EdgeKind::InContextOf
            )
        };
        let mut supported = vec![false; count];
        let mut context_parent: Vec<Option<usize>> = vec![None; count];
        let mut has_outgoing = vec![false; count];
        let mut has_incoming = vec![false; count];
        for &(e, from, to) in edges {
            if !structural(e) || from == to {
                continue;
            }
            has_outgoing[from] = true;
            has_incoming[to] = true;
            match view.edges[e].kind {
                EdgeKind::SupportedBy => supported[to] = true,
                EdgeKind::InContextOf => {
                    context_parent[to].get_or_insert(from);
                }
                EdgeKind::Neutral => {}
            }
        }
        // Beside its first InContextOf parent: a node nothing supports that
        // supports nothing itself. Anything else hangs below.
        let attached = |n: usize| !supported[n] && !has_outgoing[n] && context_parent[n].is_some();

        // Each node's candidate children, in edge order.
        let mut candidates: Vec<Vec<(usize, usize, Placement)>> = vec![Vec::new(); count];
        for &(e, from, to) in edges {
            if !structural(e) || from == to {
                continue;
            }
            let placement = if attached(to) {
                if context_parent[to] == Some(from) {
                    Placement::Beside
                } else {
                    continue;
                }
            } else {
                Placement::Below
            };
            candidates[from].push((to, e, placement));
        }

        let mut tree = Tree {
            parent: vec![None; count],
            below: vec![Vec::new(); count],
            beside: vec![Vec::new(); count],
            roots: Vec::new(),
            depth: vec![0; count],
        };
        let mut placed = vec![false; count];
        // Roots: nothing structural reaches them, in document order; then,
        // for a cycle nothing outside reaches, its first node.
        let first_roots: Vec<usize> = (0..count).filter(|n| !has_incoming[*n]).collect();
        let rest: Vec<usize> = (0..count).collect();
        for root in first_roots.into_iter().chain(rest) {
            if placed[root] {
                continue;
            }
            placed[root] = true;
            tree.roots.push(root);
            // Depth first, keeping edge order.
            let mut stack = vec![root];
            while let Some(node) = stack.pop() {
                let mut below = Vec::new();
                for &(child, e, placement) in &candidates[node] {
                    if placed[child] {
                        continue;
                    }
                    placed[child] = true;
                    tree.parent[child] = Some((node, e, placement));
                    match placement {
                        Placement::Beside => {
                            tree.depth[child] = tree.depth[node];
                            tree.beside[node].push(child);
                        }
                        _ => {
                            tree.depth[child] = tree.depth[node] + 1;
                            tree.below[node].push(child);
                            below.push(child);
                        }
                    }
                }
                stack.extend(below.into_iter().rev());
            }
        }
        tree
    }
}

/// The origin of each node's shape.
fn place(tree: &Tree, sized: &[Sized]) -> Vec<Point> {
    let count = sized.len();
    let fw = |n: usize| sized[n].footprint.width;
    // Above and below the shape's centre, for each node.
    let up = |n: usize| sized[n].shape.rect.height / 2.0 - sized[n].footprint.y;
    let down = |n: usize| sized[n].footprint.bottom() - sized[n].shape.rect.height / 2.0;

    // Subtree widths, children before parents.
    let mut order = Vec::with_capacity(count);
    for &root in &tree.roots {
        let mut stack = vec![root];
        while let Some(node) = stack.pop() {
            order.push(node);
            stack.extend(tree.below[node].iter().copied());
        }
    }
    let mut block = vec![0.0f32; count];
    let mut children = vec![0.0f32; count];
    let mut subtree = vec![0.0f32; count];
    for &node in order.iter().rev() {
        block[node] = fw(node)
            + tree.beside[node]
                .iter()
                .map(|&a| SIDE_GAP + fw(a))
                .sum::<f32>();
        let kids = &tree.below[node];
        children[node] = kids.iter().map(|&c| subtree[c]).sum::<f32>()
            + H_GAP * kids.len().saturating_sub(1) as f32;
        subtree[node] = block[node].max(children[node]);
    }

    // Rows: each node's shape centred on its row's line.
    let rows = tree.depth.iter().copied().max().map_or(0, |d| d + 1);
    let mut row_up = vec![0.0f32; rows];
    let mut row_down = vec![0.0f32; rows];
    for (node, &d) in tree.depth.iter().enumerate() {
        row_up[d] = row_up[d].max(up(node));
        row_down[d] = row_down[d].max(down(node));
    }
    let mut centre_y = vec![0.0f32; rows];
    let mut top = MARGIN;
    for d in 0..rows {
        centre_y[d] = top + row_up[d];
        top += row_up[d] + row_down[d] + V_GAP;
    }

    let mut origins = vec![Point { x: 0.0, y: 0.0 }; count];
    let set = |node: usize, left: f32, origins: &mut Vec<Point>| {
        let half = sized[node].shape.rect.height / 2.0;
        origins[node] = Point {
            x: left - sized[node].footprint.x,
            y: centre_y[tree.depth[node]] - half,
        };
    };
    let mut x = MARGIN;
    for &root in &tree.roots {
        let mut stack = vec![(root, x)];
        while let Some((node, left)) = stack.pop() {
            let mut block_left = left + (subtree[node] - block[node]) / 2.0;
            set(node, block_left, &mut origins);
            block_left += fw(node);
            for &a in &tree.beside[node] {
                block_left += SIDE_GAP;
                set(a, block_left, &mut origins);
                block_left += fw(a);
            }
            let mut child_left = left + (subtree[node] - children[node]) / 2.0;
            for &c in &tree.below[node] {
                stack.push((c, child_left));
                child_left += subtree[c] + H_GAP;
            }
        }
        x += subtree[root] + H_GAP;
    }
    origins
}

// ===== Edges =====

fn edge_geometry(
    edge: &af_gsn::view::Edge,
    from: &DiagramNode,
    to: &DiagramNode,
    placement: Placement,
) -> DiagramEdge {
    let (style, arrowhead) = match edge.kind {
        EdgeKind::SupportedBy => (LineStyle::Solid, Arrowhead::Filled),
        EdgeKind::InContextOf => (LineStyle::Solid, Arrowhead::Hollow),
        EdgeKind::Neutral => (LineStyle::Dashed, Arrowhead::Open),
    };
    let (mut points, tip, direction) = if edge.from == edge.to {
        // A loop out to the right and back.
        let shape = from.shape;
        let start = shape.outline_point(1.0, -0.5);
        let end = shape.outline_point(1.0, 0.5);
        let out = Point {
            x: shape.rect.right().max(start.x) + LOOP,
            y: start.y,
        };
        let back = Point { x: out.x, y: end.y };
        (vec![start, out, back], end, (-1.0, 0.0))
    } else {
        let a = from.shape.rect.centre();
        let b = to.shape.rect.centre();
        let start = from.shape.outline_point(b.x - a.x, b.y - a.y);
        let tip = to.shape.outline_point(a.x - b.x, a.y - b.y);
        let (dx, dy) = (tip.x - start.x, tip.y - start.y);
        let length = dx.hypot(dy).max(f32::EPSILON);
        (vec![start], tip, (dx / length, dy / length))
    };
    let base = Point {
        x: tip.x - direction.0 * ARROW_LENGTH,
        y: tip.y - direction.1 * ARROW_LENGTH,
    };
    let (px, py) = (-direction.1, direction.0);
    let head = [
        tip,
        Point {
            x: base.x + px * ARROW_HALF_WIDTH,
            y: base.y + py * ARROW_HALF_WIDTH,
        },
        Point {
            x: base.x - px * ARROW_HALF_WIDTH,
            y: base.y - py * ARROW_HALF_WIDTH,
        },
    ];
    points.push(if arrowhead == Arrowhead::Open {
        tip
    } else {
        base
    });
    DiagramEdge {
        relationship: edge.relationship.clone(),
        from: edge.from.clone(),
        to: edge.to.clone(),
        kind: edge.kind,
        style,
        points,
        arrowhead,
        head,
        placement,
    }
}
