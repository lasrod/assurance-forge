//! `af-api` against goal G4 of the diagram plan: `open` answers with a
//! revision and diagnostic counts or a refusal with its code, template,
//! arguments and location; `diagram` gives the render model at the revision;
//! `element` gives the details panel's content.

use af_api::Core;
use af_api::contract::{
    AcpGoal, AcpKind, ArgumentValue, EdgeKind, EditResult, ElementKind, NodeKind, OpenResult,
    ProblemRepair, ProblemSeverity, ProblemSource, Shape, TextRole,
};
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
    // The problem list's counts (P8): an Assumption as an inference's premise
    // (AF-GSN-004) and an undeveloped Goal that is supported (AF-GSN-010).
    assert_eq!(
        (diagnostics.errors, diagnostics.warnings, diagnostics.infos),
        (1, 1, 0)
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
    // SACM-MULT-001 for each Claim with two descriptions, and three
    // Strategies that support nothing (AF-GSN-008).
    assert_eq!((diagnostics.errors, diagnostics.warnings), (0, 165));
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
fn a_file_in_an_older_sacm_revision_is_refused_not_read_as_sacm_2_3() {
    // A strict read refuses SACM 2.2's namespace (SACM-XMI-008) rather than
    // reading it as SACM 2.3, which only a tolerant read does.
    let text = r#"<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:id="case">
  <name content="C"/>
</sacm:AssuranceCasePackage>"#;
    let OpenResult::Refused(refusal) = core("en").open(text.as_bytes()) else {
        panic!("a SACM 2.2 file was read");
    };
    assert_eq!(refusal.code, "SACM-XMI-008", "{refusal:?}");
}

#[test]
fn a_file_past_the_read_limits_is_refused() {
    // Nested deeper than libSACM's default limit of 256 (SACM-SEC-003).
    let mut text = String::from(
        r#"<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:id="case">"#,
    );
    text.push_str(&"<x>".repeat(300));
    text.push_str(&"</x>".repeat(300));
    text.push_str("</sacm:AssuranceCasePackage>");
    let OpenResult::Refused(refusal) = core("en").open(text.as_bytes()) else {
        panic!("a file nested 300 deep was read");
    };
    assert_eq!(refusal.code, "SACM-SEC-003", "{refusal:?}");
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
    assert_eq!(json["diagnostics"]["warnings"], 1);
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

#[test]
fn the_outline_is_the_diagrams_tree_with_context_before_what_supports() {
    let mut core = Core::without_fonts(Style::default(), "en");
    assert!(core.outline().is_none(), "nothing is open");
    assert!(core.packages().is_empty());
    core.open(&testdata("sacm/sacm_example_core_argument.sacm.xmi"));
    let Some(outline) = core.outline() else {
        panic!("no outline");
    };
    assert_eq!(outline.revision, 1);
    let rows: Vec<(&str, u32)> = outline
        .rows
        .iter()
        .map(|row| (row.id.as_str(), row.level))
        .collect();
    assert_eq!(
        rows,
        [
            ("cl_top", 1),
            ("ctx_1", 2),
            ("ar_1", 2),
            ("cl_sub_1", 3),
            ("cl_assumption_1", 4),
            ("ev_1", 4),
            ("cl_sub_2", 3),
            ("ev_2", 4),
        ]
    );
    assert_eq!(outline.rows[2].kind, NodeKind::Strategy);
    assert_eq!(outline.rows[0].name.as_deref(), Some("TopClaim"));
}

#[test]
fn every_node_of_every_sample_is_in_the_outline_once() {
    for name in [
        "sample.sacm.xmi",
        "open-autonomy-safety-case.updated.v2.sacm.xmi",
        "oasc-ja.sacm.xmi",
        "safety-case-sanitized-strict.sacm.xmi",
    ] {
        let mut core = Core::without_fonts(Style::default(), "en");
        let OpenResult::Opened { nodes, .. } = core.open(&testdata(&format!("sacm/{name}"))) else {
            panic!("{name}: refused");
        };
        let rows = core
            .outline()
            .map(|outline| outline.rows)
            .unwrap_or_default();
        assert_eq!(rows.len(), nodes as usize, "{name}");
        let mut ids: Vec<&str> = rows.iter().map(|row| row.id.as_str()).collect();
        ids.sort_unstable();
        ids.dedup();
        assert_eq!(ids.len(), nodes as usize, "{name}: a node twice");
        // A row is a root, or at most one level under the row before it.
        assert_eq!(rows.first().map(|row| row.level), Some(1), "{name}");
        for pair in rows.windows(2) {
            assert!(
                pair[1].level >= 1 && pair[1].level <= pair[0].level + 1,
                "{name}"
            );
        }
    }
}

#[test]
fn the_packages_of_the_core_sample_are_listed_with_what_they_hold() {
    let mut core = Core::without_fonts(Style::default(), "en");
    core.open(&testdata("sacm/sacm_example_core_argument.sacm.xmi"));
    let packages = core.packages();
    assert!(!packages.is_empty());
    assert_eq!(packages[0].level, 1);
    assert!(
        packages
            .iter()
            .any(|package| package.kind == "ArgumentPackage" && package.elements > 0)
    );
    for pair in packages.windows(2) {
        assert!(pair[1].level <= pair[0].level + 1);
    }
    core.close();
    assert!(core.packages().is_empty());
    assert!(core.outline().is_none());
}

#[test]
fn relationship_gives_the_details_panel_content() {
    let mut core = core("en");
    core.open(&testdata("sacm/challenges.sacm.xmi"));

    // An inference with a Strategy: its ends as SACM names them.
    let Some(inference) = core.relationship("i_hazards") else {
        panic!("no i_hazards");
    };
    assert_eq!(inference.class, "AssertedInference");
    assert_eq!(inference.kind, EdgeKind::SupportedBy);
    assert_eq!(
        inference.target.as_ref().map(|end| end.id.as_str()),
        Some("g_top")
    );
    assert_eq!(
        inference.target.as_ref().and_then(|end| end.node),
        Some(NodeKind::Goal)
    );
    let sources: Vec<&str> = inference
        .sources
        .iter()
        .map(|end| end.id.as_str())
        .collect();
    assert_eq!(sources, ["g_trap", "g_moving"]);
    let reasoning = inference.reasoning.as_ref();
    assert_eq!(reasoning.and_then(|end| end.node), Some(NodeKind::Strategy));
    assert!(inference.codes.is_empty());

    // A challenge of a relationship: its target is that relationship.
    let Some(challenge) = core.relationship("k_sample") else {
        panic!("no k_sample");
    };
    assert_eq!(challenge.kind, EdgeKind::Challenge);
    let Some(target) = challenge.target else {
        panic!("k_sample has no target");
    };
    assert_eq!(target.id, "e_trials");
    assert!(target.relationship);
    assert_eq!(target.node, None);
    assert_eq!(target.identifier, None);

    assert!(
        core.relationship("g_top").is_none(),
        "a node is no relationship"
    );
    assert!(core.relationship("nothing").is_none());

    let diagram = serde_json::to_value(core.diagram()).unwrap_or_default();
    let edges = diagram["diagram"]["edges"]
        .as_array()
        .cloned()
        .unwrap_or_default();
    let Some(edge) = edges.iter().find(|edge| edge["relationship"] == "k_sample") else {
        panic!("no edge of k_sample");
    };
    assert_eq!(edge["kind"], "challenge");
    assert_eq!(edge["line"], "dashed");
    assert_eq!(edge["arrowhead"], "open");
    assert_eq!(edge["toRelationship"], true);
}

#[test]
fn an_add_from_the_menu_is_drawn_at_a_new_revision_and_undo_takes_it_back() {
    use af_api::contract::{EditResult, ElementKind, History};
    let mut core = core("en");
    assert!(core.add_choices(None).is_none(), "nothing open");
    assert!(matches!(core.undo(), EditResult::Refused { ref code } if code == "AF-EDIT-007"));
    core.open(&testdata("sacm/sacm_example_core_argument.sacm.xmi"));
    assert_eq!(
        core.history(),
        History {
            revision: 1,
            can_undo: false,
            can_redo: false,
            modified: false
        }
    );

    // A Solution is a leaf; the top claim takes any of the six.
    let leaf = core
        .add_choices(Some("ev_1"))
        .unwrap_or_else(|| panic!("open"));
    assert_eq!(leaf.revision, 1);
    assert!(
        leaf.choices
            .iter()
            .all(|choice| choice.refusal.as_deref() == Some("AF-EDIT-001"))
    );
    let top = core
        .add_choices(Some("cl_top"))
        .unwrap_or_else(|| panic!("open"));
    let kinds: Vec<ElementKind> = top.choices.iter().map(|choice| choice.kind).collect();
    assert_eq!(
        kinds,
        [
            ElementKind::Goal,
            ElementKind::Strategy,
            ElementKind::Solution,
            ElementKind::Context,
            ElementKind::Assumption,
            ElementKind::Justification
        ]
    );
    assert!(top.choices.iter().all(|choice| choice.refusal.is_none()));
    assert!(matches!(
        core.add(Some("ev_1"), ElementKind::Goal, 1),
        EditResult::Refused { ref code } if code == "AF-EDIT-001"
    ));
    assert_eq!(core.revision(), 1, "a refused add makes no revision");
    // An add chosen from a menu of another revision is refused, and changes nothing.
    assert!(matches!(
        core.add(Some("cl_top"), ElementKind::Context, 0),
        EditResult::Refused { ref code } if code == "AF-EDIT-009"
    ));
    assert_eq!(core.revision(), 1);

    let before = core.diagram().unwrap_or_else(|| panic!("diagram"));
    let EditResult::Applied {
        revision, element, ..
    } = core.add(Some("cl_top"), ElementKind::Context, 1)
    else {
        panic!("not added");
    };
    let element = element.unwrap_or_default();
    assert_eq!(revision, 2);
    let after = core.diagram().unwrap_or_else(|| panic!("diagram"));
    assert_eq!(after.revision, 2);
    assert!(after.layout > before.layout);
    assert_eq!(after.diagram.nodes.len(), before.diagram.nodes.len() + 1);
    assert!(after.diagram.edges.iter().any(|edge| edge.from == "cl_top"
        && edge.to == element
        && edge.kind == EdgeKind::InContextOf));
    let details = core.element(&element).unwrap_or_else(|| panic!("details"));
    assert_eq!(details.kind, NodeKind::Context);
    assert_eq!(details.identifier, "C1");
    assert_eq!(core.outline().map(|outline| outline.revision), Some(2));
    assert_eq!(
        core.history(),
        History {
            revision: 2,
            can_undo: true,
            can_redo: false,
            modified: true
        }
    );

    // Undo and redo are revisions of their own.
    assert!(matches!(
        core.undo(),
        EditResult::Applied {
            revision: 3,
            element: None,
            ..
        }
    ));
    assert!(core.element(&element).is_none());
    assert_eq!(
        core.diagram().map(|at| at.diagram.nodes.len()),
        Some(before.diagram.nodes.len())
    );
    assert!(matches!(
        core.redo(),
        EditResult::Applied { revision: 4, .. }
    ));
    assert!(core.element(&element).is_some());
    assert!(matches!(core.redo(), EditResult::Refused { ref code } if code == "AF-EDIT-008"));

    // A file opened after is at a later revision still.
    let reopened = core.open(&testdata("sacm/sample.sacm.xmi"));
    assert!(
        matches!(reopened, OpenResult::Opened { revision: 5, .. }),
        "{reopened:?}"
    );
    assert!(!core.history().can_undo, "the history is the file's");
}

#[test]
fn a_removal_lists_what_goes_and_undo_brings_it_back() {
    use af_api::contract::EditResult;
    let mut core = core("en");
    assert!(core.removal("cl_top").is_none(), "nothing open");
    assert!(matches!(
        core.remove("cl_top", 0),
        EditResult::Refused { ref code } if code == "AF-EDIT-007"
    ));
    core.open(&testdata("sacm/sacm_example_core_argument.sacm.xmi"));
    let before = core.diagram().unwrap_or_else(|| panic!("diagram"));

    let removal = core.removal("cl_sub_1").unwrap_or_else(|| panic!("open"));
    assert_eq!(removal.revision, 1);
    assert_eq!(removal.refusal, None);
    assert_eq!(removal.nodes[0].id, "cl_sub_1");
    assert!(removal.nodes.len() > 1, "{:?}", removal.nodes);
    let unknown = core.removal("nothing").unwrap_or_else(|| panic!("open"));
    assert_eq!(unknown.refusal.as_deref(), Some("AF-EDIT-011"));
    assert!(unknown.nodes.is_empty());

    // A removal shown at another revision is refused.
    assert!(matches!(
        core.remove("cl_sub_1", 0),
        EditResult::Refused { ref code } if code == "AF-EDIT-009"
    ));
    let EditResult::Applied {
        revision,
        element,
        deleted,
        ..
    } = core.remove("cl_sub_1", 1)
    else {
        panic!("not removed");
    };
    assert_eq!(revision, 2);
    // The node it was drawn under is selected.
    assert!(element.is_some());
    assert!(deleted.iter().any(|id| id == "cl_sub_1"));
    let after = core.diagram().unwrap_or_else(|| panic!("diagram"));
    assert_eq!(
        after.diagram.nodes.len(),
        before.diagram.nodes.len() - removal.nodes.len()
    );
    for node in &removal.nodes {
        assert!(core.element(&node.id).is_none(), "{}", node.id);
    }
    assert!(matches!(core.undo(), EditResult::Applied { .. }));
    assert_eq!(
        core.diagram().map(|at| at.diagram.nodes.len()),
        Some(before.diagram.nodes.len())
    );
}

#[test]
fn a_text_set_in_element_properties_is_drawn_at_a_new_revision_and_undo_takes_it_back() {
    use af_api::contract::{EditResult, TextField};
    let mut core = core("en");
    assert!(matches!(
        core.set_text("cl_top", TextField::Name, "x"),
        EditResult::Refused { ref code } if code == "AF-EDIT-007"
    ));
    core.open(&testdata("sacm/sacm_example_core_argument.sacm.xmi"));
    let was = core.element("cl_top").unwrap_or_else(|| panic!("cl_top"));

    for (field, text) in [
        (TextField::Identifier, "G1"),
        (TextField::Name, "Braking is safe"),
        (
            TextField::Statement,
            "The braking function is acceptably safe.",
        ),
    ] {
        assert!(
            matches!(
                core.set_text("cl_top", field, text),
                EditResult::Applied { element: None, .. }
            ),
            "{field:?}"
        );
    }
    assert_eq!(core.history().revision, 4);
    let now = core.element("cl_top").unwrap_or_else(|| panic!("cl_top"));
    assert_eq!(now.identifier, "G1");
    assert_eq!(now.name.as_deref(), Some("Braking is safe"));
    assert_eq!(
        now.statement.as_deref(),
        Some("The braking function is acceptably safe.")
    );
    // The diagram and the outline are of the new text.
    let diagram = core.diagram().unwrap_or_else(|| panic!("open"));
    let drawn = diagram
        .diagram
        .nodes
        .iter()
        .find(|node| node.id == "cl_top")
        .unwrap_or_else(|| panic!("cl_top is drawn"));
    assert!(drawn.text.iter().any(|line| line.text == "G1"));
    let outline = core.outline().unwrap_or_else(|| panic!("open"));
    assert!(outline.rows.iter().any(|row| row.identifier == "G1"));

    // The same text again is no command; a refusal is none either.
    assert_eq!(
        core.set_text("cl_top", TextField::Name, "Braking is safe"),
        EditResult::Unchanged
    );
    assert!(matches!(
        core.set_text("cl_top", TextField::Identifier, ""),
        EditResult::Refused { ref code } if code == "AF-EDIT-012"
    ));
    assert!(matches!(
        core.set_text("cl_top", TextField::Identifier, "cl_sub_2"),
        EditResult::Refused { ref code } if code == "AF-EDIT-013"
    ));
    assert_eq!(core.history().revision, 4);

    // Each text is one undo step.
    for _ in 0..3 {
        assert!(matches!(core.undo(), EditResult::Applied { .. }));
    }
    assert!(!core.history().can_undo);
    assert_eq!(core.element("cl_top"), Some(was));
}

#[test]
fn the_problem_list_names_its_elements_follows_an_edit_and_its_undo() {
    let mut core = core("en");
    let opened = core.open(&testdata("sacm/sacm_example_core_argument.sacm.xmi"));
    let OpenResult::Opened { diagnostics, .. } = opened else {
        panic!("refused: {opened:?}");
    };
    let Some(before) = core.problems() else {
        panic!("no problems list");
    };
    // One count, the list's (P8).
    assert_eq!(diagnostics, before.counts);
    assert_eq!(before.revision, 1);
    assert_eq!((before.counts.errors, before.counts.warnings), (1, 1));
    let first = &before.problems[0];
    assert_eq!(first.code, "AF-GSN-004");
    assert_eq!(first.source, ProblemSource::Gsn);
    assert_eq!(first.severity, ProblemSeverity::Error);
    assert_eq!(first.requirement.as_deref(), Some("GSN3-CORE-015"));
    let named: Vec<(&str, Option<&str>, Option<NodeKind>)> = first
        .elements
        .iter()
        .map(|element| {
            (
                element.id.as_str(),
                element.identifier.as_deref(),
                element.kind,
            )
        })
        .collect();
    assert_eq!(
        named,
        [
            ("cl_sub_1", Some("cl_sub_1"), Some(NodeKind::Goal)),
            (
                "cl_assumption_1",
                Some("cl_assumption_1"),
                Some(NodeKind::Assumption)
            ),
        ]
    );

    // A Strategy added alone supports nothing yet: a warning, at the new
    // revision. Undo takes it back.
    let revision = core.revision();
    let EditResult::Applied {
        element: Some(strategy),
        ..
    } = core.add(Some("cl_top"), ElementKind::Strategy, revision)
    else {
        panic!("not added");
    };
    let Some(after) = core.problems() else {
        panic!("no problems list");
    };
    assert_eq!(after.revision, revision + 1);
    assert_eq!(after.counts.warnings, 2);
    assert!(
        after
            .problems
            .iter()
            .any(|problem| problem.code == "AF-GSN-008"
                && problem.elements.first().map(|element| element.id.as_str())
                    == Some(strategy.as_str()))
    );
    assert!(matches!(core.undo(), EditResult::Applied { .. }));
    let Some(undone) = core.problems() else {
        panic!("no problems list");
    };
    assert_eq!(undone.problems, before.problems);

    core.close();
    assert!(core.problems().is_none());
}

#[test]
fn a_move_says_where_a_node_may_go_and_undo_takes_it_back() {
    use af_api::contract::EditResult;
    let mut core = core("en");
    assert!(core.move_targets("ev_2").is_none(), "nothing open");
    assert!(matches!(
        core.move_node("ev_2", Some("cl_sub_1"), 0),
        EditResult::Refused { ref code } if code == "AF-EDIT-007"
    ));
    core.open(&testdata("sacm/sacm_example_core_argument.sacm.xmi"));
    let under = |core: &mut Core, parent: &str| {
        let diagram = core.diagram().unwrap_or_else(|| panic!("diagram")).diagram;
        diagram
            .edges
            .iter()
            .any(|edge| edge.from == parent && edge.to == "ev_2")
    };
    assert!(under(&mut core, "cl_sub_2"));

    let targets = core.move_targets("ev_2").unwrap_or_else(|| panic!("open"));
    assert_eq!((targets.revision, targets.refusal.as_deref()), (1, None));
    // Evidence is not a top-level element.
    assert_eq!(targets.canvas.as_deref(), Some("AF-EDIT-006"));
    let refusal = |id: &str| {
        targets
            .nodes
            .iter()
            .find(|target| target.id == id)
            .unwrap_or_else(|| panic!("{id}"))
            .refusal
            .as_deref()
    };
    assert_eq!(refusal("cl_sub_1"), None);
    assert_eq!(refusal("cl_sub_2"), Some("AF-EDIT-019"));
    assert_eq!(refusal("ev_2"), Some("AF-EDIT-018"));
    assert_eq!(refusal("ar_1"), Some("AF-EDIT-003"));
    assert_eq!(refusal("ev_1"), Some("AF-EDIT-001"));
    let unknown = core
        .move_targets("nothing")
        .unwrap_or_else(|| panic!("open"));
    assert_eq!(unknown.refusal.as_deref(), Some("AF-EDIT-011"));
    assert!(unknown.nodes.is_empty());

    // Targets shown at another revision, and a place that was refused.
    assert!(matches!(
        core.move_node("ev_2", Some("cl_sub_1"), 0),
        EditResult::Refused { ref code } if code == "AF-EDIT-009"
    ));
    assert!(matches!(
        core.move_node("ev_2", Some("ar_1"), 1),
        EditResult::Refused { ref code } if code == "AF-EDIT-003"
    ));
    let EditResult::Applied {
        revision,
        element,
        created,
        deleted,
        ..
    } = core.move_node("ev_2", Some("cl_sub_1"), 1)
    else {
        panic!("not moved");
    };
    assert_eq!(revision, 2);
    // The node moved stays selected, and its line was given a new end.
    assert_eq!(element.as_deref(), Some("ev_2"));
    assert!(created.is_empty() && deleted.is_empty());
    assert!(under(&mut core, "cl_sub_1") && !under(&mut core, "cl_sub_2"));

    assert!(matches!(core.undo(), EditResult::Applied { .. }));
    assert!(under(&mut core, "cl_sub_2") && !under(&mut core, "cl_sub_1"));
}

#[test]
fn the_undeveloped_mark_is_set_where_it_can_be_and_a_wrong_join_is_repaired() {
    use af_api::contract::EditResult;
    let mut core = core("en");
    assert!(matches!(
        core.set_undeveloped("cl_sub_2", false, 0),
        EditResult::Refused { ref code } if code == "AF-EDIT-007"
    ));
    core.open(&testdata("sacm/sacm_example_core_argument.sacm.xmi"));
    let refusal = |core: &Core, id: &str| {
        core.element(id)
            .unwrap_or_else(|| panic!("{id}"))
            .undeveloped_refusal
    };
    // A stale mark is cleared; a supported Goal is not marked again.
    assert_eq!(refusal(&core, "cl_sub_2"), None);
    assert_eq!(refusal(&core, "cl_top").as_deref(), Some("AF-EDIT-022"));
    assert_eq!(refusal(&core, "ev_1").as_deref(), Some("AF-EDIT-021"));
    assert!(matches!(
        core.set_undeveloped("cl_top", true, 1),
        EditResult::Refused { ref code } if code == "AF-EDIT-022"
    ));
    assert!(matches!(
        core.set_undeveloped("cl_sub_2", true, 1),
        EditResult::Unchanged
    ));
    // Asked at another revision, it is refused.
    assert!(matches!(
        core.set_undeveloped("cl_sub_2", false, 0),
        EditResult::Refused { ref code } if code == "AF-EDIT-009"
    ));
    assert!(matches!(
        core.set_undeveloped("cl_sub_2", false, 1),
        EditResult::Applied { revision: 2, .. }
    ));
    assert_eq!(
        core.element("cl_sub_2").map(|details| details.undeveloped),
        Some(false)
    );
    assert_eq!(refusal(&core, "cl_sub_2").as_deref(), Some("AF-EDIT-022"));

    // The example's Assumption is joined by a support line: the problem
    // that reports it offers the repair.
    let problems = core.problems().unwrap_or_else(|| panic!("open"));
    let wrong: Vec<_> = problems
        .problems
        .iter()
        .filter(|problem| problem.code == "AF-GSN-004")
        .collect();
    assert_eq!(wrong.len(), 1);
    let (element, relationship) = match wrong[0].repairs.as_slice() {
        [
            ProblemRepair::MakeContext {
                element,
                relationship,
            },
        ] => (element.clone(), relationship.clone()),
        other => panic!("repairs: {other:?}"),
    };
    assert_eq!(
        (element.as_str(), relationship.as_str()),
        ("cl_assumption_1", "inf_2")
    );
    assert!(
        problems
            .problems
            .iter()
            .all(|problem| problem.code == "AF-GSN-004" || problem.repairs.is_empty())
    );
    assert!(matches!(
        core.repair_join(&element, &relationship, 1),
        EditResult::Refused { ref code } if code == "AF-EDIT-009"
    ));
    let EditResult::Applied { element, .. } = core.repair_join(&element, &relationship, 2) else {
        panic!("not repaired");
    };
    assert_eq!(element.as_deref(), Some("cl_assumption_1"));
    let after = core.problems().unwrap_or_else(|| panic!("open"));
    assert!(
        after
            .problems
            .iter()
            .all(|problem| problem.code != "AF-GSN-004")
    );
    assert!(matches!(core.undo(), EditResult::Applied { .. }));
    let back = core.problems().unwrap_or_else(|| panic!("open"));
    assert!(
        back.problems
            .iter()
            .any(|problem| problem.code == "AF-GSN-004")
    );
}

#[test]
fn a_node_and_a_line_are_challenged_each_in_one_undo_step() {
    let mut core = core("en");
    assert!(core.challenge_choices("cl_top", None).is_none());
    core.open(&testdata("sacm/sacm_example_core_argument.sacm.xmi"));
    let challenges = |core: &mut Core| {
        core.diagram()
            .unwrap_or_else(|| panic!("open"))
            .diagram
            .edges
            .iter()
            .filter(|edge| edge.kind == EdgeKind::Challenge)
            .count()
    };
    let choices = core
        .challenge_choices("cl_sub_1", None)
        .unwrap_or_else(|| panic!("open"));
    let kinds: Vec<_> = choices.choices.iter().map(|choice| choice.kind).collect();
    assert_eq!(kinds, [ElementKind::Goal, ElementKind::Solution]);
    assert!(
        choices
            .choices
            .iter()
            .all(|choice| choice.refusal.is_none())
    );
    // A menu of another revision is out of date.
    assert!(matches!(
        core.challenge("cl_sub_1", None, ElementKind::Goal, 0),
        EditResult::Refused { ref code } if code == "AF-EDIT-009"
    ));
    let EditResult::Applied {
        revision, element, ..
    } = core.challenge("cl_sub_1", None, ElementKind::Goal, 1)
    else {
        panic!("not challenged");
    };
    assert_eq!(revision, 2);
    let challenger = element.unwrap_or_else(|| panic!("no challenger"));
    assert_eq!(
        core.element(&challenger).map(|details| details.kind),
        Some(NodeKind::Goal)
    );
    assert_eq!(challenges(&mut core), 1);
    // The line the challenger is drawn by is challenged with evidence.
    let line = core
        .diagram()
        .unwrap_or_else(|| panic!("open"))
        .diagram
        .edges
        .iter()
        .find(|edge| edge.from == challenger)
        .map(|edge| {
            (
                edge.relationship.clone(),
                edge.from.clone(),
                edge.to.clone(),
            )
        })
        .unwrap_or_else(|| panic!("no line"));
    let EditResult::Applied { element, .. } =
        core.challenge(&line.0, Some((&line.1, &line.2)), ElementKind::Solution, 2)
    else {
        panic!("the line is not challenged");
    };
    let evidence = element.unwrap_or_else(|| panic!("no challenger"));
    assert_eq!(
        core.element(&evidence).map(|details| details.kind),
        Some(NodeKind::Solution)
    );
    assert_eq!(challenges(&mut core), 2);
    assert!(matches!(core.undo(), EditResult::Applied { .. }));
    assert_eq!(challenges(&mut core), 1);
    assert!(matches!(core.undo(), EditResult::Applied { .. }));
    assert_eq!(challenges(&mut core), 0);
}

#[test]
fn a_package_is_shown_as_its_own_diagram_and_the_whole_case_again_without_one() {
    let mut core = core("en");
    core.open(&testdata("sacm/oasc-ja.sacm.xmi"));
    let whole = core.diagram().unwrap_or_else(|| panic!("no diagram"));
    assert_eq!(whole.package, None);

    assert!(core.show_package(Some("ARG_VehicleSafety")));
    let vehicle = core.diagram().unwrap_or_else(|| panic!("no diagram"));
    // A new layout of the same revision, of the package.
    assert_eq!(vehicle.revision, whole.revision);
    assert!(vehicle.layout > whole.layout);
    assert_eq!(vehicle.package.as_deref(), Some("ARG_VehicleSafety"));
    assert!(vehicle.diagram.nodes.len() < whole.diagram.nodes.len());
    // G4 of the main argument is drawn, marked with its package.
    let g4 = vehicle
        .diagram
        .nodes
        .iter()
        .find(|node| node.id == "G4")
        .unwrap_or_else(|| panic!("no G4"));
    // The package it belongs to, which a click on the mark opens.
    assert_eq!(g4.package.as_deref(), Some("ARG_Main"));
    // Wrapped as any text is: the lines, read together, are the name.
    let package: Vec<&str> = g4
        .text
        .iter()
        .filter(|line| line.role == TextRole::Package)
        .map(|line| line.text.trim())
        .collect();
    assert_eq!(
        package.join(" "),
        "[Open Autonomy Safety Case - Main Argument]"
    );

    // Not a package: the diagram stays as it was.
    assert!(!core.show_package(Some("G4")));
    assert_eq!(core.diagram().map(|at| at.layout), Some(vehicle.layout));

    assert!(core.show_package(None));
    let again = core.diagram().unwrap_or_else(|| panic!("no diagram"));
    assert_eq!(again.package, None);
    assert_eq!(again.diagram.nodes.len(), whole.diagram.nodes.len());
}

#[test]
fn an_acp_is_added_in_one_undo_step_and_its_package_shown_until_undone() {
    let mut core = core("en");
    core.open(&testdata("sacm/acps.sacm.xmi"));
    let choices = core
        .acp_choices("E2", "G3", "Sn2")
        .unwrap_or_else(|| panic!("nothing open"));
    assert_eq!(choices.identifier, "ACP5");
    assert!(
        choices
            .choices
            .iter()
            .all(|choice| choice.refusal.is_none())
    );
    assert_eq!(core.acp_node_refusal("Sn1").as_deref(), Some("AF-EDIT-030"));

    // A description: the Claim is answered, and shown in the details panel.
    let EditResult::Applied {
        element: Some(claim),
        ..
    } = core.add_acp("E2", "G3", "Sn2", AcpKind::Description, "x", 1)
    else {
        panic!("not added");
    };
    let details = core.element(&claim).unwrap_or_else(|| panic!("no details"));
    assert!(details.acp_description);
    assert_eq!(details.identifier, "ACP5");
    assert_eq!(details.package.as_deref(), Some("argpkg_main"));
    let drawn = core.diagram().unwrap_or_else(|| panic!("no diagram"));
    let on = drawn
        .diagram
        .edges
        .iter()
        .find(|edge| edge.relationship == "E2")
        .unwrap_or_else(|| panic!("no E2"));
    assert_eq!(on.acps.len(), 1);
    assert_eq!(on.acps[0].label.text, "ACP5");

    // A confidence argument, at a later revision: a stale menu is refused.
    assert!(matches!(
        core.add_acp("X1", "G1", "C1", AcpKind::Argument, "Confidence ACP6", 1),
        EditResult::Refused { ref code } if code == "AF-EDIT-009"
    ));
    let EditResult::Applied {
        element: Some(goal),
        revision,
        ..
    } = core.add_acp("X1", "G1", "C1", AcpKind::Argument, "Confidence ACP6", 2)
    else {
        panic!("not added");
    };
    assert_eq!(revision, 3);
    let package = core
        .element(&goal)
        .and_then(|details| details.package)
        .unwrap_or_else(|| panic!("no package"));
    assert!(core.show_package(Some(&package)));
    assert_eq!(
        core.diagram().map(|at| at.diagram.nodes.len()),
        Some(1),
        "the confidence argument's diagram holds its goal"
    );
    // Undone in one step: the package goes, and the whole case is shown.
    assert!(matches!(core.undo(), EditResult::Applied { .. }));
    assert!(core.element(&goal).is_none());
    assert_eq!(core.diagram().map(|at| at.package), Some(None));
}

#[test]
fn an_acp_is_removed_with_or_without_its_confidence_argument_in_one_undo_step() {
    let mut core = core("en");
    core.open(&testdata("sacm/acps.sacm.xmi"));
    let acps = |core: &mut Core| -> Vec<String> {
        core.diagram()
            .map(|at| {
                at.diagram
                    .edges
                    .iter()
                    .flat_map(|edge| edge.acps.iter().map(|acp| acp.label.text.clone()))
                    .collect()
            })
            .unwrap_or_default()
    };
    assert_eq!(acps(&mut core), ["ACP1", "ACP2[Confidence]", "ACP4"]);

    // A description is listed, and goes with its ACP.
    let removal = core
        .acp_removal("I1", "ACP1")
        .unwrap_or_else(|| panic!("nothing open"));
    assert_eq!(removal.label, "ACP1");
    assert_eq!(removal.refusal, None);
    assert!(
        removal
            .description
            .as_ref()
            .is_some_and(|node| node.acp_description)
    );
    assert_eq!(removal.argument, None);

    // A confidence argument is listed with its package and nodes.
    let removal = core
        .acp_removal("E1", "ACP2")
        .unwrap_or_else(|| panic!("nothing open"));
    assert_eq!(removal.label, "ACP2[Confidence]");
    assert_eq!(removal.description, None);
    let argument = removal.argument.unwrap_or_else(|| panic!("no argument"));
    assert_eq!(argument.name.as_deref(), Some("Confidence"));
    let identifiers: Vec<&str> = argument
        .nodes
        .iter()
        .map(|node| node.identifier.as_str())
        .collect();
    assert_eq!(identifiers.first().copied(), Some("ACP2"));
    assert_eq!(argument.nodes.len(), 2);
    assert_eq!(argument.refusal, None);

    // No such ACP: a code, not a sentence.
    assert_eq!(
        core.acp_removal("E2", "ACP1")
            .and_then(|removal| removal.refusal)
            .as_deref(),
        Some("AF-EDIT-031")
    );

    // Deleted with its argument, in one undo step.
    assert!(matches!(
        core.remove_acp("E1", "ACP2", true, 0),
        EditResult::Refused { ref code } if code == "AF-EDIT-009"
    ));
    assert!(matches!(
        core.remove_acp("E1", "ACP2", true, 1),
        EditResult::Applied { revision: 2, .. }
    ));
    assert_eq!(acps(&mut core), ["ACP1", "ACP4"]);
    assert!(core.element("ACP2").is_none());
    assert!(
        core.packages()
            .iter()
            .all(|package| package.id != "argpkg_confidence")
    );
    assert!(matches!(core.undo(), EditResult::Applied { .. }));
    assert_eq!(acps(&mut core), ["ACP1", "ACP2[Confidence]", "ACP4"]);

    // Kept: the package stays, and only the ACP goes.
    assert!(matches!(
        core.remove_acp("E1", "ACP2", false, 3),
        EditResult::Applied { .. }
    ));
    assert_eq!(acps(&mut core), ["ACP1", "ACP4"]);
    assert!(core.element("ACP2").is_some());
    assert!(
        core.packages()
            .iter()
            .any(|package| package.id == "argpkg_confidence")
    );
}

#[test]
fn a_kept_confidence_argument_is_linked_back_in_one_undo_step() {
    let mut core = core("en");
    core.open(&testdata("sacm/acps.sacm.xmi"));
    let acps = |core: &mut Core| -> Vec<String> {
        core.diagram()
            .map(|at| {
                at.diagram
                    .edges
                    .iter()
                    .flat_map(|edge| edge.acps.iter().map(|acp| acp.label.text.clone()))
                    .collect()
            })
            .unwrap_or_default()
    };
    // Its ACP removed, the argument kept.
    assert!(matches!(
        core.remove_acp("E1", "ACP2", false, 1),
        EditResult::Applied { revision: 2, .. }
    ));
    assert_eq!(acps(&mut core), ["ACP1", "ACP4"]);

    // The line's menu offers its top goal, labelled as the ACP would be.
    let choices = core
        .acp_choices("E1", "G2", "Sn1")
        .unwrap_or_else(|| panic!("nothing open"));
    assert_eq!(choices.revision, 2);
    assert_eq!(
        choices.goals,
        [AcpGoal {
            claim: "ACP2".to_owned(),
            label: "ACP2[Confidence]".to_owned(),
            refusal: None,
        }]
    );

    // Linked as the menu of its revision offered, in one undo step.
    assert!(matches!(
        core.link_acp("E1", "G2", "Sn1", "ACP2", 1),
        EditResult::Refused { ref code } if code == "AF-EDIT-009"
    ));
    assert!(matches!(
        core.link_acp("E1", "G2", "Sn1", "ACP2", 2),
        EditResult::Applied {
            revision: 3,
            element: None,
            ..
        }
    ));
    assert_eq!(acps(&mut core), ["ACP1", "ACP2[Confidence]", "ACP4"]);
    // Now this line's: refused, with a code.
    let again = core
        .acp_choices("E1", "G2", "Sn1")
        .unwrap_or_else(|| panic!("nothing open"));
    assert_eq!(again.goals[0].refusal.as_deref(), Some("AF-EDIT-033"));
    assert!(matches!(
        core.link_acp("E1", "G2", "Sn1", "ACP2", 3),
        EditResult::Refused { ref code } if code == "AF-EDIT-033"
    ));
    assert!(matches!(core.undo(), EditResult::Applied { .. }));
    assert_eq!(acps(&mut core), ["ACP1", "ACP4"]);
}

#[test]
fn a_top_level_goal_goes_into_the_package_shown() {
    let mut core = core("en");
    core.open(&testdata("sacm/acps.sacm.xmi"));
    assert!(core.show_package(Some("argpkg_confidence")));
    let EditResult::Applied {
        element: Some(goal),
        ..
    } = core.add(None, ElementKind::Goal, 1)
    else {
        panic!("not added");
    };
    assert_eq!(
        core.element(&goal)
            .and_then(|details| details.package)
            .as_deref(),
        Some("argpkg_confidence")
    );
    assert!(
        core.diagram()
            .is_some_and(|at| at.diagram.nodes.iter().any(|node| node.id == goal)),
        "drawn in the diagram shown"
    );
}

#[test]
fn a_removal_lists_an_acp_description_as_one() {
    let mut core = core("en");
    core.open(&testdata("sacm/acps.sacm.xmi"));
    let EditResult::Applied {
        element: Some(claim),
        ..
    } = core.add_acp("E2", "G3", "Sn2", AcpKind::Description, "x", 1)
    else {
        panic!("not added");
    };
    let removal = core
        .removal("Sn2")
        .unwrap_or_else(|| panic!("nothing open"));
    let listed: Vec<(String, bool)> = removal
        .nodes
        .iter()
        .map(|node| (node.id.clone(), node.acp_description))
        .collect();
    assert_eq!(listed, [("Sn2".to_owned(), false), (claim, true)]);
}

#[test]
fn an_acp_label_on_a_line_that_runs_across_is_clear_of_its_nodes() {
    let mut core = core("en");
    core.open(&testdata("sacm/acps.sacm.xmi"));
    // A long label, on the short context line from G1 to C1.
    assert!(matches!(
        core.add_acp("X1", "G1", "C1", AcpKind::Argument, "Confidence ACP5", 1),
        EditResult::Applied { .. }
    ));
    let at = core.diagram().unwrap_or_else(|| panic!("no diagram"));
    let x1 = at
        .diagram
        .edges
        .iter()
        .find(|edge| edge.relationship == "X1")
        .unwrap_or_else(|| panic!("no X1"));
    assert_eq!(x1.acps.len(), 2);
    for acp in &x1.acps {
        let label = &acp.label;
        let (left, right) = (label.x, label.x + label.width);
        let (top, bottom) = (
            label.baseline - label.size,
            label.baseline + label.size * 0.3,
        );
        for node in &at.diagram.nodes {
            let shape = node.bounds;
            let apart = right <= shape.x
                || left >= shape.x + shape.width
                || bottom <= shape.y
                || top >= shape.y + shape.height;
            assert!(apart, "{} lies on {}", label.text, node.id);
        }
    }
}

#[test]
fn an_acp_label_on_a_line_that_runs_down_is_beside_its_square() {
    let mut core = core("en");
    core.open(&testdata("sacm/acps.sacm.xmi"));
    let at = core.diagram().unwrap_or_else(|| panic!("no diagram"));
    let e1 = at
        .diagram
        .edges
        .iter()
        .find(|edge| edge.relationship == "E1")
        .unwrap_or_else(|| panic!("no E1"));
    let acp = &e1.acps[0];
    assert!(acp.label.x > acp.square.x + acp.square.width);
    let middle = acp.square.y + acp.square.height / 2.0;
    assert!((acp.label.baseline - acp.label.size / 2.0 - middle).abs() < acp.label.size);
}

#[test]
fn every_acp_square_is_on_its_line_however_many_it_carries() {
    let mut core = core("en");
    core.open(&testdata("sacm/acps.sacm.xmi"));
    // Three on the line G2 to Sn1: ACP2, and two more.
    for (kind, revision) in [(AcpKind::Description, 1), (AcpKind::Description, 2)] {
        assert!(matches!(
            core.add_acp("E1", "G2", "Sn1", kind, "x", revision),
            EditResult::Applied { .. }
        ));
    }
    let at = core.diagram().unwrap_or_else(|| panic!("no diagram"));
    let distance = |p: (f32, f32), a: (f32, f32), b: (f32, f32)| {
        let (dx, dy) = (b.0 - a.0, b.1 - a.1);
        let length = dx * dx + dy * dy;
        let t = if length == 0.0 {
            0.0
        } else {
            (((p.0 - a.0) * dx + (p.1 - a.1) * dy) / length).clamp(0.0, 1.0)
        };
        (p.0 - (a.0 + t * dx)).hypot(p.1 - (a.1 + t * dy))
    };
    let mut seen = 0;
    for edge in &at.diagram.edges {
        for acp in &edge.acps {
            seen += 1;
            let centre = (
                acp.square.x + acp.square.width / 2.0,
                acp.square.y + acp.square.height / 2.0,
            );
            let nearest = edge
                .points
                .windows(2)
                .map(|pair| distance(centre, (pair[0].x, pair[0].y), (pair[1].x, pair[1].y)))
                .fold(f32::MAX, f32::min);
            assert!(
                nearest < 0.5,
                "{} is {nearest} px off its line",
                acp.label.text
            );
        }
    }
    assert_eq!(seen, 5);
}

#[test]
fn a_removal_names_the_acps_it_takes() {
    let mut core = core("en");
    core.open(&testdata("sacm/acps.sacm.xmi"));
    let removal = core
        .removal("Sn1")
        .unwrap_or_else(|| panic!("nothing open"));
    assert_eq!(removal.acps, ["ACP2[Confidence]".to_owned()]);
    // G2 is a premise of I1, which carries ACP1: taking it out would change
    // what ACP1 is about, so the removal is refused (W25).
    let removal = core.removal("G2").unwrap_or_else(|| panic!("nothing open"));
    assert_eq!(removal.refusal.as_deref(), Some("AF-EDIT-015"));
}

#[test]
fn a_line_is_removed_as_its_removal_said_and_undo_brings_it_back() {
    let mut core = core("en");
    core.open(&testdata("sacm/sacm_example_core_argument.sacm.xmi"));
    let lines = |core: &mut Core| -> usize {
        core.diagram()
            .map(|at| at.diagram.edges.len())
            .unwrap_or_default()
    };
    let before = lines(&mut core);
    let refusal = |core: &Core, relationship: &str, from: &str, to: &str| {
        core.line_removal(relationship, from, to)
            .and_then(|removal| removal.refusal)
    };
    // Codes, not sentences: the Strategy's first line, and a Solution known
    // only by its line.
    assert_eq!(
        refusal(&core, "inf_1", "cl_top", "ar_1").as_deref(),
        Some("AF-EDIT-036")
    );
    assert_eq!(
        refusal(&core, "ae_1", "cl_sub_1", "ev_1").as_deref(),
        Some("AF-EDIT-037")
    );
    // The assumption a support line joins (AF-GSN-004): it goes, and the
    // assumption stays.
    let removal = core
        .line_removal("inf_2", "cl_sub_1", "cl_assumption_1")
        .unwrap_or_else(|| panic!("nothing open"));
    assert_eq!((removal.refusal, removal.acps.len()), (None, 0));
    assert!(matches!(
        core.remove_line("inf_2", "cl_sub_1", "cl_assumption_1", 0),
        EditResult::Refused { ref code } if code == "AF-EDIT-009"
    ));
    let made = core.remove_line("inf_2", "cl_sub_1", "cl_assumption_1", removal.revision);
    assert!(
        matches!(made, EditResult::Applied { ref element, .. } if element.as_deref() == Some("cl_sub_1")),
        "{made:?}"
    );
    assert_eq!(lines(&mut core), before - 1);
    assert!(core.element("cl_assumption_1").is_some());
    assert!(matches!(core.undo(), EditResult::Applied { .. }));
    assert_eq!(lines(&mut core), before);
}

#[test]
fn a_cycle_offers_its_lines_and_one_removed_breaks_it() {
    let mut core = core("en");
    core.open(&testdata("sacm/gallery-roots-and-cycle.sacm.xmi"));
    let cycles = |core: &mut Core| -> Vec<af_api::contract::Problem> {
        core.problems()
            .map(|problems| problems.problems)
            .unwrap_or_default()
            .into_iter()
            .filter(|problem| problem.code == "AF-GSN-001")
            .collect()
    };
    let found = cycles(&mut core);
    assert_eq!(found.len(), 2, "setup: {found:?}");
    // Each cycle offers its support lines, as codes and ids, not sentences.
    let offered: Vec<(String, String, String)> = found[0]
        .repairs
        .iter()
        .filter_map(|repair| match repair {
            ProblemRepair::RemoveLine {
                relationship,
                from,
                to,
                refusal: None,
            } => Some((relationship.clone(), from.clone(), to.clone())),
            _ => None,
        })
        .collect();
    assert!(!offered.is_empty(), "{:?}", found[0].repairs);
    let (relationship, from, to) = &offered[0];
    let removal = core
        .line_removal(relationship, from, to)
        .unwrap_or_else(|| panic!("nothing open"));
    assert_eq!(removal.refusal, None);
    assert!(matches!(
        core.remove_line(relationship, from, to, removal.revision),
        EditResult::Applied { .. }
    ));
    assert_eq!(cycles(&mut core).len(), 1);
}

#[test]
fn a_shared_identifier_is_renumbered_as_its_problem_offered_and_undo_takes_it_back() {
    let mut core = core("en");
    let tagged = |id: &str| {
        format!(
            r#"<argumentElement xmi:type="sacm:Claim" xmi:id="{id}"><name content="{id}"/><taggedValue xmi:id="t{id}"><key><value content="assuranceForge.gsn.identifier"/></key><content><value content="G1"/></content></taggedValue></argumentElement>"#
        )
    };
    let xml = format!(
        r#"<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:id="case">
  <name content="Case"/>
  <argumentPackage xmi:id="p"><name content="P"/>{}{}</argumentPackage>
</sacm:AssuranceCasePackage>"#,
        tagged("a"),
        tagged("b")
    );
    assert!(matches!(
        core.open(xml.as_bytes()),
        OpenResult::Opened { .. }
    ));
    let shared = |core: &mut Core| -> Vec<af_api::contract::Problem> {
        core.problems()
            .map(|problems| problems.problems)
            .unwrap_or_default()
            .into_iter()
            .filter(|problem| problem.code == "AF-GSN-009")
            .collect()
    };
    let found = shared(&mut core);
    let nodes = match found.as_slice() {
        [problem] => match problem.repairs.as_slice() {
            [
                ProblemRepair::Renumber {
                    nodes,
                    given,
                    refusal: None,
                },
            ] => {
                let given: Vec<(&str, &str)> = given
                    .iter()
                    .map(|node| (node.element.as_str(), node.identifier.as_str()))
                    .collect();
                assert_eq!(given, [("b", "G2")]);
                nodes.clone()
            }
            other => panic!("repairs: {other:?}"),
        },
        other => panic!("problems: {other:?}"),
    };
    assert!(matches!(
        core.renumber(&nodes, 0),
        EditResult::Refused { ref code } if code == "AF-EDIT-009"
    ));
    let made = core.renumber(&nodes, 1);
    assert!(
        matches!(made, EditResult::Applied { ref element, .. } if element.as_deref() == Some("b")),
        "{made:?}"
    );
    assert!(shared(&mut core).is_empty());
    assert_eq!(
        core.element("b").map(|details| details.identifier),
        Some("G2".to_owned())
    );
    assert!(matches!(core.undo(), EditResult::Applied { .. }));
    assert_eq!(shared(&mut core).len(), 1);
}
