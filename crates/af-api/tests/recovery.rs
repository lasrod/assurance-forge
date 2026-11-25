//! The recovery copy, against the plan's "Done when" 1 and 2: a copy of every
//! sample, edited, restores to the document copied, and undo takes the
//! restore back; a copy of a saved document is not made; a copy that cannot
//! be read, or one of other content, is told apart and leaves the document
//! as it was.

use af_api::contract::{EditResult, ElementKind, OpenResult, TextField, WriteResult};
use af_api::{Core, FoundRecovery, RecoveryCopy};
use af_layout::Style;

const WRITTEN_AT: f64 = 1_764_000_000_000.0;

fn testdata(name: &str) -> Vec<u8> {
    let path = format!("{}/../../testdata/sacm/{name}", env!("CARGO_MANIFEST_DIR"));
    std::fs::read(&path).unwrap_or_else(|error| panic!("setup: {path}: {error}"))
}

/// The repository's own samples.
fn samples() -> Vec<(String, Vec<u8>)> {
    let folder = format!("{}/../../testdata/sacm", env!("CARGO_MANIFEST_DIR"));
    let mut found: Vec<_> = std::fs::read_dir(&folder)
        .unwrap_or_else(|error| panic!("setup: {folder}: {error}"))
        .flatten()
        .map(|entry| entry.path())
        .filter(|path| path.to_string_lossy().ends_with(".xmi"))
        .map(|path| {
            let name = path.to_string_lossy().into_owned();
            let bytes = std::fs::read(&path).unwrap_or_else(|error| panic!("{name}: {error}"));
            (name, bytes)
        })
        .collect();
    found.sort();
    found
}

fn opened(bytes: &[u8]) -> Option<Core> {
    let mut core = Core::without_fonts(Style::default(), "en");
    matches!(core.open(bytes), OpenResult::Opened { .. }).then_some(core)
}

fn core_sample() -> (Vec<u8>, Core) {
    let bytes = testdata("sacm_example_core_argument.sacm.xmi");
    let core = opened(&bytes).unwrap_or_else(|| panic!("setup: the core sample opens"));
    (bytes, core)
}

/// The document as the checked write gives it: what tells two documents
/// apart here, since libSACM writes deterministically.
fn xmi(core: &mut Core) -> String {
    match core.write() {
        WriteResult::Written { xmi, .. } => xmi,
        WriteResult::Refused { code } => panic!("refused {code}"),
    }
}

/// Adds a top-level Goal and names it. `false` when the file has nowhere to
/// add one.
fn edit(core: &mut Core) -> bool {
    let EditResult::Applied {
        element: Some(goal),
        ..
    } = core.add(None, ElementKind::Goal, core.revision())
    else {
        return false;
    };
    matches!(
        core.set_text(&goal, TextField::Name, "復元 & <restored>"),
        EditResult::Applied { .. }
    )
}

fn copy(core: &Core) -> String {
    match core.recovery_copy("case.sacm.xmi", WRITTEN_AT) {
        RecoveryCopy::Keep { copy, revision } => {
            assert_eq!(revision, core.revision());
            copy
        }
        other => panic!("no copy: {other:?}"),
    }
}

#[test]
fn a_copy_of_every_sample_edited_restores_to_the_document_copied() {
    let mut restored = 0;
    for (name, bytes) in samples() {
        let Some(mut core) = opened(&bytes) else {
            continue;
        };
        if !edit(&mut core) {
            continue;
        }
        let kept = copy(&core);
        let edited = xmi(&mut core);

        // The application ended; the file is opened again.
        let Some(mut again) = opened(&bytes) else {
            panic!("{name}: opened once, refused the second time");
        };
        let unedited = xmi(&mut again);
        assert_eq!(
            again.read_recovery(kept.as_bytes()),
            FoundRecovery::Found {
                name: "case.sacm.xmi".to_owned(),
                written_at: WRITTEN_AT,
                base_matches: true,
            },
            "{name}"
        );
        let revision = again.revision();
        assert!(
            matches!(again.restore(kept.as_bytes()), EditResult::Applied { revision: r, .. } if r == revision + 1),
            "{name}: not restored"
        );
        assert_eq!(
            xmi(&mut again),
            edited,
            "{name}: restored to another document"
        );
        assert!(again.history().modified, "{name}");

        // Undo shows the file as it was saved.
        assert!(matches!(again.undo(), EditResult::Applied { .. }), "{name}");
        assert_eq!(xmi(&mut again), unedited, "{name}");
        assert!(!again.history().modified, "{name}");
        restored += 1;
    }
    assert!(restored >= 11, "setup: only {restored} samples restored");
}

#[test]
fn a_document_in_its_saved_state_has_no_copy() {
    let (_, mut core) = core_sample();
    assert!(matches!(
        core.recovery_copy("a", WRITTEN_AT),
        RecoveryCopy::Remove { .. }
    ));
    assert!(edit(&mut core));
    copy(&core);
    // Undo back to the saved state.
    assert!(matches!(core.undo(), EditResult::Applied { .. }));
    assert!(matches!(core.undo(), EditResult::Applied { .. }));
    assert!(matches!(
        core.recovery_copy("a", WRITTEN_AT),
        RecoveryCopy::Remove { .. }
    ));
    // Saved.
    assert!(matches!(core.redo(), EditResult::Applied { .. }));
    let WriteResult::Written { revision, .. } = core.write() else {
        panic!("not written");
    };
    assert!(core.mark_saved(revision));
    assert_eq!(
        core.recovery_copy("a", WRITTEN_AT),
        RecoveryCopy::Remove {
            revision: core.revision()
        }
    );
}

#[test]
fn a_copy_made_after_a_save_is_of_what_the_save_wrote() {
    let (bytes, mut core) = core_sample();
    assert!(edit(&mut core));
    let WriteResult::Written { revision, xmi } = core.write() else {
        panic!("not written");
    };
    assert!(core.mark_saved(revision));
    assert!(edit(&mut core));
    let kept = copy(&core);

    // The file as saved: the copy is of its content.
    let Some(saved) = opened(xmi.as_bytes()) else {
        panic!("the saved file opens");
    };
    assert!(matches!(
        saved.read_recovery(kept.as_bytes()),
        FoundRecovery::Found {
            base_matches: true,
            ..
        }
    ));
    // The file as it was before the save is other content (R6).
    let Some(before) = opened(&bytes) else {
        panic!("the sample opens");
    };
    assert!(matches!(
        before.read_recovery(kept.as_bytes()),
        FoundRecovery::Found {
            base_matches: false,
            ..
        }
    ));
}

#[test]
fn a_write_not_saved_does_not_change_the_base() {
    let (bytes, mut core) = core_sample();
    assert!(edit(&mut core));
    // Written, but the shell did not save it (a refused or cancelled save).
    xmi(&mut core);
    let kept = copy(&core);
    let Some(file) = opened(&bytes) else {
        panic!("the sample opens");
    };
    assert!(matches!(
        file.read_recovery(kept.as_bytes()),
        FoundRecovery::Found {
            base_matches: true,
            ..
        }
    ));
}

#[test]
fn a_copy_that_cannot_be_read_is_told_apart_and_changes_nothing() {
    let (_, mut source) = core_sample();
    assert!(edit(&mut source));
    let kept = copy(&source);
    let mut envelope: serde_json::Value =
        serde_json::from_str(&kept).unwrap_or_else(|error| panic!("setup: {error}"));

    let mut later = envelope.clone();
    later["version"] = serde_json::json!(2);
    envelope["xmi"] = serde_json::json!("<not-sacm/>");
    let cases = [
        ("not json".to_owned(), "AF-RECOVER-002"),
        ("{}".to_owned(), "AF-RECOVER-002"),
        (later.to_string(), "AF-RECOVER-002"),
        (envelope.to_string(), "AF-RECOVER-003"),
    ];
    for (bytes, code) in cases {
        let (_, mut core) = core_sample();
        let revision = core.revision();
        let before = xmi(&mut core);
        assert_eq!(
            core.read_recovery(bytes.as_bytes()),
            FoundRecovery::Unreadable {
                code: code.to_owned()
            },
            "{bytes:.40}"
        );
        assert_eq!(
            core.restore(bytes.as_bytes()),
            EditResult::Refused {
                code: code.to_owned()
            }
        );
        assert_eq!(core.revision(), revision);
        assert_eq!(xmi(&mut core), before);
        assert!(!core.history().modified);
    }
}

#[test]
fn a_copy_of_the_document_it_is_restores_nothing() {
    let (_, mut core) = core_sample();
    assert!(edit(&mut core));
    let kept = copy(&core);
    // Opened from what the copy holds: restoring it changes nothing.
    let edited = xmi(&mut core);
    let Some(mut again) = opened(edited.as_bytes()) else {
        panic!("the edited document opens");
    };
    assert_eq!(again.restore(kept.as_bytes()), EditResult::Unchanged);
    assert!(!again.history().can_undo);
}

#[test]
fn nothing_open_has_no_copy_and_nothing_to_restore_into() {
    let (_, mut source) = core_sample();
    assert!(edit(&mut source));
    let kept = copy(&source);
    let mut core = Core::without_fonts(Style::default(), "en");
    let nothing = "AF-RECOVER-001".to_owned();
    assert_eq!(
        core.recovery_copy("a", WRITTEN_AT),
        RecoveryCopy::Refused {
            code: nothing.clone()
        }
    );
    assert_eq!(
        core.read_recovery(kept.as_bytes()),
        FoundRecovery::Unreadable {
            code: nothing.clone()
        }
    );
    assert_eq!(
        core.restore(kept.as_bytes()),
        EditResult::Refused { code: nothing }
    );
}
