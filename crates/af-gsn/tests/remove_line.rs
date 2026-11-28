//! Removing one line, against the writing rule W37 of
//! `docs/standards/sacm-gsn-mapping.md`: the line goes, the nodes at its ends
//! stay as the kinds they were, what is refused changes nothing, and the
//! file stays valid.

use std::collections::{BTreeMap, BTreeSet};

use af_gsn::commands::{AddError, Line, Refusal, line_removal, remove_line};
use af_gsn::view::{Code, EdgeKind, NodeKind, View, view};
use sacm::model::Document;
use sacm::xmi::{self, Mode, ReadOptions, WriteOptions};

const SAMPLES: [&str; 7] = [
    "sacm_example_core_argument.sacm.xmi",
    "sample.sacm.xmi",
    "open-autonomy-safety-case.updated.v2.sacm.xmi",
    "oasc-ja.sacm.xmi",
    "safety-case-sanitized-strict.sacm.xmi",
    "challenges.sacm.xmi",
    "acps.sacm.xmi",
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

/// An AssuranceCasePackage holding one ArgumentPackage with `body`.
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

/// A Claim the editor wrote as an ACP's description (R17).
fn description(id: &str) -> String {
    format!(
        r#"<argumentElement xmi:type="sacm:Claim" xmi:id="{id}"><name content="{id}"/><taggedValue xmi:id="tv_{id}"><key><value content="assuranceForge.gsn.acp"/></key><content><value content="description"/></content></taggedValue></argumentElement>"#
    )
}

/// `g` supported through the Strategy `s` by `sub` and `other`; `top`, a
/// second top-level goal, supports `shared` and `lone`, and `sub` supports
/// `shared` too; `top` is supported by `m1` and `m2` in one inference;
/// evidence `sn` for `sub`; context `c` for `g`; the assumption `a` for
/// `top`.
fn argument() -> Document {
    case(
        &[
            element("Claim", "g", ""),
            element("Claim", "top", ""),
            element("Claim", "sub", ""),
            element("Claim", "other", ""),
            element("Claim", "shared", ""),
            element("Claim", "lone", ""),
            element("Claim", "m1", ""),
            element("Claim", "m2", ""),
            element("Claim", "a", r#" assertionDeclaration="assumed""#),
            element("ArgumentReasoning", "s", ""),
            element("ArtifactReference", "sn", ""),
            element("ArtifactReference", "c", ""),
            element(
                "AssertedInference",
                "i1",
                r#" source="sub other" target="g" reasoning="s""#,
            ),
            element(
                "AssertedInference",
                "i2",
                r#" source="shared" target="sub""#,
            ),
            element(
                "AssertedInference",
                "i3",
                r#" source="shared" target="top""#,
            ),
            element("AssertedInference", "i4", r#" source="lone" target="top""#),
            element("AssertedInference", "i5", r#" source="m1 m2" target="top""#),
            element("AssertedEvidence", "e1", r#" source="sn" target="sub""#),
            element("AssertedContext", "x1", r#" source="c" target="g""#),
            element("AssertedContext", "x2", r#" source="a" target="top""#),
        ]
        .concat(),
    )
}

fn line<'a>(relationship: &'a str, from: &'a str, to: &'a str) -> Line<'a> {
    Line {
        relationship,
        from,
        to,
    }
}

/// The nodes the view draws, each with its kind.
fn kinds(view: &View) -> BTreeMap<String, NodeKind> {
    view.nodes
        .iter()
        .map(|node| (node.id.clone(), node.kind))
        .collect()
}

/// Each line the view draws, as its relationship and two ends.
fn lines(view: &View) -> BTreeSet<(String, String, String)> {
    view.edges
        .iter()
        .map(|edge| {
            (
                edge.relationship.clone(),
                edge.from.clone(),
                edge.to.clone(),
            )
        })
        .collect()
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

/// Removes `at`, which must be removable: what was drawn is drawn but the
/// line, every node keeps its kind, and the file stays valid and written.
fn removed(document: &mut Document, at: Line<'_>) {
    let before = view(document, "en");
    let what = format!("{at:?}");
    line_removal(document, &before, at).unwrap_or_else(|refusal| panic!("{what}: {refusal:?}"));
    remove_line(document, &before, at).unwrap_or_else(|error| panic!("{what}: {error}"));
    let after = view(document, "en");
    assert_eq!(kinds(&after), kinds(&before), "{what}");
    let mut left = lines(&before);
    left.remove(&(
        at.relationship.to_owned(),
        at.from.to_owned(),
        at.to.to_owned(),
    ));
    assert_eq!(lines(&after), left, "{what}");
    assert!(sacm::validation::validate(document).is_empty(), "{what}");
    assert!(
        xmi::write(document, &WriteOptions::default()).is_ok(),
        "{what}"
    );
}

/// `at` is refused with `refusal`, by the menu's question and by the
/// command, and the document is as it was.
fn refused(document: &Document, at: Line<'_>, refusal: Refusal) {
    let shown = view(document, "en");
    assert_eq!(line_removal(document, &shown, at), Err(refusal), "{at:?}");
    let mut edited = document.clone();
    match remove_line(&mut edited, &shown, at) {
        Err(AddError::Refused(found)) => assert_eq!(found, refusal, "{at:?}"),
        other => panic!("{at:?}: {other:?}"),
    }
    assert!(edited == *document, "{at:?}: the document changed");
}

#[test]
fn w37_a_goals_only_line_goes_with_its_relationship_and_the_goal_is_drawn_at_the_top() {
    let mut document = argument();
    removed(&mut document, line("i4", "top", "lone"));
    assert!(!document.contains("i4"));
    assert!(document.contains("lone"));
    let after = view(&document, "en");
    assert!(!after.edges.iter().any(|edge| edge.to == "lone"));
}

#[test]
fn w37_a_shared_goal_keeps_its_other_line() {
    let mut document = argument();
    removed(&mut document, line("i2", "sub", "shared"));
    assert!(!document.contains("i2"));
    let after = view(&document, "en");
    assert!(after.edges.iter().any(|edge| edge.relationship == "i3"));
}

#[test]
fn w37_one_of_several_premises_is_taken_out_and_the_relationship_stays() {
    let mut document = argument();
    removed(&mut document, line("i5", "top", "m1"));
    assert_eq!(sources(&document, "i5"), ["m2"]);
    removed(&mut document, line("i1", "s", "other"));
    assert_eq!(sources(&document, "i1"), ["sub"]);
}

#[test]
fn w37_w20_a_strategys_last_goal_leaves_the_place_of_the_next() {
    let mut document = argument();
    removed(&mut document, line("i1", "s", "other"));
    removed(&mut document, line("i1", "s", "sub"));
    // The inference stays with its Strategy, holding a place R16 hides.
    let held = sources(&document, "i1");
    assert_eq!(held.len(), 1);
    let after = view(&document, "en");
    assert!(
        after
            .unplaced
            .iter()
            .any(|item| item.id == held[0] && item.code == Code::Placeholder)
    );
    assert!(
        after
            .edges
            .iter()
            .any(|edge| edge.from == "g" && edge.to == "s")
    );
}

#[test]
fn w37_an_assumptions_line_goes_and_it_stays_an_assumption() {
    let mut document = argument();
    removed(&mut document, line("x2", "top", "a"));
    let after = view(&document, "en");
    let kind = after
        .nodes
        .iter()
        .find(|node| node.id == "a")
        .map(|node| node.kind);
    assert_eq!(kind, Some(NodeKind::Assumption));
}

#[test]
fn w37_f3_the_line_from_a_goal_to_its_strategy_goes_only_with_the_strategy() {
    refused(&argument(), line("i1", "g", "s"), Refusal::StrategyLine);
}

#[test]
fn w37_a_solution_or_a_context_known_only_by_its_line_is_not_left_without_it() {
    // R9, R10: without its line, an ArtifactReference reads as neutral (R11).
    let document = argument();
    refused(&document, line("e1", "sub", "sn"), Refusal::KindFromLine);
    refused(&document, line("x1", "g", "c"), Refusal::KindFromLine);
    // With a second line, it is still a Solution.
    let mut twice = case(
        &[
            element("Claim", "g", ""),
            element("Claim", "h", ""),
            element("ArtifactReference", "sn", ""),
            element("AssertedEvidence", "e1", r#" source="sn" target="g""#),
            element("AssertedEvidence", "e2", r#" source="sn" target="h""#),
        ]
        .concat(),
    );
    removed(&mut twice, line("e1", "g", "sn"));
}

#[test]
fn w37_a_challenged_line_is_not_removed_alone() {
    let mut body = argument_body();
    body.push(element("Claim", "k", ""));
    body.push(element(
        "AssertedInference",
        "ch",
        r#" source="k" target="i4" isCounter="true""#,
    ));
    let document = case(&body.concat());
    refused(
        &document,
        line("i4", "top", "lone"),
        Refusal::LineChallenged,
    );
    // The challenge's own line goes with its challenger (W18).
    let shown = view(&document, "en");
    let challenge = shown
        .edges
        .iter()
        .find(|edge| edge.kind == EdgeKind::Challenge)
        .map(|edge| (edge.from.clone(), edge.to.clone()));
    let Some((from, to)) = challenge else {
        panic!("setup: the challenge is not drawn");
    };
    refused(
        &document,
        line("ch", &from, &to),
        Refusal::NotARemovableLine,
    );
}

#[test]
fn w37_a_reference_to_the_line_the_diagram_does_not_draw_refuses() {
    let mut body = argument_body();
    body.push(
        r#"<argumentElement xmi:type="sacm:ArgumentGroup" xmi:id="grp" argumentElement="i4"><name content="grp"/></argumentElement>"#
            .to_owned(),
    );
    let document = case(&body.concat());
    refused(&document, line("i4", "top", "lone"), Refusal::Entangled);
}

#[test]
fn w37_no_such_line_is_refused() {
    let document = argument();
    refused(
        &document,
        line("i4", "top", "nothing"),
        Refusal::NotARemovableLine,
    );
    refused(
        &document,
        line("nothing", "top", "lone"),
        Refusal::NotARemovableLine,
    );
    refused(
        &document,
        line("i4", "g", "lone"),
        Refusal::NotARemovableLine,
    );
}

#[test]
fn w37_w19_the_acps_of_a_line_go_with_it_and_its_description_too() {
    let document = case(
        &[
            element("Claim", "g", ""),
            element("Claim", "sub", ""),
            element("Claim", "local", ""),
            description("d"),
            element(
                "AssertedInference",
                "i",
                r#" source="sub" target="g" metaClaim="d local""#,
            ),
        ]
        .concat(),
    );
    let shown = view(&document, "en");
    assert_eq!(shown.acps.len(), 2, "setup: both ACPs are drawn");
    let taken = line_removal(&document, &shown, line("i", "g", "sub"))
        .unwrap_or_else(|refusal| panic!("{refusal:?}"));
    assert_eq!(
        taken.acps,
        [
            ("i".to_owned(), "d".to_owned()),
            ("i".to_owned(), "local".to_owned())
        ]
    );
    assert_eq!(taken.descriptions, ["d"]);
    let mut edited = document.clone();
    removed(&mut edited, line("i", "g", "sub"));
    assert!(!edited.contains("d"));
    // A goal of the argument stays, now drawn by its own rules.
    assert!(edited.contains("local"));
}

#[test]
fn w37_a_source_named_twice_is_not_taken_out() {
    // Which of the two is the line is not the editor's to say.
    let document = case(
        &[
            element("Claim", "g", ""),
            element("Claim", "m", ""),
            element("Claim", "n", ""),
            element("AssertedInference", "i", r#" source="m m n" target="g""#),
        ]
        .concat(),
    );
    refused(&document, line("i", "g", "m"), Refusal::NotARemovableLine);
}

#[test]
fn w37_w19_a_meta_claim_the_diagram_does_not_draw_refuses() {
    // E11 draws an ACP only for a Claim: one naming a Solution's
    // ArtifactReference would go with the line unseen.
    let document = case(
        &[
            element("Claim", "g", ""),
            element("Claim", "sub", ""),
            element("ArtifactReference", "sn", ""),
            element("Claim", "h", ""),
            element("AssertedEvidence", "e", r#" source="sn" target="h""#),
            element(
                "AssertedInference",
                "i",
                r#" source="sub" target="g" metaClaim="sn""#,
            ),
        ]
        .concat(),
    );
    assert!(
        view(&document, "en").acps.is_empty(),
        "setup: no ACP is drawn"
    );
    refused(&document, line("i", "g", "sub"), Refusal::Entangled);
}

#[test]
fn w37_w25_a_premise_is_not_taken_out_of_a_relationship_with_an_acp() {
    let document = case(
        &[
            element("Claim", "g", ""),
            element("Claim", "m1", ""),
            element("Claim", "m2", ""),
            description("d"),
            element(
                "AssertedInference",
                "i",
                r#" source="m1 m2" target="g" metaClaim="d""#,
            ),
        ]
        .concat(),
    );
    refused(&document, line("i", "g", "m1"), Refusal::Entangled);
}

/// Every support and context line of every sample: what the menu says is
/// what the command does, a removal keeps every node's kind and the file
/// valid, and a refusal changes nothing.
#[test]
fn every_line_of_every_sample_is_removed_or_refused_as_said() {
    let mut removed_count = 0;
    let mut refused_count = 0;
    for name in SAMPLES {
        let document = sample(name);
        let before = view(&document, "en");
        let diagnostics = sacm::validation::validate(&document).len();
        for edge in before
            .edges
            .iter()
            .filter(|edge| matches!(edge.kind, EdgeKind::SupportedBy | EdgeKind::InContextOf))
        {
            let at = line(&edge.relationship, &edge.from, &edge.to);
            let what = format!("{name} {at:?}");
            let said = line_removal(&document, &before, at);
            let mut edited = document.clone();
            let made = remove_line(&mut edited, &before, at);
            match (said, made) {
                (Ok(_), Ok(_)) => {
                    let after = view(&edited, "en");
                    assert_eq!(kinds(&after), kinds(&before), "{what}");
                    assert!(
                        !lines(&after).contains(&(
                            edge.relationship.clone(),
                            edge.from.clone(),
                            edge.to.clone()
                        )),
                        "{what}: the line is still drawn"
                    );
                    assert!(
                        sacm::validation::validate(&edited).len() <= diagnostics,
                        "{what}: new validation diagnostics"
                    );
                    assert!(
                        xmi::write(&edited, &WriteOptions::default()).is_ok(),
                        "{what}: not written"
                    );
                    removed_count += 1;
                }
                (Err(refusal), Err(AddError::Refused(found))) => {
                    assert_eq!(refusal, found, "{what}");
                    assert!(edited == document, "{what}: the document changed");
                    refused_count += 1;
                }
                (said, made) => panic!("{what}: said {said:?}, made {made:?}"),
            }
        }
    }
    assert!(
        removed_count > 30,
        "only {removed_count} lines were removed"
    );
    assert!(
        refused_count > 10,
        "only {refused_count} lines were refused"
    );
}

/// `top` supporting `lone`, to add to.
fn argument_body() -> Vec<String> {
    vec![
        element("Claim", "top", ""),
        element("Claim", "lone", ""),
        element("AssertedInference", "i4", r#" source="lone" target="top""#),
    ]
}
