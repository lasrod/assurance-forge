//! GSN well-formedness, against the problem-list plan's "Done when" 1: each
//! check reports a fixture built to break it, naming the element ids it is
//! about, and stays silent where nothing breaks it.

use af_gsn::view::view;
use af_gsn::wellformed::{Finding, Rule, check};
use sacm::model::Document;
use sacm::xmi::{self, Mode, ReadOptions};

fn read(bytes: &[u8]) -> Document {
    match xmi::read(bytes, &ReadOptions::default().with_mode(Mode::Strict)) {
        Ok(output) => output.into_document(),
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

fn element(kind: &str, id: &str, attributes: &str, body: &str) -> String {
    format!(
        r#"<argumentElement xmi:type="sacm:{kind}" xmi:id="{id}"{attributes}><name content="{id}"/>{body}</argumentElement>"#
    )
}

fn goal(id: &str) -> String {
    element("Claim", id, "", "")
}

fn claim(id: &str, declaration: &str) -> String {
    element(
        "Claim",
        id,
        &format!(r#" assertionDeclaration="{declaration}""#),
        "",
    )
}

fn strategy(id: &str) -> String {
    element("ArgumentReasoning", id, "", "")
}

/// `conclusion` SupportedBy `premise` (SACM: the premise is the source).
fn inference(id: &str, premise: &str, conclusion: &str) -> String {
    element(
        "AssertedInference",
        id,
        &format!(r#" source="{premise}" target="{conclusion}""#),
        "",
    )
}

fn inference_by(id: &str, premise: &str, conclusion: &str, reasoning: &str) -> String {
    element(
        "AssertedInference",
        id,
        &format!(r#" source="{premise}" target="{conclusion}" reasoning="{reasoning}""#),
        "",
    )
}

/// `of` InContextOf `context`.
fn context(id: &str, context: &str, of: &str) -> String {
    element(
        "AssertedContext",
        id,
        &format!(r#" source="{context}" target="{of}""#),
        "",
    )
}

/// `of` SupportedBy the Solution `solution`.
fn evidence(id: &str, solution: &str, of: &str) -> String {
    element(
        "AssertedEvidence",
        id,
        &format!(r#" source="{solution}" target="{of}""#),
        "",
    )
}

fn reference(id: &str) -> String {
    element("ArtifactReference", id, "", "")
}

fn identifier(id: &str, value: &str) -> String {
    element(
        "Claim",
        id,
        "",
        &format!(
            r#"<taggedValue xmi:id="tv_{id}"><key><value content="assuranceForge.gsn.identifier"/></key><content><value content="{value}"/></content></taggedValue>"#
        ),
    )
}

fn findings(parts: &[String]) -> Vec<Finding> {
    let document = case(&parts.concat());
    let view = view(&document, "en");
    check(&document, &view)
}

fn finding(rule: Rule, nodes: &[&str], relationship: Option<&str>) -> Finding {
    Finding {
        rule,
        nodes: nodes.iter().map(|id| (*id).to_owned()).collect(),
        relationship: relationship.map(str::to_owned),
    }
}

#[test]
fn a_well_formed_argument_has_no_findings() {
    // A Goal with context, an assumption and a justification, developed by
    // a Strategy into a Goal with a Solution, and a second Goal shared by
    // two parents (GSN3-CORE-014: a directed acyclic graph, not a tree).
    let found = findings(&[
        goal("g1"),
        goal("g2"),
        goal("g3"),
        goal("shared"),
        strategy("s"),
        reference("ctx"),
        claim("a", "assumed"),
        claim("j", "axiomatic"),
        reference("sn"),
        reference("sctx"),
        context("c1", "ctx", "g1"),
        context("c2", "a", "g1"),
        context("c3", "j", "s"),
        context("c4", "sctx", "s"),
        inference_by("i1", "g2", "g1", "s"),
        evidence("e1", "sn", "g2"),
        inference("i2", "shared", "g2"),
        inference("i3", "shared", "g3"),
        evidence("e2", "sn", "shared"),
    ]);
    assert_eq!(found, []);
}

#[test]
fn nodes_that_support_each_other_are_one_cycle_and_a_node_that_supports_itself_another() {
    let found = findings(&[
        goal("a"),
        goal("b"),
        goal("c"),
        goal("d"),
        goal("self"),
        inference("ab", "b", "a"),
        inference("bc", "c", "b"),
        inference("ca", "a", "c"),
        inference("cd", "d", "c"),
        inference("ss", "self", "self"),
    ]);
    assert_eq!(
        found,
        [
            finding(Rule::Cycle, &["a", "b", "c"], None),
            finding(Rule::Cycle, &["self"], None),
        ]
    );
}

#[test]
fn context_does_not_make_a_cycle() {
    // g1 SupportedBy g2, and g2 InContextOf g1: a circle only if context
    // were followed. g1 given as context is AF-GSN-005, not a cycle.
    let found = findings(&[
        goal("g1"),
        goal("g2"),
        inference("i1", "g2", "g1"),
        context("c1", "g1", "g2"),
    ]);
    assert_eq!(
        found,
        [finding(Rule::NotContextual, &["g2", "g1"], Some("c1"))]
    );
}

#[test]
fn a_leaf_that_is_supported_or_has_context_is_reported() {
    let found = findings(&[
        goal("g"),
        claim("a", "assumed"),
        claim("j", "axiomatic"),
        reference("ctx"),
        context("c0", "a", "g"),
        // The Assumption is supported by a Goal, and has context.
        inference("i1", "g", "a"),
        context("c1", "ctx", "j"),
        context("c2", "j", "g"),
    ]);
    assert!(
        found.contains(&finding(Rule::LeafSupported, &["a", "g"], Some("i1"))),
        "{found:?}"
    );
    assert!(
        found.contains(&finding(Rule::LeafHasContext, &["j", "ctx"], Some("c1"))),
        "{found:?}"
    );
}

#[test]
fn a_context_that_supports_and_a_goal_given_as_context_are_reported() {
    let found = findings(&[
        goal("g1"),
        goal("g2"),
        goal("g3"),
        claim("j", "axiomatic"),
        // g1 SupportedBy the Justification.
        inference("i1", "j", "g1"),
        // g3 InContextOf g2 (R6: g2 stays a Goal).
        context("c1", "g2", "g3"),
        inference("i2", "g3", "g1"),
    ]);
    assert_eq!(
        found,
        [
            finding(Rule::ContextualSupports, &["g1", "j"], Some("i1")),
            finding(Rule::NotContextual, &["g3", "g2"], Some("c1")),
        ]
    );
}

#[test]
fn a_strategy_supported_by_a_strategy_is_reported_and_so_is_the_one_at_the_end() {
    let found = findings(&[
        goal("g"),
        strategy("s"),
        strategy("s2"),
        goal("g2"),
        // g SupportedBy s SupportedBy s2: s2 is the inference's premise.
        inference_by("i1", "s2", "g", "s"),
        inference_by("i2", "g2", "g", "s2"),
    ]);
    assert!(
        found.contains(&finding(
            Rule::StrategySupportedByNonGoal,
            &["s", "s2"],
            Some("i1")
        )),
        "{found:?}"
    );
    assert!(
        found.contains(&finding(Rule::StrategyAtEnd, &["s2"], Some("i1"))),
        "{found:?}"
    );
}

#[test]
fn a_strategy_as_premise_is_at_an_end_but_one_with_context_is_not() {
    let found = findings(&[
        goal("g"),
        goal("g2"),
        strategy("s"),
        reference("ctx"),
        // g SupportedBy s, with s as the inference's premise (R8).
        inference("i1", "s", "g"),
        inference_by("i2", "g2", "g", "s"),
        // s InContextOf ctx: the Strategy's context.
        context("c1", "ctx", "s"),
    ]);
    assert_eq!(found, [finding(Rule::StrategyAtEnd, &["s"], Some("i1"))]);
}

#[test]
fn a_strategy_that_supports_nothing_is_reported() {
    let found = findings(&[goal("g"), strategy("alone")]);
    assert_eq!(
        found,
        [finding(Rule::StrategyUnsupported, &["alone"], None)]
    );
}

#[test]
fn nodes_sharing_an_identifier_are_reported_once() {
    let found = findings(&[
        identifier("one", "G1"),
        identifier("two", "G1"),
        identifier("three", "G1"),
        identifier("other", "G2"),
    ]);
    assert_eq!(
        found,
        [finding(
            Rule::DuplicateIdentifier,
            &["one", "two", "three"],
            None
        )]
    );
}

#[test]
fn an_undeveloped_element_that_is_supported_is_reported_once() {
    let found = findings(&[
        claim("g", "needsSupport"),
        goal("g2"),
        goal("g3"),
        inference("i1", "g2", "g"),
        inference("i2", "g3", "g"),
        claim("honest", "needsSupport"),
    ]);
    assert_eq!(found, [finding(Rule::UndevelopedSupported, &["g"], None)]);
}

#[test]
fn every_rule_has_a_code_a_requirement_and_a_severity() {
    let codes: Vec<&str> = Rule::ALL.iter().map(|rule| rule.code()).collect();
    let expected: Vec<String> = (1..=10).map(|n| format!("AF-GSN-{n:03}")).collect();
    assert_eq!(codes, expected);
    for rule in Rule::ALL {
        assert!(rule.requirement().starts_with("GSN3-CORE-"), "{rule:?}");
    }
    let warnings: Vec<Rule> = Rule::ALL
        .into_iter()
        .filter(|rule| !rule.is_error())
        .collect();
    assert_eq!(
        warnings,
        [Rule::StrategyUnsupported, Rule::UndevelopedSupported]
    );
}

/// A rule and the nodes a sample breaks it at.
type Break<'a> = (Rule, &'a [&'a str]);

/// What each sample breaks, checked against the file by hand: the checks
/// stay silent everywhere else.
#[test]
fn the_samples_break_what_they_break_and_nothing_else() {
    let expected: &[(&str, &[Break])] = &[
        (
            "sacm_example_core_argument.sacm.xmi",
            &[
                // An assumed Claim as a premise of an inference.
                (Rule::ContextualSupports, &["cl_sub_1", "cl_assumption_1"]),
                (Rule::UndevelopedSupported, &["cl_sub_2"]),
            ],
        ),
        (
            "sample.sacm.xmi",
            &[
                (Rule::StrategyUnsupported, &["S1"]),
                (Rule::StrategyUnsupported, &["S2"]),
            ],
        ),
        (
            "gallery-roots-and-cycle.sacm.xmi",
            &[(Rule::Cycle, &["Ca", "Cb", "Cc"]), (Rule::Cycle, &["Gs"])],
        ),
        ("gallery-shared-support.sacm.xmi", &[]),
        ("gallery-attachments.sacm.xmi", &[]),
        ("gallery-long-text.sacm.xmi", &[]),
        ("gallery-japanese.sacm.xmi", &[]),
        ("challenges.sacm.xmi", &[]),
        ("open-autonomy-safety-case.updated.v2.sacm.xmi", &[]),
        // Strategies with only context, the reasoning of no inference.
        ("oasc-ja.sacm.xmi", &[(Rule::StrategyUnsupported, &["S71"])]),
        (
            "safety-case-sanitized-strict.sacm.xmi",
            &[
                (Rule::StrategyUnsupported, &["ARGR_011"]),
                (Rule::StrategyUnsupported, &["ARGR_012"]),
                (Rule::StrategyUnsupported, &["ARGR_013"]),
            ],
        ),
    ];
    for (name, breaks) in expected {
        let document = sample(name);
        let found: Vec<(Rule, Vec<String>)> = check(&document, &view(&document, "en"))
            .into_iter()
            .map(|finding| (finding.rule, finding.nodes))
            .collect();
        let wanted: Vec<(Rule, Vec<String>)> = breaks
            .iter()
            .map(|(rule, nodes)| (*rule, nodes.iter().map(|id| (*id).to_owned()).collect()))
            .collect();
        assert_eq!(found, wanted, "{name}");
    }
}

#[test]
fn nothing_is_reported_at_a_neutral_node_but_a_cycle() {
    // A cited reasoning is drawn neutral (R12): as an inference's premise it
    // is not "a Strategy at an end". Neutral nodes do not share identifiers
    // either. A cycle through them is still a cycle.
    let found = findings(&[
        goal("g"),
        element("ArgumentReasoning", "cited", r#" isCitation="true""#, ""),
        inference("i1", "cited", "g"),
        element(
            "Claim",
            "n1",
            r#" isCitation="true""#,
            r#"<taggedValue xmi:id="tv_n1"><key><value content="assuranceForge.gsn.identifier"/></key><content><value content="X"/></content></taggedValue>"#,
        ),
        element(
            "Claim",
            "n2",
            r#" isCitation="true""#,
            r#"<taggedValue xmi:id="tv_n2"><key><value content="assuranceForge.gsn.identifier"/></key><content><value content="X"/></content></taggedValue>"#,
        ),
        inference("loop1", "n2", "n1"),
        inference("loop2", "n1", "n2"),
    ]);
    assert_eq!(found, [finding(Rule::Cycle, &["n1", "n2"], None)]);
}

#[test]
fn every_bad_premise_of_one_relationship_is_named() {
    let found = findings(&[
        goal("g"),
        claim("a", "assumed"),
        claim("j", "axiomatic"),
        element("AssertedInference", "i1", r#" source="a j" target="g""#, ""),
    ]);
    assert_eq!(
        found,
        [
            finding(Rule::ContextualSupports, &["g", "a"], Some("i1")),
            finding(Rule::ContextualSupports, &["g", "j"], Some("i1")),
        ]
    );
}
