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

pub mod contract;

use af_gsn::commands::{self, Refusal as AddRefusal, Target};
use af_gsn::view::{self, View};
use af_layout::{Engine, Fonts, Style};
use af_session::{ChangeRecord, Session};
use sacm::diagnostic::Severity;
use sacm::model::AbstractClass;
use sacm::xmi::{self, Mode, ReadOptions};

use contract::{
    AddChoice, AddChoices, ArgumentValue, DiagnosticCounts, DiagramAt, DiagramAtRef, EditResult,
    ElementDetails, ElementKind, History, MessageArgument, OpenResult, Outline, OutlineRow,
    Package, Refusal, RelationshipDetails, RelationshipEnd, ViewCode,
};

mod convert;

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
        // What the read reported, and what validating the document finds:
        // the counts the problem list will show.
        let validated = sacm::validation::validate(output.document());
        let mut diagnostics = DiagnosticCounts::default();
        for diagnostic in output.diagnostics().iter().chain(&validated) {
            match diagnostic.severity() {
                Severity::Error => diagnostics.errors += 1,
                Severity::Warning => diagnostics.warnings += 1,
                _ => diagnostics.infos += 1,
            }
        }
        let document = output.into_document();
        let view = view::view(&document, &self.language);
        let result = OpenResult::Opened {
            revision: self.revision,
            diagnostics,
            nodes: count(view.nodes.len()),
            edges: count(view.edges.len()),
            view_codes: count(view.findings.len() + view.unplaced.len()),
        };
        self.open = Some(Open {
            session: Session::new(document, self.revision),
            view,
            diagram: None,
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
    /// when `None`.
    pub fn add(&mut self, parent: Option<&str>, kind: ElementKind) -> EditResult {
        let Some(open) = self.open.as_mut() else {
            return refused(NOTHING_OPEN);
        };
        let kind = convert::gsn_element_kind(kind);
        match open.session.add(&open.view, target(parent), kind) {
            Ok((added, record)) => self.changed(&record, Some(added.element)),
            Err(commands::AddError::Refused(refusal)) => refused(refusal.code()),
            // libSACM refused an operation the rules made: a bug in them. The
            // document is unchanged, and the add is said to have no SACM form.
            Err(commands::AddError::Edit(_)) => refused(AddRefusal::NotMapped.code()),
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
        }
    }

    /// After an accepted command: the new revision, and the view and diagram
    /// derived again from the document.
    fn changed(&mut self, record: &ChangeRecord, element: Option<String>) -> EditResult {
        self.revision = record.revision;
        self.layout += 1;
        if let Some(open) = self.open.as_mut() {
            open.view = view::view(open.session.document(), &self.language);
            open.diagram = None;
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
