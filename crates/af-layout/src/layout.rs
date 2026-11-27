//! Sizing nodes and placing them as a layered tree (plan decision D7), then
//! drawing a line for every relationship.
//!
//! The stages: size each node from its text; classify each relationship
//! (support below, context and challenges beside); break cycles and give each
//! node a level, the longest chain of support above it (`deepest_parents`);
//! build the spanning tree from those, each node under a parent a row above
//! it; place the tree; draw the lines the tree placed in their bands and
//! lanes, and route every other line around the nodes (`route.rs`). Nothing
//! is dropped or turned round: a relationship left out of the tree is still
//! drawn, from its source to its target.
//!
//! Spacing and placement follow the C++ application's `gsn_layout.cpp` where
//! they fit; it is a reference, not code that was ported.

use std::borrow::Cow;
use std::collections::{HashMap, HashSet, VecDeque};
use std::f32::consts::SQRT_2;

use af_gsn::view::{EdgeKind, Node, NodeKind, View};

use crate::model::{
    AcpMark, Arrowhead, Decorator, Diagram, DiagramEdge, DiagramNode, LineStyle, Placement, Point,
    Rect, Shape, ShapeKind, TextLine, TextRole,
};
use crate::route::{self, Dir, Port, Run};
use crate::text::{Font, Line, Shaper};
use crate::{Cache, Style};

/// The width of a Goal, Strategy, Context or neutral node.
const NODE_WIDTH: f32 = 240.0;
/// Between a shape's edge and its text.
const PADDING: f32 = 10.0;
/// How far a Strategy's top and bottom edges are moved sideways.
const SKEW: f32 = 16.0;
/// A Context's corner radius, at most half its height: a short Context has
/// GSN's round ends, and a tall one keeps its width for text.
const CONTEXT_RADIUS: f32 = 24.0;
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
/// Half the diagonal of the undeveloped diamond. A quarter of the space
/// between rows on each side of its centre: the mark says the argument stops
/// here unfinished, so it is read at the zoom a whole branch is seen at.
const DIAMOND: f32 = 16.0;
/// Between sibling subtrees.
const H_GAP: f32 = 40.0;
/// Between rows.
const V_GAP: f32 = 64.0;
/// Between shelves of trees.
const SHELF_GAP: f32 = 2.0 * V_GAP;
/// A shelf of trees is never narrower than this, so a case of a few trees
/// keeps them in one row.
const SHELF_WIDTH: f32 = 3000.0;
/// The width to height a case of many trees is packed towards: a screen's.
const SHELF_ASPECT: f32 = 1.6;
/// Between a node and the lane beside it.
const SIDE_GAP: f32 = 40.0;
/// The same, when a line that crosses it is challenged: room for the
/// challenge's arrow to reach that line.
const CHALLENGED_GAP: f32 = 72.0;
/// Between the nodes of one lane.
const STACK_GAP: f32 = 24.0;
/// Between a node and what challenges the line into it.
const NECK_GAP: f32 = 24.0;
/// How far from a node's corner a line to its lane leaves its side.
const LANE_PAD: f32 = 12.0;
/// The straight run a bent line ends with: the arrowhead and a little more.
pub(crate) const ARROW_RUN: f32 = ARROW_LENGTH + 2.0;
/// Half the width of the column under and over a node's middle that a routed
/// line keeps out of.
const COLUMN: f32 = 24.0;
/// The segments a curve is drawn as.
const CURVE_STEPS: usize = 12;
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
    let links = links(view);

    let tree = Tree::new(view, &links, sized.len());
    let placed = place(&tree, &sized);

    let nodes: Vec<DiagramNode> = view
        .nodes
        .iter()
        .zip(&sized)
        .zip(&placed.centres)
        .map(|((node, sized), centre)| {
            let rect = sized.shape.rect;
            let origin = Point {
                x: centre.x - rect.width / 2.0,
                y: centre.y - rect.height / 2.0,
            };
            translate(node, sized, origin)
        })
        .collect();

    let mut diagram_edges: Vec<DiagramEdge> = links
        .iter()
        .zip(paths(view, &links, &tree, &nodes, &placed.band_top))
        .enumerate()
        .map(|(i, (link, path))| {
            // The node the link placed: what challenges, for a challenge;
            // the node it points at otherwise.
            let edge = &view.edges[link.edge];
            let placed = match (edge.kind, link.to) {
                (EdgeKind::Challenge, _) => Some(link.from),
                (_, Target::Node(to)) => Some(to),
                (_, Target::Link(_)) => None,
            };
            let slot = placed
                .and_then(|node| tree.parent[node])
                .filter(|&(_, by, _)| by == i)
                .map(|(_, _, slot)| slot);
            let placement = match (edge.kind, slot) {
                (_, None) => Placement::None,
                (EdgeKind::Challenge, Some(_)) => Placement::Challenger,
                (_, Some(Slot::Below)) => Placement::Below,
                (_, Some(Slot::Lane(_) | Slot::Neck(_))) => Placement::Beside,
            };
            edge_geometry(edge, path, placement)
        })
        .collect();

    place_acps(view, &mut diagram_edges, shaper, style);

    // Everything drawn: the nodes, and the edges, which a loop takes outside
    // its node.
    let bounds = nodes
        .iter()
        .map(|node| node.bounds)
        .chain(diagram_edges.iter().map(|edge| edge.bounds))
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

/// The texts a node shows, in order: identifier, name, statement (T1 to T3),
/// and in a package's diagram the other package it belongs to. A statement
/// that repeats the name is shown once.
fn texts(node: &Node) -> Vec<(TextRole, Cow<'_, str>)> {
    let mut texts = vec![(TextRole::Identifier, Cow::from(node.identifier.as_str()))];
    let name = node.name.as_deref().filter(|name| !name.trim().is_empty());
    if let Some(name) = name {
        texts.push((TextRole::Name, Cow::from(name)));
    }
    if let Some(statement) = node.statement.as_deref() {
        if !statement.trim().is_empty() && Some(statement) != name {
            texts.push((TextRole::Statement, Cow::from(statement)));
        }
    }
    if let Some(elsewhere) = node.elsewhere.as_ref() {
        // GSN's module identifier, as §1:5.2.3 delimits it.
        texts.push((
            TextRole::Package,
            Cow::from(format!("[{}]", elsewhere.name)),
        ));
    }
    texts
}

fn role_style(role: TextRole, style: Style) -> (f32, f32, Font) {
    match role {
        TextRole::Identifier | TextRole::Package | TextRole::Acp => (
            style.identifier_size,
            style.identifier_line_height,
            Font::Mono,
        ),
        TextRole::Name => (style.text_size, style.text_line_height, Font::SemiBold),
        TextRole::Statement => (style.text_size, style.text_line_height, Font::Regular),
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
    for (role, text) in &texts {
        let (size, line_height, font) = role_style(*role, style);
        let wrapped = cache
            .entry((text.to_string(), *role, width.to_bits()))
            .or_insert_with(|| shaper.wrap(text, size, font, width));
        // A gap between groups that have lines, as `size` places them.
        if wrapped.is_empty() {
            continue;
        }
        if !lines.is_empty() {
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
        let (size, line_height, font) = role_style(role, style);
        let baseline = top + (line_height - (ascent + descent) * size) / 2.0 + ascent * size;
        text.push(TextLine {
            x: centre.x - line.width / 2.0,
            baseline,
            width: line.width,
            text: line.text,
            role,
            size,
            weight: font.weight(),
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
        let letter_width = shaper.width(&letter.to_string(), size, Font::SemiBold);
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
        package: node
            .elsewhere
            .as_ref()
            .map(|elsewhere| elsewhere.package.clone()),
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

// ===== The view's edges =====

/// What a link's arrow points at.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Target {
    /// A node, by its index in [`View::nodes`].
    Node(usize),
    /// Another link, by its index among the links: a challenge of a
    /// relationship points at that relationship's first edge.
    Link(usize),
}

/// An edge of the view whose ends were found.
#[derive(Clone, Copy, Debug)]
struct Link {
    /// The edge's index in [`View::edges`].
    edge: usize,
    from: usize,
    to: Target,
}

/// The view's edges whose ends are found, in the view's order. The view
/// gives no other.
fn links(view: &View) -> Vec<Link> {
    let index: HashMap<&str, usize> = view
        .nodes
        .iter()
        .enumerate()
        .map(|(i, node)| (node.id.as_str(), i))
        .collect();
    // Each edge with its `from` node, and its `to` node unless it names a
    // relationship.
    let mut kept: Vec<(usize, usize, Option<usize>)> = view
        .edges
        .iter()
        .enumerate()
        .filter_map(|(e, edge)| {
            let from = *index.get(edge.from.as_str())?;
            if edge.to_relationship {
                Some((e, from, None))
            } else {
                Some((e, from, Some(*index.get(edge.to.as_str())?)))
            }
        })
        .collect();
    // A challenge of a relationship that has no link left goes too.
    loop {
        let drawn: HashSet<&str> = kept
            .iter()
            .map(|&(e, _, _)| view.edges[e].relationship.as_str())
            .collect();
        let before = kept.len();
        kept.retain(|&(e, _, to)| to.is_some() || drawn.contains(view.edges[e].to.as_str()));
        if kept.len() == before {
            break;
        }
    }
    let mut first: HashMap<&str, usize> = HashMap::new();
    for (i, &(e, _, _)) in kept.iter().enumerate() {
        first
            .entry(view.edges[e].relationship.as_str())
            .or_insert(i);
    }
    kept.iter()
        .map(|&(e, from, to)| Link {
            edge: e,
            from,
            to: match to {
                Some(node) => Target::Node(node),
                None => Target::Link(first[view.edges[e].to.as_str()]),
            },
        })
        .collect()
}

/// One row of the argument's outline: a node and how deep it sits.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct OutlineRow {
    /// The node's index in [`View::nodes`].
    pub node: usize,
    /// 1 for a root.
    pub level: usize,
}

/// The argument as a tree, depth first: the tree the diagram is laid out
/// from, so the outline and the picture agree. Under each node come the
/// nodes placed beside it (its context, and what challenges it), then the
/// nodes below it, each in edge order. Every node appears exactly once.
pub fn outline(view: &View) -> Vec<OutlineRow> {
    let links = links(view);
    let tree = Tree::new(view, &links, view.nodes.len());
    let mut rows = Vec::with_capacity(view.nodes.len());
    let mut stack: Vec<(usize, usize)> = tree.roots.iter().rev().map(|&root| (root, 1)).collect();
    while let Some((node, level)) = stack.pop() {
        rows.push(OutlineRow { node, level });
        let children = tree.beside[node].iter().chain(&tree.below[node]);
        let children: Vec<usize> = children.copied().collect();
        stack.extend(children.into_iter().rev().map(|child| (child, level + 1)));
    }
    rows
}

// ===== The tree =====

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Side {
    Left,
    Right,
}

impl Side {
    /// -1 for left, 1 for right.
    fn sign(self) -> f32 {
        match self {
            Side::Left => -1.0,
            Side::Right => 1.0,
        }
    }

    fn index(self) -> usize {
        match self {
            Side::Left => 0,
            Side::Right => 1,
        }
    }
}

/// Where a node sits against its tree parent.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Slot {
    /// In the row below it.
    Below,
    /// In the lane on one side of it: its context, and a challenge of it.
    Lane(Side),
    /// Beside the line that comes down into it: a challenge of that
    /// relationship.
    Neck(Side),
}

/// What an edge asks of the tree, before the tree decides the slot.
#[derive(Clone, Copy, Debug)]
enum Want {
    Below,
    Beside,
    /// The child challenges the parent.
    ChallengeNode,
    /// The child challenges this link, which the parent hangs from.
    ChallengeLink(usize),
}

/// The spanning tree the diagram is placed from.
///
/// Placement follows the C++ application's (`core/gsn_layout.cpp` on the
/// `cpp` branch): support hangs below; context sits in a lane beside its
/// node, and so does a challenge of the node; and what is beside a node
/// pushes that node's children down, not sideways. A challenge of a
/// relationship sits beside that relationship's line, not beside one of its
/// ends.
struct Tree {
    /// For each node: its tree parent, the link that placed it, and where.
    parent: Vec<Option<(usize, usize, Slot)>>,
    /// The row below each node, in edge order.
    below: Vec<Vec<usize>>,
    /// The lanes left and right of each node, each from the node's row down.
    lanes: Vec<[Vec<usize>; 2]>,
    /// Left and right of the line into each node, each from the node up.
    necks: Vec<[Vec<usize>; 2]>,
    /// Everything in a node's lanes and necks, in edge order.
    beside: Vec<Vec<usize>>,
    /// The link that placed the node is challenged by a node placed for it.
    challenged: Vec<bool>,
    roots: Vec<usize>,
    /// The row of a node reached from a root through `Below` slots only. The
    /// rows of such nodes line up across the diagram.
    level: Vec<Option<usize>>,
}

impl Tree {
    fn new(view: &View, links: &[Link], count: usize) -> Self {
        let kind = |link: &Link| view.edges[link.edge].kind;
        let live = |link: &Link| {
            matches!(kind(link), EdgeKind::SupportedBy | EdgeKind::InContextOf)
                && link.to != Target::Node(link.from)
        };
        // GSN puts a Context, an Assumption and a Justification beside what
        // they belong to. A file may reach one by support instead of context;
        // it is still placed beside, and its line still drawn as the file
        // says.
        let side_kind = |n: usize| {
            matches!(
                view.nodes[n].kind,
                NodeKind::Context | NodeKind::Assumption | NodeKind::Justification
            )
        };
        let mut supported = vec![false; count];
        let mut context_parent: Vec<Option<usize>> = vec![None; count];
        let mut has_outgoing = vec![false; count];
        for link in links.iter().filter(|link| live(link)) {
            let Target::Node(to) = link.to else { continue };
            has_outgoing[link.from] = true;
            if kind(link) == EdgeKind::SupportedBy && !side_kind(to) {
                supported[to] = true;
            } else {
                context_parent[to].get_or_insert(link.from);
            }
        }
        // Beside its first parent: a node that supports nothing itself and
        // is one of those three kinds, or is reached only as context.
        // Anything else hangs below.
        let attached = |n: usize| !supported[n] && !has_outgoing[n] && context_parent[n].is_some();

        // Each node's candidate children, in edge order: live support and
        // context and the challenges first, then what is drawn neutral,
        // which places only what nothing else has.
        let mut candidates: Vec<Vec<(usize, usize, Want)>> = vec![Vec::new(); count];
        for (i, link) in links.iter().enumerate() {
            match (kind(link), link.to) {
                (EdgeKind::SupportedBy | EdgeKind::InContextOf, Target::Node(to))
                    if to != link.from =>
                {
                    if !attached(to) {
                        candidates[link.from].push((to, i, Want::Below));
                    } else if context_parent[to] == Some(link.from) {
                        candidates[link.from].push((to, i, Want::Beside));
                    }
                }
                (EdgeKind::Challenge, Target::Node(to)) if to != link.from => {
                    candidates[to].push((link.from, i, Want::ChallengeNode));
                }
                (EdgeKind::Challenge, Target::Link(target)) => {
                    // The end the challenged link places: what challenges
                    // for a challenge, the premise or context otherwise.
                    let challenged = &links[target];
                    let anchor = match (kind(challenged), challenged.to) {
                        (EdgeKind::Challenge, _) => challenged.from,
                        (_, Target::Node(node)) => node,
                        (_, Target::Link(_)) => continue,
                    };
                    if anchor != link.from {
                        candidates[anchor].push((link.from, i, Want::ChallengeLink(target)));
                    }
                }
                _ => {}
            }
        }
        for (i, link) in links.iter().enumerate() {
            if let (EdgeKind::Neutral, Target::Node(to)) = (kind(link), link.to) {
                if to != link.from {
                    candidates[link.from].push((to, i, Want::Below));
                }
            }
        }
        // A node supported from more than one parent hangs from one a row
        // above it, so it sits below every node it supports. The lines from
        // its other parents are drawn all the same, routed around the nodes.
        let neutral: Vec<bool> = links
            .iter()
            .map(|link| kind(link) == EdgeKind::Neutral)
            .collect();
        let preferred = deepest_parents(count, &candidates, &neutral);
        for list in &mut candidates {
            list.retain(|&(child, link, want)| {
                !matches!(want, Want::Below) || preferred[child] == Some(link)
            });
        }
        let mut has_incoming = vec![false; count];
        for (child, _, _) in candidates.iter().flatten() {
            has_incoming[*child] = true;
        }

        let mut tree = Tree {
            parent: vec![None; count],
            below: vec![Vec::new(); count],
            lanes: vec![[Vec::new(), Vec::new()]; count],
            necks: vec![[Vec::new(), Vec::new()]; count],
            beside: vec![Vec::new(); count],
            challenged: vec![false; count],
            roots: Vec::new(),
            level: vec![None; count],
        };
        // The side with fewer on it; the right when they are level.
        let fewer = |sides: &[Vec<usize>; 2]| {
            if sides[0].len() < sides[1].len() {
                Side::Left
            } else {
                Side::Right
            }
        };
        let mut placed = vec![false; count];
        // Roots: nothing reaches them, in document order; then, for a cycle
        // nothing outside reaches, its first node that is not merely attached
        // as context, which its parent's turn places beside it.
        let first_roots: Vec<usize> = (0..count).filter(|n| !has_incoming[*n]).collect();
        let cycles: Vec<usize> = (0..count).filter(|n| !attached(*n)).collect();
        let rest: Vec<usize> = (0..count).collect();
        for root in first_roots.into_iter().chain(cycles).chain(rest) {
            if placed[root] {
                continue;
            }
            placed[root] = true;
            tree.roots.push(root);
            tree.level[root] = Some(0);
            // Depth first, keeping edge order.
            let mut stack = vec![root];
            while let Some(node) = stack.pop() {
                let slot = tree.parent[node].map(|(_, _, slot)| slot);
                let incoming = tree.parent[node].map(|(_, link, _)| link);
                // A node beside another keeps what is beside it on its far
                // side, and what challenges it below, so the space between
                // the two stays clear for the line that joins them.
                let outward = match slot {
                    Some(Slot::Lane(side) | Slot::Neck(side)) => Some(side),
                    _ => None,
                };
                let mut next = Vec::new();
                for &(child, link, want) in &candidates[node] {
                    if placed[child] {
                        continue;
                    }
                    placed[child] = true;
                    let lane = outward.unwrap_or_else(|| fewer(&tree.lanes[node]));
                    let challenges_incoming =
                        matches!(want, Want::ChallengeLink(target) if incoming == Some(target));
                    let slot = match want {
                        Want::Below => Slot::Below,
                        Want::Beside => Slot::Lane(lane),
                        Want::ChallengeNode | Want::ChallengeLink(_) if outward.is_some() => {
                            Slot::Below
                        }
                        Want::ChallengeLink(_) if challenges_incoming => {
                            Slot::Neck(fewer(&tree.necks[node]))
                        }
                        Want::ChallengeNode | Want::ChallengeLink(_) => Slot::Lane(lane),
                    };
                    if challenges_incoming {
                        tree.challenged[node] = true;
                    }
                    tree.parent[child] = Some((node, link, slot));
                    match slot {
                        Slot::Below => {
                            // What challenges the line into a node in a
                            // right-hand lane goes first, nearest that line.
                            if challenges_incoming && outward == Some(Side::Right) {
                                tree.below[node].insert(0, child);
                            } else {
                                tree.below[node].push(child);
                            }
                            tree.level[child] = tree.level[node].map(|level| level + 1);
                        }
                        Slot::Lane(side) => {
                            tree.lanes[node][side.index()].push(child);
                            tree.beside[node].push(child);
                        }
                        Slot::Neck(side) => {
                            tree.necks[node][side.index()].push(child);
                            tree.beside[node].push(child);
                        }
                    }
                    next.push(child);
                }
                stack.extend(next.into_iter().rev());
            }
        }
        tree
    }
}

/// For each node, the support link it hangs from: one from a parent a level
/// above it, the first in link order. A level is the longest chain of
/// support down from a node nothing supports. A cycle is broken where a
/// search depth first, from those nodes and then the rest in order, first
/// comes back to a node it is still inside: that link joins no level, and
/// places nothing, but it is still drawn.
///
/// A link drawn neutral (`neutral`, by link) counts only for a node that no
/// live support or context reaches: it places only what nothing else has,
/// so it neither sets a node's level nor takes it from its real parent.
fn deepest_parents(
    count: usize,
    candidates: &[Vec<(usize, usize, Want)>],
    neutral: &[bool],
) -> Vec<Option<usize>> {
    let mut live = vec![false; count];
    for &(child, link, want) in candidates.iter().flatten() {
        if matches!(want, Want::Below) && !neutral[link] {
            live[child] = true;
        }
    }
    let below: Vec<Vec<(usize, usize)>> = candidates
        .iter()
        .map(|list| {
            list.iter()
                .filter(|&&(child, link, want)| {
                    matches!(want, Want::Below) && !(neutral[link] && live[child])
                })
                .map(|&(child, link, _)| (child, link))
                .collect()
        })
        .collect();
    let mut supported = vec![false; count];
    for &(child, _) in below.iter().flatten() {
        supported[child] = true;
    }

    #[derive(Clone, Copy, PartialEq)]
    enum Seen {
        Not,
        Inside,
        Done,
    }
    let mut seen = vec![Seen::Not; count];
    let mut back = HashSet::new();
    let starts = (0..count).filter(|&n| !supported[n]).chain(0..count);
    for start in starts {
        if seen[start] != Seen::Not {
            continue;
        }
        seen[start] = Seen::Inside;
        let mut stack = vec![(start, 0usize)];
        while let Some(top) = stack.last_mut() {
            let node = top.0;
            let Some(&(child, link)) = below[node].get(top.1) else {
                seen[node] = Seen::Done;
                stack.pop();
                continue;
            };
            top.1 += 1;
            match seen[child] {
                Seen::Not => {
                    seen[child] = Seen::Inside;
                    stack.push((child, 0));
                }
                Seen::Inside => {
                    back.insert(link);
                }
                Seen::Done => {}
            }
        }
    }

    let mut waiting = vec![0usize; count];
    for &(child, link) in below.iter().flatten() {
        if !back.contains(&link) {
            waiting[child] += 1;
        }
    }
    let mut level = vec![0usize; count];
    let mut ready: VecDeque<usize> = (0..count).filter(|&n| waiting[n] == 0).collect();
    while let Some(node) = ready.pop_front() {
        for &(child, link) in &below[node] {
            if back.contains(&link) {
                continue;
            }
            level[child] = level[child].max(level[node] + 1);
            waiting[child] -= 1;
            if waiting[child] == 0 {
                ready.push_back(child);
            }
        }
    }

    let mut preferred: Vec<Option<usize>> = vec![None; count];
    for (parent, list) in below.iter().enumerate() {
        for &(child, link) in list {
            if back.contains(&link) || level[parent] + 1 != level[child] {
                continue;
            }
            if preferred[child].is_none_or(|first| link < first) {
                preferred[child] = Some(link);
            }
        }
    }
    preferred
}

// ===== Placing =====

/// How far something reaches from a node's centre.
#[derive(Clone, Copy, Debug, Default)]
struct Extent {
    left: f32,
    right: f32,
    up: f32,
    down: f32,
}

/// Where the nodes are.
struct Placed {
    /// The centre of each node's shape.
    centres: Vec<Point>,
    /// For each node, the top of the clear band between what is beside it
    /// and the row below it, which is `V_GAP` tall. The lines to that row
    /// bend inside it.
    band_top: Vec<f32>,
}

fn place(tree: &Tree, sized: &[Sized]) -> Placed {
    let count = sized.len();
    // What a node itself reaches, decorators included.
    let own = |n: usize| {
        let (rect, footprint) = (sized[n].shape.rect, sized[n].footprint);
        Extent {
            left: rect.width / 2.0 - footprint.x,
            right: footprint.right() - rect.width / 2.0,
            up: rect.height / 2.0 - footprint.y,
            down: footprint.bottom() - rect.height / 2.0,
        }
    };
    let children = |n: usize| {
        let sides = tree.necks[n].iter().chain(&tree.lanes[n]);
        sides.flatten().chain(&tree.below[n]).copied()
    };

    // Parents before children, and the root each node's tree grows from.
    let mut order = Vec::with_capacity(count);
    let mut tree_of = vec![0usize; count];
    for (t, &root) in tree.roots.iter().enumerate() {
        let mut stack = vec![root];
        while let Some(node) = stack.pop() {
            order.push(node);
            tree_of[node] = t;
            stack.extend(children(node));
        }
    }

    // Children first: what each node's subtree reaches, and where each child
    // sits from its parent's centre.
    let mut reach = vec![Extent::default(); count];
    let mut cluster_down = vec![0.0f32; count];
    let mut offset = vec![Point { x: 0.0, y: 0.0 }; count];
    for &node in order.iter().rev() {
        let me = own(node);
        // The node and its lanes. A lane's first node is level with it, and
        // the rest go down from there, their near sides in line.
        let mut cluster = me;
        for side in [Side::Left, Side::Right] {
            let lane = &tree.lanes[node][side.index()];
            let gap = if lane.iter().any(|&item| tree.challenged[item]) {
                CHALLENGED_GAP
            } else {
                SIDE_GAP
            };
            let edge = match side {
                Side::Left => me.left,
                Side::Right => me.right,
            };
            let mut y = 0.0;
            let mut previous: Option<usize> = None;
            for &item in lane {
                let inside = reach[item];
                if let Some(previous) = previous {
                    y += reach[previous].down + STACK_GAP + inside.up;
                }
                previous = Some(item);
                let (near, far) = match side {
                    Side::Left => (inside.right, inside.left),
                    Side::Right => (inside.left, inside.right),
                };
                offset[item] = Point {
                    x: side.sign() * (edge + gap + near),
                    y,
                };
                let out = edge + gap + near + far;
                match side {
                    Side::Left => cluster.left = cluster.left.max(out),
                    Side::Right => cluster.right = cluster.right.max(out),
                }
                cluster.up = cluster.up.max(inside.up - y);
                cluster.down = cluster.down.max(y + inside.down);
            }
        }
        cluster_down[node] = cluster.down;

        // Above them, what challenges the line into the node: beside that
        // line, from the node up.
        let mut all = cluster;
        for side in [Side::Left, Side::Right] {
            let neck = &tree.necks[node][side.index()];
            let gap = if neck.iter().any(|&item| tree.challenged[item]) {
                CHALLENGED_GAP
            } else {
                SIDE_GAP
            };
            let mut top = cluster.up + NECK_GAP - STACK_GAP;
            for &item in neck {
                let inside = reach[item];
                let (near, far) = match side {
                    Side::Left => (inside.right, inside.left),
                    Side::Right => (inside.left, inside.right),
                };
                top += STACK_GAP + inside.down;
                offset[item] = Point {
                    x: side.sign() * (gap + near),
                    y: -top,
                };
                top += inside.up;
                let out = gap + near + far;
                match side {
                    Side::Left => all.left = all.left.max(out),
                    Side::Right => all.right = all.right.max(out),
                }
                all.up = all.up.max(top);
            }
        }

        // Below them, the row of children, centred under the node.
        let row = &tree.below[node];
        if let (Some(&first), Some(&last)) = (row.first(), row.last()) {
            let mut xs = Vec::with_capacity(row.len());
            let mut x = 0.0;
            for (k, &child) in row.iter().enumerate() {
                if k > 0 {
                    x += reach[row[k - 1]].right + H_GAP + reach[child].left;
                }
                xs.push(x);
            }
            let mut shift = -x / 2.0;
            // Under a node that is beside another, the row stays off the
            // space between the two.
            let row_left = shift - reach[first].left;
            let row_right = x + shift + reach[last].right;
            match tree.parent[node].map(|(_, _, slot)| slot) {
                Some(Slot::Lane(Side::Right) | Slot::Neck(Side::Right)) if row_left < -me.left => {
                    shift += -me.left - row_left;
                }
                Some(Slot::Lane(Side::Left) | Slot::Neck(Side::Left)) if row_right > me.right => {
                    shift -= row_right - me.right;
                }
                _ => {}
            }
            let row_up = row.iter().map(|&c| reach[c].up).fold(0.0, f32::max);
            let row_down = row.iter().map(|&c| reach[c].down).fold(0.0, f32::max);
            let y = cluster.down + V_GAP + row_up;
            for (&child, x) in row.iter().zip(&xs) {
                offset[child] = Point { x: x + shift, y };
            }
            all.left = all.left.max(reach[first].left - shift);
            all.right = all.right.max(x + shift + reach[last].right);
            all.down = all.down.max(y + row_down);
        }
        reach[node] = all;
    }

    // The roots' trees side by side, in shelves: a case of many trees would
    // otherwise be one strip, too wide to see whole. A shelf is about as wide
    // as the trees' area at a screen's proportions, never narrower than the
    // widest tree or than `SHELF_WIDTH`, so a few trees stay in one row. The
    // roots keep their order, left to right and then down.
    let widths: Vec<f32> = tree
        .roots
        .iter()
        .map(|&root| reach[root].left + reach[root].right)
        .collect();
    let area: f32 = tree
        .roots
        .iter()
        .zip(&widths)
        .map(|(&root, width)| (width + H_GAP) * (reach[root].up + reach[root].down + V_GAP))
        .sum();
    let widest = widths.iter().copied().fold(0.0, f32::max);
    let limit = widest.max((area * SHELF_ASPECT).sqrt()).max(SHELF_WIDTH);
    let mut shelves: Vec<Vec<usize>> = Vec::new();
    let mut used = f32::INFINITY;
    for (t, width) in widths.iter().enumerate() {
        if used + H_GAP + width > limit {
            shelves.push(Vec::new());
            used = -H_GAP;
        }
        used += H_GAP + width;
        if let Some(shelf) = shelves.last_mut() {
            shelf.push(t);
        }
    }
    let mut shelf_of = vec![0usize; tree.roots.len()];
    for (s, shelf) in shelves.iter().enumerate() {
        for &t in shelf {
            shelf_of[t] = s;
        }
    }

    let mut centres = vec![Point { x: 0.0, y: 0.0 }; count];
    let mut band_top = vec![0.0f32; count];
    let mut top = MARGIN;
    for (s, shelf) in shelves.iter().enumerate() {
        let mine = |node: &usize| shelf_of[tree_of[*node]] == s;
        // The rows that line up across the shelf: each as tall as its
        // tallest node with what is beside it.
        let rows = order
            .iter()
            .filter(|node| mine(node))
            .filter_map(|&node| tree.level[node])
            .max()
            .map_or(0, |level| level + 1);
        let mut row_up = vec![0.0f32; rows];
        let mut row_down = vec![0.0f32; rows];
        for &node in order.iter().filter(|node| mine(node)) {
            if let Some(level) = tree.level[node] {
                row_up[level] = row_up[level].max(reach[node].up);
                row_down[level] = row_down[level].max(cluster_down[node]);
            }
        }
        let mut row_y = vec![0.0f32; rows];
        let mut y = top;
        for level in 0..rows {
            row_y[level] = y + row_up[level];
            y = row_y[level] + row_down[level] + V_GAP;
        }

        let mut x = MARGIN;
        for &t in shelf {
            let root = tree.roots[t];
            centres[root] = Point {
                x: x + reach[root].left,
                y: 0.0,
            };
            x += widths[t] + H_GAP;
        }
        let mut bottom = top;
        for &node in order.iter().filter(|node| mine(node)) {
            if let Some((parent, _, _)) = tree.parent[node] {
                centres[node] = Point {
                    x: centres[parent].x + offset[node].x,
                    y: centres[parent].y + offset[node].y,
                };
            }
            match tree.level[node] {
                Some(level) => {
                    centres[node].y = row_y[level];
                    band_top[node] = row_y[level] + row_down[level];
                }
                None => band_top[node] = centres[node].y + cluster_down[node],
            }
            bottom = bottom.max(centres[node].y + own(node).down);
        }
        top = bottom + SHELF_GAP;
    }
    Placed { centres, band_top }
}

// ===== Edges =====

/// How a link's line runs.
#[derive(Clone, Copy, Debug)]
enum Route {
    /// From a node to itself.
    Loop,
    /// Between a node and one in the row below it.
    Vertical {
        top: usize,
        bottom: usize,
        tip_at_top: bool,
    },
    /// Between a node and one in its lane.
    Lane {
        host: usize,
        item: usize,
        side: Side,
        tip_at_host: bool,
    },
    /// From a node in a neck to the line that comes down into `host`.
    Neck {
        from: usize,
        host: usize,
        side: Side,
    },
    /// From a node below `anchor` up to the line that joins `anchor` to what
    /// it is beside.
    Elbow { from: usize, anchor: usize },
    /// Straight, for an edge that placed nothing.
    Straight,
}

/// Whether the tree placed the line: everything but a line it left to route.
fn placed(route: Route) -> bool {
    !matches!(route, Route::Straight)
}

fn route(view: &View, links: &[Link], tree: &Tree, i: usize) -> Route {
    let link = &links[i];
    let placed_by_this = |node: usize| tree.parent[node].filter(|&(_, by, _)| by == i);
    match (view.edges[link.edge].kind, link.to) {
        (_, Target::Node(to)) if to == link.from => Route::Loop,
        (EdgeKind::Challenge, Target::Node(_)) => match placed_by_this(link.from) {
            Some((host, _, Slot::Lane(side))) => Route::Lane {
                host,
                item: link.from,
                side,
                tip_at_host: true,
            },
            Some((host, _, Slot::Below)) => Route::Vertical {
                top: host,
                bottom: link.from,
                tip_at_top: true,
            },
            _ => Route::Straight,
        },
        (EdgeKind::Challenge, Target::Link(target)) => match placed_by_this(link.from) {
            Some((host, _, slot)) if tree.parent[host].is_some_and(|(_, by, _)| by == target) => {
                match slot {
                    Slot::Neck(side) => Route::Neck {
                        from: link.from,
                        host,
                        side,
                    },
                    Slot::Below => Route::Elbow {
                        from: link.from,
                        anchor: host,
                    },
                    Slot::Lane(_) => Route::Straight,
                }
            }
            _ => Route::Straight,
        },
        (_, Target::Node(to)) => match placed_by_this(to) {
            Some((_, _, Slot::Below)) => Route::Vertical {
                top: link.from,
                bottom: to,
                tip_at_top: false,
            },
            Some((_, _, Slot::Lane(side))) => Route::Lane {
                host: link.from,
                item: to,
                side,
                tip_at_host: false,
            },
            _ => Route::Straight,
        },
        (_, Target::Link(_)) => Route::Straight,
    }
}

/// A cubic curve from `a` to `b` as points after `a`, `b` included.
fn curve(a: Point, c1: Point, c2: Point, b: Point, out: &mut Vec<Point>) {
    for step in 1..=CURVE_STEPS {
        let t = step as f32 / CURVE_STEPS as f32;
        let u = 1.0 - t;
        let (w0, w1, w2, w3) = (u * u * u, 3.0 * u * u * t, 3.0 * u * t * t, t * t * t);
        out.push(Point {
            x: w0 * a.x + w1 * c1.x + w2 * c2.x + w3 * b.x,
            y: w0 * a.y + w1 * c1.y + w2 * c2.y + w3 * b.y,
        });
    }
}

/// The line from `top` down to `bottom`, in the row below it: straight down
/// to the band between the rows, across inside it, and straight down again.
fn vertical_path(top: &DiagramNode, bottom: &DiagramNode, band_top: f32) -> Vec<Point> {
    let a = top.shape.outline_point(0.0, 1.0);
    let b = bottom.shape.outline_point(0.0, -1.0);
    let y1 = band_top.max(a.y + ARROW_RUN);
    let y2 = (band_top + V_GAP).min(b.y - ARROW_RUN);
    if (a.x - b.x).abs() < 0.5 || y2 - y1 < 1.0 {
        return vec![a, b];
    }
    let middle = (y1 + y2) / 2.0;
    let start = Point { x: a.x, y: y1 };
    let mut path = vec![a, start];
    curve(
        start,
        Point { x: a.x, y: middle },
        Point { x: b.x, y: middle },
        Point { x: b.x, y: y2 },
        &mut path,
    );
    path.push(b);
    path
}

/// The line from `host` out to `item` in its lane: level when the two are,
/// and otherwise leaving the host's side as near the item's height as the
/// side reaches.
fn lane_path(host: &DiagramNode, item: &DiagramNode, side: Side, tip_at_host: bool) -> Vec<Point> {
    let rect = host.shape.rect;
    let pad = LANE_PAD.min(rect.height / 2.0);
    let y = item
        .shape
        .rect
        .centre()
        .y
        .clamp(rect.y + pad, rect.bottom() - pad);
    let on_host = host
        .shape
        .outline_point(side.sign() * rect.width / 2.0, y - rect.centre().y);
    let on_item = item.shape.outline_point(-side.sign(), 0.0);
    if (on_host.y - on_item.y).abs() < 0.5 {
        return vec![on_host, on_item];
    }
    // The arrowhead's end runs level.
    let (a, b) = if tip_at_host {
        let a = Point {
            x: on_host.x + side.sign() * ARROW_RUN,
            y: on_host.y,
        };
        (a, on_item)
    } else {
        let b = Point {
            x: on_item.x - side.sign() * ARROW_RUN,
            y: on_item.y,
        };
        (on_host, b)
    };
    let middle = (a.x + b.x) / 2.0;
    let mut path = vec![on_host];
    if tip_at_host {
        path.push(a);
    }
    curve(
        a,
        Point { x: middle, y: a.y },
        Point { x: middle, y: b.y },
        b,
        &mut path,
    );
    if !tip_at_host {
        path.push(on_item);
    }
    path
}

/// The point halfway along a line.
fn midpoint(path: &[Point]) -> Point {
    let length = |a: Point, b: Point| (b.x - a.x).hypot(b.y - a.y);
    let total: f32 = path.windows(2).map(|pair| length(pair[0], pair[1])).sum();
    let mut left = total / 2.0;
    for pair in path.windows(2) {
        let segment = length(pair[0], pair[1]);
        if segment >= left && segment > 0.0 {
            let t = left / segment;
            return Point {
                x: pair[0].x + (pair[1].x - pair[0].x) * t,
                y: pair[0].y + (pair[1].y - pair[0].y) * t,
            };
        }
        left -= segment;
    }
    path.last().copied().unwrap_or(Point { x: 0.0, y: 0.0 })
}

/// Straight from `from`'s outline to `aim`.
fn straight_path(from: &DiagramNode, aim: Point) -> Vec<Point> {
    let centre = from.shape.rect.centre();
    vec![
        from.shape.outline_point(aim.x - centre.x, aim.y - centre.y),
        aim,
    ]
}

/// The ports a routed line may leave `from` by and enter `to` by, for an
/// edge of `kind`: support leaves below and enters above, context leaves and
/// enters a side, a challenge any side. Each port has a cost, so the router
/// takes the usual one unless it is blocked or much longer. A line that
/// enters the top of a node another line already enters there is moved
/// aside, so the two arrowheads stay apart.
fn ports(
    kind: EdgeKind,
    from: &DiagramNode,
    to: &DiagramNode,
    tops: &mut HashMap<String, [usize; 2]>,
) -> (Vec<Port>, Vec<Port>) {
    let port = |node: &DiagramNode, out: Dir, offset: f32, cost: f32| {
        route::port(&node.shape, node.bounds, out, offset, cost)
    };
    let sides = |node: &DiagramNode, cost: f32| {
        [
            port(node, Dir::Left, 0.0, cost),
            port(node, Dir::Right, 0.0, cost),
        ]
    };
    match kind {
        EdgeKind::SupportedBy | EdgeKind::Neutral => {
            // Beside the middle of the top, on the side the line comes from.
            let right = from.shape.rect.centre().x >= to.shape.rect.centre().x;
            let used = tops.entry(to.id.clone()).or_insert([0, 0]);
            let n = used[usize::from(right)];
            used[usize::from(right)] += 1;
            // Seven points on each side, the first three as before, so that
            // an arrowhead lands on another only past seven lines.
            let offset = [0.28, 0.4, 0.16, 0.46, 0.34, 0.22, 0.1][n % 7] * to.shape.rect.width;
            let offset = if right { offset } else { -offset };
            let mut starts = vec![port(from, Dir::Down, 0.0, 0.0)];
            starts.extend(sides(from, 160.0));
            let mut ends = vec![port(to, Dir::Up, offset, 0.0)];
            ends.extend(sides(to, 80.0));
            ends.push(port(to, Dir::Down, 0.0, 200.0));
            (starts, ends)
        }
        EdgeKind::InContextOf => {
            let mut starts = sides(from, 0.0).to_vec();
            starts.push(port(from, Dir::Down, 0.0, 60.0));
            let mut ends = sides(to, 0.0).to_vec();
            ends.extend([port(to, Dir::Up, 0.0, 40.0), port(to, Dir::Down, 0.0, 40.0)]);
            (starts, ends)
        }
        EdgeKind::Challenge => {
            let all = |node: &DiagramNode| {
                let mut all = sides(node, 0.0).to_vec();
                all.extend([
                    port(node, Dir::Up, 0.0, 20.0),
                    port(node, Dir::Down, 0.0, 20.0),
                ]);
                all
            };
            (all(from), all(to))
        }
    }
}

/// Every link's line, from where it leaves its `from` node to its tip.
fn paths(
    view: &View,
    links: &[Link],
    tree: &Tree,
    nodes: &[DiagramNode],
    band_top: &[f32],
) -> Vec<Vec<Point>> {
    let routes: Vec<Route> = (0..links.len())
        .map(|i| route(view, links, tree, i))
        .collect();
    let mut paths: Vec<Option<Vec<Point>>> = vec![None; links.len()];
    // What a routed line goes around, and the runs of those already routed.
    let obstacles: Vec<Rect> = nodes.iter().map(|node| node.bounds).collect();
    let area = obstacles
        .iter()
        .copied()
        .reduce(|a, b| a.union(&b))
        .unwrap_or(Rect {
            x: 0.0,
            y: 0.0,
            width: 0.0,
            height: 0.0,
        });
    // The columns under and over each node's middle, where its lines into
    // the rows below and above run: a routed line that ran down one would
    // be taken for that node's.
    let column = |x: f32, y: f32, height: f32| Rect {
        x: x - COLUMN,
        y,
        width: 2.0 * COLUMN,
        height,
    };
    let below: Vec<Rect> = nodes
        .iter()
        .map(|node| {
            let r = node.shape.rect;
            column(r.centre().x, r.bottom(), V_GAP + route::CLEARANCE_Y)
        })
        .collect();
    let above: Vec<Rect> = nodes
        .iter()
        .map(|node| {
            let r = node.shape.rect;
            column(r.centre().x, r.y - V_GAP, V_GAP)
        })
        .collect();
    let zones = |from: usize| -> Vec<Rect> {
        below
            .iter()
            .enumerate()
            .filter(|&(n, _)| n != from)
            .map(|(_, zone)| *zone)
            .chain(above.iter().copied())
            .collect()
    };
    let mut runs: Vec<Run> = Vec::new();
    let mut tops: HashMap<String, [usize; 2]> = HashMap::new();
    // A challenge of a relationship needs that relationship's line first.
    // Challenges that point at each other in a ring never get one, and the
    // last pass draws them at the node the other leaves.
    for last in [false, true] {
        loop {
            let mut progress = false;
            // The lines the tree placed first, so that every routed line
            // knows them and keeps off running along one.
            let order = (0..links.len())
                .filter(|&i| placed(routes[i]))
                .chain((0..links.len()).filter(|&i| !placed(routes[i])));
            for i in order {
                let link = &links[i];
                if paths[i].is_some() {
                    continue;
                }
                let from = &nodes[link.from];
                let target_middle = match link.to {
                    Target::Node(_) => None,
                    Target::Link(target) => match &paths[target] {
                        Some(path) => Some(midpoint(path)),
                        None if last => Some(nodes[links[target].from].shape.rect.centre()),
                        None => continue,
                    },
                };
                let path = match (routes[i], link.to, target_middle) {
                    (Route::Loop, _, _) => {
                        // Out to the right and back.
                        let shape = from.shape;
                        let start = shape.outline_point(1.0, -0.5);
                        let end = shape.outline_point(1.0, 0.5);
                        let out = shape.rect.right().max(start.x) + LOOP;
                        vec![
                            start,
                            Point { x: out, y: start.y },
                            Point { x: out, y: end.y },
                            end,
                        ]
                    }
                    (
                        Route::Vertical {
                            top,
                            bottom,
                            tip_at_top,
                        },
                        _,
                        _,
                    ) => {
                        let mut path = vertical_path(&nodes[top], &nodes[bottom], band_top[top]);
                        if tip_at_top {
                            path.reverse();
                        }
                        path
                    }
                    (
                        Route::Lane {
                            host,
                            item,
                            side,
                            tip_at_host,
                        },
                        _,
                        _,
                    ) => {
                        let mut path = lane_path(&nodes[host], &nodes[item], side, tip_at_host);
                        if tip_at_host {
                            path.reverse();
                        }
                        path
                    }
                    (Route::Neck { from, host, side }, _, _) => {
                        let start = nodes[from].shape.outline_point(-side.sign(), 0.0);
                        let tip = Point {
                            x: nodes[host].shape.rect.centre().x,
                            y: start.y,
                        };
                        vec![start, tip]
                    }
                    (Route::Elbow { from, anchor }, _, Some(middle)) => {
                        // Up into the band under the anchor, across it, and
                        // up between the anchor and what it is beside.
                        let start = nodes[from].shape.outline_point(0.0, -1.0);
                        let y = band_top[anchor] + V_GAP / 2.0;
                        vec![
                            start,
                            Point { x: start.x, y },
                            Point { x: middle.x, y },
                            middle,
                        ]
                    }
                    (_, _, Some(middle)) => {
                        // A challenge of a line, placed by nothing: to the
                        // middle of that line, from whichever side is open.
                        let (starts, _) = ports(EdgeKind::Challenge, from, from, &mut tops);
                        let ends: Vec<Port> = [Dir::Up, Dir::Down, Dir::Left, Dir::Right]
                            .into_iter()
                            .map(|out| route::point_port(middle, out, 0.0))
                            .collect();
                        route::route(
                            link.from,
                            &starts,
                            &ends,
                            &obstacles,
                            &zones(link.from),
                            area,
                            &mut runs,
                        )
                        .unwrap_or_else(|| straight_path(from, middle))
                    }
                    (_, Target::Node(to), None) => {
                        // Placed nothing: a second parent, the line that
                        // closes a cycle, a context of two nodes. Routed
                        // around every node.
                        let kind = view.edges[link.edge].kind;
                        let (starts, ends) = ports(kind, from, &nodes[to], &mut tops);
                        route::route(
                            link.from,
                            &starts,
                            &ends,
                            &obstacles,
                            &zones(link.from),
                            area,
                            &mut runs,
                        )
                        .unwrap_or_else(|| {
                            let (a, b) = (from.shape.rect.centre(), nodes[to].shape.rect.centre());
                            vec![
                                from.shape.outline_point(b.x - a.x, b.y - a.y),
                                nodes[to].shape.outline_point(a.x - b.x, a.y - b.y),
                            ]
                        })
                    }
                    (_, Target::Link(_), None) => continue,
                };
                if placed(routes[i]) {
                    runs.extend(route::runs_of(&path, link.from));
                }
                paths[i] = Some(path);
                progress = true;
            }
            if !progress {
                break;
            }
        }
    }
    paths.into_iter().map(Option::unwrap_or_default).collect()
}

fn bounds_of(points: &[Point], head: &[Point; 3]) -> Rect {
    let points = points.iter().chain(head);
    let (mut x0, mut y0, mut x1, mut y1) = (
        f32::INFINITY,
        f32::INFINITY,
        f32::NEG_INFINITY,
        f32::NEG_INFINITY,
    );
    for point in points {
        x0 = x0.min(point.x);
        y0 = y0.min(point.y);
        x1 = x1.max(point.x);
        y1 = y1.max(point.y);
    }
    Rect {
        x: x0,
        y: y0,
        width: x1 - x0,
        height: y1 - y0,
    }
}

/// The edge drawn along `path`, which runs from the `from` node to the tip.
fn edge_geometry(
    edge: &af_gsn::view::Edge,
    mut path: Vec<Point>,
    placement: Placement,
) -> DiagramEdge {
    let (style, arrowhead) = match edge.kind {
        EdgeKind::SupportedBy => (LineStyle::Solid, Arrowhead::Filled),
        EdgeKind::InContextOf => (LineStyle::Solid, Arrowhead::Hollow),
        EdgeKind::Challenge => (LineStyle::Dashed, Arrowhead::Open),
        EdgeKind::Neutral => (LineStyle::Dotted, Arrowhead::Open),
    };
    let origin = Point { x: 0.0, y: 0.0 };
    let tip = path.last().copied().unwrap_or(origin);
    let before = path.iter().rev().nth(1).copied().unwrap_or(tip);
    let (dx, dy) = (tip.x - before.x, tip.y - before.y);
    let length = dx.hypot(dy).max(f32::EPSILON);
    let direction = (dx / length, dy / length);
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
    // The line stops at a closed arrowhead's base, so it does not show
    // through a hollow one.
    if arrowhead != Arrowhead::Open {
        if let Some(last) = path.last_mut() {
            *last = base;
        }
    }
    let bounds = bounds_of(&path, &head);
    DiagramEdge {
        relationship: edge.relationship.clone(),
        from: edge.from.clone(),
        to: edge.to.clone(),
        to_relationship: edge.to_relationship,
        kind: edge.kind,
        style,
        points: path,
        arrowhead,
        head,
        placement,
        acps: Vec::new(),
        bounds,
    }
}

/// Half the side of an ACP's square.
const ACP_HALF: f32 = 5.0;
/// Between an ACP's square and its label.
const ACP_GAP: f32 = 4.0;

/// E11: each ACP on its relationship's first edge, halfway along its line,
/// with its label to the right; a second ACP of the same relationship below
/// the first.
fn place_acps(view: &View, edges: &mut [DiagramEdge], shaper: &Shaper<'_>, style: Style) {
    let (size, line_height, font) = role_style(TextRole::Acp, style);
    let (ascent, descent) = shaper.ascent_descent();
    for acp in &view.acps {
        // On the relationship's first edge only (GSN v3 §1:5.3.3).
        let Some(edge) = edges.iter_mut().find(|edge| {
            edge.relationship == acp.relationship && edge.from == acp.from && edge.to == acp.to
        }) else {
            continue;
        };
        let (middle, across) = halfway(&edge.points);
        let row = edge.acps.len() as f32;
        // Beside a line that runs down, above one that runs across, so the
        // label never lies along the line.
        let centre = Point {
            x: middle.x,
            y: middle.y + row * line_height,
        };
        let square = Rect {
            x: centre.x - ACP_HALF,
            y: centre.y - ACP_HALF,
            width: 2.0 * ACP_HALF,
            height: 2.0 * ACP_HALF,
        };
        let width = shaper.width(&acp.label, size, font);
        let (x, baseline) = if across {
            // Above the square, centred on it; a second ACP to the right of
            // the first, its label further up.
            let square_top = middle.y - ACP_HALF;
            (
                middle.x + row * (2.0 * ACP_HALF + ACP_GAP) - width / 2.0,
                square_top - ACP_GAP - descent * size - row * line_height,
            )
        } else {
            // Centred on the square, as a line of a node is in its line height.
            (
                square.x + square.width + ACP_GAP,
                centre.y - (ascent + descent) * size / 2.0 + ascent * size,
            )
        };
        let square = if across {
            Rect {
                x: middle.x - ACP_HALF + row * (2.0 * ACP_HALF + ACP_GAP),
                y: middle.y - ACP_HALF,
                ..square
            }
        } else {
            square
        };
        let label = TextLine {
            text: acp.label.clone(),
            role: TextRole::Acp,
            x,
            baseline,
            width,
            size,
            weight: font.weight(),
        };
        let drawn = Rect {
            x: label.x,
            y: baseline - ascent * size,
            width,
            height: (ascent + descent) * size,
        };
        edge.bounds = edge.bounds.union(&square).union(&drawn);
        edge.acps.push(AcpMark {
            claim: acp.claim.clone(),
            package: acp.package.clone(),
            square,
            label,
        });
    }
}

/// The point halfway along a line, by its length, and whether the line runs
/// more across than down there.
fn halfway(points: &[Point]) -> (Point, bool) {
    let length: f32 = points
        .windows(2)
        .map(|pair| (pair[1].x - pair[0].x).hypot(pair[1].y - pair[0].y))
        .sum();
    let mut left = length / 2.0;
    for pair in points.windows(2) {
        let step = (pair[1].x - pair[0].x).hypot(pair[1].y - pair[0].y);
        if step >= left && step > 0.0 {
            let t = left / step;
            let across = (pair[1].x - pair[0].x).abs() > (pair[1].y - pair[0].y).abs();
            return (
                Point {
                    x: pair[0].x + (pair[1].x - pair[0].x) * t,
                    y: pair[0].y + (pair[1].y - pair[0].y) * t,
                },
                across,
            );
        }
        left -= step;
    }
    (
        points.first().copied().unwrap_or(Point { x: 0.0, y: 0.0 }),
        false,
    )
}
