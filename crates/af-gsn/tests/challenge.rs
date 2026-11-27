//! Adding a challenge, against the writing rules W29 to W31 of
//! `docs/standards/sacm-gsn-mapping.md`.

use af_gsn::ElementKind;
use af_gsn::commands::{
    AddError, Challenged, Refusal, Target, challenge, challenge_choices, move_challenge, move_node,
    move_targets,
};
use af_gsn::view::{EdgeKind, NodeKind, view};
use sacm::model::{Document, ElementKind as SacmKind};
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

/// One of each kind of node and line: `g` is supported by `sub` through the
/// Strategy `s` (the inference `is`) and by the Solution `e` (`ev`); the
/// Context `x` (`cx`), the Assumption `a` (`ca`) and the Justification `j`
/// (`cj`) are its context; `k` challenges `sub` (`ch`). `n` is neutral, and
/// `cited` is a line drawn neutral.
fn argument() -> Document {
    case(
        &[
            element("Claim", "g", ""),
            element("Claim", "sub", ""),
            element("ArgumentReasoning", "s", ""),
            element("ArtifactReference", "e", ""),
            element("ArtifactReference", "x", ""),
            element("Claim", "a", r#" assertionDeclaration="assumed""#),
            element("Claim", "j", r#" assertionDeclaration="axiomatic""#),
            element("Claim", "k", ""),
            element("Claim", "n", r#" assertionDeclaration="asCited""#),
            element("Claim", "other", ""),
            element(
                "AssertedInference",
                "is",
                r#" source="sub" target="g" reasoning="s""#,
            ),
            element("AssertedEvidence", "ev", r#" source="e" target="g""#),
            element("AssertedContext", "cx", r#" source="x" target="g""#),
            element("AssertedContext", "ca", r#" source="a" target="g""#),
            element("AssertedContext", "cj", r#" source="j" target="g""#),
            element(
                "AssertedInference",
                "ch",
                r#" source="k" target="sub" isCounter="true""#,
            ),
            element(
                "AssertedInference",
                "cited",
                r#" source="other" target="g" isCitation="true""#,
            ),
        ]
        .concat(),
    )
}

/// The counter relationship from `challenger`, and what it points at.
fn counter_from(document: &Document, challenger: &str) -> (SacmKind, String) {
    let found = document
        .elements()
        .find(|element| {
            element.is_counter()
                && element
                    .sources()
                    .iter()
                    .any(|source| source.as_str() == challenger)
        })
        .unwrap_or_else(|| panic!("no counter relationship from {challenger}"));
    let target = found
        .target()
        .map(|id| id.as_str().to_owned())
        .unwrap_or_default();
    (found.kind(), target)
}

/// Challenges `at` with `kind` on a copy of `document`, and checks what was
/// made: a node of `kind`, its counter relationship to `target`, drawn as a
/// challenge from it, a valid file that writes strictly and reads back the same.
fn challenged(document: &Document, at: Challenged<'_>, kind: ElementKind, target: &str) {
    let mut document = document.clone();
    let shown = view(&document, "en");
    let problems = sacm::validation::validate(&document).len();
    let added = challenge(&mut document, &shown, at, kind)
        .unwrap_or_else(|error| panic!("{at:?} by {kind:?}: {error}"));
    let after = view(&document, "en");
    let node = after
        .nodes
        .iter()
        .find(|node| node.id == added.element)
        .unwrap_or_else(|| panic!("{at:?} by {kind:?}: the challenger is not drawn"));
    let expected = if kind == ElementKind::Solution {
        NodeKind::Solution
    } else {
        NodeKind::Goal
    };
    assert_eq!(node.kind, expected, "{at:?} by {kind:?}");
    let (relationship, to) = counter_from(&document, &added.element);
    let made = if kind == ElementKind::Solution {
        SacmKind::AssertedEvidence
    } else {
        SacmKind::AssertedInference
    };
    assert_eq!(
        (relationship, to.as_str()),
        (made, target),
        "{at:?} by {kind:?}"
    );
    assert!(
        after
            .edges
            .iter()
            .any(|edge| edge.kind == EdgeKind::Challenge && edge.from == added.element),
        "{at:?} by {kind:?}: no challenge is drawn"
    );
    assert_eq!(
        after.findings.len(),
        shown.findings.len(),
        "{at:?} by {kind:?}"
    );
    assert_eq!(
        sacm::validation::validate(&document).len(),
        problems,
        "{at:?} by {kind:?}"
    );
    let written = xmi::write(&document, &WriteOptions::default().with_mode(Mode::Strict))
        .unwrap_or_else(|error| panic!("{at:?} by {kind:?}: {error:?}"));
    let again = read(written.as_bytes());
    assert_eq!(
        counter_from(&again, &added.element),
        (made, target.to_owned()),
        "{at:?} by {kind:?}: read back"
    );
}

#[test]
fn w29_each_kind_of_node_and_line_is_challenged_by_a_goal_and_by_evidence() {
    let document = argument();
    let cases = [
        (Challenged::Node("g"), "g"),
        (Challenged::Node("sub"), "sub"),
        (Challenged::Node("e"), "e"),
        (Challenged::Node("x"), "x"),
        (Challenged::Node("a"), "a"),
        (Challenged::Node("j"), "j"),
        (Challenged::Node("k"), "k"),
        // A Strategy, through the inference it is the reasoning of.
        (Challenged::Node("s"), "is"),
        (
            Challenged::Line {
                relationship: "is",
                from: "g",
                to: "s",
            },
            "is",
        ),
        (
            Challenged::Line {
                relationship: "ev",
                from: "g",
                to: "e",
            },
            "ev",
        ),
        (
            Challenged::Line {
                relationship: "cx",
                from: "g",
                to: "x",
            },
            "cx",
        ),
        (
            Challenged::Line {
                relationship: "ca",
                from: "g",
                to: "a",
            },
            "ca",
        ),
        // A challenge is challenged.
        (
            Challenged::Line {
                relationship: "ch",
                from: "k",
                to: "sub",
            },
            "ch",
        ),
    ];
    for (at, target) in cases {
        for kind in [ElementKind::Goal, ElementKind::Solution] {
            challenged(&document, at, kind, target);
        }
        let shown = view(&document, "en");
        assert!(
            challenge_choices(&document, &shown, at)
                .iter()
                .all(|choice| choice.refusal.is_none()),
            "{at:?}"
        );
    }
}

#[test]
fn w30_only_a_goal_or_a_solution_challenges() {
    let mut document = argument();
    let shown = view(&document, "en");
    let kinds: Vec<_> = challenge_choices(&document, &shown, Challenged::Node("g"))
        .iter()
        .map(|choice| choice.kind)
        .collect();
    assert_eq!(kinds, [ElementKind::Goal, ElementKind::Solution]);
    for kind in [
        ElementKind::Strategy,
        ElementKind::Context,
        ElementKind::Assumption,
        ElementKind::Justification,
    ] {
        match challenge(&mut document, &shown, Challenged::Node("g"), kind) {
            Err(AddError::Refused(refusal)) => assert_eq!(refusal, Refusal::NotAChallenger),
            other => panic!("{kind:?}: {other:?}"),
        }
    }
    assert_eq!(Refusal::NotAChallenger.code(), "AF-EDIT-026");
}

#[test]
fn w29_what_is_of_no_gsn_kind_or_not_drawn_is_not_challenged() {
    let mut document = argument();
    let before = document.clone();
    let shown = view(&document, "en");
    let refused =
        |document: &mut Document, at| match challenge(document, &shown, at, ElementKind::Goal) {
            Err(AddError::Refused(refusal)) => refusal,
            other => panic!("{at:?}: {other:?}"),
        };
    assert_eq!(
        refused(&mut document, Challenged::Node("n")),
        Refusal::KindUnknown
    );
    assert_eq!(
        refused(
            &mut document,
            Challenged::Line {
                relationship: "cited",
                from: "g",
                to: "other"
            }
        ),
        Refusal::KindUnknown
    );
    assert_eq!(
        refused(&mut document, Challenged::Node("nothing")),
        Refusal::NotANode
    );
    assert_eq!(
        refused(
            &mut document,
            Challenged::Line {
                relationship: "nothing",
                from: "g",
                to: "sub"
            }
        ),
        Refusal::NotANode
    );
    // A line the relationship does not have.
    assert_eq!(
        refused(
            &mut document,
            Challenged::Line {
                relationship: "ev",
                from: "g",
                to: "sub"
            }
        ),
        Refusal::NotANode
    );
    // A premise's line under a Strategy is part of the inference, which is
    // challenged at its first line.
    assert_eq!(
        refused(
            &mut document,
            Challenged::Line {
                relationship: "is",
                from: "s",
                to: "sub"
            }
        ),
        Refusal::NotTheFirstLine
    );
    assert_eq!(Refusal::NotTheFirstLine.code(), "AF-EDIT-027");
    // A node's id is not a line.
    assert_eq!(
        refused(
            &mut document,
            Challenged::Line {
                relationship: "g",
                from: "g",
                to: "sub"
            }
        ),
        Refusal::NotANode
    );
    assert_eq!(document, before);
}

#[test]
fn w29_a_strategy_with_no_one_inference_is_not_challenged() {
    let mut document = case(&element("ArgumentReasoning", "alone", ""));
    let shown = view(&document, "en");
    match challenge(
        &mut document,
        &shown,
        Challenged::Node("alone"),
        ElementKind::Goal,
    ) {
        Err(AddError::Refused(refusal)) => assert_eq!(refusal, Refusal::NoOneInference),
        other => panic!("{other:?}"),
    }
}

#[test]
fn w31_a_counter_takes_the_next_cg_or_csn_identifier() {
    let mut document = argument();
    let shown = view(&document, "en");
    let added = challenge(
        &mut document,
        &shown,
        Challenged::Node("g"),
        ElementKind::Solution,
    )
    .unwrap_or_else(|error| panic!("{error}"));
    let after = view(&document, "en");
    let node = after
        .nodes
        .iter()
        .find(|node| node.id == added.element)
        .unwrap_or_else(|| panic!("not drawn"));
    assert_eq!(node.identifier, "CSn1");
    let shown = view(&document, "en");
    let added = challenge(
        &mut document,
        &shown,
        Challenged::Node("g"),
        ElementKind::Goal,
    )
    .unwrap_or_else(|error| panic!("{error}"));
    let shown = view(&document, "en");
    let again = challenge(
        &mut document,
        &shown,
        Challenged::Node("g"),
        ElementKind::Goal,
    )
    .unwrap_or_else(|error| panic!("{error}"));
    let after = view(&document, "en");
    let identifier = |id: &str| {
        after
            .nodes
            .iter()
            .find(|node| node.id == id)
            .map(|node| node.identifier.clone())
    };
    assert_eq!(identifier(&added.element).as_deref(), Some("CG1"));
    assert_eq!(identifier(&again.element).as_deref(), Some("CG2"));
}

/// In every sample, each node and each line drawn is challenged once by a
/// Goal, and the file stays valid and writes strictly.
#[test]
fn a_challenge_in_each_sample_keeps_the_file_valid() {
    for name in [
        "sacm_example_core_argument.sacm.xmi",
        "challenges.sacm.xmi",
        "open-autonomy-safety-case.updated.v2.sacm.xmi",
    ] {
        let original = sample(name);
        let shown = view(&original, "en");
        let problems = sacm::validation::validate(&original).len();
        let mut made = 0;
        let lines: Vec<Challenged<'_>> = shown
            .edges
            .iter()
            .filter(|edge| edge.kind != EdgeKind::Neutral)
            .map(|edge| Challenged::Line {
                relationship: &edge.relationship,
                from: &edge.from,
                to: &edge.to,
            })
            .collect();
        let targets = shown
            .nodes
            .iter()
            .take(40)
            .map(|node| Challenged::Node(&node.id))
            .chain(lines.into_iter().take(40));
        for at in targets {
            let mut document = original.clone();
            match challenge(&mut document, &shown, at, ElementKind::Goal) {
                Ok(_) => made += 1,
                Err(AddError::Refused(_)) => continue,
                Err(error) => panic!("{name}: {at:?}: {error}"),
            }
            assert_eq!(
                sacm::validation::validate(&document).len(),
                problems,
                "{name}: {at:?}"
            );
            let written = xmi::write(&document, &WriteOptions::default().with_mode(Mode::Strict));
            assert!(written.is_ok(), "{name}: {at:?}: {:?}", written.err());
        }
        assert!(made > 0, "{name}: nothing was challenged");
    }
}

/// `k` challenges `sub`, in [`argument`]; `cg` is a counter goal of its own
/// under `k`, supported by `ksub`.
fn challenged_argument() -> Document {
    let mut document = argument();
    let shown = view(&document, "en");
    let added = challenge(
        &mut document,
        &shown,
        Challenged::Node("k"),
        ElementKind::Goal,
    )
    .unwrap_or_else(|error| panic!("setup: {error}"));
    assert!(!added.element.is_empty());
    document
}

#[test]
fn w32_a_counter_goal_moves_its_challenge_to_a_node_or_a_first_line() {
    let original = argument();
    let shown = view(&original, "en");
    let targets = move_targets(&original, &shown, "k").unwrap_or_else(|why| panic!("{why:?}"));
    let node = |id: &str| {
        targets
            .nodes
            .iter()
            .find(|target| target.node == id)
            .unwrap_or_else(|| panic!("no {id}"))
            .refusal
    };
    let line = |relationship: &str, from: &str, to: &str| {
        targets
            .lines
            .iter()
            .find(|target| {
                target.relationship == relationship && target.from == from && target.to == to
            })
            .unwrap_or_else(|| panic!("no line {relationship}"))
            .refusal
    };
    assert_eq!(node("g"), None);
    assert_eq!(node("e"), None);
    assert_eq!(node("sub"), Some(Refusal::AlreadyThere));
    assert_eq!(node("k"), Some(Refusal::UnderItself));
    assert_eq!(node("n"), Some(Refusal::KindUnknown));
    assert_eq!(line("ev", "g", "e"), None);
    assert_eq!(line("is", "g", "s"), None);
    assert_eq!(line("is", "s", "sub"), Some(Refusal::NotTheFirstLine));
    // Its own challenge line.
    assert_eq!(line("ch", "k", "sub"), Some(Refusal::UnderItself));
    assert_eq!(targets.canvas, Some(Refusal::ChallengeStays));

    // To a node, by the menu's move.
    let mut document = original.clone();
    move_node(&mut document, &shown, "k", Target::Node("g"))
        .unwrap_or_else(|error| panic!("{error}"));
    assert_eq!(
        counter_from(&document, "k"),
        (SacmKind::AssertedInference, "g".to_owned())
    );
    assert_eq!(document.elements().count(), original.elements().count());
    // To a line.
    let mut document = original.clone();
    move_challenge(
        &mut document,
        &shown,
        "k",
        Challenged::Line {
            relationship: "ev",
            from: "g",
            to: "e",
        },
    )
    .unwrap_or_else(|error| panic!("{error}"));
    assert_eq!(
        counter_from(&document, "k"),
        (SacmKind::AssertedInference, "ev".to_owned())
    );
    let after = view(&document, "en");
    assert!(
        after
            .edges
            .iter()
            .any(|edge| edge.kind == EdgeKind::Challenge
                && edge.from == "k"
                && edge.to == "ev"
                && edge.to_relationship)
    );
    assert_eq!(after.findings.len(), shown.findings.len());
    assert!(xmi::write(&document, &WriteOptions::default().with_mode(Mode::Strict)).is_ok());
    // Not to the top level.
    let mut document = original.clone();
    match move_node(&mut document, &shown, "k", Target::Canvas) {
        Err(AddError::Refused(refusal)) => assert_eq!(refusal, Refusal::ChallengeStays),
        other => panic!("{other:?}"),
    }
    assert_eq!(Refusal::ChallengeStays.code(), "AF-EDIT-028");
}

#[test]
fn w32_a_challenge_is_not_moved_under_itself_and_one_that_is_challenged_stays() {
    // A counter goal of k's challenge line: it cannot be moved onto k, which
    // it is under, and k's challenge, now challenged, is not moved (W25).
    let document = challenged_argument();
    let shown = view(&document, "en");
    let counter = shown
        .nodes
        .iter()
        .find(|node| node.identifier == "CG1")
        .map(|node| node.id.clone())
        .unwrap_or_else(|| panic!("no CG1"));
    let targets = move_targets(&document, &shown, &counter).unwrap_or_else(|why| panic!("{why:?}"));
    assert_eq!(
        targets
            .nodes
            .iter()
            .find(|target| target.node == "k")
            .and_then(|target| target.refusal),
        Some(Refusal::AlreadyThere)
    );
    assert_eq!(
        targets
            .nodes
            .iter()
            .find(|target| target.node == "g")
            .and_then(|target| target.refusal),
        None
    );
    // k challenges sub, and is itself challenged: k's challenge line is not
    // what is challenged here, k is, so k still moves.
    assert!(move_targets(&document, &shown, "k").is_ok());
    // A challenge of the line k draws makes that line entangled.
    let mut lined = argument();
    let shown = view(&lined, "en");
    challenge(
        &mut lined,
        &shown,
        Challenged::Line {
            relationship: "ch",
            from: "k",
            to: "sub",
        },
        ElementKind::Goal,
    )
    .unwrap_or_else(|error| panic!("{error}"));
    let shown = view(&lined, "en");
    assert_eq!(
        move_targets(&lined, &shown, "k").err(),
        Some(Refusal::Entangled)
    );
}
