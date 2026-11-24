//! Removing a node and what is under it, against the writing rules W18 to W20
//! of `docs/standards/sacm-gsn-mapping.md`: exactly what [`removal`] lists
//! goes, everything else reads back as it did, and the file stays valid.

use std::collections::BTreeSet;

use af_gsn::ElementKind;
use af_gsn::commands::{AddError, Refusal, Target, add, removal, remove};
use af_gsn::view::{Code, NodeKind, View, view};
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

/// `g` supported through the Strategy `s` by `sub` and `other`; evidence
/// `sn` for `sub`; context `c` for `g`; `shared` supports both `sub` and
/// `top`, a second top-level goal; `k` challenges `sub`.
fn argument() -> Document {
    case(
        &[
            element("Claim", "g", ""),
            element("Claim", "top", ""),
            element("Claim", "sub", ""),
            element("Claim", "other", ""),
            element("Claim", "shared", ""),
            element("Claim", "k", ""),
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
            element("AssertedEvidence", "e1", r#" source="sn" target="sub""#),
            element("AssertedContext", "x1", r#" source="c" target="g""#),
            element(
                "AssertedInference",
                "ch",
                r#" source="k" target="sub" isCounter="true""#,
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

fn nodes(view: &View) -> BTreeSet<String> {
    view.nodes.iter().map(|node| node.id.clone()).collect()
}

fn set(list: &[&str]) -> BTreeSet<String> {
    list.iter().map(|id| (*id).to_owned()).collect()
}

fn listed(document: &Document, id: &str) -> Vec<String> {
    let shown = view(document, "en");
    removal(document, &shown, id)
        .unwrap_or_else(|refusal| panic!("{id}: {refusal:?}"))
        .nodes
}

fn removed(document: &mut Document, id: &str) -> Option<String> {
    let shown = view(document, "en");
    remove(document, &shown, id)
        .unwrap_or_else(|error| panic!("{id}: {error}"))
        .parent
}

fn placeholders(view: &View) -> Vec<String> {
    view.unplaced
        .iter()
        .filter(|item| item.code == Code::Placeholder)
        .map(|item| item.id.clone())
        .collect()
}

#[test]
fn a_leaf_goes_alone_with_its_relationship() {
    let mut document = argument();
    assert_eq!(listed(&document, "sn"), ["sn"]);
    assert_eq!(removed(&mut document, "sn").as_deref(), Some("sub"));
    assert!(!document.contains("sn") && !document.contains("e1"));
    assert!(document.contains("sub"));
    let after = view(&document, "en");
    let mut left = nodes(&view(&argument(), "en"));
    left.remove("sn");
    assert_eq!(nodes(&after), left);
}

#[test]
fn w18_what_is_under_a_node_goes_with_it_unless_it_is_also_under_one_that_stays() {
    let mut document = argument();
    // `shared` also supports `top`, which stays; `k` challenges `sub`, which goes.
    let list = listed(&document, "g");
    assert_eq!(list[0], "g");
    assert_eq!(
        list.iter().cloned().collect::<BTreeSet<_>>(),
        set(&["g", "s", "sub", "other", "sn", "c", "k"])
    );
    assert_eq!(removed(&mut document, "g"), None);
    let after = view(&document, "en");
    assert_eq!(nodes(&after), set(&["top", "shared"]));
    for gone in ["i1", "i2", "e1", "x1", "ch"] {
        assert!(!document.contains(gone), "{gone}");
    }
    // `shared` keeps its line to `top`, and nothing else changed.
    assert!(after.edges.iter().any(|edge| edge.relationship == "i3"));
    assert!(after.findings.is_empty(), "{:?}", after.findings);
    assert!(sacm::validation::validate(&document).is_empty());
}

#[test]
fn w18_one_goal_of_a_strategy_goes_and_the_others_stay() {
    let mut document = argument();
    assert_eq!(listed(&document, "other"), ["other"]);
    assert_eq!(removed(&mut document, "other").as_deref(), Some("s"));
    let sources: Vec<String> = document
        .get("i1")
        .map(|inference| {
            inference
                .sources()
                .iter()
                .map(|id| id.as_str().to_owned())
                .collect()
        })
        .unwrap_or_default();
    assert_eq!(sources, ["sub"]);
}

#[test]
fn w20_a_strategy_that_loses_its_last_goal_holds_the_place_of_the_next() {
    let mut document = argument();
    removed(&mut document, "other");
    assert_eq!(removed(&mut document, "sub").as_deref(), Some("s"));
    let after = view(&document, "en");
    assert_eq!(nodes(&after), set(&["g", "s", "c", "top", "shared"]));
    // The Strategy is still drawn under `g`, through the inference it was
    // the reasoning of, which now holds a placeholder.
    assert!(
        after
            .edges
            .iter()
            .any(|edge| edge.from == "g" && edge.to == "s" && edge.relationship == "i1")
    );
    let held = placeholders(&after);
    assert_eq!(held.len(), 1);
    assert!(after.findings.is_empty(), "{:?}", after.findings);
    // And the next Goal takes it.
    let goal = add(&mut document, &after, Target::Node("s"), ElementKind::Goal)
        .unwrap_or_else(|error| panic!("{error}"));
    assert_eq!(goal.element, held[0]);
}

#[test]
fn a_strategy_added_and_removed_leaves_the_file_as_it_was() {
    let mut document = argument();
    let before = ids(&document);
    let shown = view(&document, "en");
    let added = add(
        &mut document,
        &shown,
        Target::Node("top"),
        ElementKind::Strategy,
    )
    .unwrap_or_else(|error| panic!("{error}"));
    assert_eq!(listed(&document, &added.element), [added.element.as_str()]);
    removed(&mut document, &added.element);
    assert_eq!(ids(&document), before);
}

#[test]
fn w19_a_reference_the_diagram_does_not_draw_refuses_the_removal() {
    let mut document = case(
        &[
            element("Claim", "g", ""),
            element("Claim", "about", r#" metaClaim="g""#),
        ]
        .concat(),
    );
    let before = document.clone();
    let shown = view(&document, "en");
    assert_eq!(removal(&document, &shown, "g"), Err(Refusal::Entangled));
    assert!(matches!(
        remove(&mut document, &shown, "g"),
        Err(AddError::Refused(Refusal::Entangled))
    ));
    assert!(document == before);
    assert_eq!(
        removal(&document, &shown, "nothing"),
        Err(Refusal::NotANode)
    );
}

/// Every node of a few of each kind, on every sample: what is listed goes,
/// every other node stays, nothing new is reported, and the file is still
/// valid and written.
#[test]
fn every_removal_deletes_what_it_lists_on_every_sample() {
    let mut checked = 0;
    for name in SAMPLES {
        let document = sample(name);
        let before = view(&document, "en");
        let diagnostics = sacm::validation::validate(&document).len();
        let all = nodes(&before);
        for kind in [
            NodeKind::Goal,
            NodeKind::Strategy,
            NodeKind::Solution,
            NodeKind::Context,
            NodeKind::Assumption,
            NodeKind::Justification,
            NodeKind::Neutral,
        ] {
            for node in before.nodes.iter().filter(|node| node.kind == kind).take(3) {
                let what = format!("{name} {}", node.id);
                let mut edited = document.clone();
                let Ok(plan) = removal(&document, &before, &node.id) else {
                    assert!(matches!(
                        remove(&mut edited, &before, &node.id),
                        Err(AddError::Refused(_))
                    ));
                    assert!(edited == document, "{what}");
                    continue;
                };
                remove(&mut edited, &before, &node.id)
                    .unwrap_or_else(|error| panic!("{what}: {error}"));
                let after = view(&edited, "en");
                let mut left = all.clone();
                for gone in &plan.nodes {
                    left.remove(gone);
                }
                assert_eq!(nodes(&after), left, "{what}");
                for finding in &after.findings {
                    assert!(before.findings.contains(finding), "{what}: {finding:?}");
                }
                assert!(
                    sacm::validation::validate(&edited).len() <= diagnostics,
                    "{what}: new validation diagnostics"
                );
                assert!(
                    xmi::write(&edited, &WriteOptions::default()).is_ok(),
                    "{what}: not written"
                );
                checked += 1;
            }
        }
    }
    assert!(checked > 30, "only {checked} removals were checked");
}
