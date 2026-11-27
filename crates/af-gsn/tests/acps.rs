//! Assurance claim points read and drawn (E11 and R17 of
//! `docs/standards/sacm-gsn-mapping.md`; G2 of
//! `docs/roadmap/milestone-2-acps.md`).

use af_gsn::view::{Acp, Code, of_package, view};
use sacm::model::Document;
use sacm::xmi::{self, Mode, ReadOptions};

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

fn acp(relationship: &str, claim: &str, label: &str, package: Option<&str>) -> Acp {
    Acp {
        relationship: relationship.to_owned(),
        claim: claim.to_owned(),
        label: label.to_owned(),
        package: package.map(str::to_owned),
    }
}

#[test]
fn e11_a_meta_claim_of_a_support_or_context_line_is_an_acp() {
    let document = sample("acps.sacm.xmi");
    let shown = view(&document, "en");
    assert_eq!(
        shown.acps,
        vec![
            // On the inference through S1, a description (§1:5.3.3).
            acp("I1", "ACP1", "ACP1", None),
            // On a line of evidence, a confidence argument in its own
            // package, labelled with it (§1:5.2.3).
            acp("E1", "ACP2", "ACP2[Confidence]", Some("argpkg_confidence")),
            // On a line of context, a confidence goal of the same argument.
            acp("X1", "ACP4", "ACP4", None),
        ]
    );
    // No metaClaim is left undrawn.
    assert!(
        shown
            .findings
            .iter()
            .all(|finding| finding.code != Code::MetaClaimNotDrawn)
    );
}

#[test]
fn r17_a_description_is_not_a_node_and_a_supported_confidence_goal_is() {
    let document = sample("acps.sacm.xmi");
    let shown = view(&document, "en");
    let drawn = |id: &str| shown.nodes.iter().any(|node| node.id == id);
    // ACP1 says what it is in its text alone: the ACP shows it.
    assert!(!drawn("ACP1"));
    assert!(
        shown
            .unplaced
            .iter()
            .any(|item| item.id == "ACP1" && item.code == Code::AcpDescription)
    );
    assert_eq!(Code::AcpDescription.as_str(), "AF-VIEW-015");
    // ACP4 is supported: a Goal of the argument. ACP2 is in another package.
    assert!(drawn("ACP4"));
    assert!(drawn("ACP2"));
}

#[test]
fn a_package_diagram_keeps_the_acps_of_its_lines() {
    let document = sample("acps.sacm.xmi");
    let whole = view(&document, "en");
    let main = of_package(&document, &whole, "argpkg_main")
        .unwrap_or_else(|| panic!("argpkg_main is a package"));
    assert_eq!(main.acps.len(), 3);
    let confidence = of_package(&document, &whole, "argpkg_confidence")
        .unwrap_or_else(|| panic!("argpkg_confidence is a package"));
    assert!(confidence.acps.is_empty());
}

fn case(relationships: &str, claims: &str) -> Document {
    read(format!(
        r#"<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:id="case">
  <argumentPackage xmi:id="p">
    <argumentElement xmi:type="sacm:Claim" xmi:id="g" />
    <argumentElement xmi:type="sacm:Claim" xmi:id="sub" />
    <argumentElement xmi:type="sacm:Claim" xmi:id="k" />
    {claims}
    {relationships}
  </argumentPackage>
</sacm:AssuranceCasePackage>"#
    )
    .as_bytes())
}

#[test]
fn e11_a_meta_claim_of_a_challenge_or_of_anything_but_a_claim_is_not_drawn() {
    // On a challenge: GSN puts no ACP there.
    let document = case(
        r#"<argumentElement xmi:type="sacm:AssertedInference" xmi:id="i" source="sub" target="g" />
    <argumentElement xmi:type="sacm:AssertedInference" xmi:id="ch" source="k" target="g" isCounter="true" metaClaim="c" />"#,
        r#"<argumentElement xmi:type="sacm:Claim" xmi:id="c" />"#,
    );
    let shown = view(&document, "en");
    assert!(shown.acps.is_empty());
    assert!(
        shown
            .findings
            .iter()
            .any(|finding| finding.element == "ch" && finding.code == Code::MetaClaimNotDrawn)
    );
    // The Claim it names is a node: it is not a description of an ACP.
    assert!(shown.nodes.iter().any(|node| node.id == "c"));
}

#[test]
fn r17_a_claim_also_named_by_a_claim_is_drawn() {
    // c is the metaClaim of the line and of the Claim k: not only an ACP's.
    let document = case(
        r#"<argumentElement xmi:type="sacm:AssertedInference" xmi:id="i" source="sub" target="g" metaClaim="c" />"#,
        r#"<argumentElement xmi:type="sacm:Claim" xmi:id="c" />
    <argumentElement xmi:type="sacm:Claim" xmi:id="k2" metaClaim="c" />"#,
    );
    let shown = view(&document, "en");
    assert_eq!(shown.acps.len(), 1);
    assert!(shown.nodes.iter().any(|node| node.id == "c"));
}
