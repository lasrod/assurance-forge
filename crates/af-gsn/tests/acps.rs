//! Assurance claim points read and drawn (E11 and R17 of
//! `docs/standards/sacm-gsn-mapping.md`; G2 of
//! `docs/roadmap/milestone-2-acps.md`).

use af_gsn::ElementKind;
use af_gsn::commands::{
    AcpKind, AddError, Line, Refusal, acp_choices, acp_node_refusal, acp_removal, add_acp,
    link_acp, removal, remove, remove_acp,
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

fn line<'a>(relationship: &'a str, from: &'a str, to: &'a str) -> Line<'a> {
    Line {
        relationship,
        from,
        to,
    }
}

fn refusals(document: &Document, at: Line<'_>) -> Vec<Option<Refusal>> {
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
    assert_eq!(listed.acps, [("X1".to_owned(), "ACP4".to_owned())]);
    remove(&mut document, &shown, "ACP4").unwrap_or_else(|error| panic!("{error}"));
    let after = view(&strict(&document), "en");
    assert!(!after.acps.iter().any(|acp| acp.claim == "ACP4"));
    assert!(after.edges.iter().any(|edge| edge.relationship == "X1"));
    // The other ACPs stay.
    assert_eq!(after.acps.len(), 2);
}

#[test]
fn r17_what_building_a_description_finds_is_said() {
    // ACP1 records a GSN type that does not agree with its class: the file
    // says it, so the problem list does, though ACP1 is not drawn.
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
            .any(|finding| finding.element == "ACP1" && finding.code == Code::RecordedTypeNotUsed)
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
    assert_eq!(listed.acps, [("E1".to_owned(), "ACP2".to_owned())]);
}

#[test]
fn r17_only_a_claim_the_editor_marked_is_a_description() {
    // ACP4 is a confidence goal: removing its only support leaves it named
    // by X1's metaClaim alone, and it stays a Goal of the argument.
    let mut document = sample("acps.sacm.xmi");
    let shown = view(&document, "en");
    remove(&mut document, &shown, "Sn4").unwrap_or_else(|error| panic!("{error}"));
    let after = view(&document, "en");
    assert!(after.nodes.iter().any(|node| node.id == "ACP4"));
    assert!(!after.descriptions.iter().any(|node| node.id == "ACP4"));
    // A Claim another tool wrote, named only by a metaClaim, is a Goal.
    let document = case(
        r#"<argumentElement xmi:type="sacm:AssertedInference" xmi:id="i" source="sub" target="g" metaClaim="c"><name content="i" /></argumentElement>"#,
        r#"<argumentElement xmi:type="sacm:Claim" xmi:id="c"><name content="c" /></argumentElement>"#,
    );
    let shown = view(&document, "en");
    assert_eq!(shown.acps.len(), 1);
    assert!(shown.nodes.iter().any(|node| node.id == "c"));
    // A marked one declared needing support is an undeveloped Goal, drawn.
    let document = case(
        r#"<argumentElement xmi:type="sacm:AssertedInference" xmi:id="i" source="sub" target="g" metaClaim="c"><name content="i" /></argumentElement>"#,
        r#"<argumentElement xmi:type="sacm:Claim" xmi:id="c" assertionDeclaration="needsSupport"><name content="c" /><taggedValue xmi:id="tv_c"><key><value content="assuranceForge.gsn.acp" /></key><content><value content="description" /></content></taggedValue></argumentElement>"#,
    );
    let shown = view(&document, "en");
    assert!(
        shown
            .nodes
            .iter()
            .any(|node| node.id == "c" && node.undeveloped)
    );
}

#[test]
fn e11_a_meta_claim_naming_a_claim_the_view_hides_is_no_acp() {
    // The Claim is inside an interface (R15): no ACP is drawn for it, and
    // the finding names it.
    let document = read(
        br#"<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:id="case">
  <argumentPackage xmi:id="p">
    <name content="P" />
    <argumentElement xmi:type="sacm:Claim" xmi:id="g"><name content="g" /></argumentElement>
    <argumentElement xmi:type="sacm:Claim" xmi:id="sub"><name content="sub" /></argumentElement>
    <argumentElement xmi:type="sacm:ArgumentPackageInterface" xmi:id="i1" implements="p">
      <name content="i1" />
      <argumentElement xmi:type="sacm:Claim" xmi:id="hidden"><name content="hidden" /></argumentElement>
    </argumentElement>
    <argumentElement xmi:type="sacm:AssertedInference" xmi:id="r" source="sub" target="g" metaClaim="hidden"><name content="r" /></argumentElement>
  </argumentPackage>
</sacm:AssuranceCasePackage>"#,
    );
    let shown = view(&document, "en");
    assert!(shown.acps.is_empty());
    assert!(shown.findings.iter().any(|finding| finding.element == "r"
        && finding.code == Code::MetaClaimNotDrawn
        && finding.end.as_deref() == Some("hidden")));
}

#[test]
fn w25_an_inference_with_an_acp_keeps_its_premises() {
    // I1, through S1, carries ACP1: a Goal added under S1, or a premise
    // removed, would change what the ACP is about.
    let mut document = sample("acps.sacm.xmi");
    let shown = view(&document, "en");
    let offered =
        af_gsn::commands::choices(&document, &shown, af_gsn::commands::Target::Node("S1"));
    assert_eq!(
        offered
            .iter()
            .find(|choice| choice.kind == ElementKind::Goal)
            .and_then(|choice| choice.refusal),
        Some(Refusal::Entangled)
    );
    assert_eq!(
        removal(&document, &shown, "G2").err(),
        Some(Refusal::Entangled)
    );
    assert!(matches!(
        remove(&mut document, &shown, "G2"),
        Err(AddError::Refused(Refusal::Entangled))
    ));
}

#[test]
fn w19_a_relationship_with_a_meta_claim_not_drawn_is_not_deleted() {
    // A cited line, drawn neutral, names c by metaClaim: removing its source
    // would delete it and that link unseen.
    let mut document = case(
        r#"<argumentElement xmi:type="sacm:AssertedInference" xmi:id="i" source="sub" target="g" isCitation="true" metaClaim="c"><name content="i" /></argumentElement>"#,
        r#"<argumentElement xmi:type="sacm:Claim" xmi:id="c"><name content="c" /></argumentElement>"#,
    );
    let shown = view(&document, "en");
    assert_eq!(
        removal(&document, &shown, "sub").err(),
        Some(Refusal::Entangled)
    );
    assert!(matches!(
        remove(&mut document, &shown, "sub"),
        Err(AddError::Refused(Refusal::Entangled))
    ));
    // And a metaClaim named twice is one ACP.
    let document = case(
        r#"<argumentElement xmi:type="sacm:AssertedInference" xmi:id="i" source="sub" target="g" metaClaim="c c"><name content="i" /></argumentElement>"#,
        r#"<argumentElement xmi:type="sacm:Claim" xmi:id="c"><name content="c" /></argumentElement>"#,
    );
    assert_eq!(view(&document, "en").acps.len(), 1);
}

// ===== Removing an ACP (W35) =====

/// The fixture with each `from` replaced by its `to`, which must be in it.
fn changed(replacements: &[(&str, &str)]) -> Document {
    let path = format!(
        "{}/../../testdata/sacm/acps.sacm.xmi",
        env!("CARGO_MANIFEST_DIR")
    );
    let mut text = std::fs::read_to_string(&path).unwrap_or_else(|error| panic!("setup: {error}"));
    for (from, to) in replacements {
        assert!(text.contains(from), "setup: {from} is not in the fixture");
        text = text.replace(from, to);
    }
    read(text.as_bytes())
}

fn meta_claims(document: &Document, relationship: &str) -> Vec<String> {
    document
        .get(relationship)
        .map(|element| {
            element
                .meta_claims()
                .iter()
                .map(|id| id.as_str().to_owned())
                .collect()
        })
        .unwrap_or_default()
}

#[test]
fn w35_a_description_goes_with_its_acp() {
    let original = sample("acps.sacm.xmi");
    let shown = view(&original, "en");
    let plan = acp_removal(&original, &shown, "I1", "ACP1").unwrap_or_else(|why| panic!("{why:?}"));
    assert_eq!(plan.description.as_deref(), Some("ACP1"));
    assert_eq!(plan.argument, None);
    let mut document = original.clone();
    remove_acp(&mut document, &shown, "I1", "ACP1", false)
        .unwrap_or_else(|error| panic!("{error}"));
    let read_back = strict(&document);
    assert!(read_back.get("ACP1").is_none());
    assert!(meta_claims(&read_back, "I1").is_empty());
    let after = view(&read_back, "en");
    assert!(!after.acps.iter().any(|acp| acp.relationship == "I1"));
    // Not left behind as a Goal of its own, and nothing else changed.
    assert_eq!(after.nodes, shown.nodes);
    assert_eq!(after.acps.len(), shown.acps.len() - 1);
    assert!(after.descriptions.is_empty());
}

#[test]
fn w35_a_description_another_acp_shows_stays_for_it() {
    let original = changed(&[(
        r#"xmi:id="E2" source="Sn2" target="G3""#,
        r#"xmi:id="E2" source="Sn2" target="G3" metaClaim="ACP1""#,
    )]);
    let shown = view(&original, "en");
    let plan = acp_removal(&original, &shown, "I1", "ACP1").unwrap_or_else(|why| panic!("{why:?}"));
    assert_eq!(plan.description, None);
    let mut document = original.clone();
    remove_acp(&mut document, &shown, "I1", "ACP1", false)
        .unwrap_or_else(|error| panic!("{error}"));
    let after = view(&strict(&document), "en");
    assert!(meta_claims(&document, "I1").is_empty());
    assert_eq!(meta_claims(&document, "E2"), ["ACP1".to_owned()]);
    assert!(after.descriptions.iter().any(|node| node.id == "ACP1"));
}

#[test]
fn w35_a_goal_of_the_argument_stays_and_only_its_acp_goes() {
    let original = sample("acps.sacm.xmi");
    let shown = view(&original, "en");
    let plan = acp_removal(&original, &shown, "X1", "ACP4").unwrap_or_else(|why| panic!("{why:?}"));
    assert_eq!(plan.description, None);
    assert_eq!(plan.argument, None);
    let mut document = original.clone();
    // Asking for a confidence argument it does not have changes nothing more.
    remove_acp(&mut document, &shown, "X1", "ACP4", true).unwrap_or_else(|error| panic!("{error}"));
    let after = view(&strict(&document), "en");
    assert!(meta_claims(&document, "X1").is_empty());
    assert_eq!(after.nodes, shown.nodes);
    assert_eq!(after.edges.len(), shown.edges.len());
    assert!(!after.acps.iter().any(|acp| acp.relationship == "X1"));
}

#[test]
fn w35_a_confidence_argument_is_kept_or_deleted_with_its_acp() {
    let original = sample("acps.sacm.xmi");
    let shown = view(&original, "en");
    let plan = acp_removal(&original, &shown, "E1", "ACP2").unwrap_or_else(|why| panic!("{why:?}"));
    assert_eq!(plan.description, None);
    let argument = plan
        .argument
        .unwrap_or_else(|| panic!("ACP2 has a confidence argument"));
    assert_eq!(argument.package, "argpkg_confidence");
    assert_eq!(argument.nodes, ["ACP2".to_owned(), "Sn3".to_owned()]);
    assert_eq!(argument.refusal, None);

    // Kept: the package stays, its goal now a top goal of its own.
    let mut kept = original.clone();
    remove_acp(&mut kept, &shown, "E1", "ACP2", false).unwrap_or_else(|error| panic!("{error}"));
    let after = view(&strict(&kept), "en");
    assert!(meta_claims(&kept, "E1").is_empty());
    assert!(kept.get("argpkg_confidence").is_some());
    assert_eq!(after.nodes, shown.nodes);
    assert!(!after.acps.iter().any(|acp| acp.relationship == "E1"));

    // Deleted: the package goes with all it holds, and nothing else does.
    let mut deleted = original.clone();
    remove_acp(&mut deleted, &shown, "E1", "ACP2", true).unwrap_or_else(|error| panic!("{error}"));
    let read_back = strict(&deleted);
    for id in ["argpkg_confidence", "ACP2", "Sn3", "E3"] {
        assert!(read_back.get(id).is_none(), "{id} is deleted");
    }
    let after = view(&read_back, "en");
    let left: Vec<&str> = shown
        .nodes
        .iter()
        .map(|node| node.id.as_str())
        .filter(|id| !["ACP2", "Sn3"].contains(id))
        .collect();
    assert_eq!(
        after
            .nodes
            .iter()
            .map(|node| node.id.as_str())
            .collect::<Vec<_>>(),
        left
    );
    assert_eq!(after.acps.len(), shown.acps.len() - 1);
}

#[test]
fn w35_a_confidence_argument_tied_to_the_rest_is_not_deleted() {
    let cases: [&[(&str, &str)]; 4] = [
        // Another line's ACP names its goal.
        &[(
            r#"xmi:id="E2" source="Sn2" target="G3""#,
            r#"xmi:id="E2" source="Sn2" target="G3" metaClaim="ACP2""#,
        )],
        // The same line carries a second ACP into it.
        &[
            (r#"metaClaim="ACP2""#, r#"metaClaim="ACP2 ACP6""#),
            (
                r#"<argumentElement xmi:type="sacm:ArtifactReference" xmi:id="Sn3">"#,
                r#"<argumentElement xmi:type="sacm:Claim" xmi:id="ACP6"><name content="Second" /></argumentElement>
    <argumentElement xmi:type="sacm:ArtifactReference" xmi:id="Sn3">"#,
            ),
        ],
        // One of its lines leaves it.
        &[(
            r#"xmi:id="E3" source="Sn3""#,
            r#"xmi:id="E3" source="Sn3 Sn4""#,
        )],
        // It holds a relationship the diagram does not draw.
        &[(
            r#"<argumentElement xmi:type="sacm:AssertedEvidence" xmi:id="E3""#,
            r#"<argumentElement xmi:type="sacm:AssertedInference" xmi:id="I9" source="ACP2" target="E3" /><argumentElement xmi:type="sacm:AssertedEvidence" xmi:id="E3""#,
        )],
    ];
    for case in cases {
        let original = changed(case);
        let to = case[0].1;
        let shown = view(&original, "en");
        assert!(
            !shown.edges.iter().any(|edge| edge.relationship == "I9"),
            "setup: I9 is not drawn"
        );
        let plan =
            acp_removal(&original, &shown, "E1", "ACP2").unwrap_or_else(|why| panic!("{why:?}"));
        let argument = plan
            .argument
            .unwrap_or_else(|| panic!("a confidence argument"));
        assert_eq!(argument.refusal, Some(Refusal::Entangled), "{to}");
        let mut document = original.clone();
        match remove_acp(&mut document, &shown, "E1", "ACP2", true) {
            Err(AddError::Refused(refusal)) => assert_eq!(refusal, Refusal::Entangled),
            other => panic!("{other:?}"),
        }
        assert_eq!(document, original);
        // The ACP alone still goes.
        remove_acp(&mut document, &shown, "E1", "ACP2", false)
            .unwrap_or_else(|error| panic!("{error}"));
        assert!(!meta_claims(&document, "E1").contains(&"ACP2".to_owned()));
        assert!(document.get("argpkg_confidence").is_some());
    }
}

#[test]
fn w35_no_such_acp_is_refused_and_changes_nothing() {
    let original = sample("acps.sacm.xmi");
    let shown = view(&original, "en");
    assert_eq!(
        acp_removal(&original, &shown, "E2", "ACP1"),
        Err(Refusal::NotAnAcp)
    );
    let mut document = original.clone();
    match remove_acp(&mut document, &shown, "I1", "ACP2", true) {
        Err(AddError::Refused(refusal)) => assert_eq!(refusal, Refusal::NotAnAcp),
        other => panic!("{other:?}"),
    }
    assert_eq!(document, original);
}

fn goals(document: &Document, at: Line<'_>) -> Vec<(String, String, Option<Refusal>)> {
    let shown = view(document, "en");
    acp_choices(document, &shown, at)
        .goals
        .into_iter()
        .map(|goal| (goal.claim, goal.label, goal.refusal))
        .collect()
}

fn goal(claim: &str, label: &str, refusal: Option<Refusal>) -> (String, String, Option<Refusal>) {
    (claim.to_owned(), label.to_owned(), refusal)
}

#[test]
fn w36_a_line_may_name_the_top_goal_of_another_package() {
    let document = sample("acps.sacm.xmi");
    // The confidence package's top goal, labelled as its ACP would be. G1
    // and ACP4 are top goals of the line's own package, and not offered.
    assert_eq!(
        goals(&document, line("E2", "G3", "Sn2")),
        [goal("ACP2", "ACP2[Confidence]", None)]
    );
    // Already this line's ACP.
    assert_eq!(
        goals(&document, line("E1", "G2", "Sn1")),
        [goal(
            "ACP2",
            "ACP2[Confidence]",
            Some(Refusal::AlreadyAnAcp)
        )]
    );
    // What refuses the line refuses each goal.
    assert_eq!(
        goals(&document, line("I1", "S1", "G2")),
        [goal(
            "ACP2",
            "ACP2[Confidence]",
            Some(Refusal::NotTheFirstLine)
        )]
    );
    // From the confidence package, the main package's top goals: the
    // argument's, and the one of the confidence argument it holds itself.
    assert_eq!(
        goals(&document, line("E3", "ACP2", "Sn3")),
        [
            goal("G1", "G1[MainArgument]", None),
            goal("ACP4", "ACP4[MainArgument]", None)
        ]
    );
    assert_eq!(Refusal::AlreadyAnAcp.code(), "AF-EDIT-033");
    assert_eq!(Refusal::AcpOfItself.code(), "AF-EDIT-034");
    assert_eq!(Refusal::NotATopGoalElsewhere.code(), "AF-EDIT-035");
}

#[test]
fn w36_a_goal_whose_argument_holds_the_line_is_not_its_confidence_argument() {
    // ACP2 is now above G1: the main argument is part of its own.
    let document = changed(&[(
        r#"    <argumentElement xmi:type="sacm:AssertedEvidence" xmi:id="E3""#,
        r#"    <argumentElement xmi:type="sacm:AssertedInference" xmi:id="I9" source="G1" target="ACP2" />
    <argumentElement xmi:type="sacm:AssertedEvidence" xmi:id="E3""#,
    )]);
    assert_eq!(
        goals(&document, line("X1", "G1", "C1")),
        [goal("ACP2", "ACP2[Confidence]", Some(Refusal::AcpOfItself))]
    );
    let shown = view(&document, "en");
    let mut edited = document.clone();
    match link_acp(&mut edited, &shown, line("X1", "G1", "C1"), "ACP2") {
        Err(AddError::Refused(refusal)) => assert_eq!(refusal, Refusal::AcpOfItself),
        other => panic!("{other:?}"),
    }
    assert_eq!(edited, document);
}

#[test]
fn w36_linking_a_goal_adds_it_to_the_meta_claim_and_nothing_else() {
    let original = sample("acps.sacm.xmi");
    let shown = view(&original, "en");
    let mut document = original.clone();
    link_acp(&mut document, &shown, line("X1", "G1", "C1"), "ACP2")
        .unwrap_or_else(|error| panic!("{error}"));
    let read_back = strict(&document);
    // After the ACP X1 named already.
    assert_eq!(
        meta_claims(&read_back, "X1"),
        ["ACP4".to_owned(), "ACP2".to_owned()]
    );
    let after = view(&read_back, "en");
    assert!(after.acps.contains(&acp(
        "X1",
        "ACP2",
        "ACP2[Confidence]",
        Some("argpkg_confidence")
    )));
    // One ACP more; the same elements, nodes and lines.
    assert_eq!(after.acps.len(), shown.acps.len() + 1);
    assert_eq!(after.nodes, shown.nodes);
    assert_eq!(after.edges, shown.edges);
    assert_eq!(read_back.elements().count(), original.elements().count());
    // Now it is this line's: offered again, refused.
    assert_eq!(
        goals(&read_back, line("X1", "G1", "C1")),
        [goal(
            "ACP2",
            "ACP2[Confidence]",
            Some(Refusal::AlreadyAnAcp)
        )]
    );
}

#[test]
fn w36_a_confidence_argument_kept_by_a_removal_is_linked_back() {
    let original = sample("acps.sacm.xmi");
    let shown = view(&original, "en");
    let mut document = original.clone();
    remove_acp(&mut document, &shown, "E1", "ACP2", false)
        .unwrap_or_else(|error| panic!("{error}"));
    let kept = view(&document, "en");
    assert_eq!(
        goals(&document, line("E1", "G2", "Sn1")),
        [goal("ACP2", "ACP2[Confidence]", None)]
    );
    link_acp(&mut document, &kept, line("E1", "G2", "Sn1"), "ACP2")
        .unwrap_or_else(|error| panic!("{error}"));
    assert_eq!(view(&strict(&document), "en"), shown);
}

#[test]
fn w36_only_a_goal_offered_is_linked() {
    let original = sample("acps.sacm.xmi");
    let shown = view(&original, "en");
    // A top goal of the line's own package, a goal below the top, a
    // Solution, and no element.
    for claim in ["G1", "G2", "Sn3", "nothing"] {
        let mut document = original.clone();
        match link_acp(&mut document, &shown, line("E2", "G3", "Sn2"), claim) {
            Err(AddError::Refused(refusal)) => {
                assert_eq!(refusal, Refusal::NotATopGoalElsewhere, "{claim}");
            }
            other => panic!("{claim}: {other:?}"),
        }
        assert_eq!(document, original);
    }
    // Nor on a line that takes no ACP.
    let mut document = original.clone();
    match link_acp(&mut document, &shown, line("I1", "S1", "G2"), "ACP2") {
        Err(AddError::Refused(refusal)) => assert_eq!(refusal, Refusal::NotTheFirstLine),
        other => panic!("{other:?}"),
    }
    assert_eq!(document, original);
}
