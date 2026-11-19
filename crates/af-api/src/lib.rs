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
//! With nothing edited there is one revision per opened file. An edit session
//! (`af-session`) takes over the document with the first edit.

pub mod contract;

use af_gsn::view::{self, View};
use af_layout::{Engine, Fonts, Style};
use sacm::diagnostic::Severity;
use sacm::xmi::{self, Mode, ReadOptions};

use contract::{
    ArgumentValue, DiagnosticCounts, DiagramAt, DiagramAtRef, ElementDetails, MessageArgument,
    OpenResult, Refusal, ViewCode,
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
    document: sacm::model::Document,
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
            document,
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
            codes: view
                .findings
                .iter()
                .filter(|finding| finding.element == id)
                .map(|finding| ViewCode {
                    code: finding.code.as_str().to_owned(),
                    end: finding.end.clone(),
                })
                .collect(),
        })
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
            open.view = view::view(&open.document, &self.language);
            open.diagram = None;
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
