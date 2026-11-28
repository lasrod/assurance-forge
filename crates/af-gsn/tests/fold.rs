//! Making a Strategy that the file puts at an end of a support relationship
//! the reasoning of that relationship, against W39 of
//! `docs/standards/sacm-gsn-mapping.md`. No sample has the shape: these
//! fixtures are what fix it.

use af_gsn::commands::{AddError, Refusal, Repair, fold_strategy, repairs, strategy_fold};
use af_gsn::view::{Code, EdgeKind, NodeKind, view};
use af_gsn::wellformed::{Rule, check};
use sacm::model::Document;
use sacm::xmi::{self, Mode, ReadOptions, WriteOptions};

fn read(bytes: &[u8]) -> Document {
    match xmi::read(bytes, &ReadOptions::default().with_mode(Mode::Strict)) {
        Ok(output) => output.document().clone(),
        Err(error) => panic!("setup: a strict read refused the file: {error:?}"),
    }
}

fn case(body: &[String]) -> Document {
    let text = format!(
        r#"<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:id="case">
  <name content="Case"/>
  <argumentPackage xmi:id="p"><name content="P"/>
    {}
  </argumentPackage>
</sacm:AssuranceCasePackage>"#,
        body.concat()
    );
    read(text.as_bytes())
}

fn element(kind: &str, id: &str, attributes: &str) -> String {
    format!(
        r#"<argumentElement xmi:type="sacm:{kind}" xmi:id="{id}"{attributes}><name content="{id}"/></argumentElement>"#
    )
}

fn goal(id: &str) -> String {
    element("Claim", id, "")
}

fn inference(id: &str, sources: &str, target: &str) -> String {
    element(
        "AssertedInference",
        id,
        &format!(r#" source="{sources}" target="{target}""#),
    )
}

/// The Strategy s, the Goals g, g2, g3 and h, and `relationships`.
fn with(relationships: &[String]) -> Document {
    let mut body = vec![
        element("ArgumentReasoning", "s", ""),
        goal("g"),
        goal("g2"),
        goal("g3"),
        goal("h"),
    ];
    body.extend_from_slice(relationships);
    case(&body)
}

fn sources(document: &Document, id: &str) -> Vec<String> {
    document
        .get(id)
        .map(|element| {
            element
                .sources()
                .iter()
                .map(|source| source.as_str().to_owned())
                .collect()
        })
        .unwrap_or_default()
}

/// Folds s, which must fold: the relationships that had it at an end are no
/// longer reported, no error is new, and the file stays valid and written.
fn folded(document: &mut Document) {
    let before = view(document, "en");
    strategy_fold(document, &before, "s").unwrap_or_else(|refusal| panic!("{refusal:?}"));
    fold_strategy(document, &before, "s").unwrap_or_else(|error| panic!("{error}"));
    let after = view(document, "en");
    let found = check(document, &after);
    assert!(
        found
            .iter()
            .all(|finding| finding.rule != Rule::StrategyAtEnd),
        "{found:?}"
    );
    assert!(
        found.iter().all(|finding| !finding.rule.is_error()),
        "{found:?}"
    );
    assert!(
        after
            .nodes
            .iter()
            .any(|node| node.id == "s" && node.kind == NodeKind::Strategy)
    );
    assert!(sacm::validation::validate(document).is_empty());
    assert!(xmi::write(document, &WriteOptions::default()).is_ok());
}

/// s is refused with `refusal`, by the question and the command, and the
/// document is as it was.
fn refused(document: &Document, refusal: Refusal) {
    let shown = view(document, "en");
    assert_eq!(strategy_fold(document, &shown, "s").err(), Some(refusal));
    let mut edited = document.clone();
    match fold_strategy(&mut edited, &shown, "s") {
        Err(AddError::Refused(found)) => assert_eq!(found, refusal),
        other => panic!("{other:?}"),
    }
    assert!(edited == *document);
}

#[test]
fn w39_s_supports_g_and_goals_support_s_becomes_g_supported_through_s() {
    let mut document = with(&[inference("up", "s", "g"), inference("down", "g2 g3", "s")]);
    folded(&mut document);
    // The inference above stays, with its id and target.
    assert_eq!(sources(&document, "up"), ["g2", "g3"]);
    assert_eq!(
        document.get("up").and_then(|up| up
            .reasoning()
            .map(|reasoning| reasoning.as_str().to_owned())),
        Some("s".to_owned())
    );
    assert!(!document.contains("down"));
    let edges: Vec<(String, String)> = view(&document, "en")
        .edges
        .iter()
        .filter(|edge| edge.kind == EdgeKind::SupportedBy)
        .map(|edge| (edge.from.clone(), edge.to.clone()))
        .collect();
    assert_eq!(
        edges,
        [
            ("g".to_owned(), "s".to_owned()),
            ("s".to_owned(), "g2".to_owned()),
            ("s".to_owned(), "g3".to_owned()),
        ]
    );
}

#[test]
fn w39_goals_below_in_several_relationships_are_premises_in_document_order_once() {
    let mut document = with(&[
        inference("up", "s", "g"),
        inference("d1", "g3", "s"),
        inference("d2", "g2 g3", "s"),
    ]);
    folded(&mut document);
    assert_eq!(sources(&document, "up"), ["g3", "g2"]);
    assert!(!document.contains("d1") && !document.contains("d2"));
}

#[test]
fn w39_with_no_goal_below_the_strategy_holds_the_place_of_its_first() {
    let mut document = with(&[inference("up", "s", "g")]);
    folded(&mut document);
    let after = view(&document, "en");
    let held = sources(&document, "up");
    assert_eq!(held.len(), 1);
    assert!(
        after
            .unplaced
            .iter()
            .any(|item| item.id == held[0] && item.code == Code::Placeholder)
    );
    // A Strategy with no Goal yet, as an add leaves one (W2).
    let found = check(&document, &after);
    assert!(
        found
            .iter()
            .any(|finding| finding.rule == Rule::StrategyUnsupported)
    );
}

#[test]
fn w39_context_of_the_strategy_and_a_challenge_of_it_stay() {
    let mut document = with(&[
        inference("up", "s", "g"),
        inference("down", "g2", "s"),
        element("Claim", "a", r#" assertionDeclaration="assumed""#),
        element("AssertedContext", "x", r#" source="a" target="s""#),
        element("Claim", "k", ""),
        element(
            "AssertedInference",
            "ch",
            r#" source="k" target="s" isCounter="true""#,
        ),
    ]);
    folded(&mut document);
    assert!(document.contains("x") && document.contains("ch"));
}

#[test]
fn w39_a_strategy_not_at_one_goal_is_refused() {
    // Above two Goals.
    refused(
        &with(&[inference("u1", "s", "g"), inference("u2", "s", "h")]),
        Refusal::StrategyNotAtOneGoal,
    );
    // Above one, together with another Goal.
    refused(
        &with(&[inference("up", "s h", "g")]),
        Refusal::StrategyNotAtOneGoal,
    );
    // Above nothing: only Goals below.
    refused(
        &with(&[inference("down", "g2", "s")]),
        Refusal::StrategyNotAtOneGoal,
    );
}

#[test]
fn w39_something_below_that_is_not_a_goal_or_is_the_goal_above_is_refused() {
    let mut evidence = vec![
        inference("up", "s", "g"),
        element("ArtifactReference", "sn", ""),
        element("AssertedEvidence", "e", r#" source="sn" target="s""#),
    ];
    refused(&with(&evidence), Refusal::NotOnlyGoalsBelow);
    // A circle: g supports s, which supports g.
    evidence.truncate(1);
    evidence.push(inference("down", "g2 g", "s"));
    refused(&with(&evidence), Refusal::NotOnlyGoalsBelow);
}

#[test]
fn w39_a_strategy_already_the_reasoning_of_an_inference_is_refused() {
    refused(
        &with(&[
            inference("up", "s", "g"),
            element(
                "AssertedInference",
                "other",
                r#" source="g3" target="h" reasoning="s""#,
            ),
        ]),
        Refusal::AlreadyReasoning,
    );
}

#[test]
fn w39_w25_what_refers_to_the_relationships_or_is_held_by_them_refuses() {
    let challenged = with(&[
        inference("up", "s", "g"),
        inference("down", "g2", "s"),
        element("Claim", "k", ""),
        element(
            "AssertedInference",
            "ch",
            r#" source="k" target="up" isCounter="true""#,
        ),
    ]);
    refused(&challenged, Refusal::Entangled);
    let assumed = with(&[
        element(
            "AssertedInference",
            "up",
            r#" source="s" target="g" assertionDeclaration="assumed""#,
        ),
        inference("down", "g2", "s"),
    ]);
    refused(&assumed, Refusal::Entangled);
    let described = with(&[
        inference("up", "s", "g"),
        r#"<argumentElement xmi:type="sacm:AssertedInference" xmi:id="down" source="g2" target="s"><name content="down"/><description><value content="Why g2 supports s."/></description></argumentElement>"#.to_owned(),
    ]);
    refused(&described, Refusal::Entangled);
}

#[test]
fn af_gsn_007_offers_the_fold_on_each_of_its_problems() {
    let document = with(&[inference("up", "s", "g"), inference("down", "g2", "s")]);
    let shown = view(&document, "en");
    let found: Vec<_> = check(&document, &shown)
        .into_iter()
        .filter(|finding| finding.rule == Rule::StrategyAtEnd)
        .collect();
    assert_eq!(found.len(), 2, "{found:?}");
    for finding in &found {
        assert_eq!(
            repairs(&document, &shown, finding),
            [Repair::FoldStrategy {
                strategy: "s".to_owned(),
                goal: Some("g".to_owned()),
                refusal: None,
            }]
        );
    }
    // One refused says why.
    let two = with(&[inference("u1", "s", "g"), inference("u2", "s", "h")]);
    let shown = view(&two, "en");
    let finding = check(&two, &shown)
        .into_iter()
        .find(|finding| finding.rule == Rule::StrategyAtEnd)
        .unwrap_or_else(|| panic!("setup: no AF-GSN-007"));
    assert_eq!(
        repairs(&two, &shown, &finding),
        [Repair::FoldStrategy {
            strategy: "s".to_owned(),
            goal: None,
            refusal: Some(Refusal::StrategyNotAtOneGoal),
        }]
    );
}
