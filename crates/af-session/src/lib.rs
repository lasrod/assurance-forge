//! The open document and its history.
//!
//! A [`Session`] holds the one SACM document of an open file. Every change
//! is a command: the session expands it into SACM operations (through
//! `af-gsn`), applies them all or none, gives the document a new revision and
//! keeps one [`ChangeRecord`] of it. Undo and redo are commands too, and make
//! revisions of their own: a revision only ever goes up, so an answer the
//! interface holds for an earlier one is always recognisably old.
//!
//! Undo keeps whole-document snapshots: `Document` is `Clone`, and a snapshot
//! cannot be undone wrongly, as an inverse operation could. The history keeps
//! the last [`HISTORY`] commands.
//!
//! It does no I/O and builds for `wasm32-unknown-unknown`.

use af_gsn::ElementKind;
use af_gsn::commands::{self, AddError, Added, Target};
use af_gsn::view::View;
use sacm::model::Document;

/// How many commands undo can take back.
pub const HISTORY: usize = 100;

/// Who asked for a change.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[non_exhaustive]
pub enum Origin {
    /// The person at the interface.
    User,
}

/// A requested change.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Command {
    /// Add an element of `kind` under the node `parent`, or at the top level
    /// when `None`.
    Add {
        parent: Option<String>,
        kind: ElementKind,
    },
    /// Take back the change that made `revision`.
    Undo { revision: u32 },
    /// Make again the change that made `revision`, which was taken back.
    Redo { revision: u32 },
}

/// One accepted command: what it was and what it changed.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ChangeRecord {
    /// The revision the command made.
    pub revision: u32,
    pub command: Command,
    /// The SACM ids it created, changed and deleted.
    pub created: Vec<String>,
    pub changed: Vec<String>,
    pub deleted: Vec<String>,
    pub origin: Origin,
}

/// A command that can be undone: the document before it, and its record.
struct Step {
    before: Document,
    record: ChangeRecord,
}

/// The open document, its revision and its history.
pub struct Session {
    document: Document,
    revision: u32,
    undo: Vec<Step>,
    redo: Vec<Step>,
    records: Vec<ChangeRecord>,
}

impl Session {
    /// A session over `document`, which is at `revision`. The next change
    /// makes `revision + 1`.
    pub fn new(document: Document, revision: u32) -> Self {
        Self {
            document,
            revision,
            undo: Vec::new(),
            redo: Vec::new(),
            records: Vec::new(),
        }
    }

    pub fn document(&self) -> &Document {
        &self.document
    }

    pub fn revision(&self) -> u32 {
        self.revision
    }

    /// Every accepted command, oldest first: the change stream.
    pub fn records(&self) -> &[ChangeRecord] {
        &self.records
    }

    /// Adds an element of `kind` at `target`. `view` is the view of the
    /// document as it is now.
    ///
    /// # Errors
    ///
    /// What [`commands::add`] refuses; the document and the revision are then
    /// unchanged.
    pub fn add(
        &mut self,
        view: &View,
        target: Target<'_>,
        kind: ElementKind,
    ) -> Result<(Added, ChangeRecord), AddError> {
        let before = self.document.clone();
        let added = commands::add(&mut self.document, view, target, kind)?;
        let command = Command::Add {
            parent: match target {
                Target::Canvas => None,
                Target::Node(id) => Some(id.to_owned()),
            },
            kind,
        };
        let ids = |list: &[sacm::model::ElementId]| {
            list.iter()
                .map(|id| id.as_str().to_owned())
                .collect::<Vec<_>>()
        };
        let record = self.record(
            command,
            ids(added.mutation.created_ids()),
            ids(added.mutation.changed_ids()),
            ids(added.mutation.deleted_ids()),
        );
        self.undo.push(Step {
            before,
            record: record.clone(),
        });
        if self.undo.len() > HISTORY {
            self.undo.remove(0);
        }
        self.redo.clear();
        Ok((added, record))
    }

    /// The revision undo would take back, if any.
    pub fn can_undo(&self) -> Option<u32> {
        self.undo.last().map(|step| step.record.revision)
    }

    /// The revision redo would make again, if any.
    pub fn can_redo(&self) -> Option<u32> {
        self.redo.last().map(|step| step.record.revision)
    }

    /// Takes back the last command: the document is again what it was before
    /// it. `None` when there is nothing to undo.
    pub fn undo(&mut self) -> Option<ChangeRecord> {
        let step = self.undo.pop()?;
        let after = std::mem::replace(&mut self.document, step.before);
        let undone = &step.record;
        let record = self.record(
            Command::Undo {
                revision: undone.revision,
            },
            undone.deleted.clone(),
            undone.changed.clone(),
            undone.created.clone(),
        );
        self.redo.push(Step {
            before: after,
            record: step.record,
        });
        Some(record)
    }

    /// Makes again the last command undo took back. `None` when there is
    /// nothing to redo.
    pub fn redo(&mut self) -> Option<ChangeRecord> {
        let step = self.redo.pop()?;
        let before = std::mem::replace(&mut self.document, step.before);
        let redone = &step.record;
        let record = self.record(
            Command::Redo {
                revision: redone.revision,
            },
            redone.created.clone(),
            redone.changed.clone(),
            redone.deleted.clone(),
        );
        self.undo.push(Step {
            before,
            record: step.record,
        });
        Some(record)
    }

    /// Gives the document the next revision and keeps the record of the
    /// command that made it.
    fn record(
        &mut self,
        command: Command,
        created: Vec<String>,
        changed: Vec<String>,
        deleted: Vec<String>,
    ) -> ChangeRecord {
        self.revision += 1;
        let record = ChangeRecord {
            revision: self.revision,
            command,
            created,
            changed,
            deleted,
            origin: Origin::User,
        };
        self.records.push(record.clone());
        record
    }
}
