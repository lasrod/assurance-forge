//! The session's revisions, change records, and undo and redo.

use af_gsn::ElementKind;
use af_gsn::commands::{Line, Target, line_removal};
use af_gsn::view::view;
use af_session::{Command, HISTORY, Origin, Session};
use sacm::compare;
use sacm::model::Document;
use sacm::xmi::{self, Mode, ReadOptions};

fn sample() -> Document {
    let path = format!(
        "{}/../../testdata/sacm/sacm_example_core_argument.sacm.xmi",
        env!("CARGO_MANIFEST_DIR")
    );
    let bytes = std::fs::read(&path).unwrap_or_else(|error| panic!("setup: {path}: {error}"));
    match xmi::read(&bytes, &ReadOptions::default().with_mode(Mode::Strict)) {
        Ok(output) => output.document().clone(),
        Err(error) => panic!("setup: {error:?}"),
    }
}

/// Adds a Solution under the core sample's top claim.
fn add_solution(session: &mut Session) -> String {
    let now = view(session.document(), "en");
    match session.add(&now, Target::Node("cl_top"), ElementKind::Solution) {
        Ok((added, _)) => added.element,
        Err(error) => panic!("{error}"),
    }
}

#[test]
fn an_add_makes_a_revision_and_one_record_of_it() {
    let mut session = Session::new(sample(), 4);
    let now = view(session.document(), "en");
    let (added, record) = match session.add(&now, Target::Node("cl_top"), ElementKind::Goal) {
        Ok(made) => made,
        Err(error) => panic!("{error}"),
    };
    assert_eq!(session.revision(), 5);
    assert_eq!(record.revision, 5);
    assert_eq!(
        record.command,
        Command::Add {
            parent: Some("cl_top".to_owned()),
            kind: ElementKind::Goal,
            package: None,
        }
    );
    assert_eq!(record.origin, Origin::User);
    // The claim, its identifier tag and the inference.
    assert!(record.created.contains(&added.element), "{record:?}");
    assert_eq!(record.created.len(), 3, "{record:?}");
    assert!(record.deleted.is_empty());
    assert_eq!(session.records(), [record]);
    assert_eq!(session.can_undo(), Some(5));
    assert_eq!(session.can_redo(), None);
}

#[test]
fn a_refused_add_makes_no_revision_and_no_record() {
    let mut session = Session::new(sample(), 1);
    let before = session.document().clone();
    let now = view(session.document(), "en");
    // A Solution is a leaf.
    assert!(
        session
            .add(&now, Target::Node("ev_1"), ElementKind::Goal)
            .is_err()
    );
    assert_eq!(session.revision(), 1);
    assert!(session.records().is_empty());
    assert_eq!(session.can_undo(), None);
    assert!(compare::semantic_compare(&before, session.document()).is_empty());
}

#[test]
fn undo_returns_the_document_to_what_it_was_and_redo_brings_the_add_back() {
    let original = sample();
    let mut session = Session::new(original.clone(), 1);
    let element = add_solution(&mut session);
    let added = session.document().clone();

    let undone = session.undo().unwrap_or_else(|| panic!("nothing to undo"));
    assert_eq!(undone.revision, 3);
    assert_eq!(undone.command, Command::Undo { revision: 2 });
    // What the add created, the undo deletes.
    assert!(undone.deleted.contains(&element));
    assert!(compare::semantic_compare(&original, session.document()).is_empty());
    assert!(original == *session.document());
    assert!(session.document().get(&element).is_none());
    assert_eq!((session.can_undo(), session.can_redo()), (None, Some(2)));

    let redone = session.redo().unwrap_or_else(|| panic!("nothing to redo"));
    assert_eq!(redone.revision, 4);
    assert_eq!(redone.command, Command::Redo { revision: 2 });
    assert!(redone.created.contains(&element));
    assert!(compare::semantic_compare(&added, session.document()).is_empty());
    assert_eq!((session.can_undo(), session.can_redo()), (Some(2), None));
    assert_eq!(session.records().len(), 3);
    assert!(session.undo().is_some());
    assert!(session.undo().is_none(), "one add, one undo");
}

#[test]
fn a_line_removal_is_one_command_that_undo_takes_back() {
    let original = sample();
    let mut session = Session::new(original.clone(), 1);
    let now = view(session.document(), "en");
    let line = now
        .edges
        .iter()
        .map(|edge| Line {
            relationship: &edge.relationship,
            from: &edge.from,
            to: &edge.to,
        })
        .find(|line| line_removal(session.document(), &now, *line).is_ok())
        .unwrap_or_else(|| panic!("setup: the sample has no removable line"));
    let record = session
        .remove_line(&now, line)
        .unwrap_or_else(|error| panic!("{error}"));
    assert_eq!(record.revision, 2);
    assert_eq!(
        record.command,
        Command::RemoveLine {
            relationship: line.relationship.to_owned(),
            from: line.from.to_owned(),
            to: line.to.to_owned(),
        }
    );
    assert!(!(original == *session.document()));
    assert!(session.undo().is_some());
    assert!(original == *session.document());
    assert!(session.undo().is_none(), "one removal, one undo");
}

#[test]
fn a_new_command_after_an_undo_clears_redo() {
    let mut session = Session::new(sample(), 1);
    add_solution(&mut session);
    assert!(session.undo().is_some());
    assert!(session.can_redo().is_some());
    add_solution(&mut session);
    assert_eq!(session.can_redo(), None);
    assert!(session.redo().is_none());
}

#[test]
fn undo_takes_back_the_last_hundred_commands() {
    let original = sample();
    let mut session = Session::new(original.clone(), 0);
    for _ in 0..HISTORY + 2 {
        add_solution(&mut session);
    }
    let mut undone = 0;
    while session.undo().is_some() {
        undone += 1;
    }
    assert_eq!(undone, HISTORY);
    // The first two adds are kept.
    assert!(!compare::semantic_compare(&original, session.document()).is_empty());
    assert_eq!(
        session.revision(),
        u32::try_from(2 * HISTORY + 2).unwrap_or(0)
    );
}

#[test]
fn a_text_set_is_recorded_with_the_language_it_was_set_in() {
    use af_gsn::commands::TextField;
    let mut session = Session::new(sample(), 1);
    let now = view(session.document(), "ja");
    let record = match session.set_text(&now, "cl_top", TextField::Statement, "安全である", "ja")
    {
        Ok(Some(record)) => record,
        other => panic!("not recorded: {other:?}"),
    };
    assert_eq!(
        record.command,
        Command::SetText {
            element: "cl_top".to_owned(),
            field: TextField::Statement,
            text: "安全である".to_owned(),
            language: "ja".to_owned(),
        }
    );
    assert_eq!(record.revision, 2);
    assert_eq!(session.records().len(), 1);
    // The same text again is no command.
    let now = view(session.document(), "ja");
    assert!(matches!(
        session.set_text(&now, "cl_top", TextField::Statement, "安全である", "ja"),
        Ok(None)
    ));
    assert_eq!(session.records().len(), 1);
}

#[test]
fn a_session_starts_saved_and_an_edit_is_a_change_not_saved() {
    let mut session = Session::new(sample(), 3);
    assert!(!session.is_modified(), "what was opened is what is on disk");
    add_solution(&mut session);
    assert!(session.is_modified());
    session.mark_saved();
    assert!(!session.is_modified());
}

#[test]
fn undo_back_to_the_saved_state_is_saved_again() {
    let mut session = Session::new(sample(), 1);
    add_solution(&mut session);
    session.mark_saved();
    let saved = session.document().clone();
    add_solution(&mut session);
    assert!(session.is_modified());
    assert!(session.undo().is_some());
    assert!(!session.is_modified(), "the saved state, by undo");
    assert!(session.document() == &saved);
    assert!(session.undo().is_some());
    assert!(session.is_modified(), "before the saved state");
    assert!(session.redo().is_some());
    assert!(!session.is_modified(), "the saved state, by redo");
    assert!(session.redo().is_some());
    assert!(session.is_modified());
}

#[test]
fn a_saved_state_that_redo_can_no_longer_reach_is_not_saved_again() {
    let mut session = Session::new(sample(), 1);
    add_solution(&mut session);
    session.mark_saved();
    assert!(session.undo().is_some());
    // A new command clears redo: the saved state is gone for good.
    add_solution(&mut session);
    assert!(session.is_modified());
    assert!(session.undo().is_some());
    assert!(session.is_modified(), "the state opened, not the one saved");
}

#[test]
fn the_saved_state_is_known_past_the_end_of_the_history() {
    let mut session = Session::new(sample(), 0);
    add_solution(&mut session);
    add_solution(&mut session);
    session.mark_saved();
    let saved = session.document().clone();
    for _ in 0..HISTORY {
        add_solution(&mut session);
    }
    // The steps that made the saved state have left the history; undo can go
    // back exactly to it, and no further.
    let mut undone = 0;
    while session.undo().is_some() {
        undone += 1;
    }
    assert_eq!(undone, HISTORY);
    assert!(session.document() == &saved);
    assert!(!session.is_modified());
}

#[test]
fn a_restore_is_one_command_that_undo_takes_back() {
    let original = sample();
    // The document a recovery copy kept: the sample with a Solution added.
    let mut elsewhere = Session::new(original.clone(), 1);
    let element = add_solution(&mut elsewhere);
    let kept = elsewhere.document().clone();

    let mut session = Session::new(original.clone(), 7);
    let record = match session.restore(kept.clone()) {
        Some(record) => record,
        None => panic!("the copy differs from the file"),
    };
    assert_eq!(record.revision, 8);
    assert_eq!(record.command, Command::Restore);
    assert!(record.created.contains(&element), "{record:?}");
    assert!(record.deleted.is_empty(), "{record:?}");
    assert!(record.changed.iter().any(|id| id == "cl_top"), "{record:?}");
    assert_eq!(session.document(), &kept);
    assert!(session.is_modified());

    // Undo shows the file as it was saved.
    assert!(session.undo().is_some());
    assert_eq!(session.document(), &original);
    assert!(!session.is_modified());
    assert!(session.redo().is_some());
    assert_eq!(session.document(), &kept);
}

#[test]
fn restoring_the_document_it_is_records_nothing() {
    let mut session = Session::new(sample(), 1);
    assert_eq!(session.restore(sample()), None);
    assert_eq!(session.revision(), 1);
    assert!(session.records().is_empty());
    assert_eq!(session.can_undo(), None);
    assert!(!session.is_modified());
}

#[test]
fn a_restore_clears_redo() {
    let mut session = Session::new(sample(), 1);
    add_solution(&mut session);
    assert!(session.undo().is_some());
    assert!(session.can_redo().is_some());
    let mut other = Session::new(sample(), 1);
    add_solution(&mut other);
    add_solution(&mut other);
    assert!(session.restore(other.document().clone()).is_some());
    assert_eq!(session.can_redo(), None);
}
