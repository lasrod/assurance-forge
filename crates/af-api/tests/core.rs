//! `af-api` against goal G4 of the diagram plan: `open` answers with a
//! revision and diagnostic counts or a refusal with its code, template,
//! arguments and location; `diagram` gives the render model at the revision;
//! `element` gives the details panel's content.

use af_api::Core;
use af_api::contract::{ArgumentValue, EdgeKind, NodeKind, OpenResult, Shape};
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
