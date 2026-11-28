//! The repairs a GSN problem offers (F1 of
//! `docs/roadmap/milestone-2-repair.md`): each rule has its repair, and a
//! repair offered clears the problem it was offered for, adds no error, and
//! leaves the file valid.

use af_gsn::commands::{
    AddError, Line, Refusal, Repair, remove_line, renumber, repair_join, repairs, set_undeveloped,
};
use af_gsn::view::view;
use af_gsn::wellformed::{Finding, Rule, check};
use sacm::model::Document;
use sacm::xmi::{self, Mode, ReadOptions, WriteOptions};

const SAMPLES: [&str; 9] = [
    "sacm_example_core_argument.sacm.xmi",
    "sample.sacm.xmi",
    "open-autonomy-safety-case.updated.v2.sacm.xmi",
    "oasc-ja.sacm.xmi",
    "safety-case-sanitized-strict.sacm.xmi",
    "challenges.sacm.xmi",
    "acps.sacm.xmi",
    "gallery-roots-and-cycle.sacm.xmi",
    "gallery-shared-support.sacm.xmi",
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

/// The one finding of `rule` in `document`.
fn finding(document: &Document, rule: Rule) -> Finding {
    let found: Vec<Finding> = check(document, &view(document, "en"))
        .into_iter()
        .filter(|finding| finding.rule == rule)
        .collect();
    match found.as_slice() {
        [one] => one.clone(),
        other => panic!("setup: {} findings of {rule:?}: {other:?}", other.len()),
    }
}

/// Makes `repair`, which must be offered.
fn make(document: &mut Document, repair: &Repair) {
    let now = view(document, "en");
    let made = match repair {
        Repair::MakeContext {
            element,
            relationship,
        } => repair_join(document, &now, element, relationship).map(|_| ()),
        Repair::RemoveLine {
            relationship,
            from,
            to,
            refusal: None,
        } => remove_line(
            document,
            &now,
            Line {
                relationship,
                from,
                to,
            },
        )
        .map(|_| ()),
        Repair::ClearUndeveloped {
            element,
            refusal: None,
        } => set_undeveloped(document, &now, element, false).map(|_| ()),
        Repair::Renumber {
            nodes,
            refusal: None,
            ..
        } => renumber(document, &now, nodes).map(|_| ()),
        refused => panic!("{refused:?} is not offered"),
    };
    made.unwrap_or_else(|error| panic!("{repair:?}: {error}"));
}

/// Each repair `rule`'s one finding offers clears it, adds no error, and
/// leaves the file valid.
fn clears(document: &Document, rule: Rule) -> Vec<Repair> {
    let reported = finding(document, rule);
    let offered = repairs(document, &view(document, "en"), &reported);
    assert!(!offered.is_empty(), "{rule:?}: no repair");
    for repair in &offered {
        let mut repaired = document.clone();
        make(&mut repaired, repair);
        let after = check(&repaired, &view(&repaired, "en"));
        assert!(
            !after.contains(&reported),
            "{repair:?}: {reported:?} is still reported"
        );
        assert!(
            after.iter().all(|found| !found.rule.is_error()),
            "{repair:?}: {after:?}"
        );
        assert!(
            sacm::validation::validate(&repaired).is_empty(),
            "{repair:?}"
        );
        assert!(
            xmi::write(&repaired, &WriteOptions::default()).is_ok(),
            "{repair:?}"
        );
    }
    offered
}

fn removes(relationship: &str, from: &str, to: &str) -> Repair {
    Repair::RemoveLine {
        relationship: relationship.to_owned(),
        from: from.to_owned(),
        to: to.to_owned(),
        refusal: None,
    }
}

#[test]
fn af_gsn_001_a_cycle_offers_each_of_its_lines() {
    // c supports a from outside the cycle: its line is not offered.
    let document = case(&[
        goal("a"),
        goal("b"),
        goal("c"),
        element("AssertedInference", "i1", r#" source="b" target="a""#),
        element("AssertedInference", "i2", r#" source="a" target="b""#),
        element("AssertedInference", "i3", r#" source="c" target="a""#),
    ]);
    assert_eq!(
        clears(&document, Rule::Cycle),
        [removes("i1", "a", "b"), removes("i2", "b", "a")]
    );
    // A goal that supports itself: its one line.
    let itself = case(&[
        goal("a"),
        element("AssertedInference", "i", r#" source="a" target="a""#),
    ]);
    assert_eq!(clears(&itself, Rule::Cycle), [removes("i", "a", "a")]);
}

#[test]
fn af_gsn_002_a_supported_leaf_loses_the_line() {
    let document = case(&[
        goal("g"),
        element("Claim", "a", r#" assertionDeclaration="assumed""#),
        element("AssertedInference", "i", r#" source="g" target="a""#),
    ]);
    assert_eq!(
        clears(&document, Rule::LeafSupported),
        [removes("i", "a", "g")]
    );
}

#[test]
fn af_gsn_003_context_of_a_leaf_is_removed() {
    let document = case(&[
        element("Claim", "a", r#" assertionDeclaration="assumed""#),
        element("Claim", "j", r#" assertionDeclaration="axiomatic""#),
        element("AssertedContext", "x", r#" source="j" target="a""#),
    ]);
    assert_eq!(
        clears(&document, Rule::LeafHasContext),
        [removes("x", "a", "j")]
    );
}

#[test]
fn af_gsn_004_is_joined_as_context_or_under_a_strategy_loses_its_line() {
    let plain = case(&[
        goal("g"),
        element("Claim", "a", r#" assertionDeclaration="assumed""#),
        element("AssertedInference", "i", r#" source="a" target="g""#),
    ]);
    assert_eq!(
        clears(&plain, Rule::ContextualSupports),
        [Repair::MakeContext {
            element: "a".to_owned(),
            relationship: "i".to_owned(),
        }]
    );
    // U7: W27 does not say whether it is context of the Strategy or of its Goal.
    let under = case(&[
        goal("g"),
        goal("sub"),
        element("Claim", "a", r#" assertionDeclaration="assumed""#),
        element("ArgumentReasoning", "s", ""),
        element(
            "AssertedInference",
            "i",
            r#" source="sub a" target="g" reasoning="s""#,
        ),
    ]);
    assert_eq!(
        clears(&under, Rule::ContextualSupports),
        [removes("i", "s", "a")]
    );
}

#[test]
fn af_gsn_005_a_goal_given_as_context_loses_the_line() {
    let document = case(&[
        goal("g"),
        goal("h"),
        element("AssertedContext", "x", r#" source="h" target="g""#),
    ]);
    assert_eq!(
        clears(&document, Rule::NotContextual),
        [removes("x", "g", "h")]
    );
}

#[test]
fn af_gsn_006_a_strategy_supported_by_a_strategy_loses_the_line() {
    let document = case(&[
        goal("g"),
        goal("sub"),
        element("ArgumentReasoning", "s", ""),
        element("ArgumentReasoning", "s2", ""),
        element(
            "AssertedInference",
            "i",
            r#" source="sub s2" target="g" reasoning="s""#,
        ),
    ]);
    assert_eq!(
        clears(&document, Rule::StrategySupportedByNonGoal),
        [removes("i", "s", "s2")]
    );
}

#[test]
fn af_gsn_010_a_stale_mark_is_cleared() {
    let document = case(&[
        element("Claim", "g", r#" assertionDeclaration="needsSupport""#),
        goal("sub"),
        element("AssertedInference", "i", r#" source="sub" target="g""#),
    ]);
    assert_eq!(
        clears(&document, Rule::UndevelopedSupported),
        [Repair::ClearUndeveloped {
            element: "g".to_owned(),
            refusal: None,
        }]
    );
}

#[test]
fn a_line_that_cannot_go_is_offered_with_why() {
    // The Goal g is given as context of h (AF-GSN-005), and that line is
    // challenged: its removal is offered, refused, with why (AF-EDIT-038).
    let document = case(&[
        goal("g"),
        goal("h"),
        goal("k"),
        element("AssertedContext", "x", r#" source="g" target="h""#),
        element(
            "AssertedInference",
            "ch",
            r#" source="k" target="x" isCounter="true""#,
        ),
    ]);
    let reported = finding(&document, Rule::NotContextual);
    assert_eq!(
        repairs(&document, &view(&document, "en"), &reported),
        [Repair::RemoveLine {
            relationship: "x".to_owned(),
            from: "h".to_owned(),
            to: "g".to_owned(),
            refusal: Some(Refusal::LineChallenged),
        }]
    );
}

/// An element `id` of `kind` with the identifier `identifier` (T1).
fn identified(kind: &str, id: &str, identifier: &str, attributes: &str) -> String {
    format!(
        r#"<argumentElement xmi:type="sacm:{kind}" xmi:id="{id}"{attributes}><name content="{id}"/><taggedValue xmi:id="t{id}"><key><value content="assuranceForge.gsn.identifier"/></key><content><value content="{identifier}"/></content></taggedValue></argumentElement>"#
    )
}

#[test]
fn af_gsn_009_the_first_keeps_the_identifier_and_the_others_are_renumbered() {
    // G1 three times, and G4 taken already: the others get G5 and G6.
    let document = case(&[
        identified("Claim", "a", "G1", ""),
        identified("Claim", "b", "G1", ""),
        identified("Claim", "c", "G1", ""),
        identified("Claim", "d", "G4", ""),
    ]);
    let offered = clears(&document, Rule::DuplicateIdentifier);
    assert_eq!(
        offered,
        [Repair::Renumber {
            nodes: vec!["a".to_owned(), "b".to_owned(), "c".to_owned()],
            given: vec![
                ("b".to_owned(), "G5".to_owned()),
                ("c".to_owned(), "G6".to_owned()),
            ],
            refusal: None,
        }]
    );
    let mut repaired = document.clone();
    make(&mut repaired, &offered[0]);
    let identifiers: Vec<(String, String)> = view(&repaired, "en")
        .nodes
        .iter()
        .map(|node| (node.id.clone(), node.identifier.clone()))
        .collect();
    assert_eq!(
        identifiers,
        [
            ("a".to_owned(), "G1".to_owned()),
            ("b".to_owned(), "G5".to_owned()),
            ("c".to_owned(), "G6".to_owned()),
            ("d".to_owned(), "G4".to_owned()),
        ]
    );
}

#[test]
fn w38_a_node_keeps_the_prefix_of_its_identifier_or_takes_its_kinds() {
    // The file's own scheme stays: cl_sub_ for cl_sub_1. An identifier that
    // ends in no digits, or is only digits, takes the kind's prefix (W10).
    for (shared, kind, attributes, wanted) in [
        ("cl_sub_1", "Claim", "", "cl_sub_2"),
        ("TopGoal", "Claim", "", "G1"),
        ("7", "Claim", r#" assertionDeclaration="assumed""#, "A1"),
        ("Sn3", "ArtifactReference", "", "Sn4"),
    ] {
        let mut body = vec![
            identified(kind, "a", shared, attributes),
            identified(kind, "b", shared, attributes),
        ];
        if kind == "ArtifactReference" {
            // Drawn as Solutions by their evidence (R9).
            body.push(element("Claim", "g", ""));
            body.push(element(
                "AssertedEvidence",
                "e",
                r#" source="a b" target="g""#,
            ));
        }
        let document = case(&body);
        let reported = finding(&document, Rule::DuplicateIdentifier);
        let offered = repairs(&document, &view(&document, "en"), &reported);
        let Some(Repair::Renumber { given, .. }) = offered.first() else {
            panic!("{shared}: {offered:?}");
        };
        assert_eq!(given, &[("b".to_owned(), wanted.to_owned())], "{shared}");
    }
}

#[test]
fn w38_nodes_that_no_longer_share_an_identifier_are_not_renumbered() {
    let document = case(&[
        identified("Claim", "a", "G1", ""),
        identified("Claim", "b", "G2", ""),
    ]);
    let shown = view(&document, "en");
    let mut edited = document.clone();
    let nodes = ["a".to_owned(), "b".to_owned()];
    assert!(matches!(
        renumber(&mut edited, &shown, &nodes),
        Err(AddError::Refused(Refusal::NothingToRenumber))
    ));
    assert!(edited == document);
    assert!(matches!(
        renumber(&mut edited, &shown, &nodes[..1]),
        Err(AddError::Refused(Refusal::NothingToRenumber))
    ));
}

#[test]
fn rules_with_no_repair_in_this_step_offer_none() {
    // A Strategy with no Goal (AF-GSN-008), and a Strategy as the source of
    // an inference (AF-GSN-007).
    let document = case(&[
        element("ArgumentReasoning", "alone", ""),
        goal("a"),
        element("ArgumentReasoning", "s", ""),
        element("AssertedInference", "i", r#" source="s" target="a""#),
    ]);
    let found = check(&document, &view(&document, "en"));
    for rule in [Rule::StrategyAtEnd, Rule::StrategyUnsupported] {
        assert!(
            found.iter().any(|finding| finding.rule == rule),
            "setup: no {rule:?}"
        );
    }
    for finding in &found {
        if matches!(
            finding.rule,
            Rule::StrategyAtEnd | Rule::StrategyUnsupported
        ) {
            assert!(repairs(&document, &view(&document, "en"), finding).is_empty());
        }
    }
}

/// Every finding of every sample: each repair offered clears it, adds no
/// error, and leaves the file valid; one not offered says why.
#[test]
fn every_repair_offered_on_the_samples_clears_its_problem() {
    let mut made = 0;
    for name in SAMPLES {
        let document = sample(name);
        let before = view(&document, "en");
        let diagnostics = sacm::validation::validate(&document).len();
        for reported in check(&document, &before) {
            for repair in repairs(&document, &before, &reported) {
                let what = format!("{name} {repair:?}");
                let offered = match &repair {
                    Repair::RemoveLine { refusal, .. }
                    | Repair::ClearUndeveloped { refusal, .. }
                    | Repair::Renumber { refusal, .. } => refusal.is_none(),
                    Repair::MakeContext { .. } => true,
                };
                if !offered {
                    continue;
                }
                let mut repaired = document.clone();
                make(&mut repaired, &repair);
                let after = check(&repaired, &view(&repaired, "en"));
                assert!(!after.contains(&reported), "{what}: still reported");
                assert!(
                    sacm::validation::validate(&repaired).len() <= diagnostics,
                    "{what}: new validation diagnostics"
                );
                made += 1;
            }
        }
    }
    assert!(made >= 4, "only {made} repairs were made on the samples");
}
