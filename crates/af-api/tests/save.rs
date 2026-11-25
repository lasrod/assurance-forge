//! Saving, against the plan's "Done when" 1, 2 and 4: every sample, opened,
//! edited and written, reads back `==` to the document written, and a second
//! write gives the same bytes; whether the document is saved follows its
//! state, not its revision.

use af_api::Core;
use af_api::contract::{EditResult, ElementKind, OpenResult, TextField, WriteResult};
use af_layout::Style;
use sacm::xmi::{self, Mode, ReadOptions};

fn root() -> String {
    format!("{}/../..", env!("CARGO_MANIFEST_DIR"))
}

/// Every SACM file the repository holds that a strict read opens: the
/// samples, and the C++ application's example projects but for their backups.
fn samples() -> Vec<(String, Vec<u8>)> {
    let mut found = Vec::new();
    let mut stack = vec![
        std::path::PathBuf::from(format!("{}/testdata/sacm", root())),
        std::path::PathBuf::from(format!("{}/examples/projects", root())),
    ];
    while let Some(directory) = stack.pop() {
        let Ok(entries) = std::fs::read_dir(&directory) else {
            continue;
        };
        for entry in entries.flatten() {
            let path = entry.path();
            let name = path.to_string_lossy().into_owned();
            if path.is_dir() {
                if !name.ends_with(".af") {
                    stack.push(path);
                }
            } else if name.ends_with(".xmi") || name.ends_with(".sacm") {
                let bytes = std::fs::read(&path).unwrap_or_else(|error| panic!("{name}: {error}"));
                found.push((name, bytes));
            }
        }
    }
    found.sort();
    found
}

fn written(core: &Core, what: &str) -> (u32, String) {
    match core.write() {
        WriteResult::Written { revision, xmi } => (revision, xmi),
        WriteResult::Refused { code } => panic!("{what}: refused {code}"),
    }
}

fn strict(bytes: &[u8], what: &str) -> sacm::model::Document {
    match xmi::read(bytes, &ReadOptions::default().with_mode(Mode::Strict)) {
        Ok(output) => output.into_document(),
        Err(error) => panic!("{what}: {error:?}"),
    }
}

/// Adds a top-level Goal, names it, and gives it a Solution: three
/// commands, the last two on what the first made.
fn edit(core: &mut Core, what: &str) -> bool {
    let revision = core.revision();
    let EditResult::Applied {
        element: Some(goal),
        ..
    } = core.add(None, ElementKind::Goal, revision)
    else {
        // A file with no argument package has nowhere to add to.
        return false;
    };
    assert!(
        matches!(
            core.set_text(&goal, TextField::Name, "保存 & <saved>"),
            EditResult::Applied { .. }
        ),
        "{what}: name not set"
    );
    let revision = core.revision();
    assert!(
        matches!(
            core.add(Some(&goal), ElementKind::Solution, revision),
            EditResult::Applied { .. }
        ),
        "{what}: solution not added"
    );
    true
}

#[test]
fn every_sample_written_reads_back_as_itself_before_and_after_edits() {
    let samples = samples();
    assert!(samples.len() >= 12, "setup: {} samples", samples.len());
    let mut edited = 0;
    for (name, bytes) in &samples {
        let mut core = Core::without_fonts(Style::default(), "en");
        let OpenResult::Opened { .. } = core.open(bytes) else {
            // A file a strict read refuses is not opened, so never saved.
            continue;
        };
        let opened = strict(bytes, name);
        let (_, first) = written(&core, name);
        assert!(strict(first.as_bytes(), name) == opened, "{name}: changed");

        if edit(&mut core, name) {
            edited += 1;
        }
        let (revision, xmi) = written(&core, name);
        assert_eq!(revision, core.revision(), "{name}");
        // What was written opens again, and writes again to the same bytes.
        let mut again = Core::without_fonts(Style::default(), "en");
        assert!(
            matches!(again.open(xmi.as_bytes()), OpenResult::Opened { .. }),
            "{name}: written file refused"
        );
        let (_, second) = written(&again, name);
        assert_eq!(second, xmi, "{name}: a second write differs");
    }
    assert!(edited >= 10, "setup: only {edited} samples edited");
}

#[test]
fn nothing_open_is_nothing_to_write() {
    let mut core = Core::without_fonts(Style::default(), "en");
    assert_eq!(
        core.write(),
        WriteResult::Refused {
            code: "AF-SAVE-001".to_owned()
        }
    );
    assert!(!core.mark_saved(0));
}

#[test]
fn saved_follows_the_state_of_the_document_not_its_revision() {
    let bytes = std::fs::read(format!(
        "{}/testdata/sacm/sacm_example_core_argument.sacm.xmi",
        root()
    ))
    .unwrap_or_else(|error| panic!("setup: {error}"));
    let mut core = Core::without_fonts(Style::default(), "en");
    core.open(&bytes);
    assert!(!core.history().modified, "what was opened is on disk");

    assert!(edit(&mut core, "core sample"));
    assert!(core.history().modified);
    let (revision, _) = written(&core, "core sample");
    // Written is not saved: the shell has not said it put the bytes anywhere.
    assert!(core.history().modified);
    assert!(core.mark_saved(revision));
    assert!(!core.history().modified);

    // Undo leaves the saved state; redo comes back to it, at a new revision.
    assert!(matches!(core.undo(), EditResult::Applied { .. }));
    assert!(core.history().modified);
    assert!(matches!(core.redo(), EditResult::Applied { .. }));
    assert!(!core.history().modified);
    assert_ne!(core.revision(), revision);

    // What was written at an earlier revision is not marked saved.
    let (stale, _) = written(&core, "core sample");
    assert!(matches!(core.undo(), EditResult::Applied { .. }));
    assert!(!core.mark_saved(stale));
    assert!(core.history().modified);

    // A file opened again starts saved.
    core.open(&bytes);
    assert!(!core.history().modified);
}
