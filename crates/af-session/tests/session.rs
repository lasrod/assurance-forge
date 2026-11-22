//! The session's revisions, change records, and undo and redo.

use af_gsn::ElementKind;
use af_gsn::commands::Target;
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
            kind: ElementKind::Goal
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
