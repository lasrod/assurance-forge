//! The add commands against the plan's "What may be added where" and the
//! writing rules W1 to W11 of `docs/standards/sacm-gsn-mapping.md`: what is
//! added reads back, by the reading rules, as the kind chosen, joined to its
//! parent as chosen.

use std::collections::BTreeSet;

use af_gsn::ElementKind;
use af_gsn::commands::{AddError, Choice, Refusal, Target, add, choices};
use af_gsn::view::{Code, EdgeKind, NodeKind, View, view};
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

/// One node of each kind, and an undeveloped goal: `g` supported through the
/// Strategy `s` by `sub`, evidence `sn` for `sub`, and context, assumption
/// and justification for `g`.
fn every_kind() -> Document {
    case(
        &[
            element("Claim", "g", ""),
            element("Claim", "sub", ""),
            element("Claim", "todo", r#" assertionDeclaration="needsSupport""#),
            element("Claim", "a", r#" assertionDeclaration="assumed""#),
            element("Claim", "j", r#" assertionDeclaration="axiomatic""#),
            element("Claim", "gone", r#" assertionDeclaration="defeated""#),
            element("ArgumentReasoning", "s", ""),
            element("ArtifactReference", "sn", ""),
            element("ArtifactReference", "c", ""),
            element(
                "AssertedInference",
                "i1",
                r#" source="sub" target="g" reasoning="s""#,
            ),
            element("AssertedInference", "i2", r#" source="todo" target="sub""#),
            element("AssertedEvidence", "e1", r#" source="sn" target="sub""#),
            element("AssertedContext", "x1", r#" source="c a j" target="g""#),
        ]
        .concat(),
    )
}

fn refusals(choices: &[Choice]) -> Vec<Option<Refusal>> {
    choices.iter().map(|choice| choice.refusal).collect()
}

fn kind_of(view: &View, id: &str) -> NodeKind {
    view.nodes
        .iter()
        .find(|node| node.id == id)
        .unwrap_or_else(|| panic!("no node {id}"))
        .kind
}

fn node_kind(kind: ElementKind) -> NodeKind {
    match kind {
        ElementKind::Goal => NodeKind::Goal,
        ElementKind::Strategy => NodeKind::Strategy,
        ElementKind::Solution => NodeKind::Solution,
        ElementKind::Context => NodeKind::Context,
        ElementKind::Assumption => NodeKind::Assumption,
        ElementKind::Justification => NodeKind::Justification,
        _ => unreachable!(),
    }
}

/// Every finding of the view, as (code, element, end).
fn findings(view: &View) -> BTreeSet<String> {
    view.findings
        .iter()
        .map(|finding| format!("{:?} {} {:?}", finding.code, finding.element, finding.end))
        .collect()
}

#[test]
fn the_menu_offers_what_gsn_allows_on_each_kind_of_node() {
    let document = every_kind();
    let view = view(&document, "en");
    let all = |refusal: Option<Refusal>| vec![refusal; 6];
    // Goal, Strategy, Solution, Context, Assumption, Justification.
    assert_eq!(
        refusals(&choices(&document, &view, Target::Node("g"))),
        all(None)
    );
    assert_eq!(
        refusals(&choices(&document, &view, Target::Node("s"))),
        [
            None,
            Some(Refusal::NotUnderStrategy),
            Some(Refusal::NotUnderStrategy),
            None,
            None,
            None
        ]
    );
    for leaf in ["sn", "c", "a", "j"] {
        assert_eq!(
            refusals(&choices(&document, &view, Target::Node(leaf))),
            all(Some(Refusal::Leaf)),
            "{leaf}"
        );
    }
    assert_eq!(kind_of(&view, "gone"), NodeKind::Neutral);
    assert_eq!(
        refusals(&choices(&document, &view, Target::Node("gone"))),
        all(Some(Refusal::KindUnknown))
    );
    assert_eq!(
        refusals(&choices(&document, &view, Target::Node("nothing"))),
        all(Some(Refusal::KindUnknown))
    );
    let mut canvas = all(Some(Refusal::TopLevelGoalOnly));
    canvas[0] = None;
    assert_eq!(refusals(&choices(&document, &view, Target::Canvas)), canvas);
    // Every choice lists the six kinds in the standard's order.
    let kinds: Vec<ElementKind> = choices(&document, &view, Target::Canvas)
        .iter()
        .map(|choice| choice.kind)
        .collect();
    assert_eq!(kinds, ElementKind::ALL);
}

#[test]
fn w11_support_under_an_undeveloped_goal_develops_it() {
    let mut document = every_kind();
    let before = view(&document, "en");
    let offered = choices(&document, &before, Target::Node("todo"));
    let develops: Vec<bool> = offered.iter().map(|choice| choice.develops).collect();
    assert_eq!(develops, [true, true, true, false, false, false]);
    assert!(offered.iter().all(|choice| choice.refusal.is_none()));
    // Context does not develop it.
    let added = add(
        &mut document,
        &before,
        Target::Node("todo"),
        ElementKind::Context,
    )
    .unwrap_or_else(|error| panic!("{error}"));
    let after = view(&document, "en");
    assert!(
        after
            .nodes
            .iter()
            .any(|node| node.id == "todo" && node.undeveloped)
    );
    // Support does.
    add(
        &mut document,
        &after,
        Target::Node("todo"),
        ElementKind::Solution,
    )
    .unwrap_or_else(|error| panic!("{error}"));
    let last = view(&document, "en");
    let todo = last.nodes.iter().find(|node| node.id == "todo");
    assert_eq!(
        todo.map(|node| (node.kind, node.undeveloped)),
        Some((NodeKind::Goal, false))
    );
    assert!(!added.element.is_empty());
}

/// The SACM ids of the placeholders the view does not draw (R16).
fn placeholders(view: &View) -> Vec<String> {
    view.unplaced
        .iter()
        .filter(|item| item.code == Code::Placeholder)
        .map(|item| item.id.clone())
        .collect()
}

fn edges_of<'a>(view: &'a View, id: &str) -> Vec<(&'a str, &'a str, EdgeKind)> {
    view.edges
        .iter()
        .filter(|edge| edge.from == id || edge.to == id)
        .map(|edge| (edge.from.as_str(), edge.to.as_str(), edge.kind))
        .collect()
}

#[test]
fn w2_a_strategy_is_added_alone_and_its_first_goal_takes_the_place_it_holds() {
    let mut document = every_kind();
    let before = view(&document, "en");
    let added = add(
        &mut document,
        &before,
        Target::Node("g"),
        ElementKind::Strategy,
    )
    .unwrap_or_else(|error| panic!("{error}"));
    let after = view(&document, "en");
    let strategy = added.element.as_str();
    assert_eq!(kind_of(&after, strategy), NodeKind::Strategy);
    // Only the Strategy is drawn: the Claim SACM needs as its inference's
    // source is held, not drawn, and says nothing the view reports.
    assert_eq!(after.nodes.len(), before.nodes.len() + 1);
    assert_eq!(
        edges_of(&after, strategy),
        [("g", strategy, EdgeKind::SupportedBy)]
    );
    let held = placeholders(&after);
    assert_eq!(held.len(), 1);
    assert_eq!(findings(&after), findings(&before));
    let inference = document
        .elements()
        .find(|element| element.reasoning().is_some_and(|r| r.as_str() == strategy))
        .unwrap_or_else(|| panic!("no inference of the Strategy"));
    assert_eq!(
        inference
            .sources()
            .iter()
            .map(|id| id.as_str())
            .collect::<Vec<_>>(),
        [held[0].as_str()]
    );
    assert_eq!(inference.target().map(|id| id.as_str()), Some("g"));
    let inference = inference.id().as_str().to_owned();
    // The file is valid SACM, and reads back the same.
    let diagnostics = sacm::validation::validate(&every_kind()).len();
    assert!(sacm::validation::validate(&document).len() <= diagnostics);
    let written =
        xmi::write(&document, &WriteOptions::default()).unwrap_or_else(|error| panic!("{error:?}"));
    assert_eq!(placeholders(&view(&read(written.as_bytes()), "en")), held);
    // Its identifier follows the ones the file has (W10): none are tagged
    // here, so it is the first of its prefix.
    let identifier = |view: &View, id: &str| {
        view.nodes
            .iter()
            .find(|node| node.id == id)
            .map(|node| node.identifier.clone())
    };
    assert_eq!(identifier(&after, strategy).as_deref(), Some("S1"));

    // W3: the first Goal under it is the Claim that held its place.
    let goal = add(
        &mut document,
        &after,
        Target::Node(strategy),
        ElementKind::Goal,
    )
    .unwrap_or_else(|error| panic!("{error}"));
    let last = view(&document, "en");
    assert_eq!(goal.element, held[0]);
    assert_eq!(kind_of(&last, &goal.element), NodeKind::Goal);
    assert_eq!(identifier(&last, &goal.element).as_deref(), Some("G1"));
    assert!(placeholders(&last).is_empty());
    assert_eq!(
        edges_of(&last, strategy),
        [
            ("g", strategy, EdgeKind::SupportedBy),
            (strategy, goal.element.as_str(), EdgeKind::SupportedBy),
        ]
    );
    assert!(
        last.edges
            .iter()
            .all(|edge| edge.from != strategy || edge.relationship == inference)
    );
    assert_eq!(findings(&last), findings(&before));
    // A second Goal is a second source of the one inference.
    let second = add(
        &mut document,
        &last,
        Target::Node(strategy),
        ElementKind::Goal,
    )
    .unwrap_or_else(|error| panic!("{error}"));
    assert_ne!(second.element, goal.element);
    let sources = document
        .get(&inference)
        .map(|element| element.sources().len());
    assert_eq!(sources, Some(2));
}

/// R16 hides a placeholder only while it says nothing more: one with a name,
/// or one a second relationship refers to, is drawn as the Goal it is.
#[test]
fn r16_a_placeholder_that_says_more_is_drawn() {
    let held = |name: &str, extra: &str| {
        case(
            &[
                element("Claim", "g", ""),
                element("ArgumentReasoning", "s", ""),
                format!(
                    r#"<argumentElement xmi:type="sacm:Claim" xmi:id="ph"><name content="{name}"/><taggedValue xmi:id="tv"><key><value content="assuranceForge.gsn.placeholder"/></key><content><value content="subGoal"/></content></taggedValue></argumentElement>"#
                ),
                element("AssertedInference", "i", r#" source="ph" target="g" reasoning="s""#),
                extra.to_owned(),
            ]
            .concat(),
        )
    };
    assert_eq!(placeholders(&view(&held("", ""), "en")), ["ph"]);
    let named = view(&held("A goal someone wrote", ""), "en");
    assert!(placeholders(&named).is_empty());
    assert_eq!(kind_of(&named, "ph"), NodeKind::Goal);
    let referred = view(
        &held(
            "",
            &element("AssertedInference", "i2", r#" source="ph" target="g""#),
        ),
        "en",
    );
    assert!(placeholders(&referred).is_empty());
    assert_eq!(kind_of(&referred, "ph"), NodeKind::Goal);
}

#[test]
fn w3_a_goal_under_a_strategy_is_a_premise_of_its_inference() {
    let mut document = every_kind();
    let before = view(&document, "en");
    let added = add(&mut document, &before, Target::Node("s"), ElementKind::Goal)
        .unwrap_or_else(|error| panic!("{error}"));
    let after = view(&document, "en");
    assert!(after.edges.iter().any(|edge| {
        edge.from == "s"
            && edge.to == added.element
            && edge.kind == EdgeKind::SupportedBy
            && edge.relationship == "i1"
    }));
    // No new relationship: the inference has another source.
    assert_eq!(after.edges.len(), before.edges.len() + 1);
}

#[test]
fn a_strategy_that_is_the_reasoning_of_no_one_inference_takes_no_sub_goal() {
    let document = case(
        &[
            element("Claim", "g", ""),
            element("Claim", "h", ""),
            element("Claim", "sub", ""),
            element("ArgumentReasoning", "alone", ""),
            element("ArgumentReasoning", "shared", ""),
            element(
                "AssertedInference",
                "i1",
                r#" source="sub" target="g" reasoning="shared""#,
            ),
            element(
                "AssertedInference",
                "i2",
                r#" source="sub" target="h" reasoning="shared""#,
            ),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    for strategy in ["alone", "shared"] {
        let offered = choices(&document, &view, Target::Node(strategy));
        assert_eq!(
            offered[0].refusal,
            Some(Refusal::NoOneInference),
            "{strategy}"
        );
        // Context is still offered: it joins the Strategy, not an inference.
        assert_eq!(offered[3].refusal, None, "{strategy}");
    }
}

#[test]
fn w8_and_w9_a_top_level_goal_goes_into_the_first_argument_package() {
    let mut document = every_kind();
    let before = view(&document, "en");
    let added = add(&mut document, &before, Target::Canvas, ElementKind::Goal)
        .unwrap_or_else(|error| panic!("{error}"));
    let parent = document
        .get(&added.element)
        .and_then(|element| element.parent())
        .map(|parent| parent.id().as_str().to_owned());
    assert_eq!(parent.as_deref(), Some("p"));
    assert!(matches!(
        add(
            &mut document,
            &before,
            Target::Canvas,
            ElementKind::Solution
        ),
        Err(AddError::Refused(Refusal::TopLevelGoalOnly))
    ));

    // A case with no ArgumentPackage has nowhere to put one.
    let empty = read(
        br#"<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:id="case"><name content="Case"/></sacm:AssuranceCasePackage>"#,
    );
    let none = view(&empty, "en");
    assert_eq!(
        choices(&empty, &none, Target::Canvas)[0].refusal,
        Some(Refusal::NoPackage)
    );
}

#[test]
fn w10_an_identifier_is_the_lowest_number_above_those_of_its_prefix() {
    let tagged = |id: &str, kind: &str, identifier: &str| {
        format!(
            r#"<argumentElement xmi:type="sacm:{kind}" xmi:id="{id}"><name content="{id}"/><taggedValue xmi:id="tv_{id}"><key><value content="assuranceForge.gsn.identifier"/></key><content><value content="{identifier}"/></content></taggedValue></argumentElement>"#
        )
    };
    let mut document = case(
        &[
            tagged("g", "Claim", "G7"),
            tagged("g2", "Claim", "G2a"),
            tagged("e", "ArtifactReference", "Sn4"),
            element("AssertedEvidence", "ae", r#" source="e" target="g""#),
        ]
        .concat(),
    );
    let before = view(&document, "en");
    let goal = add(&mut document, &before, Target::Node("g"), ElementKind::Goal)
        .unwrap_or_else(|error| panic!("{error}"));
    let middle = view(&document, "en");
    let solution = add(
        &mut document,
        &middle,
        Target::Node("g"),
        ElementKind::Solution,
    )
    .unwrap_or_else(|error| panic!("{error}"));
    let after = view(&document, "en");
    let identifier = |id: &str| {
        after
            .nodes
            .iter()
            .find(|node| node.id == id)
            .map(|node| node.identifier.as_str())
    };
    // "G2a" is not a number of G; "Sn4" is not one of S.
    assert_eq!(identifier(&goal.element), Some("G8"));
    assert_eq!(identifier(&solution.element), Some("Sn5"));
}

/// W10 counts every identifier of the file, not only those of drawn nodes:
/// an `ArgumentGroup` is not drawn (R14), and its `G9` is not given again.
#[test]
fn w10_an_identifier_the_view_does_not_draw_is_not_given_again() {
    let mut document = case(
        &[
            element("Claim", "g", ""),
            r#"<argumentElement xmi:type="sacm:ArgumentGroup" xmi:id="group"><name content="group"/><taggedValue xmi:id="tv_group"><key><value content="assuranceForge.gsn.identifier"/></key><content><value content="G9"/></content></taggedValue></argumentElement>"#.to_owned(),
        ]
        .concat(),
    );
    let before = view(&document, "en");
    assert!(
        before.nodes.iter().all(|node| node.identifier != "G9"),
        "setup: the group is not drawn"
    );
    let goal = add(&mut document, &before, Target::Node("g"), ElementKind::Goal)
        .unwrap_or_else(|error| panic!("{error}"));
    let after = view(&document, "en");
    let identifier = after
        .nodes
        .iter()
        .find(|node| node.id == goal.element)
        .map(|node| node.identifier.as_str());
    assert_eq!(identifier, Some("G10"));
}

#[test]
fn a_refused_add_changes_nothing() {
    let mut document = every_kind();
    let before = document.clone();
    let view = view(&document, "en");
    for (target, kind) in [
        (Target::Node("sn"), ElementKind::Goal),
        (Target::Node("s"), ElementKind::Solution),
        (Target::Node("gone"), ElementKind::Context),
        (Target::Canvas, ElementKind::Strategy),
    ] {
        assert!(matches!(
            add(&mut document, &view, target, kind),
            Err(AddError::Refused(_))
        ));
    }
    assert!(document == before);
}

/// Every choice the menu enables, on every kind of node of every sample:
/// the element reads back as the kind chosen, joined to its parent by the
/// edge chosen, with no new finding, and the document is still valid and
/// still written.
#[test]
fn every_enabled_choice_reads_back_as_chosen_on_every_sample() {
    let mut added = 0;
    for name in SAMPLES {
        let document = sample(name);
        let before = view(&document, "en");
        let diagnostics = sacm::validation::validate(&document).len();
        let old_findings = findings(&before);
        // A few nodes of each kind keep the run short on the large samples.
        let mut targets: Vec<Target<'_>> = vec![Target::Canvas];
        for kind in [
            NodeKind::Goal,
            NodeKind::Strategy,
            NodeKind::Solution,
            NodeKind::Context,
            NodeKind::Assumption,
            NodeKind::Justification,
            NodeKind::Neutral,
        ] {
            targets.extend(
                before
                    .nodes
                    .iter()
                    .filter(|node| node.kind == kind)
                    .take(3)
                    .map(|node| Target::Node(&node.id)),
            );
        }
        for target in targets {
            for choice in choices(&document, &before, target) {
                let mut edited = document.clone();
                let result = add(&mut edited, &before, target, choice.kind);
                let Some(refusal) = choice.refusal else {
                    let made = result.unwrap_or_else(|error| {
                        panic!("{name} {target:?} {:?}: {error}", choice.kind)
                    });
                    let after = view(&edited, "en");
                    let what = format!("{name} {target:?} {:?}", choice.kind);
                    assert_eq!(
                        kind_of(&after, &made.element),
                        node_kind(choice.kind),
                        "{what}"
                    );
                    if let Target::Node(parent) = target {
                        let edge = match choice.kind {
                            ElementKind::Goal | ElementKind::Strategy | ElementKind::Solution => {
                                EdgeKind::SupportedBy
                            }
                            _ => EdgeKind::InContextOf,
                        };
                        assert!(
                            after.edges.iter().any(|candidate| candidate.from == parent
                                && candidate.to == made.element
                                && candidate.kind == edge),
                            "{what}: no {edge:?} from the parent"
                        );
                    } else {
                        assert!(
                            !after.edges.iter().any(|edge| edge.to == made.element),
                            "{what}: a top-level Goal is reached by nothing"
                        );
                    }
                    let new: Vec<String> = findings(&after)
                        .difference(&old_findings)
                        .cloned()
                        .collect();
                    assert!(new.is_empty(), "{what}: {new:?}");
                    assert!(
                        sacm::validation::validate(&edited).len() <= diagnostics,
                        "{what}: new validation diagnostics"
                    );
                    assert!(
                        xmi::write(&edited, &WriteOptions::default()).is_ok(),
                        "{what}: not written"
                    );
                    added += 1;
                    continue;
                };
                assert!(
                    matches!(result, Err(AddError::Refused(given)) if given == refusal),
                    "{name} {target:?} {:?}",
                    choice.kind
                );
                assert!(edited == document);
            }
        }
    }
    assert!(added > 100, "only {added} adds were checked");
}
