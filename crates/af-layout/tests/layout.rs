//! `af-layout` against goal G3 of the diagram plan: text measured and wrapped
//! from font bytes, deterministic output, no overlapping nodes, edges that end
//! on their nodes' outlines, children below their parent and context beside
//! it.

use af_gsn::view::{EdgeKind, NodeKind, View, view};
use af_layout::{
    Arrowhead, Decorator, Diagram, DiagramNode, Engine, Fonts, LineStyle, Placement, Point, Style,
    Weight,
};
use sacm::xmi::{self, Mode, ReadOptions};

const SAMPLES: [&str; 5] = [
    "sacm_example_core_argument.sacm.xmi",
    "sample.sacm.xmi",
    "open-autonomy-safety-case.updated.v2.sacm.xmi",
    "oasc-ja.sacm.xmi",
    "safety-case-sanitized-strict.sacm.xmi",
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

// ===== Text =====

#[test]
fn latin_text_is_kerned() {
    let engine = engine();
    let pair = engine.measure("AV", 14.0, Weight::Regular);
    let apart =
        engine.measure("A", 14.0, Weight::Regular) + engine.measure("V", 14.0, Weight::Regular);
    assert!(pair < apart - 0.1, "AV {pair} against A + V {apart}");
}

#[test]
fn kana_is_not_kerned() {
    // The Japanese face kerns ー then ノ by -60 units: 0.84 px at 14 px.
    let engine = engine();
    let pair = engine.measure("ーノ", 14.0, Weight::Regular);
    let apart =
        engine.measure("ー", 14.0, Weight::Regular) + engine.measure("ノ", 14.0, Weight::Regular);
    assert!(
        (pair - apart).abs() < 0.001,
        "ーノ {pair} against ー + ノ {apart}"
    );
}

#[test]
fn a_character_the_latin_face_lacks_comes_from_the_japanese_face() {
    // A kanji is one em wide in the Japanese face.
    let engine = engine();
    let width = engine.measure("自動", 14.0, Weight::Regular);
    assert!((width - 28.0).abs() < 0.01, "自動 is {width}");
}

#[test]
fn wrapped_lines_fit_and_keep_every_word() {
    let engine = engine();
    let text = "The braking system architecture mitigates identified hazardous failures in every operating mode.";
    let lines = engine.wrap(text, 14.0, Weight::Regular, 160.0);
    assert!(lines.len() > 2, "{lines:?}");
    for line in &lines {
        assert!(line.width <= 160.0, "{line:?}");
        assert_eq!(line.text, line.text.trim(), "{line:?}");
        let measured = engine.measure(&line.text, 14.0, Weight::Regular);
        assert!((measured - line.width).abs() < 0.001, "{line:?}");
    }
    let joined: Vec<&str> = lines.iter().map(|line| line.text.as_str()).collect();
    assert_eq!(joined.join(" "), text);
}

#[test]
fn a_word_wider_than_the_line_is_broken_between_characters() {
    let engine = engine();
    let word = "Reasoning_ArchitectureAndVerification";
    let lines = engine.wrap(word, 14.0, Weight::SemiBold, 120.0);
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
        Weight::Regular,
        100.0,
    );
    assert!(lines.len() > 1, "{lines:?}");
    assert!(lines.iter().all(|line| line.width <= 100.0), "{lines:?}");
    let lines = engine.wrap("first\nsecond", 14.0, Weight::Regular, 500.0);
    let texts: Vec<&str> = lines.iter().map(|line| line.text.as_str()).collect();
    assert_eq!(texts, ["first", "second"]);
    assert!(engine.wrap("", 14.0, Weight::Regular, 100.0).is_empty());
}

#[test]
fn a_file_that_is_not_a_font_is_refused() {
    let refused = Fonts::new(
        b"not a font".to_vec(),
        testdata("fonts/IBMPlexSans-SemiBold.ttf"),
        testdata("fonts/AFTestSansJP-Regular.ttf"),
        testdata("fonts/AFTestSansJP-SemiBold.ttf"),
    );
    assert!(matches!(refused, Err(error) if error.face == af_layout::FaceName::SansRegular));
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
            EdgeKind::Neutral => Arrowhead::Open,
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
            let to = node(&diagram, &edge.to);
            let start = edge.points[0];
            let tip = edge.head[0];
            assert!(
                from.shape.outline_distance(start) < 0.05,
                "{name}: {} starts off {}",
                edge.relationship,
                from.id
            );
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
        for edge in &diagram.edges {
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
                    let (a, b) = (parent.shape.rect.centre().y, child.shape.rect.centre().y);
                    assert!(
                        (a - b).abs() < 0.01,
                        "{name}: {} not level with {}",
                        child.id,
                        parent.id
                    );
                    assert!(
                        child.bounds.x >= parent.bounds.right(),
                        "{name}: {} not beside {}",
                        child.id,
                        parent.id
                    );
                    if edge.kind == EdgeKind::InContextOf {
                        placed += 1;
                    }
                }
                Placement::None => {}
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
    let lines = engine.wrap("Statement text\n    ", 14.0, Weight::Regular, 500.0);
    let texts: Vec<&str> = lines.iter().map(|line| line.text.as_str()).collect();
    assert_eq!(texts, ["Statement text"]);
    let lines = engine.wrap("a\n\nb", 14.0, Weight::Regular, 500.0);
    let texts: Vec<&str> = lines.iter().map(|line| line.text.as_str()).collect();
    assert_eq!(texts, ["a", "", "b"]);
}

#[test]
fn a_wrapped_line_is_as_full_as_it_can_be() {
    // Greedy wrapping: no line could take the next line's first word.
    let engine = engine();
    let text = "Verification activities demonstrate that the braking requirements are satisfied in every mode we know of today.";
    let lines = engine.wrap(text, 14.0, Weight::Regular, 150.0);
    for pair in lines.windows(2) {
        let next_word = pair[1].text.split(' ').next().unwrap_or_default();
        let joined = format!("{} {next_word}", pair[0].text);
        assert!(
            engine.measure(&joined, 14.0, Weight::Regular) > 150.0,
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
