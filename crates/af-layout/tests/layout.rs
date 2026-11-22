//! `af-layout` against goal G3 of the diagram plan: text measured and wrapped
//! from font bytes, deterministic output, no overlapping nodes, edges that end
//! on their nodes' outlines, children below their parent and context beside
//! it.

use af_gsn::view::{EdgeKind, NodeKind, View, view};
use af_layout::{
    Arrowhead, Decorator, Diagram, DiagramEdge, DiagramNode, Engine, Font, Fonts, LineStyle,
    Placement, Point, Style, TextRole, Weight, check,
};
use sacm::xmi::{self, Mode, ReadOptions};

const SAMPLES: [&str; 11] = [
    "challenges.sacm.xmi",
    "sacm_example_core_argument.sacm.xmi",
    "sample.sacm.xmi",
    "open-autonomy-safety-case.updated.v2.sacm.xmi",
    "oasc-ja.sacm.xmi",
    "safety-case-sanitized-strict.sacm.xmi",
    // The layout gallery (testdata/sacm/README.md).
    "gallery-shared-support.sacm.xmi",
    "gallery-attachments.sacm.xmi",
    "gallery-long-text.sacm.xmi",
    "gallery-japanese.sacm.xmi",
    "gallery-roots-and-cycle.sacm.xmi",
];

fn testdata(path: &str) -> Vec<u8> {
    let path = format!("{}/../../testdata/{path}", env!("CARGO_MANIFEST_DIR"));
    std::fs::read(&path).unwrap_or_else(|error| panic!("setup: {path}: {error}"))
}

fn fonts() -> Fonts {
    match Fonts::new(
        testdata("fonts/IBMPlexSans-Regular.ttf"),
        testdata("fonts/IBMPlexSans-SemiBold.ttf"),
        testdata("fonts/AFTestSansJP-Regular.ttf"),
        testdata("fonts/AFTestSansJP-SemiBold.ttf"),
        testdata("fonts/IBMPlexMono-Regular.ttf"),
    ) {
        Ok(fonts) => fonts,
        Err(error) => panic!("setup: {error:?}"),
    }
}

fn engine() -> Engine {
    Engine::new(fonts(), Style::default())
}

fn sample_view(name: &str, language: &str) -> View {
    let bytes = testdata(&format!("sacm/{name}"));
    match xmi::read(&bytes, &ReadOptions::default().with_mode(Mode::Strict)) {
        Ok(output) => view(output.document(), language),
        Err(error) => panic!("setup: {name}: {error:?}"),
    }
}

fn node<'a>(diagram: &'a Diagram, id: &str) -> &'a DiagramNode {
    match diagram.nodes.iter().find(|node| node.id == id) {
        Some(node) => node,
        None => panic!("no node {id}"),
    }
}

/// Whether `point` is inside the node's shape, by the ray from its centre.
fn inside(node: &DiagramNode, point: Point) -> bool {
    let c = node.shape.rect.centre();
    let (dx, dy) = (point.x - c.x, point.y - c.y);
    let on = node.shape.outline_point(dx, dy);
    dx.hypot(dy) <= (on.x - c.x).hypot(on.y - c.y) + 0.01
}

fn first_edge<'a>(diagram: &'a Diagram, relationship: &str) -> &'a DiagramEdge {
    match diagram
        .edges
        .iter()
        .find(|edge| edge.relationship == relationship)
    {
        Some(edge) => edge,
        None => panic!("no edge of {relationship}"),
    }
}

/// An edge's whole line: its points, and on to the arrowhead's tip.
fn line(edge: &DiagramEdge) -> Vec<Point> {
    let mut line = edge.points.clone();
    line.push(edge.head[0]);
    line
}

/// How far `point` is from an edge's line.
fn distance_to_line(edge: &DiagramEdge, point: Point) -> f32 {
    line(edge)
        .windows(2)
        .map(|pair| {
            let (a, b) = (pair[0], pair[1]);
            let (dx, dy) = (b.x - a.x, b.y - a.y);
            let length = dx * dx + dy * dy;
            let t = if length == 0.0 {
                0.0
            } else {
                (((point.x - a.x) * dx + (point.y - a.y) * dy) / length).clamp(0.0, 1.0)
            };
            (point.x - (a.x + dx * t)).hypot(point.y - (a.y + dy * t))
        })
        .fold(f32::INFINITY, f32::min)
}

/// The points of an edge's line, a pixel or so apart.
fn along(edge: &DiagramEdge) -> Vec<Point> {
    let mut points = Vec::new();
    for pair in line(edge).windows(2) {
        let (a, b) = (pair[0], pair[1]);
        let steps = (b.x - a.x).hypot(b.y - a.y).ceil().max(1.0) as usize;
        for step in 0..=steps {
            let t = step as f32 / steps as f32;
            points.push(Point {
                x: a.x + (b.x - a.x) * t,
                y: a.y + (b.y - a.y) * t,
            });
        }
    }
    points
}

/// Whether `point` is inside the node's shape by more than a pixel.
fn well_inside(node: &DiagramNode, point: Point) -> bool {
    let r = node.shape.rect;
    point.x > r.x && point.x < r.right() && point.y > r.y && point.y < r.bottom() && {
        let c = r.centre();
        let (dx, dy) = (point.x - c.x, point.y - c.y);
        let on = node.shape.outline_point(dx, dy);
        dx.hypot(dy) < (on.x - c.x).hypot(on.y - c.y) - 1.0
    }
}

// ===== Text =====

#[test]
fn latin_text_is_kerned() {
    let engine = engine();
    let pair = engine.measure("AV", 14.0, Font::Regular);
    let apart = engine.measure("A", 14.0, Font::Regular) + engine.measure("V", 14.0, Font::Regular);
    assert!(pair < apart - 0.1, "AV {pair} against A + V {apart}");
}

#[test]
fn kana_is_not_kerned() {
    // The Japanese face kerns ー then ノ by -60 units: 0.84 px at 14 px.
    let engine = engine();
    let pair = engine.measure("ーノ", 14.0, Font::Regular);
    let apart =
        engine.measure("ー", 14.0, Font::Regular) + engine.measure("ノ", 14.0, Font::Regular);
    assert!(
        (pair - apart).abs() < 0.001,
        "ーノ {pair} against ー + ノ {apart}"
    );
}

#[test]
fn a_character_the_latin_face_lacks_comes_from_the_japanese_face() {
    // A kanji is one em wide in the Japanese face.
    let engine = engine();
    let width = engine.measure("自動", 14.0, Font::Regular);
    assert!((width - 28.0).abs() < 0.01, "自動 is {width}");
}

#[test]
fn wrapped_lines_fit_and_keep_every_word() {
    let engine = engine();
    let text = "The braking system architecture mitigates identified hazardous failures in every operating mode.";
    let lines = engine.wrap(text, 14.0, Font::Regular, 160.0);
    assert!(lines.len() > 2, "{lines:?}");
    for line in &lines {
        assert!(line.width <= 160.0, "{line:?}");
        assert_eq!(line.text, line.text.trim(), "{line:?}");
        let measured = engine.measure(&line.text, 14.0, Font::Regular);
        assert!((measured - line.width).abs() < 0.001, "{line:?}");
    }
    let joined: Vec<&str> = lines.iter().map(|line| line.text.as_str()).collect();
    assert_eq!(joined.join(" "), text);
}

#[test]
fn a_word_wider_than_the_line_is_broken_between_characters() {
    let engine = engine();
    let word = "Reasoning_ArchitectureAndVerification";
    let lines = engine.wrap(word, 14.0, Font::SemiBold, 120.0);
    assert!(lines.len() > 1, "{lines:?}");
    assert!(lines.iter().all(|line| line.width <= 120.0), "{lines:?}");
    let joined: String = lines.iter().map(|line| line.text.as_str()).collect();
    assert_eq!(joined, word);
}

#[test]
fn japanese_wraps_between_characters_and_a_newline_ends_a_line() {
    let engine = engine();
    let lines = engine.wrap(
        "自動運転車は導入するのに十分安全である。",
        14.0,
        Font::Regular,
        100.0,
    );
    assert!(lines.len() > 1, "{lines:?}");
    assert!(lines.iter().all(|line| line.width <= 100.0), "{lines:?}");
    let lines = engine.wrap("first\nsecond", 14.0, Font::Regular, 500.0);
    let texts: Vec<&str> = lines.iter().map(|line| line.text.as_str()).collect();
    assert_eq!(texts, ["first", "second"]);
    assert!(engine.wrap("", 14.0, Font::Regular, 100.0).is_empty());
}

#[test]
fn a_file_that_is_not_a_font_is_refused() {
    let refused = Fonts::new(
        b"not a font".to_vec(),
        testdata("fonts/IBMPlexSans-SemiBold.ttf"),
        testdata("fonts/AFTestSansJP-Regular.ttf"),
        testdata("fonts/AFTestSansJP-SemiBold.ttf"),
        testdata("fonts/IBMPlexMono-Regular.ttf"),
    );
    assert!(matches!(refused, Err(error) if error.face == af_layout::FaceName::SansRegular));
    let refused = Fonts::new(
        testdata("fonts/IBMPlexSans-Regular.ttf"),
        testdata("fonts/IBMPlexSans-SemiBold.ttf"),
        testdata("fonts/AFTestSansJP-Regular.ttf"),
        testdata("fonts/AFTestSansJP-SemiBold.ttf"),
        b"not a font".to_vec(),
    );
    assert!(matches!(refused, Err(error) if error.face == af_layout::FaceName::MonoRegular));
}

#[test]
fn an_identifier_is_set_in_plex_mono() {
    // Every Plex Mono advance is 600 units in 1,000 (DATUM decision 0027).
    let engine = engine();
    let width = engine.measure("cl_top", 100.0, Font::Mono);
    assert!((width - 360.0).abs() < 0.01, "cl_top is {width}");
    // Not the sans face, whose letters differ in width.
    let sans = engine.measure("cl_top", 100.0, Font::Regular);
    assert!((width - sans).abs() > 1.0, "mono {width}, sans {sans}");
}

#[test]
fn a_character_plex_mono_lacks_comes_from_the_faces_after_it() {
    // Plex Mono has no kanji: it is the Japanese face's, one em wide.
    let engine = engine();
    let width = engine.measure("G自", 100.0, Font::Mono);
    assert!((width - 160.0).abs() < 0.01, "G自 is {width}");
}

#[test]
fn every_identifier_in_the_diagram_is_measured_in_plex_mono() {
    let mut engine = engine();
    let diagram = engine.layout(&sample_view("sacm_example_core_argument.sacm.xmi", "en"));
    let size = Style::default().identifier_size;
    let identifiers: Vec<_> = diagram
        .nodes
        .iter()
        .flat_map(|node| &node.text)
        .filter(|line| line.role == TextRole::Identifier)
        .collect();
    assert_eq!(identifiers.len(), diagram.nodes.len());
    for line in identifiers {
        let mono = engine.measure(&line.text, size, Font::Mono);
        assert!((line.width - mono).abs() < 0.001, "{line:?} against {mono}");
        assert_eq!(line.weight, Weight::Regular, "{line:?}");
    }
}

// ===== The core sample =====

#[test]
fn the_core_sample_is_drawn_as_the_plan_expects() {
    let view = sample_view("sacm_example_core_argument.sacm.xmi", "en");
    let diagram = engine().layout(&view);
    assert_eq!(diagram.nodes.len(), 8);
    assert_eq!(diagram.edges.len(), 7);

    let top = node(&diagram, "cl_top");
    let context = node(&diagram, "ctx_1");
    assert!((context.shape.rect.centre().y - top.shape.rect.centre().y).abs() < 0.01);
    assert!(context.bounds.x >= top.bounds.right());
    assert!(node(&diagram, "ar_1").bounds.y >= top.bounds.bottom());

    let undeveloped = node(&diagram, "cl_sub_2");
    assert!(
        undeveloped
            .decorators
            .iter()
            .any(|decorator| matches!(decorator, Decorator::Undeveloped { .. }))
    );
    let assumption = node(&diagram, "cl_assumption_1");
    assert!(
        assumption
            .decorators
            .iter()
            .any(|decorator| matches!(decorator, Decorator::Letter { text: 'A', .. }))
    );

    for edge in &diagram.edges {
        let expected = match edge.kind {
            EdgeKind::SupportedBy => Arrowhead::Filled,
            EdgeKind::InContextOf => Arrowhead::Hollow,
            EdgeKind::Challenge | EdgeKind::Neutral => Arrowhead::Open,
        };
        assert_eq!(edge.arrowhead, expected, "{}", edge.relationship);
        assert_eq!(edge.style, LineStyle::Solid);
    }
    // The text shows identifier, name and statement.
    let texts: Vec<&str> = top.text.iter().map(|line| line.text.as_str()).collect();
    assert_eq!(texts.first(), Some(&"cl_top"));
    assert!(texts.contains(&"TopClaim"));
}

// ===== Every sample =====

#[test]
fn the_layout_is_deterministic() {
    for name in SAMPLES {
        let view = sample_view(name, "en");
        let first = engine().layout(&view);
        let mut warm = engine();
        warm.layout(&view);
        assert_eq!(first, warm.layout(&view), "{name}");
    }
}

#[test]
fn every_node_and_edge_of_the_view_is_drawn() {
    for name in SAMPLES {
        let view = sample_view(name, "en");
        let diagram = engine().layout(&view);
        let ids: Vec<&str> = diagram.nodes.iter().map(|node| node.id.as_str()).collect();
        let expected: Vec<&str> = view.nodes.iter().map(|node| node.id.as_str()).collect();
        assert_eq!(ids, expected, "{name}");
        assert_eq!(diagram.edges.len(), view.edges.len(), "{name}");
    }
}

#[test]
fn no_two_nodes_overlap() {
    for name in SAMPLES {
        let diagram = engine().layout(&sample_view(name, "en"));
        for (i, a) in diagram.nodes.iter().enumerate() {
            for b in &diagram.nodes[i + 1..] {
                assert!(
                    !a.bounds.overlaps(&b.bounds),
                    "{name}: {} and {} overlap",
                    a.id,
                    b.id
                );
            }
        }
    }
}

#[test]
fn edges_start_and_end_on_their_nodes_outlines() {
    for name in SAMPLES {
        let diagram = engine().layout(&sample_view(name, "en"));
        for edge in &diagram.edges {
            let from = node(&diagram, &edge.from);
            let start = edge.points[0];
            let tip = edge.head[0];
            assert!(
                from.shape.outline_distance(start) < 0.05,
                "{name}: {} starts off {}",
                edge.relationship,
                from.id
            );
            if edge.to_relationship {
                // A challenge of a relationship ends on that relationship's line.
                let off = distance_to_line(first_edge(&diagram, &edge.to), tip);
                assert!(
                    off < 0.05,
                    "{name}: {} ends {off} off the line of {}",
                    edge.relationship,
                    edge.to
                );
                continue;
            }
            let to = node(&diagram, &edge.to);
            assert!(
                to.shape.outline_distance(tip) < 0.05,
                "{name}: {} ends off {}",
                edge.relationship,
                to.id
            );
        }
    }
}

#[test]
fn children_sit_below_their_parent_and_context_beside_it() {
    for name in SAMPLES {
        let diagram = engine().layout(&sample_view(name, "en"));
        let mut placed = 0;
        for edge in diagram.edges.iter().filter(|edge| !edge.to_relationship) {
            let parent = node(&diagram, &edge.from);
            let child = node(&diagram, &edge.to);
            match edge.placement {
                Placement::Below => {
                    assert!(
                        child.bounds.y >= parent.bounds.bottom(),
                        "{name}: {} not below {}",
                        child.id,
                        parent.id
                    );
                    placed += 1;
                }
                Placement::Beside => {
                    // In a lane on either side, from the parent's row down.
                    let (a, b) = (parent.shape.rect.centre().y, child.shape.rect.centre().y);
                    assert!(b >= a - 0.01, "{name}: {} above {}", child.id, parent.id);
                    assert!(
                        child.bounds.x >= parent.bounds.right()
                            || child.bounds.right() <= parent.bounds.x,
                        "{name}: {} not beside {}",
                        child.id,
                        parent.id
                    );
                    if edge.kind == EdgeKind::InContextOf {
                        placed += 1;
                    }
                }
                Placement::Challenger | Placement::None => {}
            }
        }
        assert!(placed > 0, "{name}: no edge placed anything");
    }
}

#[test]
fn text_stays_inside_its_shape() {
    for (name, language) in [
        ("sacm_example_core_argument.sacm.xmi", "en"),
        ("oasc-ja.sacm.xmi", "ja"),
        ("safety-case-sanitized-strict.sacm.xmi", "en"),
    ] {
        let diagram = engine().layout(&sample_view(name, language));
        for node in &diagram.nodes {
            for line in &node.text {
                let top = line.baseline - line.size;
                let bottom = line.baseline + line.size * 0.25;
                for corner in [
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
                ] {
                    assert!(
                        inside(node, corner),
                        "{name}: {:?} leaves {} ({:?})",
                        line.text,
                        node.id,
                        node.kind
                    );
                }
            }
        }
    }
}

#[test]
fn the_large_case_draws_its_justifications_as_ellipses() {
    let diagram = engine().layout(&sample_view("safety-case-sanitized-strict.sacm.xmi", "en"));
    let ellipses = diagram
        .nodes
        .iter()
        .filter(|node| node.kind == NodeKind::Justification)
        .filter(|node| matches!(node.shape.kind, af_layout::ShapeKind::Ellipse))
        .count();
    assert_eq!(ellipses, 13);
    let neutral = diagram
        .nodes
        .iter()
        .filter(|node| matches!(node.shape.kind, af_layout::ShapeKind::Neutral))
        .count();
    assert_eq!(neutral, 20);
}

#[test]
fn measurements_are_kept_and_dropped_with_the_style() {
    let view = sample_view("open-autonomy-safety-case.updated.v2.sacm.xmi", "en");
    let mut engine = engine();
    engine.layout(&view);
    let kept = engine.cached_texts();
    assert!(kept > 0);
    engine.layout(&view);
    assert_eq!(
        engine.cached_texts(),
        kept,
        "a second layout measured again"
    );
    engine.set_style(Style {
        text_size: 16.0,
        ..Style::default()
    });
    assert_eq!(engine.cached_texts(), 0);
}

#[test]
fn a_larger_text_size_makes_larger_nodes() {
    let view = sample_view("sacm_example_core_argument.sacm.xmi", "en");
    let small = engine().layout(&view);
    let mut large_engine = Engine::new(
        fonts(),
        Style {
            text_size: 18.0,
            text_line_height: 26.0,
            ..Style::default()
        },
    );
    let large = large_engine.layout(&view);
    let height = |diagram: &Diagram| node(diagram, "cl_top").shape.rect.height;
    assert!(height(&large) > height(&small));
}

/// The plan's budget for native layout, in a release build. Run with
/// `cargo test --release -p af-layout --test layout -- --ignored`.
#[test]
#[ignore = "a timing check: run it in release on the development machine"]
fn native_layout_is_within_budget() {
    for (name, budget_ms) in [
        ("sacm_example_core_argument.sacm.xmi", 10.0),
        ("safety-case-sanitized-strict.sacm.xmi", 300.0),
    ] {
        let view = sample_view(name, "en");
        let fonts = fonts();
        let mut best = f64::INFINITY;
        for _ in 0..5 {
            let mut engine = Engine::new(fonts.clone(), Style::default());
            let start = std::time::Instant::now();
            engine.layout(&view);
            best = best.min(start.elapsed().as_secs_f64() * 1000.0);
        }
        println!("{name}: first layout {best:.2} ms (budget {budget_ms} ms)");
        assert!(
            best <= budget_ms,
            "{name}: {best:.2} ms over {budget_ms} ms"
        );
    }
}

// ===== Found by the code review =====

#[test]
fn trailing_newline_and_spaces_add_no_line_and_blank_lines_between_are_kept() {
    let engine = engine();
    let lines = engine.wrap("Statement text\n    ", 14.0, Font::Regular, 500.0);
    let texts: Vec<&str> = lines.iter().map(|line| line.text.as_str()).collect();
    assert_eq!(texts, ["Statement text"]);
    let lines = engine.wrap("a\n\nb", 14.0, Font::Regular, 500.0);
    let texts: Vec<&str> = lines.iter().map(|line| line.text.as_str()).collect();
    assert_eq!(texts, ["a", "", "b"]);
}

#[test]
fn a_wrapped_line_is_as_full_as_it_can_be() {
    // Greedy wrapping: no line could take the next line's first word.
    let engine = engine();
    let text = "Verification activities demonstrate that the braking requirements are satisfied in every mode we know of today.";
    let lines = engine.wrap(text, 14.0, Font::Regular, 150.0);
    for pair in lines.windows(2) {
        let next_word = pair[1].text.split(' ').next().unwrap_or_default();
        let joined = format!("{} {next_word}", pair[0].text);
        assert!(
            engine.measure(&joined, 14.0, Font::Regular) > 150.0,
            "{:?} could have taken {next_word:?}",
            pair[0].text
        );
    }
}

#[test]
fn the_bounds_hold_every_edge_too() {
    // An edge from a node to itself loops out past the node.
    let text = r#"<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:id="case">
  <name content="C"/>
  <argumentPackage xmi:id="p"><name content="P"/>
    <argumentElement xmi:type="sacm:Claim" xmi:id="g"><name content="g"/></argumentElement>
    <argumentElement xmi:type="sacm:AssertedInference" xmi:id="i" source="g" target="g"><name content="i"/></argumentElement>
  </argumentPackage>
</sacm:AssuranceCasePackage>"#;
    let document = match xmi::read(
        text.as_bytes(),
        &ReadOptions::default().with_mode(Mode::Strict),
    ) {
        Ok(output) => output.into_document(),
        Err(error) => panic!("setup: {error:?}"),
    };
    let diagram = engine().layout(&view(&document, "en"));
    let b = diagram.bounds;
    for edge in &diagram.edges {
        for point in edge.points.iter().chain(&edge.head) {
            assert!(
                point.x >= b.x && point.x <= b.right() && point.y >= b.y && point.y <= b.bottom(),
                "{point:?} is outside {b:?}"
            );
        }
    }
}

#[test]
fn indented_text_starts_with_its_first_word() {
    let engine = engine();
    let lines = engine.wrap("\n  The system is safe", 14.0, Font::Regular, 120.0);
    // Without its indentation it fits on one line.
    let texts: Vec<&str> = lines.iter().map(|line| line.text.as_str()).collect();
    assert_eq!(texts, ["The system is safe"]);
    let lines = engine.wrap(
        "   Averyveryverylongwordthatmustbreak",
        14.0,
        Font::Regular,
        120.0,
    );
    assert!(lines.iter().all(|line| !line.text.is_empty()), "{lines:?}");
    assert!(lines[0].text.starts_with('A'), "{lines:?}");
    let lines = engine.wrap(
        "abc\n   Averyveryverylongwordthatmustbreak",
        14.0,
        Font::Regular,
        120.0,
    );
    assert_eq!(lines[0].text, "abc");
    assert!(lines[1].text.starts_with('A'), "{lines:?}");
}

#[test]
fn a_cycle_is_rooted_at_its_first_node_that_is_not_only_context() {
    // G1 and G2 support each other; C1 is G2's context and comes first.
    let text = r#"<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:id="case">
  <name content="C"/>
  <argumentPackage xmi:id="p"><name content="P"/>
    <argumentElement xmi:type="sacm:ArtifactReference" xmi:id="c1"><name content="c1"/></argumentElement>
    <argumentElement xmi:type="sacm:Claim" xmi:id="g1"><name content="g1"/></argumentElement>
    <argumentElement xmi:type="sacm:Claim" xmi:id="g2"><name content="g2"/></argumentElement>
    <argumentElement xmi:type="sacm:AssertedInference" xmi:id="i1" source="g2" target="g1"><name content="i1"/></argumentElement>
    <argumentElement xmi:type="sacm:AssertedInference" xmi:id="i2" source="g1" target="g2"><name content="i2"/></argumentElement>
    <argumentElement xmi:type="sacm:AssertedContext" xmi:id="x1" source="c1" target="g2"><name content="x1"/></argumentElement>
  </argumentPackage>
</sacm:AssuranceCasePackage>"#;
    let document = match xmi::read(
        text.as_bytes(),
        &ReadOptions::default().with_mode(Mode::Strict),
    ) {
        Ok(output) => output.into_document(),
        Err(error) => panic!("setup: {error:?}"),
    };
    let diagram = engine().layout(&view(&document, "en"));
    let context = diagram.edges.iter().find(|edge| edge.relationship == "x1");
    assert_eq!(context.map(|edge| edge.placement), Some(Placement::Beside));
}

#[test]
fn blank_lines_before_only_spaces_are_dropped() {
    let engine = engine();
    for text in ["Statement text\n\n    ", "Statement text\n\n\n"] {
        let lines = engine.wrap(text, 14.0, Font::Regular, 500.0);
        let texts: Vec<&str> = lines.iter().map(|line| line.text.as_str()).collect();
        assert_eq!(texts, ["Statement text"], "{text:?}");
    }
}

#[test]
fn a_size_that_is_not_a_number_changes_nothing() {
    let mut engine = engine();
    let unresolved = Style {
        text_size: f32::NAN,
        ..Style::default()
    };
    assert!(!engine.set_style(unresolved));
    assert!(
        !engine.set_style(unresolved),
        "NaN is never equal to itself"
    );
    assert_eq!(engine.style(), Style::default());
    assert!(engine.set_style(Style {
        text_size: 16.0,
        ..Style::default()
    }));
}

// ===== Lanes, challenges and the lines between =====

fn case_view(body: &str) -> View {
    let text = format!(
        r#"<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:id="case">
  <name content="C"/>
  <argumentPackage xmi:id="p"><name content="P"/>
    {body}
  </argumentPackage>
</sacm:AssuranceCasePackage>"#
    );
    match xmi::read(
        text.as_bytes(),
        &ReadOptions::default().with_mode(Mode::Strict),
    ) {
        Ok(output) => view(output.document(), "en"),
        Err(error) => panic!("setup: {error:?}"),
    }
}

fn element(kind: &str, id: &str, attributes: &str) -> String {
    format!(
        r#"<argumentElement xmi:type="sacm:{kind}" xmi:id="{id}"{attributes}><name content="{id}"/></argumentElement>"#
    )
}

#[test]
fn a_line_the_layout_placed_crosses_no_node() {
    for name in SAMPLES {
        let diagram = engine().layout(&sample_view(name, "en"));
        for edge in &diagram.edges {
            if edge.placement == Placement::None {
                continue;
            }
            let points = along(edge);
            for other in &diagram.nodes {
                if other.id == edge.from || (!edge.to_relationship && other.id == edge.to) {
                    continue;
                }
                assert!(
                    !points.iter().any(|point| well_inside(other, *point)),
                    "{name}: {} crosses {}",
                    edge.relationship,
                    other.id
                );
            }
        }
    }
}

#[test]
fn the_crossing_check_sees_a_line_through_a_node() {
    // The check above passes only if it can fail: an edge that placed
    // nothing runs straight, here through the node between its ends.
    let diagram = engine().layout(&case_view(
        &[
            element("Claim", "a", ""),
            element("Claim", "b", ""),
            element("Claim", "c", ""),
            element("AssertedInference", "i1", r#" source="b" target="a""#),
            element("AssertedInference", "i2", r#" source="c" target="b""#),
            element("AssertedInference", "i3", r#" source="a" target="c""#),
        ]
        .concat(),
    ));
    let straight = first_edge(&diagram, "i3");
    assert_eq!(straight.placement, Placement::None);
    let between = node(&diagram, "b");
    assert!(
        along(straight)
            .iter()
            .any(|point| well_inside(between, *point))
    );
}

#[test]
fn context_takes_both_sides_and_stacks_from_its_node_down() {
    let diagram = engine().layout(&sample_view("challenges.sacm.xmi", "en"));
    let top = node(&diagram, "g_top");
    let first = node(&diagram, "c_scope");
    let second = node(&diagram, "c_definition");
    let third = node(&diagram, "a_maintained");
    let level = |a: &DiagramNode, b: &DiagramNode| {
        (a.shape.rect.centre().y - b.shape.rect.centre().y).abs() < 0.01
    };
    // The first on the right, the second on the left, both level with it.
    assert!(first.bounds.x >= top.bounds.right());
    assert!(second.bounds.right() <= top.bounds.x);
    assert!(level(first, top) && level(second, top));
    // The third is under the first, their near sides in line.
    assert!(third.bounds.y >= first.bounds.bottom());
    assert!((third.shape.rect.x - first.shape.rect.x).abs() < 0.01);
    // What is beside a node pushes its children down, not sideways.
    let strategy = node(&diagram, "s_hazards");
    assert!(strategy.bounds.y >= third.bounds.bottom());
    assert!((strategy.shape.rect.centre().x - top.shape.rect.centre().x).abs() < 0.01);
}

#[test]
fn a_challenge_sits_by_what_it_challenges_and_points_at_it() {
    let diagram = engine().layout(&sample_view("challenges.sacm.xmi", "en"));
    for edge in &diagram.edges {
        if edge.kind == EdgeKind::Challenge {
            assert_eq!(edge.style, LineStyle::Dashed, "{}", edge.relationship);
            assert_eq!(edge.arrowhead, Arrowhead::Open, "{}", edge.relationship);
            assert_eq!(
                edge.placement,
                Placement::Challenger,
                "{}",
                edge.relationship
            );
        } else {
            assert_eq!(edge.style, LineStyle::Solid, "{}", edge.relationship);
        }
    }
    let beside = |a: &DiagramNode, b: &DiagramNode| {
        a.bounds.x >= b.bounds.right() || a.bounds.right() <= b.bounds.x
    };

    // Of a claim: in a lane beside it.
    let (claim, counter) = (node(&diagram, "g_trap"), node(&diagram, "cg_thin"));
    assert!(beside(counter, claim));
    assert!(counter.bounds.y < claim.bounds.bottom() && claim.bounds.y < counter.bounds.bottom());
    assert!(
        claim
            .shape
            .outline_distance(first_edge(&diagram, "k_thin").head[0])
            < 0.05
    );

    // Of a context, which is itself in a lane: under it.
    let (context, counter) = (node(&diagram, "c_definition"), node(&diagram, "ce_audit"));
    assert!(counter.bounds.y >= context.bounds.bottom());
    assert!(
        context
            .shape
            .outline_distance(first_edge(&diagram, "k_audit").head[0])
            < 0.05
    );

    // Of the line down into a node: beside that line, between its ends.
    let (goal, evidence) = (node(&diagram, "g_trap"), node(&diagram, "sn_trials"));
    let counter = node(&diagram, "cg_sample");
    assert!(counter.bounds.y >= goal.bounds.bottom());
    assert!(counter.bounds.bottom() <= evidence.bounds.y);
    let tip = first_edge(&diagram, "k_sample").head[0];
    assert!((tip.x - evidence.shape.rect.centre().x).abs() < 0.01);
    assert!((tip.y - counter.shape.rect.centre().y).abs() < 0.01);

    // Of an inference with a Strategy: at its first edge, the one into the
    // Strategy.
    let tip = first_edge(&diagram, "k_complete").head[0];
    let into_strategy = first_edge(&diagram, "i_hazards");
    assert_eq!(into_strategy.to, "s_hazards");
    assert!(distance_to_line(into_strategy, tip) < 0.05);

    // Of the line out to a context: under the context, its arrow coming up
    // between the context and its node.
    let (top, context) = (node(&diagram, "g_top"), node(&diagram, "c_scope"));
    let counter = node(&diagram, "cg_scope");
    assert!(counter.bounds.y >= context.bounds.bottom());
    let tip = first_edge(&diagram, "k_scope").head[0];
    assert!(tip.x > top.bounds.right() && tip.x < context.bounds.x);
    assert!(distance_to_line(first_edge(&diagram, "x_scope"), tip) < 0.05);

    // Of a challenge: at the dashed line it challenges.
    let tip = first_edge(&diagram, "k_rebuttal").head[0];
    assert!(distance_to_line(first_edge(&diagram, "k_thin"), tip) < 0.05);
}

#[test]
fn a_bent_line_ends_straight_for_its_arrowhead() {
    for name in SAMPLES {
        let diagram = engine().layout(&sample_view(name, "en"));
        for edge in diagram.edges.iter().filter(|edge| edge.points.len() > 2) {
            let line = line(edge);
            let (tip, before) = (line[line.len() - 1], line[line.len() - 2]);
            let earlier = line[line.len() - 3];
            let run = (tip.x - earlier.x).hypot(tip.y - earlier.y);
            let turn = (tip.x - before.x) * (before.y - earlier.y)
                - (tip.y - before.y) * (before.x - earlier.x);
            assert!(
                turn.abs() < 0.01 && run >= 10.0,
                "{name}: {} bends under its arrowhead",
                edge.relationship
            );
        }
    }
}

#[test]
fn challenges_that_point_at_each_other_are_still_drawn() {
    let diagram = engine().layout(&case_view(
        &[
            element("Claim", "g", ""),
            element("Claim", "a", ""),
            element("Claim", "b", ""),
            element(
                "AssertedInference",
                "k0",
                r#" source="a" target="g" isCounter="true""#,
            ),
            element(
                "AssertedInference",
                "k1",
                r#" source="a" target="k2" isCounter="true""#,
            ),
            element(
                "AssertedInference",
                "k2",
                r#" source="b" target="k1" isCounter="true""#,
            ),
        ]
        .concat(),
    ));
    assert_eq!(diagram.edges.len(), 3);
    for edge in &diagram.edges {
        assert!(edge.points.len() >= 2, "{}", edge.relationship);
        for point in edge.points.iter().chain(&edge.head) {
            assert!(
                point.x.is_finite() && point.y.is_finite(),
                "{}",
                edge.relationship
            );
        }
    }
}

#[test]
fn a_neutral_edge_is_dotted_and_keeps_its_end_in_the_tree() {
    // A defeated inference is still drawn below its conclusion (E5a).
    let diagram = engine().layout(&case_view(
        &[
            element("Claim", "top", ""),
            element("Claim", "sub", ""),
            element(
                "AssertedInference",
                "i",
                r#" source="sub" target="top" assertionDeclaration="defeated""#,
            ),
        ]
        .concat(),
    ));
    let edge = first_edge(&diagram, "i");
    assert_eq!(edge.kind, EdgeKind::Neutral);
    assert_eq!(edge.style, LineStyle::Dotted);
    assert_eq!(edge.arrowhead, Arrowhead::Open);
    assert_eq!(edge.placement, Placement::Below);
    assert!(node(&diagram, "sub").bounds.y >= node(&diagram, "top").bounds.bottom());
}

#[test]
fn an_assumption_reached_by_support_still_sits_beside_its_goal() {
    // The core sample reaches its Assumption by an AssertedInference. GSN
    // puts an Assumption beside its goal; the line stays as the file says.
    let diagram = engine().layout(&sample_view("sacm_example_core_argument.sacm.xmi", "en"));
    let (goal, assumption) = (
        node(&diagram, "cl_sub_1"),
        node(&diagram, "cl_assumption_1"),
    );
    let level = (goal.shape.rect.centre().y - assumption.shape.rect.centre().y).abs() < 0.01;
    assert!(level, "not level with its goal");
    assert!(
        assumption.bounds.x >= goal.bounds.right() || assumption.bounds.right() <= goal.bounds.x
    );
    let edge = first_edge(&diagram, "inf_2");
    assert_eq!(edge.placement, Placement::Beside);
    assert_eq!(edge.kind, EdgeKind::SupportedBy);
    assert_eq!(edge.arrowhead, Arrowhead::Filled);
    // The evidence is still below.
    assert!(node(&diagram, "ev_1").bounds.y >= goal.bounds.bottom());
}

// ===== The geometry check =====

#[test]
fn the_geometry_check_passes_every_sample_on_overlap_and_text() {
    for name in SAMPLES {
        let language = if name.contains("ja") { "ja" } else { "en" };
        let report = check(&engine().layout(&sample_view(name, language)));
        assert_eq!(report.node_overlaps, [], "{name}");
        assert_eq!(report.text_escapes, [], "{name}");
    }
}

/// The check judges geometry, so it is shown wrong geometry: a valid layout
/// broken by hand in each way it must find. A check that cannot fail checks
/// nothing.
#[test]
fn the_geometry_check_finds_each_defect_made_by_hand() {
    let valid = engine().layout(&sample_view("sacm_example_core_argument.sacm.xmi", "en"));
    let report = check(&valid);
    assert!(report.is_valid(), "{report:?}");
    assert_eq!(report.edge_crossings, 0);
    assert_eq!((report.nodes, report.edges), (8, 7));

    // A node moved onto another.
    let mut overlapping = valid.clone();
    let on = node(&overlapping, "cl_sub_2").bounds;
    let moved = overlapping
        .nodes
        .iter_mut()
        .find(|node| node.id == "ev_1")
        .map(|node| node.bounds = on);
    assert!(moved.is_some());
    let report = check(&overlapping);
    assert!(!report.is_valid());
    assert!(
        report
            .node_overlaps
            .iter()
            .any(|(a, b)| [a.as_str(), b.as_str()].contains(&"ev_1")
                && [a.as_str(), b.as_str()].contains(&"cl_sub_2")),
        "{report:?}"
    );

    // A line from cl_top to ar_1 sent through ev_1, which it does not join.
    let mut through = valid.clone();
    let detour = node(&through, "ev_1").shape.rect.centre();
    let edge = through
        .edges
        .iter_mut()
        .find(|edge| edge.from == "cl_top" && edge.to == "ar_1");
    let Some(edge) = edge else {
        panic!("no edge cl_top to ar_1")
    };
    let start = edge.points[0];
    edge.points = vec![start, detour, edge.head[0]];
    edge.bounds = af_layout::Rect {
        x: -1e4,
        y: -1e4,
        width: 2e4,
        height: 2e4,
    };
    let report = check(&through);
    assert!(!report.is_valid());
    assert!(
        report
            .edge_node_intersections
            .iter()
            .any(|i| i.node == "ev_1"),
        "{report:?}"
    );
    // Its own ends are not counted.
    assert!(
        !report
            .edge_node_intersections
            .iter()
            .any(|i| i.node == "cl_top" || i.node == "ar_1"),
        "{report:?}"
    );

    // A line of text pushed out of its node.
    let mut escaping = valid.clone();
    escaping.nodes[0].text[0].x += 400.0;
    let report = check(&escaping);
    assert_eq!(report.text_escapes.len(), 1, "{report:?}");
    assert_eq!(report.text_escapes[0].node, escaping.nodes[0].id);

    // Two lines across each other: counted, not a defect.
    let mut crossing = valid.clone();
    let (a, b) = (Point { x: 0.0, y: 0.0 }, Point { x: 100.0, y: 100.0 });
    let (c, d) = (Point { x: 0.0, y: 100.0 }, Point { x: 100.0, y: 0.0 });
    let whole = af_layout::Rect {
        x: 0.0,
        y: 0.0,
        width: 100.0,
        height: 100.0,
    };
    for (edge, (from, to)) in crossing.edges.iter_mut().zip([(a, b), (c, d)]) {
        edge.points = vec![from];
        edge.head[0] = to;
        edge.bounds = whole;
    }
    let report = check(&crossing);
    assert_eq!(report.edge_crossings, 1, "{report:?}");
    // Lines that only meet at an end do not cross.
    let mut touching = crossing.clone();
    touching.edges[1].points = vec![b];
    touching.edges[1].head[0] = Point { x: 200.0, y: 0.0 };
    assert_eq!(check(&touching).edge_crossings, 0);
}
