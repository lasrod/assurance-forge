//! Adding a challenge, against the writing rules W29 to W31 of
//! `docs/standards/sacm-gsn-mapping.md`.

use af_gsn::ElementKind;
use af_gsn::commands::{AddError, Challenged, Refusal, challenge, challenge_choices};
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
        (Challenged::Line("is"), "is"),
        (Challenged::Line("ev"), "ev"),
        (Challenged::Line("cx"), "cx"),
        (Challenged::Line("ca"), "ca"),
        // A challenge is challenged.
        (Challenged::Line("ch"), "ch"),
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
        refused(&mut document, Challenged::Line("cited")),
        Refusal::KindUnknown
    );
    assert_eq!(
        refused(&mut document, Challenged::Node("nothing")),
        Refusal::NotANode
    );
    assert_eq!(
        refused(&mut document, Challenged::Line("nothing")),
        Refusal::NotANode
    );
    // A node's id is not a line.
    assert_eq!(
        refused(&mut document, Challenged::Line("g")),
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
fn w31_the_challenger_takes_the_next_identifier_of_its_kind() {
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
    assert_eq!(node.identifier, "Sn1");
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
        let lines: Vec<&str> = shown
            .edges
            .iter()
            .filter(|edge| edge.kind != EdgeKind::Neutral)
            .map(|edge| edge.relationship.as_str())
            .collect();
        let targets = shown
            .nodes
            .iter()
            .take(40)
            .map(|node| Challenged::Node(&node.id))
            .chain(lines.into_iter().take(40).map(Challenged::Line));
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
