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
//! The session also knows which state of the document was last saved, and
//! whether the document is in that state: undo back to it is saved again
//! (plan decision S3). A revision cannot say this, since undo makes one too;
//! each state is named instead by the revision of the command that made it.
//!
//! It does no I/O and builds for `wasm32-unknown-unknown`.

use af_gsn::ElementKind;
use af_gsn::commands::{
    self, AcpKind, AddError, Added, Challenged, Line, Moved, Removed, Repaired, Target, TextField,
    TextSet,
};
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
        /// For a top-level Goal: the package it went into.
        package: Option<String>,
    },
    /// Set the identifier, name or statement of the node `element` to `text`.
    SetText {
        element: String,
        field: TextField,
        text: String,
        /// The interface language it was set in: which of a statement's
        /// language values it changed (W17).
        language: String,
    },
    /// Remove the node `element` and what goes with it.
    Remove { element: String },
    /// Move the node `element`, with what is under it, under the node `to`,
    /// or to the top level when `None`.
    Move { element: String, to: Option<String> },
    /// Move the challenge the node `element` makes onto the line from `from`
    /// to `to` of `relationship`.
    MoveToLine {
        element: String,
        relationship: String,
        from: String,
        to: String,
    },
    /// Mark the Goal `element` undeveloped, or clear the mark.
    SetUndeveloped { element: String, undeveloped: bool },
    /// Join the node `element` by context in place of the support line that
    /// `relationship` draws.
    RepairJoin {
        element: String,
        relationship: String,
    },
    /// Challenge the node, or the line of the relationship, `at` with a new
    /// element of `kind`.
    Challenge {
        at: String,
        /// For a line: the nodes it runs from and to.
        line: Option<(String, String)>,
        kind: ElementKind,
    },
    /// Add an assurance claim point of `kind` to the line from `from` to
    /// `to` of `relationship`; a confidence argument's package is named
    /// `package_name`.
    AddAcp {
        relationship: String,
        from: String,
        to: String,
        kind: AcpKind,
        package_name: String,
    },
    /// Name the goal `claim`, the top goal of another argument package, as
    /// an assurance claim point of the line from `from` to `to` of
    /// `relationship`.
    LinkAcp {
        relationship: String,
        from: String,
        to: String,
        claim: String,
    },
    /// Remove the assurance claim point that `relationship` carries for the
    /// Claim `claim`, with its confidence argument when `with_argument`.
    RemoveAcp {
        relationship: String,
        claim: String,
        with_argument: bool,
    },
    /// Remove the line from `from` to `to` of `relationship`, the nodes at
    /// its ends staying (W37).
    RemoveLine {
        relationship: String,
        from: String,
        to: String,
    },
    /// Renumber the nodes `elements`, which share one identifier: the first
    /// keeps it (W38).
    Renumber { elements: Vec<String> },
    /// Make the Strategy `element` the reasoning of the one relationship
    /// above it (W39).
    FoldStrategy { element: String },
    /// Replace the document by one kept in a recovery copy (plan decision
    /// R5).
    Restore,
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
    /// The state of the document before the oldest command undo can take
    /// back: the revision of the command that made it.
    base: u32,
    /// The state last saved, or `None` when none is known to be on disk.
    saved: Option<u32>,
}

impl Session {
    /// A session over `document`, which is at `revision` and is what its
    /// file holds: it starts saved. The next change makes `revision + 1`.
    pub fn new(document: Document, revision: u32) -> Self {
        Self {
            document,
            revision,
            undo: Vec::new(),
            redo: Vec::new(),
            records: Vec::new(),
            base: revision,
            saved: Some(revision),
        }
    }

    /// The state the document is in: the revision of the command that made
    /// it, or the one it was opened at. Undo and redo return to a state, and
    /// so to its name; a new command makes a new one.
    pub fn state(&self) -> u32 {
        self.undo
            .last()
            .map_or(self.base, |step| step.record.revision)
    }

    /// The document as it is now has been saved.
    pub fn mark_saved(&mut self) {
        self.saved = Some(self.state());
    }

    /// Whether the document has changes that are not saved: it is not in the
    /// state last saved.
    pub fn is_modified(&self) -> bool {
        self.saved != Some(self.state())
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
        self.add_in(view, target, kind, None)
    }

    /// [`Session::add`], with a top-level Goal going into the argument
    /// package `home`, the one whose diagram is shown (W9).
    ///
    /// # Errors
    ///
    /// As [`Session::add`].
    pub fn add_in(
        &mut self,
        view: &View,
        target: Target<'_>,
        kind: ElementKind,
        home: Option<&str>,
    ) -> Result<(Added, ChangeRecord), AddError> {
        let before = self.document.clone();
        let added = commands::add_in(&mut self.document, view, target, kind, home)?;
        let command = Command::Add {
            parent: match target {
                Target::Canvas => None,
                Target::Node(id) => Some(id.to_owned()),
            },
            kind,
            // The package a top-level Goal went into, as made.
            package: match target {
                Target::Canvas => af_gsn::view::argument_package_of(&self.document, &added.element),
                Target::Node(_) => None,
            },
        };
        let record = self.made(before, command, &added.mutation);
        Ok((added, record))
    }

    /// Sets the identifier, name or statement of the node `element` to
    /// `text`. `view` is the view of the document as it is now, made for
    /// `language`. `None` when the text was already that: nothing is recorded
    /// and there is nothing to undo.
    ///
    /// # Errors
    ///
    /// What [`commands::set_text`] refuses; the document and the revision are
    /// then unchanged.
    pub fn set_text(
        &mut self,
        view: &View,
        element: &str,
        field: TextField,
        text: &str,
        language: &str,
    ) -> Result<Option<ChangeRecord>, AddError> {
        let before = self.document.clone();
        let set = commands::set_text(&mut self.document, view, element, field, text, language)?;
        let TextSet::Changed(mutation) = set else {
            return Ok(None);
        };
        let command = Command::SetText {
            element: element.to_owned(),
            field,
            text: text.to_owned(),
            language: language.to_owned(),
        };
        Ok(Some(self.made(before, command, &mutation)))
    }

    /// Removes the node `element` and what goes with it. `view` is the view
    /// of the document as it is now.
    ///
    /// # Errors
    ///
    /// What [`commands::remove`] refuses; the document and the revision are
    /// then unchanged.
    pub fn remove(
        &mut self,
        view: &View,
        element: &str,
    ) -> Result<(Removed, ChangeRecord), AddError> {
        let before = self.document.clone();
        let removed = commands::remove(&mut self.document, view, element)?;
        let command = Command::Remove {
            element: element.to_owned(),
        };
        let record = self.made(before, command, &removed.mutation);
        Ok((removed, record))
    }

    /// Moves the node `element`, with what is under it, to `target`. `view`
    /// is the view of the document as it is now.
    ///
    /// # Errors
    ///
    /// What [`commands::move_node`] refuses; the document and the revision
    /// are then unchanged.
    pub fn move_node(
        &mut self,
        view: &View,
        element: &str,
        target: Target<'_>,
    ) -> Result<(Moved, ChangeRecord), AddError> {
        let before = self.document.clone();
        let moved = commands::move_node(&mut self.document, view, element, target)?;
        let command = Command::Move {
            element: element.to_owned(),
            to: match target {
                Target::Canvas => None,
                Target::Node(id) => Some(id.to_owned()),
            },
        };
        let record = self.made(before, command, &moved.mutation);
        Ok((moved, record))
    }

    /// Moves the challenge the node `element` makes onto `line`, one line of
    /// a relationship (W32), as one command.
    ///
    /// # Errors
    ///
    /// What [`commands::move_challenge`] refuses; the document and the
    /// revision are then unchanged.
    pub fn move_to_line(
        &mut self,
        view: &View,
        element: &str,
        relationship: &str,
        from: &str,
        to: &str,
    ) -> Result<(Moved, ChangeRecord), AddError> {
        let before = self.document.clone();
        let line = Challenged::Line {
            relationship,
            from,
            to,
        };
        let moved = commands::move_challenge(&mut self.document, view, element, line)?;
        let command = Command::MoveToLine {
            element: element.to_owned(),
            relationship: relationship.to_owned(),
            from: from.to_owned(),
            to: to.to_owned(),
        };
        let record = self.made(before, command, &moved.mutation);
        Ok((moved, record))
    }

    /// Marks the Goal `element` undeveloped, or clears the mark. `None` when
    /// it was that already.
    ///
    /// # Errors
    ///
    /// What [`commands::set_undeveloped`] refuses; the document and the
    /// revision are then unchanged.
    pub fn set_undeveloped(
        &mut self,
        view: &View,
        element: &str,
        undeveloped: bool,
    ) -> Result<Option<ChangeRecord>, AddError> {
        let before = self.document.clone();
        let set = commands::set_undeveloped(&mut self.document, view, element, undeveloped)?;
        let TextSet::Changed(mutation) = set else {
            return Ok(None);
        };
        let command = Command::SetUndeveloped {
            element: element.to_owned(),
            undeveloped,
        };
        Ok(Some(self.made(before, command, &mutation)))
    }

    /// Joins the node `element` by context in place of the support line
    /// that `relationship` draws.
    ///
    /// # Errors
    ///
    /// What [`commands::repair_join`] refuses; the document and the revision
    /// are then unchanged.
    pub fn repair_join(
        &mut self,
        view: &View,
        element: &str,
        relationship: &str,
    ) -> Result<(Repaired, ChangeRecord), AddError> {
        let before = self.document.clone();
        let repaired = commands::repair_join(&mut self.document, view, element, relationship)?;
        let command = Command::RepairJoin {
            element: element.to_owned(),
            relationship: relationship.to_owned(),
        };
        let record = self.made(before, command, &repaired.mutation);
        Ok((repaired, record))
    }

    /// Challenges `at` with a new element of `kind`, a Goal or a Solution
    /// (W29 to W31), as one command.
    ///
    /// # Errors
    ///
    /// What [`commands::challenge`] refuses; the document and the revision
    /// are then unchanged.
    pub fn challenge(
        &mut self,
        view: &View,
        at: Challenged<'_>,
        kind: ElementKind,
    ) -> Result<(Added, ChangeRecord), AddError> {
        let before = self.document.clone();
        let added = commands::challenge(&mut self.document, view, at, kind)?;
        let (id, line) = match at {
            Challenged::Node(id) => (id, None),
            Challenged::Line {
                relationship,
                from,
                to,
            } => (relationship, Some((from.to_owned(), to.to_owned()))),
        };
        let command = Command::Challenge {
            at: id.to_owned(),
            line,
            kind,
        };
        let record = self.made(before, command, &added.mutation);
        Ok((added, record))
    }

    /// Adds an assurance claim point of `kind` to `line` (W33, W34), as one
    /// command.
    ///
    /// # Errors
    ///
    /// What [`commands::add_acp`] refuses; the document and the revision
    /// are then unchanged.
    pub fn add_acp(
        &mut self,
        view: &View,
        line: Line<'_>,
        kind: AcpKind,
        package_name: &str,
    ) -> Result<(Added, ChangeRecord), AddError> {
        let before = self.document.clone();
        let added = commands::add_acp(&mut self.document, view, line, kind, package_name)?;
        let command = Command::AddAcp {
            relationship: line.relationship.to_owned(),
            from: line.from.to_owned(),
            to: line.to.to_owned(),
            kind,
            package_name: package_name.to_owned(),
        };
        let record = self.made(before, command, &added.mutation);
        Ok((added, record))
    }

    /// Names the goal `claim`, the top goal of another argument package, as
    /// an assurance claim point of `line` (W36), as one command.
    ///
    /// # Errors
    ///
    /// What [`commands::link_acp`] refuses; the document and the revision
    /// are then unchanged.
    pub fn link_acp(
        &mut self,
        view: &View,
        line: Line<'_>,
        claim: &str,
    ) -> Result<ChangeRecord, AddError> {
        let before = self.document.clone();
        let mutation = commands::link_acp(&mut self.document, view, line, claim)?;
        let command = Command::LinkAcp {
            relationship: line.relationship.to_owned(),
            from: line.from.to_owned(),
            to: line.to.to_owned(),
            claim: claim.to_owned(),
        };
        Ok(self.made(before, command, &mutation))
    }

    /// Removes the ACP that `relationship` carries for the Claim `claim`,
    /// with its description, and with its confidence argument when
    /// `with_argument` (W35). `view` is the view of the document as it is.
    ///
    /// # Errors
    ///
    /// What [`commands::remove_acp`] refuses; the document and the revision
    /// are then unchanged.
    pub fn remove_acp(
        &mut self,
        view: &View,
        relationship: &str,
        claim: &str,
        with_argument: bool,
    ) -> Result<ChangeRecord, AddError> {
        let before = self.document.clone();
        let mutation =
            commands::remove_acp(&mut self.document, view, relationship, claim, with_argument)?;
        let command = Command::RemoveAcp {
            relationship: relationship.to_owned(),
            claim: claim.to_owned(),
            with_argument,
        };
        Ok(self.made(before, command, &mutation))
    }

    /// Removes `line`, the nodes at its ends staying (W37). `view` is the
    /// view of the document as it is now.
    ///
    /// # Errors
    ///
    /// What [`commands::remove_line`] refuses; the document and the revision
    /// are then unchanged.
    pub fn remove_line(&mut self, view: &View, line: Line<'_>) -> Result<ChangeRecord, AddError> {
        let before = self.document.clone();
        let removed = commands::remove_line(&mut self.document, view, line)?;
        let command = Command::RemoveLine {
            relationship: line.relationship.to_owned(),
            from: line.from.to_owned(),
            to: line.to.to_owned(),
        };
        Ok(self.made(before, command, &removed.mutation))
    }

    /// Renumbers the nodes `elements`, which share one identifier, the first
    /// keeping it (W38). `view` is the view of the document as it is now.
    ///
    /// # Errors
    ///
    /// What [`commands::renumber`] refuses; the document and the revision
    /// are then unchanged.
    pub fn renumber(&mut self, view: &View, elements: &[String]) -> Result<ChangeRecord, AddError> {
        let before = self.document.clone();
        let renumbered = commands::renumber(&mut self.document, view, elements)?;
        let command = Command::Renumber {
            elements: elements.to_vec(),
        };
        Ok(self.made(before, command, &renumbered.mutation))
    }

    /// Makes the Strategy `element` the reasoning of the one relationship
    /// above it (W39). `view` is the view of the document as it is now.
    ///
    /// # Errors
    ///
    /// What [`commands::fold_strategy`] refuses; the document and the
    /// revision are then unchanged.
    pub fn fold_strategy(&mut self, view: &View, element: &str) -> Result<ChangeRecord, AddError> {
        let before = self.document.clone();
        let folded = commands::fold_strategy(&mut self.document, view, element)?;
        let command = Command::FoldStrategy {
            element: element.to_owned(),
        };
        Ok(self.made(before, command, &folded.mutation))
    }

    /// Replaces the document by `document`, the one a recovery copy kept, as
    /// one command that undo takes back (plan decision R5). `None` when the
    /// document is that already: nothing is recorded.
    ///
    /// The record lists the ids only one of the two documents has as created
    /// and deleted, and the ids both have as changed: an element is not
    /// compared with its namesake.
    pub fn restore(&mut self, document: Document) -> Option<ChangeRecord> {
        if document == self.document {
            return None;
        }
        let ids = |document: &Document| {
            document
                .elements()
                .map(|element| element.id().as_str().to_owned())
                .collect::<std::collections::BTreeSet<_>>()
        };
        let (old, new) = (ids(&self.document), ids(&document));
        let before = std::mem::replace(&mut self.document, document);
        let record = self.record(
            Command::Restore,
            new.difference(&old).cloned().collect(),
            new.intersection(&old).cloned().collect(),
            old.difference(&new).cloned().collect(),
        );
        Some(self.step(before, record))
    }

    /// Records an applied command, which undo can take back to `before`.
    fn made(
        &mut self,
        before: Document,
        command: Command,
        mutation: &sacm::edit::BatchMutation,
    ) -> ChangeRecord {
        let ids = |list: &[sacm::model::ElementId]| {
            list.iter()
                .map(|id| id.as_str().to_owned())
                .collect::<Vec<_>>()
        };
        let record = self.record(
            command,
            ids(mutation.created_ids()),
            ids(mutation.changed_ids()),
            ids(mutation.deleted_ids()),
        );
        self.step(before, record)
    }

    /// Keeps a command undo can take back to `before`; redo is cleared.
    fn step(&mut self, before: Document, record: ChangeRecord) -> ChangeRecord {
        self.undo.push(Step {
            before,
            record: record.clone(),
        });
        if self.undo.len() > HISTORY {
            // The state before the oldest step left is the one it made.
            let dropped = self.undo.remove(0);
            self.base = dropped.record.revision;
        }
        self.redo.clear();
        record
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
