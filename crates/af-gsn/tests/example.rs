//! The example the welcome page opens (`testdata/sacm/example.sacm.xmi`):
//! every GSN element and mark the editor supports, read strictly with no
//! diagnostics, drawn whole, and with nothing for the problem list to say.

use std::collections::HashSet;

use af_gsn::view::{Code, EdgeKind, NodeKind, view};
use af_gsn::wellformed::check;
use sacm::xmi::{self, Mode, ReadOptions};

fn example() -> (sacm::model::Document, usize) {
    let path = format!(
        "{}/../../testdata/sacm/example.sacm.xmi",
        env!("CARGO_MANIFEST_DIR")
    );
    let bytes = std::fs::read(&path).unwrap_or_else(|error| panic!("{path}: {error}"));
    match xmi::read(&bytes, &ReadOptions::default().with_mode(Mode::Strict)) {
        Ok(output) => (output.document().clone(), output.diagnostics().len()),
        Err(error) => panic!("a strict read refused the example: {error:?}"),
    }
}

#[test]
fn the_example_reads_strictly_and_is_valid() {
    let (document, diagnostics) = example();
    assert_eq!(diagnostics, 0);
    assert!(sacm::validation::validate(&document).is_empty());
}

#[test]
fn the_example_has_every_element_and_mark_the_editor_supports() {
    let (document, _) = example();
    let shown = view(&document, "en");
    let kinds: HashSet<NodeKind> = shown.nodes.iter().map(|node| node.kind).collect();
    for kind in [
        NodeKind::Goal,
        NodeKind::Strategy,
        NodeKind::Solution,
        NodeKind::Context,
        NodeKind::Assumption,
        NodeKind::Justification,
    ] {
        assert!(kinds.contains(&kind), "no {kind:?}");
    }
    assert!(
        !kinds.contains(&NodeKind::Neutral),
        "a node of unknown kind"
    );
    // An undeveloped Goal.
    assert!(
        shown
            .nodes
            .iter()
            .any(|node| node.id == "G4" && node.undeveloped)
    );
    // A counter goal challenging a line, and counter evidence challenging a node.
    let challenges: Vec<(&str, &str, bool)> = shown
        .edges
        .iter()
        .filter(|edge| edge.kind == EdgeKind::Challenge)
        .map(|edge| (edge.from.as_str(), edge.to.as_str(), edge.to_relationship))
        .collect();
    assert_eq!(challenges, [("CG1", "E3", true), ("CSn1", "G3", false)]);
    // An ACP with a description, and one with a confidence argument elsewhere.
    let labels: Vec<&str> = shown.acps.iter().map(|acp| acp.label.as_str()).collect();
    assert_eq!(labels, ["ACP1", "ACP2[FmeaConfidence]"]);
    // Context of a Goal and of a Strategy.
    assert!(
        shown
            .edges
            .iter()
            .any(|edge| edge.kind == EdgeKind::InContextOf && edge.from == "S1")
    );
    // Everything is drawn, but the ACP's description, which R17 draws only
    // as the ACP.
    let unplaced: Vec<(&str, Code)> = shown
        .unplaced
        .iter()
        .map(|item| (item.id.as_str(), item.code))
        .collect();
    assert_eq!(unplaced, [("ACP1", Code::AcpDescription)]);
    assert!(shown.findings.is_empty(), "{:?}", shown.findings);
}

#[test]
fn the_example_has_nothing_for_the_problem_list() {
    let (document, _) = example();
    assert_eq!(check(&document, &view(&document, "en")), []);
}

#[test]
fn the_example_reads_in_japanese_too() {
    let (document, _) = example();
    let shown = view(&document, "ja");
    let top = shown.nodes.iter().find(|node| node.id == "G1");
    assert_eq!(
        top.and_then(|node| node.statement.as_deref()),
        Some("制動機能は、運用設計領域において許容できる程度に安全である。")
    );
}
