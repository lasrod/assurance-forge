//! The problem list, against the plan's "Done when" 2: one list from four
//! sources, ordered by severity and source, the same on every derivation.

use af_gsn::ElementKind;
use af_gsn::commands::{self, Target};
use af_gsn::view::view;
use af_problems::{Problem, Severity, Source, problems};
use sacm::xmi::{self, Mode, ReadOptions};

fn read(name: &str) -> (Vec<sacm::diagnostic::Diagnostic>, sacm::model::Document) {
    let path = format!("{}/../../testdata/sacm/{name}", env!("CARGO_MANIFEST_DIR"));
    let bytes = std::fs::read(&path).unwrap_or_else(|error| panic!("setup: {path}: {error}"));
    match xmi::read(&bytes, &ReadOptions::default().with_mode(Mode::Strict)) {
        Ok(output) => (output.diagnostics().to_vec(), output.into_document()),
        Err(error) => panic!("setup: {name}: {error:?}"),
    }
}

fn list(name: &str) -> Vec<Problem> {
    let (diagnostics, document) = read(name);
    problems(&diagnostics, &document, &view(&document, "en"))
}

const SAMPLES: [&str; 6] = [
    "sacm_example_core_argument.sacm.xmi",
    "sample.sacm.xmi",
    "gallery-roots-and-cycle.sacm.xmi",
    "oasc-ja.sacm.xmi",
    "open-autonomy-safety-case.updated.v2.sacm.xmi",
    "safety-case-sanitized-strict.sacm.xmi",
];

#[test]
fn the_list_is_ordered_by_severity_then_source_and_the_same_each_time() {
    for name in SAMPLES {
        let first = list(name);
        assert_eq!(first, list(name), "{name}: derived twice, differs");
        let keys: Vec<(Severity, Source)> = first
            .iter()
            .map(|problem| (problem.severity, problem.source))
            .collect();
        let mut sorted = keys.clone();
        sorted.sort();
        assert_eq!(keys, sorted, "{name}");
    }
}

#[test]
fn the_core_sample_lists_its_gsn_defects_first_and_what_libsacm_found() {
    let found = list("sacm_example_core_argument.sacm.xmi");
    let gsn: Vec<(&str, Severity, &[String])> = found
        .iter()
        .filter(|problem| problem.source == Source::Gsn)
        .map(|problem| {
            (
                problem.code.as_str(),
                problem.severity,
                problem.elements.as_slice(),
            )
        })
        .collect();
    assert_eq!(
        gsn,
        [
            (
                "AF-GSN-004",
                Severity::Error,
                ["cl_sub_1".to_owned(), "cl_assumption_1".to_owned()].as_slice()
            ),
            (
                "AF-GSN-010",
                Severity::Warning,
                ["cl_sub_2".to_owned()].as_slice()
            ),
        ]
    );
    let first = &found[0];
    assert_eq!(first.code, "AF-GSN-004");
    assert_eq!(first.requirement.as_deref(), Some("GSN3-CORE-015"));
    assert_eq!(first.relationship.as_deref(), Some("inf_2"));
}

#[test]
fn what_libsacm_reports_keeps_its_code_template_arguments_ids_and_place() {
    // Validation of the large case reports SACM-MULT-001 (a Claim with two
    // descriptions), naming the Claim.
    let found = list("safety-case-sanitized-strict.sacm.xmi");
    let sacm: Vec<&Problem> = found
        .iter()
        .filter(|problem| problem.source == Source::Sacm)
        .collect();
    assert!(
        !sacm.is_empty(),
        "setup: the large case has validation findings"
    );
    for problem in sacm {
        assert!(problem.code.starts_with("SACM-"), "{problem:?}");
        // Codes and names, never libSACM's English sentences.
        for (name, _) in &problem.arguments {
            assert!(
                !["error", "message", "reason", "detail", "label"].contains(&name.as_str()),
                "{problem:?}"
            );
        }
        assert!(problem.template.is_some(), "{problem:?}");
        assert!(!problem.elements.is_empty(), "{problem:?}");
    }
}

#[test]
fn a_read_diagnostic_is_listed_with_where_it_is_in_the_file() {
    // A Claim with no xmi:id: libSACM gives it one, and says so (SACM-XMI-004,
    // information), at the element's start tag on line 5.
    let text = r#"<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:id="case">
  <name content="Case"/>
  <argumentPackage xmi:id="p"><name content="P"/>
    <argumentElement xmi:type="sacm:Claim"><name content="G"/></argumentElement>
  </argumentPackage>
</sacm:AssuranceCasePackage>"#;
    let output = match xmi::read(
        text.as_bytes(),
        &ReadOptions::default().with_mode(Mode::Strict),
    ) {
        Ok(output) => output,
        Err(error) => panic!("setup: {error:?}"),
    };
    let diagnostics = output.diagnostics().to_vec();
    assert_eq!(diagnostics.len(), 1, "setup: {diagnostics:?}");
    let document = output.into_document();
    let found = problems(&diagnostics, &document, &view(&document, "en"));
    let read: Vec<&Problem> = found
        .iter()
        .filter(|problem| problem.source == Source::Read)
        .collect();
    assert_eq!(read.len(), 1, "{found:#?}");
    let problem = read[0];
    assert_eq!(problem.code, "SACM-XMI-004");
    assert_eq!(problem.severity, Severity::Info);
    assert_eq!(problem.line, Some(5));
    assert!(problem.column.is_some());
    assert!(problem.template.is_some());
    assert_eq!(problem.elements.len(), 1);
    // Information comes after the GSN problems and libSACM's validation.
    assert_eq!(found.last().map(|last| last.source), Some(Source::Read));
}

#[test]
fn the_editors_own_placeholder_is_not_a_problem_but_the_strategy_without_a_goal_is() {
    let (_, mut document) = read("sacm_example_core_argument.sacm.xmi");
    let now = view(&document, "en");
    let added = match commands::add(
        &mut document,
        &now,
        Target::Node("cl_sub_1"),
        ElementKind::Strategy,
    ) {
        Ok(added) => added,
        Err(error) => panic!("setup: {error}"),
    };
    let after = view(&document, "en");
    let found = problems(&[], &document, &after);
    assert!(
        found.iter().all(|problem| problem.code != "AF-VIEW-014"),
        "{found:#?}"
    );
    assert!(found.iter().any(|problem| problem.code == "AF-GSN-008"
        && problem.severity == Severity::Warning
        && problem.elements == [added.element.clone()]));
}

#[test]
fn what_the_diagram_does_not_draw_is_information() {
    for name in SAMPLES {
        for problem in list(name)
            .iter()
            .filter(|problem| problem.source == Source::Diagram)
        {
            assert_eq!(problem.severity, Severity::Info, "{name}: {problem:?}");
            assert!(problem.code.starts_with("AF-VIEW-"), "{name}: {problem:?}");
            assert_eq!(problem.elements.len(), 1);
        }
    }
}
