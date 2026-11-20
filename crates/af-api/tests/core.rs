//! `af-api` against goal G4 of the diagram plan: `open` answers with a
//! revision and diagnostic counts or a refusal with its code, template,
//! arguments and location; `diagram` gives the render model at the revision;
//! `element` gives the details panel's content.

use af_api::Core;
use af_api::contract::{ArgumentValue, NodeKind, OpenResult, Shape};
use af_layout::{Fonts, Style};

fn testdata(path: &str) -> Vec<u8> {
    let path = format!("{}/../../testdata/{path}", env!("CARGO_MANIFEST_DIR"));
    std::fs::read(&path).unwrap_or_else(|error| panic!("setup: {path}: {error}"))
}

fn fonts() -> Fonts {
    Fonts::new(
        testdata("fonts/IBMPlexSans-Regular.ttf"),
        testdata("fonts/IBMPlexSans-SemiBold.ttf"),
        testdata("fonts/AFTestSansJP-Regular.ttf"),
        testdata("fonts/AFTestSansJP-SemiBold.ttf"),
        testdata("fonts/IBMPlexMono-Regular.ttf"),
    )
    .unwrap_or_else(|error| panic!("setup: {error:?}"))
}

fn core(language: &str) -> Core {
    Core::new(fonts(), Style::default(), language)
}

#[test]
fn a_file_opens_before_the_fonts_arrive_and_is_drawn_once_they_have() {
    // The desktop shell opens a file named on the command line before its
    // page has loaded the fonts.
    let mut core = Core::without_fonts(Style::default(), "en");
    assert!(!core.has_fonts());
    let opened = core.open(&testdata("sacm/sacm_example_core_argument.sacm.xmi"));
    assert!(
        matches!(
            opened,
            OpenResult::Opened {
                revision: 1,
                nodes: 8,
                edges: 7,
                ..
            }
        ),
        "{opened:?}"
    );
    assert!(core.element("cl_top").is_some());
    assert!(core.diagram().is_none(), "no layout without fonts");

    // A text size set meanwhile is the one the layout uses.
    let larger = Style {
        text_size: 17.5,
        text_line_height: 25.0,
        identifier_size: 15.0,
        identifier_line_height: 20.0,
    };
    core.set_style(larger);
    core.set_fonts(fonts());
    assert!(core.has_fonts());
    let Some(at) = core.diagram() else {
        panic!("no diagram once the fonts are in");
    };
    assert_eq!(at.revision, 1);
    assert_eq!(at.diagram.nodes.len(), 8);
    let statement = at.diagram.nodes[0]
        .text
        .iter()
        .find(|line| line.role == af_api::contract::TextRole::Statement)
        .map(|line| line.size);
    assert_eq!(statement, Some(17.5));
}

#[test]
fn opening_the_core_sample_gives_its_counts_and_diagram() {
    let mut core = core("en");
    let opened = core.open(&testdata("sacm/sacm_example_core_argument.sacm.xmi"));
    let OpenResult::Opened {
        revision,
        diagnostics,
        nodes,
        edges,
        view_codes,
    } = opened
    else {
        panic!("refused: {opened:?}");
    };
    assert_eq!(revision, 1);
    assert_eq!(
        (diagnostics.errors, diagnostics.warnings, diagnostics.infos),
        (0, 0, 0)
    );
    assert_eq!((nodes, edges, view_codes), (8, 7, 0));

    let Some(at) = core.diagram() else {
        panic!("no diagram");
    };
    assert_eq!(at.revision, 1);
    assert_eq!(at.diagram.nodes.len(), 8);
    assert_eq!(at.diagram.edges.len(), 7);
    let strategy = at.diagram.nodes.iter().find(|node| node.id == "ar_1");
    assert!(matches!(
        strategy.map(|node| node.shape),
        Some(Shape::Parallelogram { .. })
    ));
}

#[test]
fn the_large_case_counts_its_warnings_and_view_codes() {
    let mut core = core("en");
    let opened = core.open(&testdata("sacm/safety-case-sanitized-strict.sacm.xmi"));
    let OpenResult::Opened {
        diagnostics,
        view_codes,
        ..
    } = opened
    else {
        panic!("refused: {opened:?}");
    };
    // SACM-MULT-001 for each Claim with two descriptions.
    assert_eq!((diagnostics.errors, diagnostics.warnings), (0, 162));
    // The 20 ArtifactReferences nothing reaches (R11).
    assert_eq!(view_codes, 20);
}

#[test]
fn a_refused_file_gives_its_code_template_arguments_and_location_and_no_diagram() {
    let mut core = core("en");
    core.open(&testdata("sacm/sacm_example_core_argument.sacm.xmi"));
    let refused = core.open(b"<?xml version=\"1.0\"?>\n<sacm:AssuranceCasePackage\n  oops");
    let OpenResult::Refused(refusal) = refused else {
        panic!("opened: {refused:?}");
    };
    assert!(refusal.code.starts_with("SACM-"), "{refusal:?}");
    assert!(refusal.template.starts_with(&refusal.code), "{refusal:?}");
    assert!(
        refusal.line.is_some() && refusal.column.is_some(),
        "{refusal:?}"
    );
    assert!(
        core.diagram().is_none(),
        "a refused file leaves nothing open"
    );
    assert!(core.element("cl_top").is_none());
    assert_eq!(core.revision(), 2);
}

#[test]
fn a_refusal_carries_its_arguments() {
    // A repeated xmi:id: the id is an argument.
    let text = r#"<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:id="case">
  <name content="C"/>
  <argumentPackage xmi:id="p"><name content="P"/>
    <argumentElement xmi:type="sacm:Claim" xmi:id="twice"><name content="a"/></argumentElement>
    <argumentElement xmi:type="sacm:Claim" xmi:id="twice"><name content="b"/></argumentElement>
  </argumentPackage>
</sacm:AssuranceCasePackage>"#;
    let OpenResult::Refused(refusal) = core("en").open(text.as_bytes()) else {
        panic!("a repeated id was read");
    };
    assert!(
        refusal
            .arguments
            .iter()
            .any(|argument| argument.value == ArgumentValue::Text("twice".to_owned())),
        "{refusal:?}"
    );
    assert_eq!(refusal.line, Some(6), "{refusal:?}");
}

#[test]
fn element_gives_the_details_panel_content() {
    let mut core = core("en");
    core.open(&testdata("sacm/sacm_example_core_argument.sacm.xmi"));
    let Some(details) = core.element("cl_sub_2") else {
        panic!("no cl_sub_2");
    };
    assert_eq!(details.kind, NodeKind::Goal);
    assert!(details.undeveloped);
    assert_eq!(details.identifier, "cl_sub_2");
    assert_eq!(details.name.as_deref(), Some("SubClaim_Verification"));
    assert_eq!(
        details.statement.as_deref(),
        Some("Verification activities demonstrate braking requirements are satisfied.")
    );
    assert!(details.codes.is_empty());
    assert!(
        core.element("inf_1").is_none(),
        "a relationship has no node details"
    );
    assert!(core.element("nothing").is_none());

    core.open(&testdata("sacm/safety-case-sanitized-strict.sacm.xmi"));
    let Some(reference) = core.element("REF_003") else {
        panic!("no REF_003");
    };
    assert_eq!(reference.kind, NodeKind::Neutral);
    let codes: Vec<&str> = reference
        .codes
        .iter()
        .map(|code| code.code.as_str())
        .collect();
    assert_eq!(codes, ["AF-VIEW-003"]);
}

#[test]
fn changing_the_language_changes_the_text_and_keeps_the_revision() {
    let mut core = core("en");
    core.open(&testdata("sacm/oasc-ja.sacm.xmi"));
    let statement = |core: &Core| core.element("G1").and_then(|details| details.statement);
    assert_eq!(
        statement(&core).as_deref(),
        Some("The autonomous vehicle is safe enough to deploy.")
    );
    let before = core.diagram().map(|at| at.diagram);
    core.set_language("ja");
    assert_eq!(
        statement(&core).as_deref(),
        Some("自動運転車は導入するのに十分安全である。")
    );
    let after = core.diagram();
    assert_eq!(after.as_ref().map(|at| at.revision), Some(1));
    assert_ne!(
        after.map(|at| at.diagram),
        before,
        "the diagram was not laid out again"
    );
}

#[test]
fn the_contract_serializes_in_camel_case_with_tags() {
    let mut core = core("en");
    let opened = core.open(&testdata("sacm/sacm_example_core_argument.sacm.xmi"));
    let json = serde_json::to_value(&opened).unwrap_or_default();
    assert_eq!(json["result"], "opened");
    assert_eq!(json["viewCodes"], 0);
    assert_eq!(json["diagnostics"]["warnings"], 0);
    let diagram = serde_json::to_value(core.diagram()).unwrap_or_default();
    let node = &diagram["diagram"]["nodes"][0];
    assert_eq!(node["kind"], "goal");
    assert_eq!(node["shape"]["type"], "rectangle");
    assert_eq!(diagram["diagram"]["edges"][0]["kind"], "supportedBy");
    assert_eq!(diagram["diagram"]["edges"][0]["arrowhead"], "filled");
}

#[test]
fn the_borrowed_diagram_serializes_exactly_as_the_contract_type() {
    let mut core = core("en");
    core.open(&testdata("sacm/sacm_example_core_argument.sacm.xmi"));
    let owned = serde_json::to_value(core.diagram()).unwrap_or_default();
    let borrowed = serde_json::to_value(core.diagram_ref()).unwrap_or_default();
    assert!(owned.is_object());
    assert_eq!(owned, borrowed);
}

#[test]
fn the_layout_number_changes_with_the_language_and_text_size_but_not_otherwise() {
    let mut core = core("en");
    core.open(&testdata("sacm/sacm_example_core_argument.sacm.xmi"));
    let layout = |core: &mut Core| core.diagram().map(|at| (at.revision, at.layout));
    let first = layout(&mut core);
    assert_eq!(layout(&mut core), first, "asking again is the same layout");
    core.set_language("ja");
    let second = layout(&mut core);
    assert_eq!(
        second.map(|(revision, _)| revision),
        first.map(|(revision, _)| revision)
    );
    assert_ne!(second, first);
    core.set_style(Style::default());
    assert_eq!(
        layout(&mut core),
        second,
        "an unchanged style lays nothing out again"
    );
    core.set_style(Style {
        text_size: 16.0,
        ..Style::default()
    });
    assert_ne!(layout(&mut core), second);
}
