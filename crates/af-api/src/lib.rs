//! The frontend contract.
//!
//! [`Core`] holds the open document and answers the interface's queries: open
//! a file, give the diagram, give an element's details. The desktop shell
//! (`af-desktop`) and the browser shell (`af-wasm`) both serve this one type,
//! so the interface is the same code on both (plan decision D1). The types in
//! [`contract`] are what crosses to the frontend, as JSON.
//!
//! It does no I/O and builds for `wasm32-unknown-unknown`: a shell reads the
//! file and the fonts and hands in bytes. A file opens before the fonts
//! arrive; the diagram is laid out once they have.
//!
//! The open document lives in an `af-session` [`Session`]: every change is a
//! command it applies all or not at all, with one revision per opened file
//! and one more per accepted command, undo and redo included. A revision only
//! goes up, across files too.
//!
//! [`Core::write`] gives the open document as SACM 2.3 XMI, only once it has
//! read the bytes back as the same document (plan decision S1). The shell
//! puts them on disk, or in the browser's downloads, and then says so with
//! [`Core::mark_saved`].
//!
//! [`Core::recovery_copy`] gives the bytes of a recovery copy while the
//! document has changes that are not saved, and [`Core::read_recovery`] and
//! [`Core::restore`] read one back (plan decisions R1 to R7). The core knows
//! the **base** of the open document, a digest of what its file held when it
//! was opened or last saved, and a copy carries it, so that a copy of other
//! content is told apart. The shells keep the bytes and do not read them.
//!
//! [`Core::problems`] gives the problem list of the open document at its
//! revision (`af-problems`): what libSACM reported reading the file, kept
//! from the open, and what validation, the GSN checks and the view find now.

pub mod contract;

use af_gsn::commands::{self, Target};
use af_gsn::view::{self, View};
use af_layout::{Engine, Fonts, Style};
use af_session::{ChangeRecord, Session};
use sacm::model::AbstractClass;
use sacm::xmi::{self, Mode, ReadOptions, WriteOptions};

use contract::{
    AddChoice, AddChoices, ArgumentValue, DiagnosticCounts, DiagramAt, DiagramAtRef, EditResult,
    ElementDetails, ElementKind, History, MessageArgument, OpenResult, Outline, OutlineRow,
    Package, Refusal, RelationshipDetails, RelationshipEnd, Removal, RemovalNode, ViewCode,
    WriteResult,
};
pub use contract::{FoundRecovery, KeepResult, RecoveryCopy};

mod convert;
mod recovery;

/// The open document and what is derived from it.
pub struct Core {
    /// The layout engine, once the shell has handed in the fonts.
    engine: Option<Engine>,
    /// The text sizes, kept for an engine that does not exist yet.
    style: Style,
    language: String,
    revision: u32,
    /// Counts layouts, which change with the language and the text sizes.
    layout: u32,
    open: Option<Open>,
}

struct Open {
    session: Session,
    view: View,
    /// Laid out on first request, and again after the language or text size
    /// changes.
    diagram: Option<contract::Diagram>,
    /// A digest of what the file held when it was opened or last saved.
    base: String,
    /// The revision [`Core::write`] last wrote, and a digest of what it gave:
    /// the base once the shell says it is saved.
    written: Option<(u32, String)>,
    /// What libSACM reported reading the file: about the file as read, so
    /// kept until the next open.
    read: Vec<sacm::diagnostic::Diagnostic>,
    /// The problem list, derived once per revision.
    problems: Option<contract::Problems>,
}

impl Core {
    /// A core with nothing open, laying text out with `fonts`. `language` is
    /// the interface language, a BCP 47 tag such as `en` or `ja`.
    pub fn new(fonts: Fonts, style: Style, language: &str) -> Self {
        let mut core = Self::without_fonts(style, language);
        core.set_fonts(fonts);
        core
    }

    /// A core with nothing open and no fonts yet. It opens files, counts what
    /// they hold and answers [`Core::element`]; [`Core::diagram`] waits for
    /// [`Core::set_fonts`]. The desktop shell opens a file named on the
    /// command line this way, before its page has loaded the fonts.
    pub fn without_fonts(style: Style, language: &str) -> Self {
        Self {
            engine: None,
            style,
            language: language.to_owned(),
            revision: 0,
            layout: 0,
            open: None,
        }
    }

    /// Lays text out with `fonts` from now on. What is open is laid out again.
    pub fn set_fonts(&mut self, fonts: Fonts) {
        self.engine = Some(Engine::new(fonts, self.style));
        self.layout += 1;
        if let Some(open) = self.open.as_mut() {
            open.diagram = None;
        }
    }

    /// Whether the fonts have been handed in, so a diagram can be laid out.
    pub fn has_fonts(&self) -> bool {
        self.engine.is_some()
    }

    /// Reads `bytes` as SACM 2.3 XMI by a strict read (plan decision D8).
    /// A refused file leaves nothing open.
    pub fn open(&mut self, bytes: &[u8]) -> OpenResult {
        self.revision += 1;
        self.layout += 1;
        // The last document's measurements go, whether or not this one opens.
        if let Some(engine) = self.engine.as_mut() {
            engine.clear_cache();
        }
        let output = match xmi::read(bytes, &ReadOptions::default().with_mode(Mode::Strict)) {
            Ok(output) => output,
            Err(error) => {
                self.open = None;
                let location = error.location();
                return OpenResult::Refused(Refusal {
                    code: error.code().as_str().to_owned(),
                    template: error.template().to_owned(),
                    arguments: error.arguments().iter().map(argument).collect(),
                    line: location.map(|location| location.line()),
                    column: location.map(|location| location.column()),
                });
            }
        };
        let read = output.diagnostics().to_vec();
        let document = output.into_document();
        let view = view::view(&document, &self.language);
        // The problem list, derived once: the open's counts are its counts
        // (P8), and `problems` answers it until the document changes.
        let problems = derive_problems(&read, &document, &view, self.revision);
        let result = OpenResult::Opened {
            revision: self.revision,
            diagnostics: problems.counts,
            nodes: count(view.nodes.len()),
            edges: count(view.edges.len()),
            // A placeholder (R16) is the editor's own mark, not something
            // the file says that the diagram leaves out.
            view_codes: count(
                view.findings.len()
                    + view
                        .unplaced
                        .iter()
                        .filter(|item| item.code != view::Code::Placeholder)
                        .count(),
            ),
        };
        self.open = Some(Open {
            session: Session::new(document, self.revision),
            view,
            diagram: None,
            base: recovery::digest(bytes),
            written: None,
            read,
            problems: Some(problems),
        });
        result
    }

    /// Closes what is open, as a refused file does: nothing is open after.
    pub fn close(&mut self) {
        self.open = None;
        if let Some(engine) = self.engine.as_mut() {
            engine.clear_cache();
        }
    }

    /// How many nodes and edges the open document's view has, or `None`
    /// when nothing is open: what a recent file is listed with.
    pub fn counts(&self) -> Option<(u32, u32)> {
        let view = &self.open.as_ref()?.view;
        Some((count(view.nodes.len()), count(view.edges.len())))
    }

    /// The revision of what is open, or of the last refusal.
    pub fn revision(&self) -> u32 {
        self.revision
    }

    /// The diagram of the open document, or `None` when nothing is open or
    /// the fonts have not been handed in.
    pub fn diagram(&mut self) -> Option<DiagramAt> {
        self.diagram_ref().map(|at| DiagramAt {
            revision: at.revision,
            layout: at.layout,
            diagram: at.diagram.clone(),
        })
    }

    /// The diagram of the open document, borrowed: what a shell serializes
    /// without copying it first.
    pub fn diagram_ref(&mut self) -> Option<DiagramAtRef<'_>> {
        let open = self.open.as_mut()?;
        let engine = self.engine.as_mut()?;
        let diagram = open
            .diagram
            .get_or_insert_with(|| convert::diagram(&engine.layout(&open.view)));
        Some(DiagramAtRef {
            revision: self.revision,
            layout: self.layout,
            diagram,
        })
    }

    /// What the details panel shows for the node with SACM id `id`.
    pub fn element(&self, id: &str) -> Option<ElementDetails> {
        let view = &self.open.as_ref()?.view;
        let node = view.nodes.iter().find(|node| node.id == id)?;
        Some(ElementDetails {
            id: node.id.clone(),
            kind: convert::node_kind(node.kind),
            undeveloped: node.undeveloped,
            identifier: node.identifier.clone(),
            name: node.name.clone(),
            statement: node.statement.clone(),
            codes: codes(view, id),
        })
    }

    /// What the details panel shows for the relationship with SACM id `id`,
    /// or `None` when the diagram draws no edge for it.
    pub fn relationship(&self, id: &str) -> Option<RelationshipDetails> {
        let open = self.open.as_ref()?;
        let view = &open.view;
        let edge = view.edges.iter().find(|edge| edge.relationship == id)?;
        let element = open
            .session
            .document()
            .elements()
            .find(|element| element.id().as_str() == id)?;
        let end = |id: &str| {
            let node = view.nodes.iter().find(|node| node.id == id);
            RelationshipEnd {
                id: id.to_owned(),
                identifier: node.map(|node| node.identifier.clone()),
                node: node.map(|node| convert::node_kind(node.kind)),
                relationship: view.edges.iter().any(|edge| edge.relationship == id),
            }
        };
        // A source named twice is one end, as it is one edge.
        let mut sources: Vec<RelationshipEnd> = Vec::new();
        for source in element.sources() {
            if !sources.iter().any(|known| known.id == source.as_str()) {
                sources.push(end(source.as_str()));
            }
        }
        Some(RelationshipDetails {
            id: id.to_owned(),
            class: element.kind().name().to_owned(),
            kind: convert::edge_kind(edge.kind),
            name: element
                .name()
                .and_then(|name| name.content())
                .map(str::to_owned),
            target: element.target().map(|target| end(target.as_str())),
            sources,
            reasoning: element.reasoning().map(|reasoning| end(reasoning.as_str())),
            codes: codes(view, id),
        })
    }

    /// The argument as a tree, for the Argument Navigator: the tree the
    /// diagram is laid out from, so the two agree. `None` when nothing is
    /// open. It needs no fonts.
    pub fn outline(&self) -> Option<Outline> {
        let view = &self.open.as_ref()?.view;
        let rows = af_layout::outline(view)
            .into_iter()
            .map(|row| {
                let node = &view.nodes[row.node];
                OutlineRow {
                    id: node.id.clone(),
                    level: count(row.level),
                    kind: convert::node_kind(node.kind),
                    undeveloped: node.undeveloped,
                    identifier: node.identifier.clone(),
                    name: node.name.clone(),
                    statement: node.statement.clone(),
                }
            })
            .collect();
        Some(Outline {
            revision: self.revision,
            rows,
        })
    }

    /// The packages of the open file, depth first in document order, for the
    /// Case Explorer. Empty when nothing is open.
    pub fn packages(&self) -> Vec<Package> {
        let Some(open) = self.open.as_ref() else {
            return Vec::new();
        };
        let mut packages = Vec::new();
        let mut stack: Vec<(sacm::model::SacmElement<'_>, u32)> = open
            .session
            .document()
            .roots()
            .filter(|root| root.kind().is_package())
            .map(|root| (root, 1))
            .collect();
        stack.reverse();
        while let Some((element, level)) = stack.pop() {
            let held = element
                .children()
                .filter(|child| !child.kind().is_a(AbstractClass::UtilityElement))
                .count();
            packages.push(Package {
                id: element.id().as_str().to_owned(),
                level,
                kind: element.kind().name().to_owned(),
                name: element
                    .name()
                    .and_then(|name| name.content())
                    .map(str::to_owned),
                elements: count(held),
            });
            let inner: Vec<_> = element
                .children()
                .filter(|child| child.kind().is_package())
                .collect();
            stack.extend(inner.into_iter().rev().map(|child| (child, level + 1)));
        }
        packages
    }

    /// Changes the interface language, which changes the node text (T3).
    /// The open document's view and diagram are derived again; its revision
    /// stays, since the document has not changed.
    pub fn set_language(&mut self, language: &str) {
        if language == self.language {
            return;
        }
        self.language = language.to_owned();
        // The measurements are kept: they are by text, and only the texts in
        // the other language are new.
        self.layout += 1;
        if let Some(open) = self.open.as_mut() {
            open.view = view::view(open.session.document(), &self.language);
            open.diagram = None;
        }
    }

    /// The add menu for the node with SACM id `parent`, or for the empty
    /// canvas when `None`: the six kinds, each with its refusal code when it
    /// cannot be added there. `None` when nothing is open.
    pub fn add_choices(&self, parent: Option<&str>) -> Option<AddChoices> {
        let open = self.open.as_ref()?;
        let choices = commands::choices(open.session.document(), &open.view, target(parent))
            .into_iter()
            .filter_map(|choice| {
                Some(AddChoice {
                    kind: convert::element_kind(choice.kind)?,
                    refusal: choice.refusal.map(|refusal| refusal.code().to_owned()),
                    develops: choice.develops,
                })
            })
            .collect();
        Some(AddChoices {
            revision: self.revision,
            choices,
        })
    }

    /// Adds an element of `kind` under the node `parent`, or at the top level
    /// when `None`. `revision` is the one the add menu was made for: an add
    /// chosen from a menu of another revision is refused (AF-EDIT-009), since
    /// the choices it offered may no longer hold.
    pub fn add(&mut self, parent: Option<&str>, kind: ElementKind, revision: u32) -> EditResult {
        let Some(open) = self.open.as_mut() else {
            return refused(NOTHING_OPEN);
        };
        if revision != self.revision {
            return refused(OUT_OF_DATE);
        }
        let kind = convert::gsn_element_kind(kind);
        match open.session.add(&open.view, target(parent), kind) {
            Ok((added, record)) => self.changed(&record, Some(added.element)),
            Err(commands::AddError::Refused(refusal)) => refused(refusal.code()),
            // libSACM refused an operation the rules made: a bug in them, not
            // a mapping decision. The document is unchanged.
            Err(commands::AddError::Edit(_)) => refused(NOT_MADE),
        }
    }

    /// What removing the node with SACM id `id` would delete, for the
    /// interface to show before it is made. `None` when nothing is open.
    pub fn removal(&self, id: &str) -> Option<Removal> {
        let open = self.open.as_ref()?;
        let view = &open.view;
        let (nodes, refusal) = match commands::removal(open.session.document(), view, id) {
            Ok(removal) => (
                removal
                    .nodes
                    .iter()
                    .filter_map(|id| view.nodes.iter().find(|node| node.id == *id))
                    .map(|node| RemovalNode {
                        id: node.id.clone(),
                        kind: convert::node_kind(node.kind),
                        identifier: node.identifier.clone(),
                        name: node.name.clone(),
                        statement: node.statement.clone(),
                    })
                    .collect(),
                None,
            ),
            Err(refusal) => (Vec::new(), Some(refusal.code().to_owned())),
        };
        Some(Removal {
            revision: self.revision,
            nodes,
            refusal,
        })
    }

    /// Removes the node with SACM id `id` and what goes with it, as
    /// [`Core::removal`] said at `revision`: a removal asked for at another
    /// revision is refused (AF-EDIT-009), since what it deletes may differ.
    pub fn remove(&mut self, id: &str, revision: u32) -> EditResult {
        let Some(open) = self.open.as_mut() else {
            return refused(NOTHING_OPEN);
        };
        if revision != self.revision {
            return refused(OUT_OF_DATE);
        }
        match open.session.remove(&open.view, id) {
            Ok((removed, record)) => self.changed(&record, removed.parent),
            Err(commands::AddError::Refused(refusal)) => refused(refusal.code()),
            Err(commands::AddError::Edit(_)) => refused(NOT_MADE),
        }
    }

    /// Sets the identifier, name or statement of the node with SACM id `id`
    /// to `text` (W15 to W17). The statement set is the one shown: the value
    /// in the interface language, as T3 reads it.
    pub fn set_text(&mut self, id: &str, field: contract::TextField, text: &str) -> EditResult {
        let Some(open) = self.open.as_mut() else {
            return refused(NOTHING_OPEN);
        };
        let field = match field {
            contract::TextField::Identifier => commands::TextField::Identifier,
            contract::TextField::Name => commands::TextField::Name,
            contract::TextField::Statement => commands::TextField::Statement,
        };
        match open
            .session
            .set_text(&open.view, id, field, text, &self.language)
        {
            Ok(Some(record)) => self.changed(&record, None),
            Ok(None) => EditResult::Unchanged,
            Err(commands::AddError::Refused(refusal)) => refused(refusal.code()),
            Err(commands::AddError::Edit(_)) => refused(NOT_MADE),
        }
    }

    /// Takes back the last command.
    pub fn undo(&mut self) -> EditResult {
        let Some(open) = self.open.as_mut() else {
            return refused(NOTHING_OPEN);
        };
        match open.session.undo() {
            Some(record) => self.changed(&record, None),
            None => refused(NOTHING_TO_DO),
        }
    }

    /// Makes again the last command undo took back.
    pub fn redo(&mut self) -> EditResult {
        let Some(open) = self.open.as_mut() else {
            return refused(NOTHING_OPEN);
        };
        match open.session.redo() {
            Some(record) => self.changed(&record, None),
            None => refused(NOTHING_TO_DO),
        }
    }

    /// What Edit ▸ Undo and Redo would do. Both false when nothing is open.
    pub fn history(&self) -> History {
        let Some(open) = self.open.as_ref() else {
            return History {
                revision: self.revision,
                ..History::default()
            };
        };
        History {
            revision: self.revision,
            can_undo: open.session.can_undo().is_some(),
            can_redo: open.session.can_redo().is_some(),
            modified: open.session.is_modified(),
        }
    }

    /// The open document as SACM 2.3 XMI, by libSACM's strict save, which
    /// checks that the output is schema-valid. The bytes are read back by a
    /// strict read and given out only if they are the document (S1): a
    /// writer fault never reaches the disk. Nothing is marked saved.
    pub fn write(&mut self) -> WriteResult {
        let revision = self.revision;
        let Some(open) = self.open.as_mut() else {
            return write_refused(SAVE_NOTHING_OPEN);
        };
        match checked_xmi(open.session.document()) {
            Ok(xmi) => {
                open.written = Some((revision, recovery::digest(xmi.as_bytes())));
                WriteResult::Written { revision, xmi }
            }
            Err(code) => write_refused(code),
        }
    }

    /// The shell has saved what [`Core::write`] gave at `revision`: the
    /// document is now saved, until it changes, and what was written is what
    /// its file holds. `false`, and nothing marked, when the document has
    /// changed since (a revision other than the one written) or nothing is
    /// open.
    pub fn mark_saved(&mut self, revision: u32) -> bool {
        match self.open.as_mut() {
            Some(open) if revision == self.revision => {
                open.session.mark_saved();
                if let Some((_, written)) = open.written.take_if(|(at, _)| *at == revision) {
                    open.base = written;
                }
                true
            }
            _ => false,
        }
    }

    /// A recovery copy of the open document, the file `name`, written at
    /// `written_at` (milliseconds since 1970, by the shell's clock): the
    /// bytes to keep while it has changes that are not saved, or `Remove`
    /// once it is in its saved state (R1, R3). The document is written by
    /// the checked write (S1), so a copy restores to the document.
    pub fn recovery_copy(&self, name: &str, written_at: f64) -> RecoveryCopy {
        let Some(open) = self.open.as_ref() else {
            return RecoveryCopy::Refused {
                code: RECOVER_NOTHING_OPEN.to_owned(),
            };
        };
        if !open.session.is_modified() {
            return RecoveryCopy::Remove {
                revision: self.revision,
            };
        }
        let copy = checked_xmi(open.session.document())
            .ok()
            .and_then(|xmi| recovery::Envelope::new(name, &open.base, written_at, xmi).to_json());
        match copy {
            Some(copy) => RecoveryCopy::Keep {
                revision: self.revision,
                copy,
            },
            None => RecoveryCopy::Refused {
                code: RECOVER_NOT_WRITTEN.to_owned(),
            },
        }
    }

    /// What the recovery copy in `copy`, found for the file just opened, is:
    /// one to offer, with whether the file still holds the content its
    /// changes began from (R6), or one that cannot be read (R7).
    pub fn read_recovery(&self, copy: &[u8]) -> FoundRecovery {
        let Some(open) = self.open.as_ref() else {
            return FoundRecovery::Unreadable {
                code: RECOVER_NOTHING_OPEN.to_owned(),
            };
        };
        match read_copy(copy) {
            Ok((envelope, _)) => FoundRecovery::Found {
                name: envelope.name,
                written_at: envelope.written_at,
                base_matches: envelope.base == open.base,
            },
            Err(code) => FoundRecovery::Unreadable {
                code: code.to_owned(),
            },
        }
    }

    /// Replaces the open document by the one the recovery copy `copy` kept,
    /// as one command that Edit ▸ Undo takes back (R5). `Unchanged` when it
    /// is that document already.
    pub fn restore(&mut self, copy: &[u8]) -> EditResult {
        let Some(open) = self.open.as_mut() else {
            return refused(RECOVER_NOTHING_OPEN);
        };
        let (envelope, document) = match read_copy(copy) {
            Ok(read) => read,
            Err(code) => return refused(code),
        };
        match open.session.restore(document) {
            Some(record) => {
                // The changes began from the copy's base, not from what the
                // file holds now: later copies say so too, until a save, so a
                // copy of other content is still flagged after another crash
                // (R6).
                open.base = envelope.base;
                self.changed(&record, None)
            }
            None => EditResult::Unchanged,
        }
    }

    /// The problem list of the open document at its revision, derived once
    /// per revision. `None` when nothing is open.
    pub fn problems(&mut self) -> Option<contract::Problems> {
        let revision = self.revision;
        let open = self.open.as_mut()?;
        let problems = open.problems.get_or_insert_with(|| {
            derive_problems(&open.read, open.session.document(), &open.view, revision)
        });
        Some(problems.clone())
    }

    /// After an accepted command: the new revision, and the view and diagram
    /// derived again from the document.
    fn changed(&mut self, record: &ChangeRecord, element: Option<String>) -> EditResult {
        self.revision = record.revision;
        self.layout += 1;
        if let Some(open) = self.open.as_mut() {
            open.view = view::view(open.session.document(), &self.language);
            open.diagram = None;
            open.problems = None;
        }
        EditResult::Applied {
            revision: record.revision,
            element,
            created: record.created.clone(),
            changed: record.changed.clone(),
            deleted: record.deleted.clone(),
        }
    }

    /// Changes the text sizes, which lays the diagram out again.
    pub fn set_style(&mut self, style: Style) {
        // Without an engine yet, checked as the engine checks it and kept.
        let changed = match self.engine.as_mut() {
            Some(engine) => engine.set_style(style),
            None => style != self.style && Engine::style_is_usable(style),
        };
        if !changed {
            return;
        }
        self.style = style;
        self.layout += 1;
        if let Some(open) = self.open.as_mut() {
            open.diagram = None;
        }
    }
}

/// AF-EDIT-007: nothing is open.
const NOTHING_OPEN: &str = "AF-EDIT-007";
/// AF-EDIT-008: there is nothing to undo, or nothing to redo.
const NOTHING_TO_DO: &str = "AF-EDIT-008";
/// AF-EDIT-009: the document changed after the add menu, or the removal
/// shown, was made.
const OUT_OF_DATE: &str = "AF-EDIT-009";
/// AF-EDIT-010: libSACM refused the operations the writing rules made.
const NOT_MADE: &str = "AF-EDIT-010";

/// AF-SAVE-001: nothing is open to save.
const SAVE_NOTHING_OPEN: &str = "AF-SAVE-001";
/// AF-SAVE-002: libSACM refused to write the document.
const SAVE_NOT_WRITTEN: &str = "AF-SAVE-002";
/// AF-SAVE-003: what was written does not read back as the document.
const SAVE_NOT_READ_BACK: &str = "AF-SAVE-003";

/// AF-RECOVER-001: nothing is open.
const RECOVER_NOTHING_OPEN: &str = "AF-RECOVER-001";
/// AF-RECOVER-002: not a recovery copy this version reads.
const RECOVER_UNKNOWN: &str = "AF-RECOVER-002";
/// AF-RECOVER-003: the copy's SACM is refused by a strict read.
const RECOVER_NOT_SACM: &str = "AF-RECOVER-003";
/// AF-RECOVER-004: the copy could not be made.
const RECOVER_NOT_WRITTEN: &str = "AF-RECOVER-004";

/// The problem list of `document`, read with `read`, whose view is `view`,
/// at `revision`: every problem counted by its severity, those of the
/// diagram among the information.
fn derive_problems(
    read: &[sacm::diagnostic::Diagnostic],
    document: &sacm::model::Document,
    view: &View,
    revision: u32,
) -> contract::Problems {
    let mut counts = DiagnosticCounts::default();
    let problems = af_problems::problems(read, document, view)
        .into_iter()
        .map(|problem| {
            let severity = match problem.severity {
                af_problems::Severity::Error => {
                    counts.errors += 1;
                    contract::ProblemSeverity::Error
                }
                af_problems::Severity::Warning => {
                    counts.warnings += 1;
                    contract::ProblemSeverity::Warning
                }
                af_problems::Severity::Info => {
                    counts.infos += 1;
                    contract::ProblemSeverity::Info
                }
            };
            convert::problem(problem, severity, view)
        })
        .collect();
    contract::Problems {
        revision,
        problems,
        counts,
    }
}

/// `document` as XMI by the strict save, read back as `document` (S1), or
/// the `AF-SAVE-*` code of why not.
fn checked_xmi(document: &sacm::model::Document) -> Result<String, &'static str> {
    let xmi = xmi::write(document, &WriteOptions::default()).map_err(|_| SAVE_NOT_WRITTEN)?;
    if reads_back_as(&xmi, document) {
        Ok(xmi)
    } else {
        Err(SAVE_NOT_READ_BACK)
    }
}

/// The envelope in `copy` and the document its XMI holds, by the strict read
/// a file is opened with, or the `AF-RECOVER-*` code of why not.
fn read_copy(copy: &[u8]) -> Result<(recovery::Envelope, sacm::model::Document), &'static str> {
    let envelope = recovery::Envelope::read(copy).ok_or(RECOVER_UNKNOWN)?;
    let document = xmi::read(
        envelope.xmi.as_bytes(),
        &ReadOptions::default().with_mode(Mode::Strict),
    )
    .map_err(|_| RECOVER_NOT_SACM)?
    .into_document();
    Ok((envelope, document))
}

/// Whether `xmi`, read as a file is opened (a strict read), is `document`.
fn reads_back_as(xmi: &str, document: &sacm::model::Document) -> bool {
    matches!(
        xmi::read(xmi.as_bytes(), &ReadOptions::default().with_mode(Mode::Strict)),
        Ok(output) if output.document() == document
    )
}

fn write_refused(code: &str) -> WriteResult {
    WriteResult::Refused {
        code: code.to_owned(),
    }
}

fn refused(code: &str) -> EditResult {
    EditResult::Refused {
        code: code.to_owned(),
    }
}

fn target(parent: Option<&str>) -> Target<'_> {
    parent.map_or(Target::Canvas, Target::Node)
}

/// The view's codes about one element or relationship.
fn codes(view: &View, id: &str) -> Vec<ViewCode> {
    view.findings
        .iter()
        .filter(|finding| finding.element == id)
        .map(|finding| ViewCode {
            code: finding.code.as_str().to_owned(),
            end: finding.end.clone(),
        })
        .collect()
}

fn count(n: usize) -> u32 {
    u32::try_from(n).unwrap_or(u32::MAX)
}

fn argument(argument: &sacm::diagnostic::Argument) -> MessageArgument {
    let value = match argument.value() {
        sacm::diagnostic::ArgumentValue::Count(count) => ArgumentValue::Count(*count),
        other => ArgumentValue::Text(other.to_string()),
    };
    MessageArgument {
        name: argument.name().to_owned(),
        value,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample() -> (String, sacm::model::Document) {
        let path = concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../../testdata/sacm/sacm_example_core_argument.sacm.xmi"
        );
        let text = std::fs::read_to_string(path).unwrap_or_else(|error| panic!("setup: {error}"));
        let document = match xmi::read(
            text.as_bytes(),
            &ReadOptions::default().with_mode(Mode::Strict),
        ) {
            Ok(output) => output.into_document(),
            Err(error) => panic!("setup: {error:?}"),
        };
        (text, document)
    }

    #[test]
    fn bytes_that_read_back_as_another_document_are_not_given_out() {
        let (text, document) = sample();
        assert!(reads_back_as(&text, &document));
        // One letter of one statement different: a writer that lost or
        // changed anything would read back like this.
        let changed = text.replacen("safe", "sAfe", 1);
        assert_ne!(changed, text, "setup: the sample says `safe`");
        assert!(!reads_back_as(&changed, &document));
        // Bytes a strict read refuses are not the document either.
        assert!(!reads_back_as("<not-sacm/>", &document));
    }
}
