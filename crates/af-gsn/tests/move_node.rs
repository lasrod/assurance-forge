//! Moving a node, against the writing rules W21 to W25 of
//! `docs/standards/sacm-gsn-mapping.md`: the node is drawn under its new
//! parent, joined as it was, everything else reads back as it did, and the
//! file stays valid.

use std::collections::BTreeSet;

use af_gsn::ElementKind;
use af_gsn::commands::{AddError, Refusal, Target, add, move_node, move_targets};
use af_gsn::view::{Code, EdgeKind, View, view};
use sacm::model::Document;
use sacm::xmi::{self, Mode, ReadOptions, WriteOptions};

const SAMPLES: [&str; 6] = [
    "sacm_example_core_argument.sacm.xmi",
    "sample.sacm.xmi",
    "open-autonomy-safety-case.updated.v2.sacm.xmi",
    "oasc-ja.sacm.xmi",
    "safety-case-sanitized-strict.sacm.xmi",
    "challenges.sacm.xmi",
];

fn read(bytes: &[u8]) -> Document {
    match xmi::read(bytes, &ReadOptions::default().with_mode(Mode::Strict)) {
        Ok(output) => output.document().clone(),
        Err(error) => panic!("setup: a strict read refused the file: {error:?}"),
    }
}

fn sample(name: &str) -> Document {
    let path = format!("{}/../../testdata/sacm/{name}", env!("CARGO_MANIFEST_DIR"));
    let bytes = std::fs::read(&path).unwrap_or_else(|error| panic!("setup: {path}: {error}"));
    read(&bytes)
}

/// An AssuranceCasePackage holding the ArgumentPackage `p` with `body`, and
/// a second, `q`, with the Goal `far`.
fn case(body: &str) -> Document {
    let text = format!(
        r#"<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:id="case">
  <name content="Case"/>
  <argumentPackage xmi:id="p"><name content="P"/>
    {body}
  </argumentPackage>
  <argumentPackage xmi:id="q"><name content="Q"/>
    {far}
  </argumentPackage>
</sacm:AssuranceCasePackage>"#,
        far = element("Claim", "far", "")
    );
    read(text.as_bytes())
}

fn element(kind: &str, id: &str, attributes: &str) -> String {
    format!(
        r#"<argumentElement xmi:type="sacm:{kind}" xmi:id="{id}"{attributes}><name content="{id}"/></argumentElement>"#
    )
}

/// `g` is supported through the Strategy `s` by `a` and `b`, and has the
/// context `c`; `sn` is evidence for `a`. `g2` is supported by `d` alone
/// (`id`), by `m` and `n` in one inference (`im`), and through the Strategy
/// `u` by `only`. `todo` is an undeveloped top-level Goal. `shared` supports
/// both `b` and `d`; `k` challenges `b`.
fn argument() -> Document {
    case(
        &[
            element("Claim", "g", ""),
            element("Claim", "g2", ""),
            element("Claim", "todo", r#" assertionDeclaration="needsSupport""#),
            element("Claim", "a", ""),
            element("Claim", "b", ""),
            element("Claim", "d", ""),
            element("Claim", "m", ""),
            element("Claim", "n", ""),
            element("Claim", "only", ""),
            element("Claim", "shared", ""),
            element("Claim", "k", ""),
            element("ArgumentReasoning", "s", ""),
            element("ArgumentReasoning", "u", ""),
            element("ArtifactReference", "sn", ""),
            element("ArtifactReference", "c", ""),
            element(
                "AssertedInference",
                "i1",
                r#" source="a b" target="g" reasoning="s""#,
            ),
            element("AssertedInference", "id", r#" source="d" target="g2""#),
            element("AssertedInference", "im", r#" source="m n" target="g2""#),
            element(
                "AssertedInference",
                "iu",
                r#" source="only" target="g2" reasoning="u""#,
            ),
            element("AssertedInference", "is1", r#" source="shared" target="b""#),
            element("AssertedInference", "is2", r#" source="shared" target="d""#),
            element("AssertedEvidence", "e1", r#" source="sn" target="a""#),
            element("AssertedContext", "x1", r#" source="c" target="g""#),
            element(
                "AssertedInference",
                "ch",
                r#" source="k" target="b" isCounter="true""#,
            ),
        ]
        .concat(),
    )
}

fn ids(document: &Document) -> BTreeSet<String> {
    document
        .elements()
        .map(|element| element.id().as_str().to_owned())
        .collect()
}

/// Every edge as `from>to:kind`, without the relationship that draws it.
fn lines(view: &View) -> BTreeSet<String> {
    view.edges
        .iter()
        .map(|edge| format!("{}>{}:{:?}", edge.from, edge.to, edge.kind))
        .collect()
}

fn has_line(document: &Document, from: &str, to: &str, kind: EdgeKind) -> bool {
    view(document, "en")
        .edges
        .iter()
        .any(|edge| edge.from == from && edge.to == to && edge.kind == kind)
}

fn refusal(document: &Document, id: &str, to: Target<'_>) -> Option<Refusal> {
    let shown = view(document, "en");
    let targets = move_targets(document, &shown, id).unwrap_or_else(|why| panic!("{id}: {why:?}"));
    match to {
        Target::Canvas => targets.canvas,
        Target::Node(to) => {
            targets
                .nodes
                .iter()
                .find(|target| target.node == to)
                .unwrap_or_else(|| panic!("no target {to}"))
                .refusal
        }
    }
}

fn moved(document: &mut Document, id: &str, to: Target<'_>) {
    let shown = view(document, "en");
    assert_eq!(refusal(document, id, to), None, "{id} to {to:?}");
    move_node(document, &shown, id, to).unwrap_or_else(|error| panic!("{id} to {to:?}: {error}"));
    assert!(
        sacm::validation::validate(document).is_empty(),
        "{:?}",
        sacm::validation::validate(document)
    );
    let after = view(document, "en");
    assert!(after.findings.is_empty(), "{:?}", after.findings);
}

/// What a refused move is refused with, the document left as it was.
fn refused(document: &mut Document, id: &str, to: Target<'_>) -> Refusal {
    let shown = view(document, "en");
    let before = ids(document);
    let lines_before = lines(&shown);
    let refusal = match move_node(document, &shown, id, to) {
        Err(AddError::Refused(refusal)) => refusal,
        other => panic!("{id} to {to:?}: {other:?}"),
    };
    assert_eq!(ids(document), before);
    assert_eq!(lines(&view(document, "en")), lines_before);
    refusal
}

fn sources(document: &Document, relationship: &str) -> Vec<String> {
    document
        .get(relationship)
        .map(|element| {
            element
                .sources()
                .iter()
                .map(|source| source.as_str().to_owned())
                .collect()
        })
        .unwrap_or_default()
}

fn placeholders(view: &View) -> Vec<String> {
    view.unplaced
        .iter()
        .filter(|item| item.code == Code::Placeholder)
        .map(|item| item.id.clone())
        .collect()
}

#[test]
fn w22_where_a_goal_may_go() {
    let document = argument();
    let node = Target::Node;
    assert_eq!(
        refusal(&document, "a", node("a")),
        Some(Refusal::UnderItself)
    );
    assert_eq!(
        refusal(&document, "a", node("sn")),
        Some(Refusal::UnderItself)
    );
    assert_eq!(
        refusal(&document, "a", node("s")),
        Some(Refusal::AlreadyThere)
    );
    assert_eq!(refusal(&document, "a", node("c")), Some(Refusal::Leaf));
    assert_eq!(
        refusal(&document, "a", node("far")),
        Some(Refusal::OtherPackage)
    );
    for free in ["g", "g2", "b", "d", "u", "todo", "k"] {
        assert_eq!(refusal(&document, "a", node(free)), None, "{free}");
    }
    assert_eq!(refusal(&document, "a", Target::Canvas), None);
    assert_eq!(
        refusal(&document, "g", Target::Canvas),
        Some(Refusal::AlreadyThere)
    );
    // What challenges a node is under it.
    assert_eq!(
        refusal(&document, "b", node("k")),
        Some(Refusal::UnderItself)
    );
}

#[test]
fn w22_a_solution_and_a_strategy_go_only_under_a_goal() {
    let document = argument();
    for id in ["sn", "s"] {
        assert_eq!(
            refusal(&document, id, Target::Node("u")),
            Some(Refusal::NotUnderStrategy),
            "{id}"
        );
        assert_eq!(refusal(&document, id, Target::Node("g2")), None, "{id}");
        assert_eq!(
            refusal(&document, id, Target::Canvas),
            Some(Refusal::TopLevelGoalOnly),
            "{id}"
        );
    }
    assert_eq!(refusal(&document, "c", Target::Node("u")), None);
}

#[test]
fn w21_a_node_under_several_or_that_challenges_is_not_moved() {
    let mut document = argument();
    let shown = view(&document, "en");
    assert_eq!(
        move_targets(&document, &shown, "shared").err(),
        Some(Refusal::SeveralParents)
    );
    assert_eq!(
        move_targets(&document, &shown, "k").err(),
        Some(Refusal::JoinNotMoved)
    );
    assert_eq!(
        move_targets(&document, &shown, "nothing").err(),
        Some(Refusal::NotANode)
    );
    assert_eq!(
        refused(&mut document, "shared", Target::Node("g")),
        Refusal::SeveralParents
    );
    assert_eq!(
        refused(&mut document, "a", Target::Node("sn")),
        Refusal::UnderItself
    );
}

#[test]
fn w23_a_goal_alone_in_its_inference_keeps_the_inference() {
    let mut document = argument();
    let before = ids(&document);
    moved(&mut document, "d", Target::Node("g"));
    assert_eq!(ids(&document), before);
    assert!(has_line(&document, "g", "d", EdgeKind::SupportedBy));
    assert!(!has_line(&document, "g2", "d", EdgeKind::SupportedBy));
    // What is under it came with it.
    assert!(has_line(&document, "d", "shared", EdgeKind::SupportedBy));
}

#[test]
fn w23_w24_a_goal_goes_from_a_goal_to_a_strategy() {
    let mut document = argument();
    moved(&mut document, "d", Target::Node("s"));
    assert!(!document.contains("id"));
    assert_eq!(sources(&document, "i1"), ["a", "b", "d"]);
    assert!(has_line(&document, "s", "d", EdgeKind::SupportedBy));
}

#[test]
fn w23_w24_a_goal_goes_from_a_strategy_to_a_goal() {
    let mut document = argument();
    let before = ids(&document);
    moved(&mut document, "a", Target::Node("g2"));
    assert_eq!(sources(&document, "i1"), ["b"]);
    assert!(has_line(&document, "g2", "a", EdgeKind::SupportedBy));
    assert!(has_line(&document, "a", "sn", EdgeKind::SupportedBy));
    // One new inference, and nothing gone.
    let after = ids(&document);
    assert!(before.is_subset(&after));
    assert_eq!(after.len(), before.len() + 1);
}

#[test]
fn w20_a_strategy_that_loses_its_last_goal_holds_a_place() {
    let mut document = argument();
    moved(&mut document, "only", Target::Node("g"));
    let after = view(&document, "en");
    assert_eq!(placeholders(&after).len(), 1);
    assert_eq!(sources(&document, "iu"), placeholders(&after));
    assert!(has_line(&document, "g2", "u", EdgeKind::SupportedBy));
    assert!(has_line(&document, "g", "only", EdgeKind::SupportedBy));

    // W3: a Goal moved under it takes the place, which is deleted.
    let held = placeholders(&after)[0].clone();
    moved(&mut document, "only", Target::Node("u"));
    assert!(!document.contains(&held));
    assert_eq!(sources(&document, "iu"), ["only"]);
    assert!(placeholders(&view(&document, "en")).is_empty());
}

#[test]
fn w3_a_goal_moved_under_a_new_strategy_takes_its_place() {
    let mut document = argument();
    let shown = view(&document, "en");
    let strategy = add(
        &mut document,
        &shown,
        Target::Node("g2"),
        ElementKind::Strategy,
    )
    .unwrap_or_else(|error| panic!("{error}"))
    .element;
    assert_eq!(placeholders(&view(&document, "en")).len(), 1);
    moved(&mut document, "d", Target::Node(&strategy));
    assert!(placeholders(&view(&document, "en")).is_empty());
    assert!(has_line(&document, &strategy, "d", EdgeKind::SupportedBy));
    assert!(!document.contains("id"));
}

#[test]
fn w23_a_goal_among_several_in_its_inference_leaves_it() {
    let mut document = argument();
    moved(&mut document, "m", Target::Node("g"));
    assert_eq!(sources(&document, "im"), ["n"]);
    assert!(has_line(&document, "g", "m", EdgeKind::SupportedBy));
    assert!(has_line(&document, "g2", "n", EdgeKind::SupportedBy));
}

#[test]
fn w23_a_strategy_moves_with_its_goals() {
    let mut document = argument();
    let before = ids(&document);
    moved(&mut document, "s", Target::Node("g2"));
    assert_eq!(ids(&document), before);
    assert!(has_line(&document, "g2", "s", EdgeKind::SupportedBy));
    assert!(!has_line(&document, "g", "s", EdgeKind::SupportedBy));
    assert!(has_line(&document, "s", "a", EdgeKind::SupportedBy));
    assert!(has_line(&document, "s", "b", EdgeKind::SupportedBy));
}

#[test]
fn w23_context_and_evidence_keep_their_relationship() {
    let mut document = argument();
    let before = ids(&document);
    moved(&mut document, "c", Target::Node("u"));
    assert!(has_line(&document, "u", "c", EdgeKind::InContextOf));
    moved(&mut document, "sn", Target::Node("g2"));
    assert!(has_line(&document, "g2", "sn", EdgeKind::SupportedBy));
    assert_eq!(ids(&document), before);
    let after = view(&document, "en");
    let kind = |id: &str| {
        after
            .nodes
            .iter()
            .find(|node| node.id == id)
            .map(|n| n.kind)
    };
    assert_eq!(kind("c"), Some(af_gsn::view::NodeKind::Context));
    assert_eq!(kind("sn"), Some(af_gsn::view::NodeKind::Solution));
}

#[test]
fn w11_support_moved_under_an_undeveloped_goal_develops_it() {
    let mut document = argument();
    let shown = view(&document, "en");
    let targets = move_targets(&document, &shown, "d").unwrap_or_else(|why| panic!("{why:?}"));
    let develops = |id: &str| {
        targets
            .nodes
            .iter()
            .find(|target| target.node == id)
            .is_some_and(|target| target.develops)
    };
    assert!(develops("todo"));
    assert!(!develops("g"));
    // Context does not develop a Goal.
    let context = move_targets(&document, &shown, "c").unwrap_or_else(|why| panic!("{why:?}"));
    assert!(context.nodes.iter().all(|target| !target.develops));

    moved(&mut document, "d", Target::Node("todo"));
    let after = view(&document, "en");
    let todo = after.nodes.iter().find(|node| node.id == "todo");
    assert_eq!(todo.map(|node| node.undeveloped), Some(false));
}

#[test]
fn a_goal_moves_to_the_top_level_and_back_under_a_goal() {
    let mut document = argument();
    moved(&mut document, "d", Target::Canvas);
    assert!(!document.contains("id"));
    let after = view(&document, "en");
    assert!(!after.edges.iter().any(|edge| edge.to == "d"));
    assert!(has_line(&document, "d", "shared", EdgeKind::SupportedBy));

    moved(&mut document, "d", Target::Node("g"));
    assert!(has_line(&document, "g", "d", EdgeKind::SupportedBy));

    // Out of a Strategy.
    moved(&mut document, "a", Target::Canvas);
    assert_eq!(sources(&document, "i1"), ["b"]);
    assert!(has_line(&document, "a", "sn", EdgeKind::SupportedBy));
}

#[test]
fn w25_a_line_something_refers_to_is_not_replaced() {
    // `kr` challenges the line from `g2` to `d`.
    let mut document = case(
        &[
            element("Claim", "g", ""),
            element("Claim", "g2", ""),
            element("Claim", "d", ""),
            element("Claim", "kr", ""),
            element("AssertedInference", "id", r#" source="d" target="g2""#),
            element(
                "AssertedInference",
                "ch",
                r#" source="kr" target="id" isCounter="true""#,
            ),
        ]
        .concat(),
    );
    assert_eq!(refusal(&document, "d", Target::Node("g")), None);
    assert_eq!(
        refused(&mut document, "d", Target::Node("g")),
        Refusal::Entangled
    );
    assert_eq!(
        refused(&mut document, "d", Target::Canvas),
        Refusal::Entangled
    );
}

#[test]
fn w21_a_node_joined_as_gsn_does_not_allow_keeps_its_line() {
    // The Assumption `a` supports `g`, and `b` and `c` support `g2` in one
    // inference: GSN reaches an Assumption only as context.
    let mut document = case(
        &[
            element("Claim", "g", ""),
            element("Claim", "g2", ""),
            element("Claim", "todo", r#" assertionDeclaration="needsSupport""#),
            element("Claim", "sub", ""),
            element("Claim", "a", r#" assertionDeclaration="assumed""#),
            element("Claim", "b", r#" assertionDeclaration="assumed""#),
            element("Claim", "c", r#" assertionDeclaration="assumed""#),
            element("ArgumentReasoning", "s", ""),
            element("AssertedInference", "ia", r#" source="a" target="g""#),
            element("AssertedInference", "ibc", r#" source="b c" target="g2""#),
            element(
                "AssertedInference",
                "is",
                r#" source="sub" target="g" reasoning="s""#,
            ),
        ]
        .concat(),
    );
    assert!(has_line(&document, "g", "a", EdgeKind::SupportedBy));
    assert_eq!(
        refusal(&document, "a", Target::Node("s")),
        Some(Refusal::NotUnderStrategy)
    );
    assert_eq!(
        refusal(&document, "a", Target::Canvas),
        Some(Refusal::TopLevelGoalOnly)
    );
    // The line is the file's, not support the editor made: it develops nothing.
    let shown = view(&document, "en");
    let targets = move_targets(&document, &shown, "a").unwrap_or_else(|why| panic!("{why:?}"));
    assert!(targets.nodes.iter().all(|target| !target.develops));
    // One of several in a relationship would need a new line of that kind.
    assert_eq!(
        move_targets(&document, &shown, "b").err(),
        Some(Refusal::JoinNotMoved)
    );

    let before = ids(&document);
    let findings = shown.findings.len();
    let shown = view(&document, "en");
    move_node(&mut document, &shown, "a", Target::Node("todo"))
        .unwrap_or_else(|error| panic!("{error}"));
    // The same relationship, as wrong as it was, with a new end.
    assert_eq!(ids(&document), before);
    assert!(has_line(&document, "todo", "a", EdgeKind::SupportedBy));
    assert!(!has_line(&document, "g", "a", EdgeKind::SupportedBy));
    let after = view(&document, "en");
    assert_eq!(after.findings.len(), findings);
    let node = |id: &str| after.nodes.iter().find(|node| node.id == id);
    assert_eq!(
        node("a").map(|node| node.kind),
        Some(af_gsn::view::NodeKind::Assumption)
    );
    assert_eq!(node("todo").map(|node| node.undeveloped), Some(true));
}

/// In every sample, each node that can be moved goes to the first place it
/// may, and the file stays valid, reads back with the node there and no new
/// finding, and writes strictly.
#[test]
fn a_move_in_each_sample_keeps_the_file_valid() {
    for name in SAMPLES {
        let original = sample(name);
        let shown = view(&original, "en");
        let problems = sacm::validation::validate(&original).len();
        let mut made = 0;
        for node in shown.nodes.iter().take(60) {
            let Ok(targets) = move_targets(&original, &shown, &node.id) else {
                continue;
            };
            let Some(target) = targets.nodes.iter().find(|target| target.refusal.is_none()) else {
                continue;
            };
            let mut document = original.clone();
            match move_node(&mut document, &shown, &node.id, Target::Node(&target.node)) {
                Ok(_) => {}
                // W25 is known only when the move is made.
                Err(AddError::Refused(Refusal::Entangled)) => continue,
                Err(error) => panic!("{name}: {} to {}: {error}", node.id, target.node),
            }
            made += 1;
            let after = view(&document, "en");
            assert!(
                after
                    .edges
                    .iter()
                    .any(|edge| edge.from == target.node && edge.to == node.id),
                "{name}: {} to {}",
                node.id,
                target.node
            );
            assert_eq!(after.nodes.len(), shown.nodes.len(), "{name}: {}", node.id);
            assert_eq!(
                after.findings.len(),
                shown.findings.len(),
                "{name}: {}",
                node.id
            );
            assert_eq!(
                sacm::validation::validate(&document).len(),
                problems,
                "{name}: {}",
                node.id
            );
            let written = xmi::write(&document, &WriteOptions::default().with_mode(Mode::Strict));
            assert!(written.is_ok(), "{name}: {}: {:?}", node.id, written.err());
        }
        assert!(made > 0, "{name}: no node was moved");
    }
}

#[test]
fn w25_a_move_does_not_change_or_lose_what_a_relationship_says() {
    // `k` challenges the inference `m` and `n` share; `id` is declared
    // assumed; `iu` (the Strategy `u`'s) is challenged by `k2`.
    let mut document = case(
        &[
            element("Claim", "g", ""),
            element("Claim", "g2", ""),
            element("Claim", "m", ""),
            element("Claim", "n", ""),
            element("Claim", "d", ""),
            element("Claim", "only", ""),
            element("Claim", "k", ""),
            element("Claim", "k2", ""),
            element("ArgumentReasoning", "u", ""),
            element("AssertedInference", "im", r#" source="m n" target="g2""#),
            element(
                "AssertedInference",
                "id",
                r#" source="d" target="g2" assertionDeclaration="assumed""#,
            ),
            element(
                "AssertedInference",
                "iu",
                r#" source="only" target="g" reasoning="u""#,
            ),
            element(
                "AssertedInference",
                "ch",
                r#" source="k" target="im" isCounter="true""#,
            ),
            element(
                "AssertedInference",
                "ch2",
                r#" source="k2" target="iu" isCounter="true""#,
            ),
        ]
        .concat(),
    );
    // One of several leaves a challenged relationship.
    assert_eq!(
        refused(&mut document, "m", Target::Node("g")),
        Refusal::Entangled
    );
    // A Goal is not added to, or taken from, a challenged Strategy's inference.
    assert_eq!(
        refused(&mut document, "d", Target::Node("u")),
        Refusal::Entangled
    );
    assert_eq!(
        refused(&mut document, "only", Target::Node("g2")),
        Refusal::Entangled
    );
    // A declared relationship is kept when it is given a new end, and not
    // deleted where it would have to be: under a Strategy, or at the top.
    assert_eq!(
        refused(&mut document, "d", Target::Canvas),
        Refusal::Entangled
    );
    let shown = view(&document, "en");
    move_node(&mut document, &shown, "d", Target::Node("g"))
        .unwrap_or_else(|error| panic!("{error}"));
    assert_eq!(
        document
            .get("id")
            .and_then(|held| held.assertion_declaration()),
        Some(sacm::model::AssertionDeclaration::Assumed)
    );
}

#[test]
fn w21_a_strategy_that_supports_no_goal_has_no_line_to_move() {
    let document = case(
        &[
            element("Claim", "g", ""),
            element("ArgumentReasoning", "s", ""),
        ]
        .concat(),
    );
    let shown = view(&document, "en");
    assert_eq!(
        move_targets(&document, &shown, "s").err(),
        Some(Refusal::StrategyAlone)
    );
    assert_eq!(Refusal::StrategyAlone.code(), "AF-EDIT-024");
}
