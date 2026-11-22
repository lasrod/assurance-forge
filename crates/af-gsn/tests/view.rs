//! The GSN view against the reading rules of `docs/standards/sacm-gsn-mapping.md`
//! ("Reading a SACM 2.3 file as GSN") and the plan's expected view.

use std::collections::HashSet;

use af_gsn::view::{Code, EdgeKind, NodeKind, View, view};
use sacm::model::{Document, ElementKind as SacmKind, MetamodelPackage};
use sacm::xmi::{self, Mode, ReadOptions};

const SAMPLES: [&str; 10] = [
    "sacm_example_core_argument.sacm.xmi",
    "sample.sacm.xmi",
    "open-autonomy-safety-case.updated.v2.sacm.xmi",
    "oasc-ja.sacm.xmi",
    "safety-case-sanitized-strict.sacm.xmi",
    // The layout gallery (testdata/sacm/README.md).
    "gallery-shared-support.sacm.xmi",
    "gallery-attachments.sacm.xmi",
    "gallery-long-text.sacm.xmi",
    "gallery-japanese.sacm.xmi",
    "gallery-roots-and-cycle.sacm.xmi",
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

/// An argument element of `kind` with `attributes` and `body`, named after its id.
fn element(kind: &str, id: &str, attributes: &str, body: &str) -> String {
    format!(
        r#"<argumentElement xmi:type="sacm:{kind}" xmi:id="{id}"{attributes}><name content="{id}"/>{body}</argumentElement>"#
    )
}

fn claim(id: &str, attributes: &str) -> String {
    element("Claim", id, attributes, "")
}

fn reference(id: &str) -> String {
    element("ArtifactReference", id, "", "")
}

/// A tagged value; each gets a fresh id, so an element may have several.
fn tagged(key: &str, value: &str) -> String {
    use std::sync::atomic::{AtomicUsize, Ordering};
    static NEXT: AtomicUsize = AtomicUsize::new(0);
    let n = NEXT.fetch_add(1, Ordering::Relaxed);
    format!(
        r#"<taggedValue xmi:id="tv_{n}"><key><value content="{key}"/></key><content><value content="{value}"/></content></taggedValue>"#
    )
}

fn kind_of(view: &View, id: &str) -> NodeKind {
    match view.nodes.iter().find(|node| node.id == id) {
        Some(node) => node.kind,
        None => panic!("no node {id} in {view:#?}"),
    }
}

fn codes_of(view: &View, id: &str) -> Vec<Code> {
    view.findings
        .iter()
        .filter(|finding| finding.element == id)
        .map(|finding| finding.code)
        .collect()
}

fn edges(view: &View) -> Vec<(String, String, EdgeKind, String)> {
    view.edges
        .iter()
        .map(|edge| {
            (
                edge.from.clone(),
                edge.to.clone(),
                edge.kind,
                edge.relationship.clone(),
            )
        })
        .collect()
}

fn edge(
    from: &str,
    to: &str,
    kind: EdgeKind,
    relationship: &str,
) -> (String, String, EdgeKind, String) {
    (from.into(), to.into(), kind, relationship.into())
}

/// D4: every argument element is a node, the relationship of an edge, or
/// unplaced, and only one of those.
fn assert_each_element_once(document: &Document, view: &View, what: &str) {
    let expected: Vec<&str> = document
        .elements()
        .filter(|element| {
            // Independent of the view's own definition: every kind of the
            // Argumentation package but a plain ArgumentPackage.
            element.kind().package() == MetamodelPackage::Argumentation
                && element.kind() != SacmKind::ArgumentPackage
        })
        .map(|element| element.id().as_str())
        .collect();
    let nodes: Vec<&str> = view.nodes.iter().map(|node| node.id.as_str()).collect();
    let relationships: HashSet<&str> = view
        .edges
        .iter()
        .map(|edge| edge.relationship.as_str())
        .collect();
    let unplaced: Vec<&str> = view.unplaced.iter().map(|item| item.id.as_str()).collect();

    let mut seen: HashSet<&str> = HashSet::new();
    for id in nodes
        .iter()
        .chain(relationships.iter())
        .chain(unplaced.iter())
    {
        assert!(
            seen.insert(id),
            "{what}: {id} appears more than once in the view"
        );
    }
    let expected_set: HashSet<&str> = expected.iter().copied().collect();
    let missing: Vec<&&str> = expected.iter().filter(|id| !seen.contains(*id)).collect();
    let extra: Vec<&&str> = seen
        .iter()
        .filter(|id| !expected_set.contains(*id))
        .collect();
    assert!(missing.is_empty(), "{what}: not in the view: {missing:?}");
    assert!(
        extra.is_empty(),
        "{what}: in the view but not argument elements: {extra:?}"
    );
}

// ===== The plan's expected view and D4 =====

#[test]
fn the_core_sample_draws_the_expected_view() {
    let document = sample("sacm_example_core_argument.sacm.xmi");
    let view = view(&document, "en");

    let nodes: Vec<(&str, NodeKind, bool)> = view
        .nodes
        .iter()
        .map(|node| (node.id.as_str(), node.kind, node.undeveloped))
        .collect();
    assert_eq!(
        nodes,
        [
            ("cl_top", NodeKind::Goal, false),
            ("cl_sub_1", NodeKind::Goal, false),
            ("cl_sub_2", NodeKind::Goal, true),
            ("cl_assumption_1", NodeKind::Assumption, false),
            ("ar_1", NodeKind::Strategy, false),
            ("ctx_1", NodeKind::Context, false),
            ("ev_1", NodeKind::Solution, false),
            ("ev_2", NodeKind::Solution, false),
        ]
    );
    assert_eq!(
        edges(&view),
        [
            edge("cl_top", "ar_1", EdgeKind::SupportedBy, "inf_1"),
            edge("ar_1", "cl_sub_1", EdgeKind::SupportedBy, "inf_1"),
            edge("ar_1", "cl_sub_2", EdgeKind::SupportedBy, "inf_1"),
            edge(
                "cl_sub_1",
                "cl_assumption_1",
                EdgeKind::SupportedBy,
                "inf_2"
            ),
            edge("cl_top", "ctx_1", EdgeKind::InContextOf, "acx_1"),
            edge("cl_sub_1", "ev_1", EdgeKind::SupportedBy, "ae_1"),
            edge("cl_sub_2", "ev_2", EdgeKind::SupportedBy, "ae_2"),
        ]
    );
    assert!(view.unplaced.is_empty(), "{:?}", view.unplaced);
    assert!(view.findings.is_empty(), "{:?}", view.findings);
    assert_each_element_once(&document, &view, "core sample");
}

#[test]
fn d4_every_argument_element_of_every_sample_appears_once() {
    for name in SAMPLES {
        let document = sample(name);
        let view = view(&document, "en");
        assert_each_element_once(&document, &view, name);
        assert!(!view.nodes.is_empty(), "{name}: no nodes");
    }
}

#[test]
fn the_large_case_reads_as_its_names_say() {
    let document = sample("safety-case-sanitized-strict.sacm.xmi");
    let view = view(&document, "en");
    let count = |kind| view.nodes.iter().filter(|node| node.kind == kind).count();
    // R5: all 13 axiomatic Claims are justifications, J_nnn.
    assert_eq!(count(NodeKind::Justification), 13);
    for node in view
        .nodes
        .iter()
        .filter(|node| node.kind == NodeKind::Justification)
    {
        let name = node.name.as_deref().unwrap_or_default();
        assert!(name.starts_with("J_"), "{} is named {name}", node.id);
    }
    // R11: the 20 ArtifactReferences nothing reaches are neutral.
    assert_eq!(count(NodeKind::Neutral), 20);
    assert_eq!(count(NodeKind::Solution), 64);
    assert_eq!(count(NodeKind::Context), 25);
    // E4: context for a Strategy.
    let strategy_context = view
        .edges
        .iter()
        .filter(|edge| {
            edge.kind == EdgeKind::InContextOf && kind_of(&view, &edge.from) == NodeKind::Strategy
        })
        .count();
    assert_eq!(strategy_context, 12);
}

// ===== Node text (T1 to T3) =====

#[test]
fn t1_to_t3_text_follows_the_interface_language() {
    let document = sample("oasc-ja.sacm.xmi");
    let english = view(&document, "en");
    let japanese = view(&document, "ja");
    let g1 = |view: &View| view.nodes.iter().find(|node| node.id == "G1").cloned();
    let (Some(en), Some(ja)) = (g1(&english), g1(&japanese)) else {
        panic!("setup: G1 is in oasc-ja");
    };
    assert_eq!(en.identifier, "G1");
    assert_eq!(en.name.as_deref(), Some("Goal 1"));
    assert_eq!(
        en.statement.as_deref(),
        Some("The autonomous vehicle is safe enough to deploy.")
    );
    // T2: the name holds one language; `sacm.import.name` is not read.
    assert_eq!(ja.name.as_deref(), Some("Goal 1"));
    // T3: the first description in the interface language, not the second.
    assert_eq!(
        ja.statement.as_deref(),
        Some("自動運転車は導入するのに十分安全である。")
    );
}

#[test]
fn t1_the_gsn_identifier_tag_wins_over_the_sacm_id() {
    let document = case(&element(
        "Claim",
        "c1",
        "",
        &tagged("assuranceForge.gsn.identifier", "G7"),
    ));
    let view = view(&document, "en");
    assert_eq!(view.nodes[0].identifier, "G7");
}

// ===== Nodes =====

#[test]
fn r2_to_r5_and_r7_a_claim_is_drawn_by_its_declaration() {
    let document = case(
        &[
            claim("asserted", ""),
            claim("needs", r#" assertionDeclaration="needsSupport""#),
            claim("assumed", r#" assertionDeclaration="assumed""#),
            claim("axiomatic", r#" assertionDeclaration="axiomatic""#),
            claim("defeated", r#" assertionDeclaration="defeated""#),
            claim("cited", r#" assertionDeclaration="asCited""#),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    assert_eq!(kind_of(&view, "asserted"), NodeKind::Goal);
    assert_eq!(kind_of(&view, "needs"), NodeKind::Goal);
    assert!(view.nodes[1].undeveloped);
    assert!(!view.nodes[0].undeveloped);
    assert_eq!(kind_of(&view, "assumed"), NodeKind::Assumption);
    assert_eq!(kind_of(&view, "axiomatic"), NodeKind::Justification);
    assert_eq!(kind_of(&view, "defeated"), NodeKind::Neutral);
    assert_eq!(codes_of(&view, "defeated"), [Code::DeclarationHasNoShape]);
    assert_eq!(kind_of(&view, "cited"), NodeKind::Neutral);
    assert_eq!(codes_of(&view, "cited"), [Code::DeclarationHasNoShape]);
    assert_each_element_once(&document, &view, "declarations");
}

#[test]
fn r5_and_r6_how_a_claim_is_reached_does_not_change_it() {
    let document = case(
        &[
            claim("goal", ""),
            claim("j", r#" assertionDeclaration="axiomatic""#),
            claim("as_context", ""),
            element("AssertedContext", "c1", r#" source="j" target="goal""#, ""),
            element(
                "AssertedContext",
                "c2",
                r#" source="as_context" target="goal""#,
                "",
            ),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    assert_eq!(kind_of(&view, "j"), NodeKind::Justification);
    assert_eq!(kind_of(&view, "as_context"), NodeKind::Goal);
    assert!(edges(&view).contains(&edge("goal", "as_context", EdgeKind::InContextOf, "c2")));
    assert!(view.findings.is_empty(), "{:?}", view.findings);
}

#[test]
fn r9_to_r11_an_artifact_reference_is_drawn_by_how_it_is_reached() {
    let document = case(
        &[
            claim("goal", ""),
            reference("evidence"),
            reference("context"),
            reference("both"),
            reference("neither"),
            reference("premise"),
            element(
                "AssertedEvidence",
                "e1",
                r#" source="evidence both" target="goal""#,
                "",
            ),
            element(
                "AssertedContext",
                "c1",
                r#" source="context both" target="goal""#,
                "",
            ),
            element(
                "AssertedInference",
                "i1",
                r#" source="premise" target="goal""#,
                "",
            ),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    assert_eq!(kind_of(&view, "evidence"), NodeKind::Solution);
    assert_eq!(kind_of(&view, "context"), NodeKind::Context);
    for id in ["both", "neither", "premise"] {
        assert_eq!(kind_of(&view, id), NodeKind::Neutral, "{id}");
        assert_eq!(
            codes_of(&view, id),
            [Code::ArtifactReferenceNotPlaced],
            "{id}"
        );
    }
    assert_each_element_once(&document, &view, "artifact references");
}

#[test]
fn reached_counts_only_live_relationships() {
    // A counter AssertedEvidence does not make its source a Solution, and an
    // AssertedArtifactSupport does not stop one being a Solution.
    let document = case(
        &[
            claim("goal", ""),
            reference("evidence"),
            reference("other"),
            reference("counter"),
            element(
                "AssertedEvidence",
                "e1",
                r#" source="evidence" target="goal""#,
                "",
            ),
            element(
                "AssertedEvidence",
                "e2",
                r#" source="counter" target="goal" isCounter="true""#,
                "",
            ),
            element(
                "AssertedArtifactSupport",
                "s1",
                r#" source="evidence" target="other""#,
                "",
            ),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    assert_eq!(kind_of(&view, "evidence"), NodeKind::Solution);
    assert_eq!(kind_of(&view, "counter"), NodeKind::Neutral);
    assert_eq!(kind_of(&view, "other"), NodeKind::Neutral);
    assert_eq!(view.unplaced.len(), 1);
    assert_eq!(view.unplaced[0].id, "s1");
    assert_eq!(view.unplaced[0].code, Code::ArtifactRelationshipNotDrawn);
    assert_each_element_once(&document, &view, "reached");
}

#[test]
fn r1_a_recorded_type_is_used_when_it_agrees() {
    let goal_type = "{http://scsc.acwg.gsn/2.0}Goal";
    let context_type = "{http://scsc.acwg.gsn/2.0}Context";
    let document = case(
        &[
            // Agrees.
            element(
                "Claim",
                "role",
                r#" assertionDeclaration="axiomatic""#,
                &tagged("assuranceForge.gsn.role", "Justification"),
            ),
            element(
                "Claim",
                "legacy",
                r#" assertionDeclaration="axiomatic""#,
                &tagged("sacm.import.assertionDeclaration", "justification"),
            ),
            element(
                "ArtifactReference",
                "recorded_context",
                "",
                &tagged("sacm.import.extensionType", context_type),
            ),
            element(
                "Claim",
                "recorded_goal",
                r#" assertionDeclaration="needsSupport""#,
                &tagged("sacm.import.extensionType", goal_type),
            ),
            // Disagrees: a Goal recorded on an assumed Claim, a Context on a Claim.
            element(
                "Claim",
                "wrong",
                r#" assertionDeclaration="assumed""#,
                &tagged("sacm.import.extensionType", goal_type),
            ),
            element(
                "Claim",
                "context_claim",
                r#" assertionDeclaration="axiomatic""#,
                &tagged("sacm.import.extensionType", context_type),
            ),
            // A type this step does not draw.
            element(
                "Claim",
                "away",
                "",
                &tagged(
                    "sacm.import.extensionType",
                    "{http://scsc.acwg.gsn/2.0}AwayGoal",
                ),
            ),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    assert_eq!(kind_of(&view, "role"), NodeKind::Justification);
    assert_eq!(kind_of(&view, "legacy"), NodeKind::Justification);
    // Reached by nothing, so R11 would make it neutral: the recorded type wins.
    assert_eq!(kind_of(&view, "recorded_context"), NodeKind::Context);
    assert_eq!(kind_of(&view, "recorded_goal"), NodeKind::Goal);
    assert!(
        view.nodes
            .iter()
            .any(|node| node.id == "recorded_goal" && node.undeveloped)
    );
    for id in ["role", "legacy", "recorded_context", "recorded_goal"] {
        assert!(
            codes_of(&view, id).is_empty(),
            "{id}: {:?}",
            codes_of(&view, id)
        );
    }
    assert_eq!(kind_of(&view, "wrong"), NodeKind::Assumption);
    assert_eq!(kind_of(&view, "context_claim"), NodeKind::Justification);
    assert_eq!(kind_of(&view, "away"), NodeKind::Goal);
    for id in ["wrong", "context_claim", "away"] {
        assert_eq!(codes_of(&view, id), [Code::RecordedTypeNotUsed], "{id}");
    }
}

#[test]
fn r12_and_r13_citations_are_neutral_and_abstract_elements_say_so() {
    let document = case(
        &[
            claim("target", ""),
            claim("away", r#" isCitation="true" citedElement="target""#),
            claim("pattern", r#" isAbstract="true""#),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    assert_eq!(kind_of(&view, "away"), NodeKind::Neutral);
    assert_eq!(codes_of(&view, "away"), [Code::Citation]);
    assert_eq!(kind_of(&view, "pattern"), NodeKind::Goal);
    assert_eq!(codes_of(&view, "pattern"), [Code::UninstantiatedNotDrawn]);
}

#[test]
fn r14_and_r15_groups_interfaces_and_their_contents_are_not_drawn() {
    let document = case(
        &[
            claim("c1", ""),
            claim("c2", ""),
            element("ArgumentGroup", "g1", r#" argumentElement="c1 c2""#, ""),
            element(
                "ArgumentPackageInterface",
                "i1",
                r#" implements="p""#,
                &[
                    claim("inside", r#" isCitation="true" citedElement="c1""#),
                    claim("inside2", ""),
                ]
                .concat(),
            ),
            // E7: an end that names an element the view does not draw.
            element(
                "AssertedInference",
                "into_interface",
                r#" source="inside2" target="c1""#,
                "",
            ),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    assert_eq!(
        view.nodes
            .iter()
            .map(|node| node.id.as_str())
            .collect::<Vec<_>>(),
        ["c1", "c2"]
    );
    let unplaced: Vec<(&str, Code)> = view
        .unplaced
        .iter()
        .map(|item| (item.id.as_str(), item.code))
        .collect();
    assert_eq!(
        unplaced,
        [
            ("g1", Code::KindNotDrawn),
            ("i1", Code::KindNotDrawn),
            ("inside", Code::KindNotDrawn),
            ("inside2", Code::KindNotDrawn),
            ("into_interface", Code::EndNotPlaced),
        ]
    );
    assert!(codes_of(&view, "inside").is_empty(), "R15 comes before R12");
    assert_each_element_once(&document, &view, "groups and interfaces");
}

#[test]
fn a_nested_package_is_drawn_as_its_elements() {
    let document = case(&format!(
        r#"{}<argumentElement xmi:type="sacm:ArgumentPackage" xmi:id="module"><name content="M"/>{}</argumentElement>"#,
        claim("outer", ""),
        claim("inner", "")
    ));
    let view = view(&document, "en");
    assert_eq!(
        view.nodes
            .iter()
            .map(|node| node.id.as_str())
            .collect::<Vec<_>>(),
        ["outer", "inner"]
    );
    assert_each_element_once(&document, &view, "nested package");
}

// ===== Edges =====

#[test]
fn e2_and_e3_support_without_a_strategy_runs_from_conclusion_to_premise() {
    let document = case(
        &[
            claim("top", ""),
            claim("sub", ""),
            reference("ev"),
            element(
                "AssertedInference",
                "i1",
                r#" source="sub" target="top""#,
                "",
            ),
            element("AssertedEvidence", "e1", r#" source="ev" target="sub""#, ""),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    assert_eq!(
        edges(&view),
        [
            edge("top", "sub", EdgeKind::SupportedBy, "i1"),
            edge("sub", "ev", EdgeKind::SupportedBy, "e1"),
        ]
    );
}

#[test]
fn e5_a_counter_relationship_points_at_what_it_challenges() {
    let document = case(
        &[
            claim("goal", ""),
            claim("counter_claim", ""),
            element("ArgumentReasoning", "r", "", ""),
            element(
                "AssertedInference",
                "i1",
                r#" source="counter_claim" target="goal" reasoning="r" isCounter="true""#,
                "",
            ),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    assert_eq!(
        edges(&view),
        [edge("counter_claim", "goal", EdgeKind::Challenge, "i1")]
    );
    assert_eq!(codes_of(&view, "i1"), [Code::EndNotPlaced]);
    assert_eq!(
        view.findings
            .iter()
            .find(|f| f.element == "i1" && f.code == Code::EndNotPlaced)
            .and_then(|f| f.end.as_deref()),
        Some("r")
    );
}

#[test]
fn e5_a_defeated_challenge_is_drawn_as_a_challenge_with_both_codes() {
    let document = case(&[
        claim("goal", ""),
        claim("counter_claim", ""),
        element("AssertedInference", "i1", r#" source="counter_claim" target="goal" isCounter="true" assertionDeclaration="defeated""#, ""),
    ]
    .concat());
    let view = view(&document, "en");
    assert_eq!(
        edges(&view),
        [edge("counter_claim", "goal", EdgeKind::Challenge, "i1")]
    );
    assert_eq!(codes_of(&view, "i1"), [Code::Defeated]);
}

#[test]
fn e5a_and_e9_defeated_and_cited_support_keep_the_gsn_direction_and_the_strategy() {
    let document = case(
        &[
            claim("top", ""),
            claim("sub", ""),
            element("ArgumentReasoning", "r", "", ""),
            element(
                "AssertedInference",
                "defeated",
                r#" source="sub" target="top" reasoning="r" assertionDeclaration="defeated""#,
                "",
            ),
            element(
                "AssertedInference",
                "cited",
                r#" source="sub" target="top" isCitation="true""#,
                "",
            ),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    assert_eq!(
        edges(&view),
        [
            edge("top", "r", EdgeKind::Neutral, "defeated"),
            edge("r", "sub", EdgeKind::Neutral, "defeated"),
            edge("top", "sub", EdgeKind::Neutral, "cited"),
        ]
    );
    assert_eq!(codes_of(&view, "defeated"), [Code::Defeated]);
    assert_eq!(codes_of(&view, "cited"), [Code::Citation]);
}

#[test]
fn e6_a_declaration_on_a_relationship_is_coded_and_the_edge_stays_live() {
    let document = case(
        &[
            claim("top", ""),
            claim("sub", ""),
            element(
                "AssertedInference",
                "i1",
                r#" source="sub" target="top" assertionDeclaration="assumed""#,
                "",
            ),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    assert_eq!(
        edges(&view),
        [edge("top", "sub", EdgeKind::SupportedBy, "i1")]
    );
    assert_eq!(
        codes_of(&view, "i1"),
        [Code::RelationshipDeclarationNotDrawn]
    );
}

#[test]
fn e7_the_ends_that_can_be_placed_keep_their_edges() {
    let document = case(
        &[
            claim("top", ""),
            claim("sub", ""),
            element(
                "AssertedInference",
                "partly",
                r#" source="sub gone" target="top""#,
                "",
            ),
            element(
                "AssertedInference",
                "no_target",
                r#" source="sub" target="nowhere""#,
                "",
            ),
            element(
                "AssertedEvidence",
                "about_relationship",
                r#" source="sub" target="partly""#,
                "",
            ),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    assert_eq!(
        edges(&view),
        [edge("top", "sub", EdgeKind::SupportedBy, "partly")]
    );
    let ends = |id: &str| -> Vec<Option<String>> {
        view.findings
            .iter()
            .filter(|f| f.element == id && f.code == Code::EndNotPlaced)
            .map(|f| f.end.clone())
            .collect()
    };
    assert_eq!(ends("partly"), [Some("gone".to_owned())]);
    assert_eq!(ends("no_target"), [Some("nowhere".to_owned())]);
    assert_eq!(ends("about_relationship"), [Some("partly".to_owned())]);
    let unplaced: Vec<&str> = view.unplaced.iter().map(|item| item.id.as_str()).collect();
    assert_eq!(unplaced, ["no_target", "about_relationship"]);
    assert_each_element_once(&document, &view, "unresolved ends");
}

#[test]
fn e8_an_inference_whose_reasoning_cannot_be_placed_is_drawn_directly() {
    let document = case(
        &[
            claim("top", ""),
            claim("sub", ""),
            element(
                "AssertedInference",
                "i1",
                r#" source="sub" target="top" reasoning="missing""#,
                "",
            ),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    assert_eq!(
        edges(&view),
        [edge("top", "sub", EdgeKind::SupportedBy, "i1")]
    );
    assert_eq!(codes_of(&view, "i1"), [Code::EndNotPlaced]);
}

#[test]
fn e1_one_strategy_named_by_two_inferences_is_one_node() {
    let document = case(
        &[
            claim("top", ""),
            claim("a", ""),
            claim("b", ""),
            element("ArgumentReasoning", "r", "", ""),
            element(
                "AssertedInference",
                "i1",
                r#" source="a" target="top" reasoning="r""#,
                "",
            ),
            element(
                "AssertedInference",
                "i2",
                r#" source="b" target="top" reasoning="r""#,
                "",
            ),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    assert_eq!(view.nodes.iter().filter(|node| node.id == "r").count(), 1);
    assert_eq!(
        edges(&view),
        [
            edge("top", "r", EdgeKind::SupportedBy, "i1"),
            edge("r", "a", EdgeKind::SupportedBy, "i1"),
            edge("top", "r", EdgeKind::SupportedBy, "i2"),
            edge("r", "b", EdgeKind::SupportedBy, "i2"),
        ]
    );
}

#[test]
fn e11_a_meta_claim_is_coded_on_its_assertion() {
    let document = case(
        &[
            claim("top", r#" metaClaim="confidence""#),
            claim("confidence", ""),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    assert_eq!(codes_of(&view, "top"), [Code::MetaClaimNotDrawn]);
    assert_eq!(kind_of(&view, "confidence"), NodeKind::Goal);
}

#[test]
fn e8_a_reasoning_on_evidence_is_not_linked() {
    let document = case(
        &[
            claim("goal", ""),
            reference("ev"),
            element("ArgumentReasoning", "r", "", ""),
            element(
                "AssertedEvidence",
                "e1",
                r#" source="ev" target="goal" reasoning="r""#,
                "",
            ),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    assert_eq!(
        edges(&view),
        [edge("goal", "ev", EdgeKind::SupportedBy, "e1")]
    );
    assert_eq!(codes_of(&view, "e1"), [Code::EndNotPlaced]);
    assert_eq!(kind_of(&view, "ev"), NodeKind::Solution);
}

// ===== Found by the independent review of the code =====

fn inference(id: &str, attributes: &str) -> String {
    element("AssertedInference", id, attributes, "")
}

#[test]
fn e5_a_cited_challenge_is_still_drawn_as_a_challenge() {
    let document = case(
        &[
            claim("goal", ""),
            claim("against", ""),
            inference(
                "i1",
                r#" source="against" target="goal" isCounter="true" isCitation="true""#,
            ),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    assert_eq!(
        edges(&view),
        [edge("against", "goal", EdgeKind::Challenge, "i1")]
    );
    assert_eq!(codes_of(&view, "i1"), [Code::Citation]);
}

#[test]
fn every_code_whose_condition_holds_is_sent() {
    let document = case(
        &[
            claim("top", ""),
            claim("sub", ""),
            inference(
                "counter_assumed",
                r#" source="sub" target="top" isCounter="true" assertionDeclaration="assumed""#,
            ),
            inference(
                "cited_defeated",
                r#" source="sub" target="top" isCitation="true" assertionDeclaration="defeated""#,
            ),
            inference(
                "abstract_meta",
                r#" source="sub" target="top" isAbstract="true" metaClaim="sub""#,
            ),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    assert_eq!(
        codes_of(&view, "counter_assumed"),
        [Code::RelationshipDeclarationNotDrawn]
    );
    assert_eq!(
        codes_of(&view, "cited_defeated"),
        [Code::Citation, Code::Defeated]
    );
    assert_eq!(
        codes_of(&view, "abstract_meta"),
        [Code::UninstantiatedNotDrawn, Code::MetaClaimNotDrawn]
    );
    assert!(
        edges(&view).contains(&edge("top", "sub", EdgeKind::SupportedBy, "abstract_meta")),
        "an abstract relationship with a metaClaim is still live support"
    );
}

#[test]
fn e6_every_other_declaration_on_a_relationship_is_coded() {
    let literals = ["needsSupport", "assumed", "axiomatic", "asCited"];
    let body: String = [claim("top", ""), claim("sub", "")]
        .into_iter()
        .chain(literals.iter().map(|literal| {
            inference(
                literal,
                &format!(r#" source="sub" target="top" assertionDeclaration="{literal}""#),
            )
        }))
        .collect();
    let view = view(&case(&body), "en");
    for literal in literals {
        assert_eq!(
            codes_of(&view, literal),
            [Code::RelationshipDeclarationNotDrawn],
            "{literal}"
        );
        assert!(edges(&view).contains(&edge("top", "sub", EdgeKind::SupportedBy, literal)));
    }
}

#[test]
fn e8_a_reasoning_that_names_no_argument_reasoning_is_not_a_strategy() {
    let document = case(
        &[
            claim("top", ""),
            claim("sub", ""),
            claim("not_reasoning", ""),
            inference(
                "i1",
                r#" source="sub" target="top" reasoning="not_reasoning""#,
            ),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    assert_eq!(
        edges(&view),
        [edge("top", "sub", EdgeKind::SupportedBy, "i1")]
    );
    assert_eq!(codes_of(&view, "i1"), [Code::EndNotPlaced]);
}

#[test]
fn e10_comes_before_e9_and_r12_before_r1() {
    let document = case(
        &[
            reference("a"),
            reference("b"),
            element(
                "AssertedArtifactSupport",
                "cited_support",
                r#" source="a" target="b" isCitation="true""#,
                "",
            ),
            element(
                "Claim",
                "cited_goal",
                r#" isCitation="true" citedElement="a""#,
                &tagged(
                    "sacm.import.extensionType",
                    "{http://scsc.acwg.gsn/2.0}Goal",
                ),
            ),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    assert_eq!(view.unplaced.len(), 1);
    assert_eq!(view.unplaced[0].code, Code::ArtifactRelationshipNotDrawn);
    assert!(codes_of(&view, "cited_support").is_empty());
    assert_eq!(kind_of(&view, "cited_goal"), NodeKind::Neutral);
    assert_eq!(codes_of(&view, "cited_goal"), [Code::Citation]);
}

#[test]
fn r1_every_gsn_namespace_counts_conflicts_do_not_and_other_tags_are_ignored() {
    let document = case(
        &[
            // Reached by nothing, so only the recorded type makes it a Context.
            element(
                "ArtifactReference",
                "acwg",
                "",
                &tagged(
                    "sacm.import.extensionType",
                    "{http://acwg.org/3.0/gsn}Context",
                ),
            ),
            element(
                "ArtifactReference",
                "acme",
                "",
                &tagged(
                    "sacm.import.extensionType",
                    "{http://org.eclipse.acme/1.0/gsn}Solution",
                ),
            ),
            element(
                "Claim",
                "conflict",
                r#" assertionDeclaration="axiomatic""#,
                &[
                    tagged("assuranceForge.gsn.role", "Justification"),
                    tagged(
                        "sacm.import.extensionType",
                        "{http://scsc.acwg.gsn/2.0}Goal",
                    ),
                ]
                .concat(),
            ),
            element(
                "Claim",
                "not_gsn",
                "",
                &[
                    tagged("sacm.import.extensionType", "{http://example.org/cae}Claim"),
                    tagged("assuranceForge.gsn.role", "Goal"),
                ]
                .concat(),
            ),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    assert_eq!(kind_of(&view, "acwg"), NodeKind::Context);
    assert_eq!(kind_of(&view, "acme"), NodeKind::Solution);
    assert!(
        codes_of(&view, "acme").is_empty(),
        "the recorded type beats R11"
    );
    assert_eq!(kind_of(&view, "conflict"), NodeKind::Justification);
    assert_eq!(codes_of(&view, "conflict"), [Code::RecordedTypeNotUsed]);
    assert_eq!(kind_of(&view, "not_gsn"), NodeKind::Goal);
    assert!(codes_of(&view, "not_gsn").is_empty());
}

#[test]
fn r15_the_contents_of_a_binding_are_not_drawn() {
    let document = case(
        &[
            claim("c1", ""),
            element(
                "ArgumentPackageBinding",
                "b1",
                r#" participantPackage="p p""#,
                &claim("bound", ""),
            ),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    let nodes: Vec<&str> = view.nodes.iter().map(|node| node.id.as_str()).collect();
    assert_eq!(nodes, ["c1"]);
    let unplaced: Vec<&str> = view.unplaced.iter().map(|item| item.id.as_str()).collect();
    assert_eq!(unplaced, ["b1", "bound"]);
    assert_each_element_once(&document, &view, "binding");
}

#[test]
fn r11_evidence_that_is_also_reached_otherwise_is_neutral() {
    let document = case(
        &[
            claim("goal", ""),
            reference("ev"),
            element(
                "AssertedEvidence",
                "e1",
                r#" source="ev" target="goal""#,
                "",
            ),
            inference("i1", r#" source="goal" target="ev""#),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    assert_eq!(kind_of(&view, "ev"), NodeKind::Neutral);
}

#[test]
fn t3_an_exact_tag_beats_a_primary_subtag_and_empty_values_are_skipped() {
    let document = case(&element(
        "Claim",
        "c1",
        "",
        r#"<description xmi:id="d1"><content><value lang="en" content=""/><value lang="en-GB" content="colour"/><value lang="en-US" content="color"/></content></description>"#,
    ));
    assert_eq!(
        view(&document, "en-US").nodes[0].statement.as_deref(),
        Some("color")
    );
    assert_eq!(
        view(&document, "en").nodes[0].statement.as_deref(),
        Some("colour")
    );
}

#[test]
fn findings_come_in_document_order() {
    let document = case(
        &[
            claim("first", r#" isAbstract="true""#),
            claim("top", ""),
            inference(
                "later",
                r#" source="first" target="top" assertionDeclaration="assumed""#,
            ),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    let order: Vec<&str> = view.findings.iter().map(|f| f.element.as_str()).collect();
    assert_eq!(order, ["first", "later"]);
}

#[test]
fn e7_a_relationship_with_no_source_and_one_with_a_repeated_source() {
    let document = case(
        &[
            claim("top", ""),
            claim("sub", ""),
            inference("no_source", r#" target="top""#),
            inference("twice", r#" source="sub sub" target="top""#),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    let no_source: Vec<(Code, Option<String>)> = view
        .findings
        .iter()
        .filter(|f| f.element == "no_source")
        .map(|f| (f.code, f.end.clone()))
        .collect();
    assert_eq!(no_source, [(Code::EndNotPlaced, None)]);
    assert_eq!(view.unplaced[0].id, "no_source");
    assert_eq!(
        edges(&view),
        [edge("top", "sub", EdgeKind::SupportedBy, "twice")]
    );
}

#[test]
fn e12_a_strategy_structure_is_coded() {
    let document = case(&element("ArgumentReasoning", "r", r#" structure="p""#, ""));
    let view = view(&document, "en");
    assert_eq!(kind_of(&view, "r"), NodeKind::Strategy);
    assert_eq!(codes_of(&view, "r"), [Code::StructureNotDrawn]);
}

#[test]
fn e5_a_challenge_may_point_at_a_relationship_that_is_drawn() {
    let document = case(
        &[
            claim("top", ""),
            claim("sub", ""),
            claim("against", ""),
            claim("against_that", ""),
            element(
                "AssertedInference",
                "i1",
                r#" source="sub" target="top""#,
                "",
            ),
            // At a relationship, and at the challenge of it.
            element(
                "AssertedInference",
                "k1",
                r#" source="against" target="i1" isCounter="true""#,
                "",
            ),
            element(
                "AssertedInference",
                "k2",
                r#" source="against_that" target="k1" isCounter="true""#,
                "",
            ),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    assert_eq!(
        edges(&view),
        [
            edge("top", "sub", EdgeKind::SupportedBy, "i1"),
            edge("against", "i1", EdgeKind::Challenge, "k1"),
            edge("against_that", "k1", EdgeKind::Challenge, "k2"),
        ]
    );
    let at_relationship: Vec<bool> = view.edges.iter().map(|edge| edge.to_relationship).collect();
    assert_eq!(at_relationship, [false, true, true]);
    assert!(view.findings.is_empty(), "{:?}", view.findings);
    assert!(view.unplaced.is_empty());
    assert_each_element_once(&document, &view, "challenges of relationships");
}

#[test]
fn e5_and_e7_a_challenge_of_a_relationship_with_no_edge_is_not_placed() {
    let document = case(
        &[
            claim("top", ""),
            claim("against", ""),
            claim("against_that", ""),
            // No end of `nothing` can be placed, so it has no edge; `k1`
            // challenges it, `k2` challenges `k1`, and `own` itself.
            element(
                "AssertedInference",
                "nothing",
                r#" source="gone" target="nowhere""#,
                "",
            ),
            element(
                "AssertedInference",
                "k1",
                r#" source="against" target="nothing" isCounter="true""#,
                "",
            ),
            element(
                "AssertedInference",
                "k2",
                r#" source="against_that" target="k1" isCounter="true""#,
                "",
            ),
            element(
                "AssertedInference",
                "own",
                r#" source="against" target="own" isCounter="true""#,
                "",
            ),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    assert!(view.edges.is_empty(), "{:?}", view.edges);
    for (id, end) in [("k1", "nothing"), ("k2", "k1"), ("own", "own")] {
        assert_eq!(codes_of(&view, id), [Code::EndNotPlaced], "{id}");
        let named = view
            .findings
            .iter()
            .find(|finding| finding.element == id)
            .and_then(|finding| finding.end.as_deref());
        assert_eq!(named, Some(end), "{id}");
        assert!(view.unplaced.iter().any(|item| item.id == id), "{id}");
    }
    assert_each_element_once(&document, &view, "challenges of nothing");
}

#[test]
fn e5_and_e7_a_challenge_whose_sources_cannot_be_placed_names_a_target_with_no_edge() {
    let document = case(
        &[
            claim("top", ""),
            claim("sub", ""),
            element(
                "AssertedInference",
                "drawn",
                r#" source="sub" target="top""#,
                "",
            ),
            element(
                "AssertedInference",
                "nothing",
                r#" source="gone" target="nowhere""#,
                "",
            ),
            // Neither challenge has a source that can be placed. The first
            // challenges a relationship with no edge, which its finding
            // names as it would were its source placed; the second one that
            // is drawn, which is not named.
            element(
                "AssertedInference",
                "of_nothing",
                r#" source="missing" target="nothing" isCounter="true""#,
                "",
            ),
            element(
                "AssertedInference",
                "of_drawn",
                r#" source="missing" target="drawn" isCounter="true""#,
                "",
            ),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    let named = |id: &str| -> Vec<Option<String>> {
        view.findings
            .iter()
            .filter(|finding| finding.element == id)
            .map(|finding| finding.end.clone())
            .collect()
    };
    assert_eq!(
        named("of_nothing"),
        [Some("missing".to_owned()), Some("nothing".to_owned())]
    );
    assert_eq!(named("of_drawn"), [Some("missing".to_owned())]);
    assert_eq!(codes_of(&view, "of_nothing"), [Code::EndNotPlaced; 2]);
    for id in ["of_nothing", "of_drawn"] {
        assert!(view.unplaced.iter().any(|item| item.id == id), "{id}");
    }
    assert_eq!(
        edges(&view),
        [edge("top", "sub", EdgeKind::SupportedBy, "drawn")]
    );
    assert_each_element_once(&document, &view, "challenges with no source");
}

#[test]
fn e7_only_a_challenge_may_point_at_a_relationship() {
    // Support for a relationship has no GSN form: it stays unplaced.
    let document = case(
        &[
            claim("top", ""),
            claim("sub", ""),
            claim("other", ""),
            element(
                "AssertedInference",
                "i1",
                r#" source="sub" target="top""#,
                "",
            ),
            element(
                "AssertedInference",
                "i2",
                r#" source="other" target="i1""#,
                "",
            ),
        ]
        .concat(),
    );
    let view = view(&document, "en");
    assert_eq!(
        edges(&view),
        [edge("top", "sub", EdgeKind::SupportedBy, "i1")]
    );
    assert_eq!(codes_of(&view, "i2"), [Code::EndNotPlaced]);
}

#[test]
fn the_retired_counter_code_is_never_sent() {
    let document = sample("../sacm/challenges.sacm.xmi");
    let view = view(&document, "en");
    assert!(
        view.edges
            .iter()
            .any(|edge| edge.kind == EdgeKind::Challenge)
    );
    assert!(
        view.findings
            .iter()
            .all(|finding| finding.code != Code::Counter)
    );
}
