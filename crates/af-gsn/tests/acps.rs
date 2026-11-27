//! Assurance claim points read and drawn (E11 and R17 of
//! `docs/standards/sacm-gsn-mapping.md`; G2 of
//! `docs/roadmap/milestone-2-acps.md`).

use af_gsn::commands::{
    AcpKind, AcpLine, AddError, Refusal, acp_choices, acp_node_refusal, add_acp, removal, remove,
};
use af_gsn::view::{Acp, Code, argument_package_of, of_package, view};
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

fn acp(relationship: &str, claim: &str, label: &str, package: Option<&str>) -> Acp {
    // The first edge of each relationship of the fixture.
    let (from, to) = match relationship {
        "I1" => ("G1", "S1"),
        "E1" => ("G2", "Sn1"),
        "E2" => ("G3", "Sn2"),
        "X1" => ("G1", "C1"),
        other => panic!("no first edge known for {other}"),
    };
    Acp {
        relationship: relationship.to_owned(),
        from: from.to_owned(),
        to: to.to_owned(),
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

fn line<'a>(relationship: &'a str, from: &'a str, to: &'a str) -> AcpLine<'a> {
    AcpLine {
        relationship,
        from,
        to,
    }
}

fn refusals(document: &Document, at: AcpLine<'_>) -> Vec<Option<Refusal>> {
    let shown = view(document, "en");
    acp_choices(document, &shown, at)
        .choices
        .into_iter()
        .map(|(_, refusal)| refusal)
        .collect()
}

#[test]
fn w33_a_support_or_context_line_takes_an_acp_on_its_first_line_only() {
    let document = sample("acps.sacm.xmi");
    let shown = view(&document, "en");
    // The next identifier is above every ACP the file has.
    assert_eq!(
        acp_choices(&document, &shown, line("E2", "G3", "Sn2")).identifier,
        "ACP5"
    );
    assert_eq!(refusals(&document, line("E2", "G3", "Sn2")), [None, None]);
    assert_eq!(refusals(&document, line("X1", "G1", "C1")), [None, None]);
    // An inference through a Strategy: on its first line, into S1, and not
    // on a premise's below it (GSN v3 §1:5.3.3).
    assert_eq!(refusals(&document, line("I1", "G1", "S1")), [None, None]);
    assert_eq!(
        refusals(&document, line("I1", "S1", "G2")),
        [Some(Refusal::NotTheFirstLine); 2]
    );
    assert_eq!(
        refusals(&document, line("E2", "G1", "Sn2")),
        [Some(Refusal::NotANode); 2]
    );
    // Not on a node, other than one SACM 2.4 will let it on.
    assert_eq!(acp_node_refusal(&shown, "Sn1"), Refusal::AcpNeedsSacm24);
    assert_eq!(acp_node_refusal(&shown, "C1"), Refusal::AcpNeedsSacm24);
    assert_eq!(acp_node_refusal(&shown, "G1"), Refusal::NotAnAcpPlace);
    assert_eq!(Refusal::NotAnAcpPlace.code(), "AF-EDIT-029");
    assert_eq!(Refusal::AcpNeedsSacm24.code(), "AF-EDIT-030");
}

#[test]
fn w33_a_challenge_takes_no_acp() {
    let document = case(
        r#"<argumentElement xmi:type="sacm:AssertedInference" xmi:id="i" source="sub" target="g" />
    <argumentElement xmi:type="sacm:AssertedInference" xmi:id="ch" source="k" target="g" isCounter="true" />"#,
        "",
    );
    assert_eq!(
        refusals(&document, line("ch", "k", "g")),
        [Some(Refusal::NotAnAcpPlace); 2]
    );
    assert_eq!(refusals(&document, line("i", "g", "sub")), [None, None]);
}

fn strict(document: &Document) -> Document {
    let written = xmi::write(document, &WriteOptions::default().with_mode(Mode::Strict))
        .unwrap_or_else(|error| panic!("a strict write refused it: {error:?}"));
    read(written.as_bytes())
}

#[test]
fn w34_a_description_is_a_claim_of_the_lines_package_shown_by_the_acp() {
    let original = sample("acps.sacm.xmi");
    let shown = view(&original, "en");
    let mut document = original.clone();
    let added = add_acp(
        &mut document,
        &shown,
        line("E2", "G3", "Sn2"),
        AcpKind::Description,
        "unused",
    )
    .unwrap_or_else(|error| panic!("{error}"));
    let after = view(&strict(&document), "en");
    assert!(
        after
            .acps
            .contains(&acp("E2", &added.element, "ACP5", None))
    );
    // Not a node: the ACP shows it, and its text is edited as a node's is.
    assert!(!after.nodes.iter().any(|node| node.id == added.element));
    assert!(
        after
            .descriptions
            .iter()
            .any(|node| node.id == added.element)
    );
    assert_eq!(
        argument_package_of(&document, &added.element).as_deref(),
        Some("argpkg_main")
    );
    // No package is made, and nothing else changed.
    let packages = |document: &Document| {
        document
            .elements()
            .filter(|element| element.kind() == SacmKind::ArgumentPackage)
            .count()
    };
    assert_eq!(packages(&document), packages(&original));
    assert_eq!(after.nodes.len(), shown.nodes.len());
    assert_eq!(after.findings.len(), shown.findings.len());
}

#[test]
fn w34_a_confidence_argument_is_a_new_package_beside_the_lines() {
    let original = sample("acps.sacm.xmi");
    let shown = view(&original, "en");
    let mut document = original.clone();
    let added = add_acp(
        &mut document,
        &shown,
        line("X1", "G1", "C1"),
        AcpKind::Argument,
        "Confidence ACP5",
    )
    .unwrap_or_else(|error| panic!("{error}"));
    let read_back = strict(&document);
    let after = view(&read_back, "en");
    let package = argument_package_of(&read_back, &added.element)
        .unwrap_or_else(|| panic!("the Claim is in a package"));
    assert_ne!(package, "argpkg_main");
    // Beside the line's package, in the assurance case.
    let holder = read_back
        .get(package.as_str())
        .and_then(|found| found.parent())
        .map(|parent| parent.id().as_str().to_owned());
    assert_eq!(holder.as_deref(), Some("case_acps"));
    // The ACP already on X1 stays, and the new one is labelled with the
    // package (§1:5.2.3); its Claim is that package's top goal, a node.
    let on_x1: Vec<&Acp> = after
        .acps
        .iter()
        .filter(|acp| acp.relationship == "X1")
        .collect();
    assert_eq!(on_x1.len(), 2);
    assert_eq!(on_x1[0].claim, "ACP4");
    assert_eq!(on_x1[1].label, "ACP5[Confidence ACP5]");
    assert_eq!(on_x1[1].package.as_deref(), Some(package.as_str()));
    assert!(after.nodes.iter().any(|node| node.id == added.element));
    let confidence =
        of_package(&read_back, &after, &package).unwrap_or_else(|| panic!("a package"));
    assert_eq!(confidence.nodes.len(), 1);
}

#[test]
fn w34_a_refused_acp_changes_nothing() {
    let original = sample("acps.sacm.xmi");
    let shown = view(&original, "en");
    let mut document = original.clone();
    match add_acp(
        &mut document,
        &shown,
        line("I1", "S1", "G2"),
        AcpKind::Argument,
        "x",
    ) {
        Err(AddError::Refused(refusal)) => assert_eq!(refusal, Refusal::NotTheFirstLine),
        other => panic!("{other:?}"),
    }
    assert_eq!(document.elements().count(), original.elements().count());
}

#[test]
fn w18_a_removal_takes_the_description_only_its_lines_named() {
    let original = sample("acps.sacm.xmi");
    let shown = view(&original, "en");
    let mut document = original.clone();
    let added = add_acp(
        &mut document,
        &shown,
        line("E2", "G3", "Sn2"),
        AcpKind::Description,
        "unused",
    )
    .unwrap_or_else(|error| panic!("{error}"));
    let shown = view(&document, "en");
    // Removing Sn2 deletes E2, the only line naming the description: it is
    // listed, and goes too.
    let listed = removal(&document, &shown, "Sn2").unwrap_or_else(|why| panic!("{why:?}"));
    assert_eq!(listed.nodes, ["Sn2".to_owned(), added.element.clone()]);
    remove(&mut document, &shown, "Sn2").unwrap_or_else(|error| panic!("{error}"));
    assert!(document.get(added.element.as_str()).is_none());
    let after = view(&document, "en");
    assert!(!after.nodes.iter().any(|node| node.identifier == "ACP5"));
    // ACP1, on I1, which stays, is kept.
    assert!(after.descriptions.iter().any(|node| node.id == "ACP1"));
}

#[test]
fn r17_a_claim_no_acp_is_drawn_for_is_a_node() {
    // The relationship's source is inside an interface (R15): it draws no
    // edge, so no ACP, and the Claim it names stays in view as a node.
    let document = read(
        br#"<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:id="case">
  <argumentPackage xmi:id="p">
    <name content="P" />
    <argumentElement xmi:type="sacm:Claim" xmi:id="c1"><name content="c1" /></argumentElement>
    <argumentElement xmi:type="sacm:Claim" xmi:id="d"><name content="d" /></argumentElement>
    <argumentElement xmi:type="sacm:ArgumentPackageInterface" xmi:id="i1" implements="p">
      <name content="i1" />
      <argumentElement xmi:type="sacm:Claim" xmi:id="inside"><name content="inside" /></argumentElement>
    </argumentElement>
    <argumentElement xmi:type="sacm:AssertedInference" xmi:id="r" source="inside" target="c1" metaClaim="d"><name content="r" /></argumentElement>
  </argumentPackage>
</sacm:AssuranceCasePackage>"#,
    );
    let shown = view(&document, "en");
    assert!(shown.acps.is_empty());
    assert!(shown.nodes.iter().any(|node| node.id == "d"));
    assert!(
        shown
            .findings
            .iter()
            .any(|finding| finding.element == "r" && finding.code == Code::MetaClaimNotDrawn)
    );
}

fn two_packages(a: &str, b: &str) -> Document {
    read(format!(
        r#"<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:id="case">
  <argumentPackage xmi:id="a"><name content="A" />{a}</argumentPackage>
  <argumentPackage xmi:id="b"><name content="B" />{b}</argumentPackage>
</sacm:AssuranceCasePackage>"#
    )
    .as_bytes())
}

#[test]
fn a_package_diagram_keeps_what_its_challenges_point_at() {
    // CG1, in A, challenges the inference i of B.
    let document = two_packages(
        r#"<argumentElement xmi:type="sacm:Claim" xmi:id="cg"><name content="cg" /></argumentElement>
    <argumentElement xmi:type="sacm:AssertedInference" xmi:id="ch" source="cg" target="i" isCounter="true"><name content="ch" /></argumentElement>"#,
        r#"<argumentElement xmi:type="sacm:Claim" xmi:id="g"><name content="g" /></argumentElement>
    <argumentElement xmi:type="sacm:Claim" xmi:id="sub"><name content="sub" /></argumentElement>
    <argumentElement xmi:type="sacm:AssertedInference" xmi:id="i" source="sub" target="g"><name content="i" /></argumentElement>"#,
    );
    let whole = view(&document, "en");
    let a = of_package(&document, &whole, "a").unwrap_or_else(|| panic!("a package"));
    assert!(a.edges.iter().any(|edge| edge.relationship == "ch"));
    assert!(a.edges.iter().any(|edge| edge.relationship == "i"));
    let elsewhere = |id: &str| {
        a.nodes
            .iter()
            .find(|node| node.id == id)
            .and_then(|node| node.elsewhere.as_ref())
            .map(|elsewhere| elsewhere.package.clone())
    };
    assert_eq!(elsewhere("g").as_deref(), Some("b"));
    assert_eq!(elsewhere("sub").as_deref(), Some("b"));
}

#[test]
fn a_package_diagram_draws_an_acp_with_its_first_edge() {
    // The inference runs from G1 through S1, of A, to G2, of B; its ACP is
    // on the line into S1, which B's diagram keeps for it, with G1 and S1
    // marked as A's.
    let document = two_packages(
        r#"<argumentElement xmi:type="sacm:Claim" xmi:id="G1"><name content="G1" /></argumentElement>
    <argumentElement xmi:type="sacm:ArgumentReasoning" xmi:id="S1"><name content="S1" /></argumentElement>
    <argumentElement xmi:type="sacm:Claim" xmi:id="c"><name content="c" /></argumentElement>
    <argumentElement xmi:type="sacm:AssertedInference" xmi:id="I1" source="G2" target="G1" reasoning="S1" metaClaim="c"><name content="I1" /></argumentElement>"#,
        r#"<argumentElement xmi:type="sacm:Claim" xmi:id="G2"><name content="G2" /></argumentElement>"#,
    );
    let whole = view(&document, "en");
    assert_eq!(whole.acps.len(), 1);
    let a = of_package(&document, &whole, "a").unwrap_or_else(|| panic!("a package"));
    assert_eq!(a.acps.len(), 1);
    let b = of_package(&document, &whole, "b").unwrap_or_else(|| panic!("b package"));
    assert!(
        b.edges
            .iter()
            .any(|edge| edge.relationship == "I1" && edge.from == "G1" && edge.to == "S1")
    );
    assert_eq!(b.acps.len(), 1);
    assert!(
        b.nodes
            .iter()
            .filter(|node| node.id == "G1" || node.id == "S1")
            .all(|node| node.elsewhere.as_ref().is_some_and(|at| at.package == "a"))
    );
}

#[test]
fn w19_a_confidence_goal_is_removed_with_its_acp() {
    // ACP4 is a local confidence goal: the line X1 names it. Removing it
    // takes it out of X1's metaClaim, and the ACP goes with it.
    let mut document = sample("acps.sacm.xmi");
    let shown = view(&document, "en");
    let listed = removal(&document, &shown, "ACP4").unwrap_or_else(|why| panic!("{why:?}"));
    assert_eq!(listed.nodes, ["ACP4".to_owned(), "Sn4".to_owned()]);
    // Its ACP, on X1, which stays, goes with it, and is listed.
    assert_eq!(listed.acps, ["ACP4".to_owned()]);
    remove(&mut document, &shown, "ACP4").unwrap_or_else(|error| panic!("{error}"));
    let after = view(&strict(&document), "en");
    assert!(!after.acps.iter().any(|acp| acp.claim == "ACP4"));
    assert!(after.edges.iter().any(|edge| edge.relationship == "X1"));
    // The other ACPs stay.
    assert_eq!(after.acps.len(), 2);
}

#[test]
fn r17_what_building_a_description_finds_is_not_said() {
    // ACP1 records a GSN type that does not agree with its class: as a node
    // it would say so, but it is not drawn.
    let text = std::fs::read_to_string(format!(
        "{}/../../testdata/sacm/acps.sacm.xmi",
        env!("CARGO_MANIFEST_DIR")
    ))
    .unwrap_or_else(|error| panic!("setup: {error}"));
    let tagged = text.replacen(
        r#"<name content="HazardsIdentified" />"#,
        r#"<name content="HazardsIdentified" />
      <taggedValue xmi:id="tv_acp1"><key><value content="sacm.import.extensionType" /></key><content><value content="{http://acwg.org/3.0/gsn}Solution" /></content></taggedValue>"#,
        1,
    );
    assert_ne!(tagged, text, "setup: the name is where it was");
    let document = read(tagged.as_bytes());
    let shown = view(&document, "en");
    assert!(shown.descriptions.iter().any(|node| node.id == "ACP1"));
    assert!(
        shown
            .findings
            .iter()
            .all(|finding| finding.element != "ACP1")
    );
}

#[test]
fn r17_a_description_on_a_line_with_a_declaration_is_one() {
    // An assumed inference is drawn live (E6): its ACP's description is not
    // a node either.
    let mut document = case(
        r#"<argumentElement xmi:type="sacm:AssertedInference" xmi:id="i" source="sub" target="g" assertionDeclaration="assumed"><name content="i" /></argumentElement>"#,
        "",
    );
    let shown = view(&document, "en");
    let added = add_acp(
        &mut document,
        &shown,
        line("i", "g", "sub"),
        AcpKind::Description,
        "x",
    )
    .unwrap_or_else(|error| panic!("{error}"));
    let after = view(&document, "en");
    assert!(
        after
            .descriptions
            .iter()
            .any(|node| node.id == added.element)
    );
    assert!(!after.nodes.iter().any(|node| node.id == added.element));
}

#[test]
fn w19_a_meta_claim_the_diagram_does_not_draw_still_refuses() {
    // The challenge ch names c by metaClaim, which E11 does not draw on a
    // challenge: removing c would change it unseen.
    let mut document = case(
        r#"<argumentElement xmi:type="sacm:AssertedInference" xmi:id="i" source="sub" target="g"><name content="i" /></argumentElement>
    <argumentElement xmi:type="sacm:AssertedInference" xmi:id="ch" source="k" target="g" isCounter="true" metaClaim="c"><name content="ch" /></argumentElement>"#,
        r#"<argumentElement xmi:type="sacm:Claim" xmi:id="c"><name content="c" /></argumentElement>"#,
    );
    let shown = view(&document, "en");
    assert_eq!(
        removal(&document, &shown, "c").err(),
        Some(Refusal::Entangled)
    );
    assert!(matches!(
        remove(&mut document, &shown, "c"),
        Err(AddError::Refused(Refusal::Entangled))
    ));
}

#[test]
fn w19_a_removal_lists_an_acp_whose_line_goes() {
    // Removing Sn1 deletes E1, which carries ACP2 to the confidence package:
    // the Claim stays, its ACP goes, and the removal says so.
    let document = sample("acps.sacm.xmi");
    let shown = view(&document, "en");
    let listed = removal(&document, &shown, "Sn1").unwrap_or_else(|why| panic!("{why:?}"));
    assert_eq!(listed.nodes, ["Sn1".to_owned()]);
    assert_eq!(listed.acps, ["ACP2".to_owned()]);
}
