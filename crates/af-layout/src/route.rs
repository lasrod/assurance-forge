//! Routing a line around the nodes: for a relationship that placed no node
//! (a second parent, the edge that closes a cycle, a context of two nodes),
//! whose line the placement does not give.
//!
//! The line runs across and down only, keeps [`CLEARANCE`] from every node,
//! and turns as seldom as it can: a shortest path, with each turn costed,
//! over the lines that run along the nodes' sides and between them. Its
//! corners are rounded. It leaves and enters a node at a port: a point on the
//! outline with the direction the line runs there.

use std::cmp::Reverse;
use std::collections::BinaryHeap;

use crate::model::{Point, Rect, Shape};

/// Kept between a routed line and every node's drawn bounds, beside it.
pub(crate) const CLEARANCE: f32 = 14.0;
/// The same above and below a node: a line across runs nearer the middle of
/// the band between two rows than the nodes' edges, where lines leave and
/// enter them.
pub(crate) const CLEARANCE_Y: f32 = 20.0;
/// The most points a routing grid may have: its search must stay small
/// enough for the browser, where the core runs as WebAssembly.
const MAX_GRID: usize = 200_000;
/// What a turn costs, in px of length.
const TURN: f32 = 48.0;
/// What running along a line from another node costs, per px: lines that
/// share a run cannot be told apart.
const SHARED: f32 = 6.0;
/// What running down another node's column costs, per px: the line would be
/// taken for one of that node's own.
const ZONED: f32 = 10.0;
/// The radius of a rounded corner, at most.
const CORNER: f32 = 8.0;
/// Points a rounded corner is drawn with.
const CORNER_STEPS: usize = 4;

/// A direction a line runs in.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Dir {
    Up,
    Down,
    Left,
    Right,
}

impl Dir {
    fn index(self) -> usize {
        match self {
            Dir::Up => 0,
            Dir::Down => 1,
            Dir::Left => 2,
            Dir::Right => 3,
        }
    }

    fn opposite(self) -> Dir {
        match self {
            Dir::Up => Dir::Down,
            Dir::Down => Dir::Up,
            Dir::Left => Dir::Right,
            Dir::Right => Dir::Left,
        }
    }

    fn vector(self) -> (f32, f32) {
        match self {
            Dir::Up => (0.0, -1.0),
            Dir::Down => (0.0, 1.0),
            Dir::Left => (-1.0, 0.0),
            Dir::Right => (1.0, 0.0),
        }
    }
}

const DIRS: [Dir; 4] = [Dir::Up, Dir::Down, Dir::Left, Dir::Right];

/// Where a line may leave or enter: a point, the direction away from what it
/// is on, how much choosing it costs, and the box the line must first clear.
#[derive(Clone, Copy, Debug)]
pub(crate) struct Port {
    pub at: Point,
    /// Away from the node: the way a line leaves, and the opposite of the
    /// way it enters.
    pub out: Dir,
    pub cost: f32,
    /// What the line must be clear of before it may turn: the node's bounds
    /// with the clearance, or a point's small box.
    pub clear: Rect,
}

/// Whether `point` is inside `shape`, by the ray from its centre.
fn inside(shape: &Shape, point: Point) -> bool {
    let c = shape.rect.centre();
    let (dx, dy) = (point.x - c.x, point.y - c.y);
    let on = shape.outline_point(dx, dy);
    dx.hypot(dy) <= (on.x - c.x).hypot(on.y - c.y)
}

/// The port on `shape`'s side `out`, `offset` px from the middle of that side
/// along it: where a line running towards the shape along that offset first
/// meets its outline.
pub(crate) fn port(shape: &Shape, bounds: Rect, out: Dir, offset: f32, cost: f32) -> Port {
    let c = shape.rect.centre();
    let (vx, vy) = out.vector();
    // Along the side: across for Up and Down, down for Left and Right.
    let (ax, ay) = (vy.abs(), vx.abs());
    let base = Point {
        x: c.x + ax * offset,
        y: c.y + ay * offset,
    };
    // From outside the shape in to the line through the centre.
    let far = shape.rect.width + shape.rect.height;
    let (mut outer, mut inner) = (far, 0.0f32);
    let at = |t: f32| Point {
        x: base.x + vx * t,
        y: base.y + vy * t,
    };
    if inside(shape, at(0.0)) {
        for _ in 0..24 {
            let middle = (outer + inner) / 2.0;
            if inside(shape, at(middle)) {
                inner = middle;
            } else {
                outer = middle;
            }
        }
    } else {
        // The offset misses the shape: the side's middle instead.
        return port(shape, bounds, out, 0.0, cost);
    }
    Port {
        at: at(outer),
        out,
        cost,
        clear: keep_clear(bounds),
    }
}

/// A port at a point that is not on a node: the line comes to it from `out`.
pub(crate) fn point_port(at: Point, out: Dir, cost: f32) -> Port {
    Port {
        at,
        out,
        cost,
        clear: inflate(
            Rect {
                x: at.x,
                y: at.y,
                width: 0.0,
                height: 0.0,
            },
            CLEARANCE,
        ),
    }
}

pub(crate) fn inflate(rect: Rect, by: f32) -> Rect {
    inflate_xy(rect, by, by)
}

/// A node's bounds with the clearance a routed line keeps from it.
fn keep_clear(rect: Rect) -> Rect {
    inflate_xy(rect, CLEARANCE, CLEARANCE_Y)
}

fn inflate_xy(rect: Rect, x: f32, y: f32) -> Rect {
    Rect {
        x: rect.x - x,
        y: rect.y - y,
        width: rect.width + 2.0 * x,
        height: rect.height + 2.0 * y,
    }
}

/// Where a line from `port` has cleared what the port is on.
fn stub(port: &Port) -> Point {
    let c = port.clear;
    match port.out {
        Dir::Up => Point {
            x: port.at.x,
            y: c.y - 1.0,
        },
        Dir::Down => Point {
            x: port.at.x,
            y: c.bottom() + 1.0,
        },
        Dir::Left => Point {
            x: c.x - 1.0,
            y: port.at.y,
        },
        Dir::Right => Point {
            x: c.right() + 1.0,
            y: port.at.y,
        },
    }
}

/// A run of a line already routed, which a line from another node should not
/// share. Lines from one node may: they fan out from one place.
#[derive(Clone, Copy, Debug)]
pub(crate) struct Run {
    a: Point,
    b: Point,
    /// The node the line leaves.
    from: usize,
}

/// The straight runs of a line already drawn that leaves the node `from`,
/// for the lines routed after it to keep off.
pub(crate) fn runs_of(path: &[Point], from: usize) -> impl Iterator<Item = Run> + '_ {
    path.windows(2).map(move |pair| Run {
        a: pair[0],
        b: pair[1],
        from,
    })
}

/// Coordinates, sorted, with those within a hundredth of a px merged.
struct Axis(Vec<f32>);

impl Axis {
    fn new(mut values: Vec<f32>) -> Self {
        values.sort_by(f32::total_cmp);
        values.dedup_by(|b, a| (*b - *a).abs() < 0.01);
        Axis(values)
    }

    /// The index of the coordinate at `value`, which must be one.
    fn at(&self, value: f32) -> usize {
        let i = self.0.partition_point(|v| *v < value - 0.01);
        i.min(self.0.len().saturating_sub(1))
    }
}

/// The routing grid over a window of the diagram.
struct Grid {
    xs: Axis,
    ys: Axis,
    /// Per point (x index × ys + y index): inside an obstacle.
    point: Vec<bool>,
    /// The run from a point to the next one right.
    right: Vec<bool>,
    /// The run from a point to the next one down.
    down: Vec<bool>,
}

impl Grid {
    /// The grid, or None when it would have more than [`MAX_GRID`] points
    /// even without the lines between obstacles.
    fn new(window: Rect, obstacles: &[Rect], ports: &[(Point, Point)]) -> Option<Self> {
        let mut xs = vec![window.x, window.right()];
        let mut ys = vec![window.y, window.bottom()];
        let inside_window = |r: &Rect| r.overlaps(&window);
        let kept: Vec<Rect> = obstacles.iter().copied().filter(inside_window).collect();
        // An obstacle's edges, but only within the window: past it are nodes
        // that are not obstacles here, which a grid line there could cross.
        for r in &kept {
            xs.extend([r.x.max(window.x), r.right().min(window.right())]);
            ys.extend([r.y.max(window.y), r.bottom().min(window.bottom())]);
        }
        // Between neighbouring obstacles, across: a line in the middle of
        // the band between two rows. Not down the middle of a gap between
        // two nodes, which is where the line into their parent comes down.
        let mut mids_y = Vec::new();
        {
            let mut sorted = ys.clone();
            sorted.sort_by(f32::total_cmp);
            for pair in sorted.windows(2) {
                if pair[1] - pair[0] > 1.0 {
                    mids_y.push((pair[0] + pair[1]) / 2.0);
                }
            }
        }
        for (a, b) in ports {
            xs.extend([a.x, b.x]);
            ys.extend([a.y, b.y]);
        }
        let plain = (Axis::new(xs.clone()), Axis::new(ys.clone()));
        ys.extend(mids_y);
        let (xs, ys) = {
            let with_gaps = (Axis::new(xs), Axis::new(ys));
            if with_gaps.0.0.len() * with_gaps.1.0.len() <= MAX_GRID {
                with_gaps
            } else if plain.0.0.len() * plain.1.0.len() <= MAX_GRID {
                plain
            } else {
                return None;
            }
        };
        let (w, h) = (xs.0.len(), ys.0.len());
        let mut grid = Grid {
            point: vec![false; w * h],
            right: vec![false; w * h],
            down: vec![false; w * h],
            xs,
            ys,
        };
        for r in &kept {
            let (a, b) = (grid.xs.at(r.x), grid.xs.at(r.right()));
            let (c, d) = (grid.ys.at(r.y), grid.ys.at(r.bottom()));
            for i in a..=b {
                for j in c..=d {
                    let k = i * h + j;
                    let inside_x = i > a && i < b;
                    let inside_y = j > c && j < d;
                    if inside_x && inside_y {
                        grid.point[k] = true;
                    }
                    if i < b && inside_y {
                        grid.right[k] = true;
                    }
                    if j < d && inside_x {
                        grid.down[k] = true;
                    }
                }
            }
        }
        Some(grid)
    }

    fn width(&self) -> usize {
        self.xs.0.len()
    }

    fn height(&self) -> usize {
        self.ys.0.len()
    }

    fn point(&self, k: usize) -> Point {
        let h = self.height();
        Point {
            x: self.xs.0[k / h],
            y: self.ys.0[k % h],
        }
    }

    /// The next point in `dir`, if the run there is free.
    fn step(&self, k: usize, dir: Dir) -> Option<usize> {
        let h = self.height();
        let (i, j) = (k / h, k % h);
        let next = match dir {
            Dir::Up if j > 0 && !self.down[k - 1] => k - 1,
            Dir::Down if j + 1 < h && !self.down[k] => k + 1,
            Dir::Left if i > 0 && !self.right[k - h] => k - h,
            Dir::Right if i + 1 < self.width() && !self.right[k] => k + h,
            _ => return None,
        };
        (!self.point[next]).then_some(next)
    }
}

/// How much of the run from `a` to `b` runs up or down inside a zone.
fn zoned(a: Point, b: Point, zones: &[Rect]) -> f32 {
    if (a.x - b.x).abs() > 0.5 {
        return 0.0;
    }
    let (lo, hi) = (a.y.min(b.y), a.y.max(b.y));
    zones
        .iter()
        .filter(|zone| a.x > zone.x && a.x < zone.right())
        .map(|zone| (hi.min(zone.bottom()) - lo.max(zone.y)).max(0.0))
        .sum()
}

/// How much of the run from `a` to `b` lies along a line from another node.
fn shared(a: Point, b: Point, from: usize, runs: &[Run]) -> f32 {
    let mut length = 0.0;
    for run in runs.iter().filter(|run| run.from != from) {
        if (a.y - b.y).abs() < 0.5 && (run.a.y - run.b.y).abs() < 0.5 && (a.y - run.a.y).abs() < 2.0
        {
            let (lo, hi) = (a.x.min(b.x), a.x.max(b.x));
            let (rlo, rhi) = (run.a.x.min(run.b.x), run.a.x.max(run.b.x));
            length += (hi.min(rhi) - lo.max(rlo)).max(0.0);
        } else if (a.x - b.x).abs() < 0.5
            && (run.a.x - run.b.x).abs() < 0.5
            && (a.x - run.a.x).abs() < 2.0
        {
            let (lo, hi) = (a.y.min(b.y), a.y.max(b.y));
            let (rlo, rhi) = (run.a.y.min(run.b.y), run.a.y.max(run.b.y));
            length += (hi.min(rhi) - lo.max(rlo)).max(0.0);
        }
    }
    length
}

/// The cheapest line from one of `starts` to one of `ends` around
/// `obstacles` (drawn bounds, without clearance), in a window around its
/// ends first and the whole `area` after. It keeps out of `zones` where it
/// can: the columns where other lines leave and enter nodes. The line is the points from the start
/// port to the end port, its corners rounded; `runs` gets its straight runs.
pub(crate) fn route(
    from: usize,
    starts: &[Port],
    ends: &[Port],
    obstacles: &[Rect],
    zones: &[Rect],
    area: Rect,
    runs: &mut Vec<Run>,
) -> Option<Vec<Point>> {
    let inflated: Vec<Rect> = obstacles.iter().map(|r| keep_clear(*r)).collect();
    let ports: Vec<(Point, Point)> = starts
        .iter()
        .chain(ends)
        .map(|port| (port.at, stub(port)))
        .collect();
    let span = ports.iter().fold(None::<Rect>, |acc, (_, s)| {
        let r = Rect {
            x: s.x,
            y: s.y,
            width: 0.0,
            height: 0.0,
        };
        Some(acc.map_or(r, |a| a.union(&r)))
    })?;
    let whole = inflate(area, 4.0 * CLEARANCE).union(&inflate(span, 4.0 * CLEARANCE));
    for margin in [160.0, 640.0, f32::INFINITY] {
        let window = if margin.is_finite() {
            inflate(span, margin)
        } else {
            whole
        };
        let request = Request {
            from,
            starts,
            ends,
            obstacles: &inflated,
            zones,
            ports: &ports,
            runs,
        };
        if let Some(path) = search(&request, window) {
            for pair in path.windows(2) {
                runs.push(Run {
                    a: pair[0],
                    b: pair[1],
                    from,
                });
            }
            return Some(round(&path));
        }
        if !margin.is_finite() {
            break;
        }
    }
    None
}

/// One line to route: from which node, between which ports, around what.
struct Request<'a> {
    from: usize,
    starts: &'a [Port],
    ends: &'a [Port],
    /// Every node's bounds with the clearance.
    obstacles: &'a [Rect],
    zones: &'a [Rect],
    /// Each port and where it has cleared its node.
    ports: &'a [(Point, Point)],
    runs: &'a [Run],
}

fn search(request: &Request<'_>, window: Rect) -> Option<Vec<Point>> {
    let Request {
        from: from_node,
        starts,
        ends,
        obstacles,
        zones,
        ports,
        runs,
    } = *request;
    let grid = Grid::new(window, obstacles, ports)?;
    let (w, h) = (grid.width(), grid.height());
    let index = |p: Point| grid.xs.at(p.x) * h + grid.ys.at(p.y);
    let goals: Vec<(usize, usize)> = ends
        .iter()
        .enumerate()
        .map(|(e, port)| (index(stub(port)), e))
        .collect();
    let estimate = |k: usize| {
        let p = grid.point(k);
        goals
            .iter()
            .map(|(g, _)| {
                let q = grid.point(*g);
                (p.x - q.x).abs() + (p.y - q.y).abs()
            })
            .fold(f32::INFINITY, f32::min)
    };
    let states = w * h * 4;
    let mut best = vec![f32::INFINITY; states];
    let mut from: Vec<u32> = vec![u32::MAX; states];
    let mut origin: Vec<u16> = vec![u16::MAX; states];
    let mut heap = BinaryHeap::new();
    let key = |cost: f32| (cost * 16.0).round() as i64;
    for (s, port) in starts.iter().enumerate() {
        let k = index(stub(port));
        if grid.point[k] {
            continue;
        }
        let state = k * 4 + port.out.index();
        let length = (port.at.x - stub(port).x).abs() + (port.at.y - stub(port).y).abs();
        let cost = port.cost + length;
        if cost < best[state] {
            best[state] = cost;
            origin[state] = s as u16;
            heap.push(Reverse((key(cost + estimate(k)), state)));
        }
    }
    let mut found: Option<(usize, usize, f32)> = None;
    while let Some(Reverse((_, state))) = heap.pop() {
        let (k, dir) = (state / 4, DIRS[state % 4]);
        let cost = best[state];
        if let Some((_, _, bound)) = found {
            if cost + estimate(k) >= bound {
                break;
            }
        }
        for &(g, e) in &goals {
            if g == k {
                let port = &ends[e];
                let arrive = port.out.opposite();
                let total = cost
                    + port.cost
                    + if dir == arrive { 0.0 } else { TURN }
                    + (port.at.x - stub(port).x).abs()
                    + (port.at.y - stub(port).y).abs();
                if found.is_none_or(|(_, _, bound)| total < bound) {
                    found = Some((state, e, total));
                }
            }
        }
        for next_dir in DIRS {
            if next_dir == dir.opposite() {
                continue;
            }
            let Some(next) = grid.step(k, next_dir) else {
                continue;
            };
            let (a, b) = (grid.point(k), grid.point(next));
            let length = (a.x - b.x).abs() + (a.y - b.y).abs();
            let turn = if next_dir == dir { 0.0 } else { TURN };
            let next_cost = cost
                + length
                + turn
                + SHARED * shared(a, b, from_node, runs)
                + ZONED * zoned(a, b, zones);
            let next_state = next * 4 + next_dir.index();
            if next_cost < best[next_state] {
                best[next_state] = next_cost;
                from[next_state] = state as u32;
                origin[next_state] = origin[state];
                heap.push(Reverse((key(next_cost + estimate(next)), next_state)));
            }
        }
    }
    let (state, end, _) = found?;
    let mut points = vec![ends[end].at];
    let mut at = state;
    loop {
        points.push(grid.point(at / 4));
        if from[at] == u32::MAX {
            break;
        }
        at = from[at] as usize;
    }
    points.push(starts[usize::from(origin[state])].at);
    points.reverse();
    Some(simplify(points))
}

/// Without repeated points and points in the middle of a straight run.
fn simplify(points: Vec<Point>) -> Vec<Point> {
    let mut out: Vec<Point> = Vec::with_capacity(points.len());
    for p in points {
        if out
            .last()
            .is_some_and(|q| (q.x - p.x).abs() < 0.01 && (q.y - p.y).abs() < 0.01)
        {
            continue;
        }
        if out.len() >= 2 {
            let (a, b) = (out[out.len() - 2], out[out.len() - 1]);
            let straight = ((a.x - b.x).abs() < 0.01 && (b.x - p.x).abs() < 0.01)
                || ((a.y - b.y).abs() < 0.01 && (b.y - p.y).abs() < 0.01);
            if straight {
                out.pop();
            }
        }
        out.push(p);
    }
    out
}

/// The corners rounded. The last run keeps room for an arrowhead.
fn round(points: &[Point]) -> Vec<Point> {
    if points.len() < 3 {
        return points.to_vec();
    }
    let length = |a: Point, b: Point| (b.x - a.x).hypot(b.y - a.y);
    let mut out = vec![points[0]];
    for i in 1..points.len() - 1 {
        let (a, p, b) = (points[i - 1], points[i], points[i + 1]);
        let (la, lb) = (length(a, p), length(p, b));
        let mut r = CORNER.min(la / 2.0).min(lb / 2.0);
        if i + 2 == points.len() {
            r = r.min((lb - crate::layout::ARROW_RUN).max(0.0));
        }
        if i == 1 {
            r = r.min(la / 2.0);
        }
        if r < 0.5 {
            out.push(p);
            continue;
        }
        let into = Point {
            x: p.x - (p.x - a.x) / la * r,
            y: p.y - (p.y - a.y) / la * r,
        };
        let away = Point {
            x: p.x + (b.x - p.x) / lb * r,
            y: p.y + (b.y - p.y) / lb * r,
        };
        out.push(into);
        for step in 1..=CORNER_STEPS {
            let t = step as f32 / CORNER_STEPS as f32;
            let u = 1.0 - t;
            out.push(Point {
                x: u * u * into.x + 2.0 * u * t * p.x + t * t * away.x,
                y: u * u * into.y + 2.0 * u * t * p.y + t * t * away.y,
            });
        }
    }
    out.push(points[points.len() - 1]);
    out
}
