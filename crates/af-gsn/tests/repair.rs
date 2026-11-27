//! Setting the undeveloped mark and repairing a join GSN does not allow,
//! against the writing rules W26 and W27 of
//! `docs/standards/sacm-gsn-mapping.md`.

use std::collections::BTreeSet;

use af_gsn::commands::{
    AddError, Refusal, TextSet, repair_join, repairable, set_undeveloped, undeveloped_change,
};
use af_gsn::view::{EdgeKind, NodeKind, view};
use af_gsn::wellformed::{self, Rule};
use sacm::model::Document;
use sacm::xmi::{self, Mode, ReadOptions, WriteOptions};

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

fn case(body: &str) -> Document {
    let text = format!(
        r#"<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:id="case">
  <name content="Case"/>
  <argumentPackage xmi:id="p"><name content="P"/>
    {body}
  </argumentPackage>
</sacm:AssuranceCasePackage>"#
    );
    read(text.as_bytes())
}

fn element(kind: &str, id: &str, attributes: &str) -> String {
    format!(
        r#"<argumentElement xmi:type="sacm:{kind}" xmi:id="{id}"{attributes}><name content="{id}"/></argumentElement>"#
    )
}

/// `g` is supported by `sub` through the Strategy `s`; `leaf` and `todo`
/// (undeveloped) are Goals with no support. The Assumption `a` and the
/// Justification `j` are each joined to `g` by a support line of their own;
/// the Assumptions `b` and `c` share one to the Goal `other`; `k` challenges the line
/// that joins the Assumption `d` to `sub`.
fn argument() -> Document {
    case(
        &[
            element("Claim", "g", ""),
            element("Claim", "sub", ""),
            element("Claim", "leaf", ""),
            element("Claim", "other", ""),
            element("Claim", "todo", r#" assertionDeclaration="needsSupport""#),
            element("Claim", "a", r#" assertionDeclaration="assumed""#),
            element("Claim", "j", r#" assertionDeclaration="axiomatic""#),
            element("Claim", "b", r#" assertionDeclaration="assumed""#),
            element("Claim", "c", r#" assertionDeclaration="assumed""#),
            element("Claim", "d", r#" assertionDeclaration="assumed""#),
            element("Claim", "k", ""),
            element("ArgumentReasoning", "s", ""),
            element(
                "AssertedInference",
                "is",
                r#" source="sub" target="g" reasoning="s""#,
            ),
            element("AssertedInference", "ia", r#" source="a" target="g""#),
            element("AssertedInference", "ij", r#" source="j" target="g""#),
            element(
                "AssertedInference",
                "ibc",
                r#" source="b c" target="other""#,
            ),
            element("AssertedInference", "id", r#" source="d" target="sub""#),
            element(
                "AssertedInference",
                "ch",
                r#" source="k" target="id" isCounter="true""#,
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

fn line(document: &Document, from: &str, to: &str) -> Option<EdgeKind> {
    view(document, "en")
        .edges
        .iter()
        .find(|edge| edge.from == from && edge.to == to)
        .map(|edge| edge.kind)
}

fn undeveloped(document: &Document, id: &str) -> Option<bool> {
    view(document, "en")
        .nodes
        .iter()
        .find(|node| node.id == id)
        .map(|node| node.undeveloped)
}

fn mark(document: &mut Document, id: &str, on: bool) -> Result<TextSet, AddError> {
    let shown = view(document, "en");
    set_undeveloped(document, &shown, id, on)
}

fn refusal(result: Result<TextSet, AddError>) -> Option<Refusal> {
    match result {
        Err(AddError::Refused(refusal)) => Some(refusal),
        _ => None,
    }
}

fn joins_wrongly(document: &Document) -> usize {
    wellformed::check(document, &view(document, "en"))
        .iter()
        .filter(|finding| finding.rule == Rule::ContextualSupports)
        .count()
}

#[test]
fn w26_a_goal_with_no_support_is_marked_undeveloped_and_cleared() {
    let mut document = argument();
    assert_eq!(undeveloped(&document, "leaf"), Some(false));
    assert!(matches!(
        mark(&mut document, "leaf", true),
        Ok(TextSet::Changed(_))
    ));
    assert_eq!(undeveloped(&document, "leaf"), Some(true));
    // Set to what it is: nothing changes.
    assert!(matches!(
        mark(&mut document, "leaf", true),
        Ok(TextSet::Unchanged)
    ));
    assert!(matches!(
        mark(&mut document, "leaf", false),
        Ok(TextSet::Changed(_))
    ));
    assert_eq!(undeveloped(&document, "leaf"), Some(false));
    assert!(sacm::validation::validate(&document).is_empty());
    // It reads back from the bytes as it was set.
    assert!(matches!(
        mark(&mut document, "leaf", true),
        Ok(TextSet::Changed(_))
    ));
    let written = xmi::write(&document, &WriteOptions::default().with_mode(Mode::Strict))
        .unwrap_or_else(|error| panic!("{error:?}"));
    assert_eq!(undeveloped(&read(written.as_bytes()), "leaf"), Some(true));
}

#[test]
fn w26_only_a_goal_with_no_support_is_marked() {
    let mut document = argument();
    let before = ids(&document);
    // A supported Goal is not marked: an undeveloped element has no support.
    assert_eq!(
        refusal(mark(&mut document, "g", true)),
        Some(Refusal::GoalSupported)
    );
    // The declaration of an Assumption or a Justification says what it is.
    for id in ["a", "j", "s"] {
        assert_eq!(
            refusal(mark(&mut document, id, true)),
            Some(Refusal::NotAGoal),
            "{id}"
        );
        assert_eq!(
            refusal(mark(&mut document, id, false)),
            Some(Refusal::NotAGoal),
            "{id}"
        );
    }
    assert_eq!(
        refusal(mark(&mut document, "nothing", true)),
        Some(Refusal::NotANode)
    );
    assert_eq!(ids(&document), before);
    assert_eq!(undeveloped(&document, "a"), Some(false));
    let shown = view(&document, "en");
    assert_eq!(
        undeveloped_change(&document, &shown, "todo", true),
        Ok(false)
    );
    assert_eq!(
        undeveloped_change(&document, &shown, "todo", false),
        Ok(true)
    );
    assert_eq!(
        undeveloped_change(&document, &shown, "leaf", true),
        Ok(true)
    );
}

#[test]
fn w26_a_stale_mark_on_a_supported_goal_is_cleared() {
    // The bundled example marks a Goal undeveloped that has evidence.
    let mut document = sample("sacm_example_core_argument.sacm.xmi");
    assert_eq!(undeveloped(&document, "cl_sub_2"), Some(true));
    assert!(matches!(
        mark(&mut document, "cl_sub_2", false),
        Ok(TextSet::Changed(_))
    ));
    assert_eq!(undeveloped(&document, "cl_sub_2"), Some(false));
    // And is then not set again while it is supported.
    assert_eq!(
        refusal(mark(&mut document, "cl_sub_2", true)),
        Some(Refusal::GoalSupported)
    );
}

#[test]
fn w27_a_support_line_to_an_assumption_becomes_context() {
    let mut document = argument();
    let wrong = joins_wrongly(&document);
    assert_eq!(line(&document, "g", "a"), Some(EdgeKind::SupportedBy));
    let shown = view(&document, "en");
    assert_eq!(repairable(&document, &shown, "a", "ia"), Ok(()));
    let repaired =
        repair_join(&mut document, &shown, "a", "ia").unwrap_or_else(|error| panic!("{error}"));
    assert!(!document.contains("ia"));
    assert_eq!(line(&document, "g", "a"), Some(EdgeKind::InContextOf));
    // The new relationship keeps the name of the one it replaces.
    let made = document
        .get(&repaired.relationship)
        .unwrap_or_else(|| panic!("the new relationship"));
    assert_eq!(made.kind(), sacm::model::ElementKind::AssertedContext);
    assert_eq!(made.name().and_then(|name| name.content()), Some("ia"));
    // The node is what it was, and one problem fewer is reported.
    let after = view(&document, "en");
    let node = after.nodes.iter().find(|node| node.id == "a");
    assert_eq!(node.map(|node| node.kind), Some(NodeKind::Assumption));
    assert_eq!(joins_wrongly(&document), wrong - 1);
    assert!(sacm::validation::validate(&document).is_empty());
    assert!(xmi::write(&document, &WriteOptions::default().with_mode(Mode::Strict)).is_ok());

    // A Justification the same way.
    let shown = view(&document, "en");
    repair_join(&mut document, &shown, "j", "ij").unwrap_or_else(|error| panic!("{error}"));
    assert_eq!(line(&document, "g", "j"), Some(EdgeKind::InContextOf));
}

#[test]
fn w27_one_of_several_in_a_relationship_leaves_it() {
    let mut document = argument();
    let shown = view(&document, "en");
    repair_join(&mut document, &shown, "b", "ibc").unwrap_or_else(|error| panic!("{error}"));
    assert_eq!(line(&document, "other", "b"), Some(EdgeKind::InContextOf));
    // The other stays as it was, in the relationship that stays.
    assert_eq!(line(&document, "other", "c"), Some(EdgeKind::SupportedBy));
    let sources: Vec<&str> = document
        .get("ibc")
        .map(|held| held.sources().iter().map(|id| id.as_str()).collect())
        .unwrap_or_default();
    assert_eq!(sources, ["c"]);
    assert!(sacm::validation::validate(&document).is_empty());
}

#[test]
fn w27_what_is_not_such_a_join_or_is_referred_to_is_not_repaired() {
    let mut document = argument();
    let before = ids(&document);
    let shown = view(&document, "en");
    // A Goal under a Strategy is joined as GSN allows.
    assert_eq!(
        repairable(&document, &shown, "sub", "is"),
        Err(Refusal::NothingToRepair)
    );
    // Not the line that joins it.
    assert_eq!(
        repairable(&document, &shown, "a", "ij"),
        Err(Refusal::NothingToRepair)
    );
    // `k` challenges the line: it is not replaced (W25).
    assert_eq!(
        repairable(&document, &shown, "d", "id"),
        Err(Refusal::Entangled)
    );
    assert!(matches!(
        repair_join(&mut document, &shown, "d", "id"),
        Err(AddError::Refused(Refusal::Entangled))
    ));
    assert_eq!(ids(&document), before);
    assert_eq!(line(&document, "sub", "d"), Some(EdgeKind::SupportedBy));
}

#[test]
fn w27_the_bundled_examples_assumption_is_repaired() {
    let mut document = sample("sacm_example_core_argument.sacm.xmi");
    let problems = sacm::validation::validate(&document).len();
    assert_eq!(joins_wrongly(&document), 1);
    let shown = view(&document, "en");
    repair_join(&mut document, &shown, "cl_assumption_1", "inf_2")
        .unwrap_or_else(|error| panic!("{error}"));
    assert_eq!(joins_wrongly(&document), 0);
    assert_eq!(
        line(&document, "cl_sub_1", "cl_assumption_1"),
        Some(EdgeKind::InContextOf)
    );
    assert_eq!(sacm::validation::validate(&document).len(), problems);
    assert!(xmi::write(&document, &WriteOptions::default().with_mode(Mode::Strict)).is_ok());
}

#[test]
fn w28_a_relationship_that_says_more_than_support_is_not_replaced() {
    // `ia` is declared assumed, `ix` is abstract, `im` has a meta-claim
    // `mc`: each says more than that the Assumption supports the Goal.
    let mut document = case(
        &[
            element("Claim", "g", ""),
            element("Claim", "mc", ""),
            element("Claim", "a", r#" assertionDeclaration="assumed""#),
            element("Claim", "x", r#" assertionDeclaration="assumed""#),
            element("Claim", "m", r#" assertionDeclaration="assumed""#),
            element(
                "AssertedInference",
                "ia",
                r#" source="a" target="g" assertionDeclaration="assumed""#,
            ),
            element(
                "AssertedInference",
                "ix",
                r#" source="x" target="g" isAbstract="true""#,
            ),
            element(
                "AssertedInference",
                "im",
                r#" source="m" target="g" metaClaim="mc""#,
            ),
        ]
        .concat(),
    );
    let before = ids(&document);
    let shown = view(&document, "en");
    for (node, relationship) in [("a", "ia"), ("x", "ix"), ("m", "im")] {
        // Each is drawn as a support line, so the problem list reports it.
        assert!(
            shown
                .edges
                .iter()
                .any(|edge| edge.relationship == relationship && edge.to == node),
            "{relationship} is not drawn"
        );
        assert_eq!(
            repairable(&document, &shown, node, relationship),
            Err(Refusal::Entangled),
            "{relationship}"
        );
        assert!(matches!(
            repair_join(&mut document, &shown, node, relationship),
            Err(AddError::Refused(Refusal::Entangled))
        ));
    }
    assert_eq!(ids(&document), before);
}

#[test]
fn w28_one_of_several_is_not_taken_out_of_a_relationship_something_refers_to() {
    // `k` challenges the inference that `b` and `c` share.
    let mut document = case(
        &[
            element("Claim", "other", ""),
            element("Claim", "k", ""),
            element("Claim", "b", r#" assertionDeclaration="assumed""#),
            element("Claim", "c", r#" assertionDeclaration="assumed""#),
            element(
                "AssertedInference",
                "ibc",
                r#" source="b c" target="other""#,
            ),
            element(
                "AssertedInference",
                "ch",
                r#" source="k" target="ibc" isCounter="true""#,
            ),
        ]
        .concat(),
    );
    let before = ids(&document);
    let shown = view(&document, "en");
    assert_eq!(
        repairable(&document, &shown, "b", "ibc"),
        Err(Refusal::Entangled)
    );
    assert!(matches!(
        repair_join(&mut document, &shown, "b", "ibc"),
        Err(AddError::Refused(Refusal::Entangled))
    ));
    assert_eq!(ids(&document), before);
    let sources: Vec<&str> = document
        .get("ibc")
        .map(|held| held.sources().iter().map(|id| id.as_str()).collect())
        .unwrap_or_default();
    assert_eq!(sources, ["b", "c"]);
}

#[test]
fn w27_an_element_already_context_of_the_goal_only_loses_the_support_line() {
    let mut document = case(
        &[
            element("Claim", "g", ""),
            element("Claim", "a", r#" assertionDeclaration="assumed""#),
            element("AssertedInference", "ia", r#" source="a" target="g""#),
            element("AssertedContext", "ca", r#" source="a" target="g""#),
        ]
        .concat(),
    );
    let shown = view(&document, "en");
    let repaired =
        repair_join(&mut document, &shown, "a", "ia").unwrap_or_else(|error| panic!("{error}"));
    assert_eq!(repaired.relationship, "ca");
    assert!(!document.contains("ia"));
    let contexts = document
        .elements()
        .filter(|element| element.kind() == sacm::model::ElementKind::AssertedContext)
        .count();
    assert_eq!(contexts, 1);
    assert_eq!(line(&document, "g", "a"), Some(EdgeKind::InContextOf));
}

#[test]
fn w28_a_context_line_that_is_not_drawn_as_plain_context_is_not_leaned_on() {
    // Cited, the line is drawn neutral; abstract, it is a pattern's. Either
    // way the repair makes a context line of its own.
    for attributes in [r#" isCitation="true""#, r#" isAbstract="true""#] {
        let mut document = case(
            &[
                element("Claim", "g", ""),
                element("Claim", "a", r#" assertionDeclaration="assumed""#),
                element("AssertedInference", "ia", r#" source="a" target="g""#),
                element(
                    "AssertedContext",
                    "ca",
                    &format!(r#" source="a" target="g"{attributes}"#),
                ),
            ]
            .concat(),
        );
        let shown = view(&document, "en");
        let repaired = repair_join(&mut document, &shown, "a", "ia")
            .unwrap_or_else(|error| panic!("{attributes}: {error}"));
        assert_ne!(repaired.relationship, "ca", "{attributes}");
        assert!(!document.contains("ia"), "{attributes}");
        assert!(document.contains("ca"), "{attributes}");
        let after = view(&document, "en");
        assert!(
            after
                .edges
                .iter()
                .any(|edge| edge.relationship == repaired.relationship
                    && edge.kind == EdgeKind::InContextOf
                    && edge.from == "g"
                    && edge.to == "a"),
            "{attributes}: the new context line is drawn"
        );
    }
}
