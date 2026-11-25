//! The problem list.
//!
//! Four sources make one list (plan decision P1,
//! `docs/roadmap/milestone-1-problems.md`):
//!
//! - **Read**: what libSACM's strict read reported, about the file as read;
//! - **SACM**: what libSACM's validation finds in the document;
//! - **GSN**: where the argument breaks GSN's rules (`af_gsn::wellformed`);
//! - **Diagram**: what the GSN view does not draw, or draws neutral.
//!
//! Each problem is a code with its arguments and the SACM ids it is about;
//! the frontend words it (P5). The list is ordered by severity, then by
//! source, then in the order each source found them (P6).
//!
//! It holds no state and does no I/O: it builds for `wasm32-unknown-unknown`.

use af_gsn::view::{Code as ViewCode, View};
use af_gsn::wellformed;
use sacm::diagnostic::{ArgumentValue, Diagnostic, Severity as SacmSeverity};
use sacm::model::Document;

/// How much a problem matters.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum Severity {
    /// The content is wrong: it does not conform, or breaks GSN.
    Error,
    /// Questionable, unfinished or stale, but usable.
    Warning,
    /// Nothing is wrong: something the user should know, such as what the
    /// diagram does not draw.
    Info,
}

/// Where a problem comes from, in the order the list shows them (P6).
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum Source {
    Gsn,
    Sacm,
    Read,
    Diagram,
}

/// A value a problem's words take.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Value {
    Text(String),
    Count(u64),
}

/// One problem.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Problem {
    pub source: Source,
    pub severity: Severity,
    /// Such as `AF-GSN-001`, `SACM-REF-001` or `AF-VIEW-003`.
    pub code: String,
    /// The requirement it enforces: a GSN v3 requirement, or the row of
    /// libSACM's functionality matrix.
    pub requirement: Option<String>,
    /// libSACM's template id, such as `SACM-MULT-001/too-many`: which form of
    /// the code it is.
    pub template: Option<String>,
    /// The values its words take, by name.
    pub arguments: Vec<(String, Value)>,
    /// The SACM ids of the elements it is about, in the order that matters
    /// to it: the first is the one to show.
    pub elements: Vec<String>,
    /// The SACM id of the relationship it is about, if any.
    pub relationship: Option<String>,
    /// Where in the file, for what libSACM reported reading it.
    pub line: Option<u32>,
    pub column: Option<u32>,
}

/// The list: `read`, what libSACM reported reading the file; the validation
/// of `document`; the GSN checks of `view`, the view of `document`; and
/// what `view` does not draw.
pub fn problems(read: &[Diagnostic], document: &Document, view: &View) -> Vec<Problem> {
    let validation = sacm::validation::validate(document);
    let mut list: Vec<Problem> = wellformed::check(document, view)
        .into_iter()
        .map(gsn)
        .chain(validation.iter().map(|d| diagnostic(Source::Sacm, d)))
        .chain(read.iter().map(|d| diagnostic(Source::Read, d)))
        .chain(diagram(view))
        .collect();
    // Stable: within a severity and source, the order found.
    list.sort_by_key(|problem| (problem.severity, problem.source));
    list
}

fn gsn(finding: wellformed::Finding) -> Problem {
    Problem {
        source: Source::Gsn,
        severity: if finding.rule.is_error() {
            Severity::Error
        } else {
            Severity::Warning
        },
        code: finding.rule.code().to_owned(),
        requirement: Some(finding.rule.requirement().to_owned()),
        template: None,
        arguments: Vec::new(),
        elements: finding.nodes,
        relationship: finding.relationship,
        line: None,
        column: None,
    }
}

fn diagnostic(source: Source, diagnostic: &Diagnostic) -> Problem {
    let severity = match diagnostic.severity() {
        SacmSeverity::Error => Severity::Error,
        SacmSeverity::Warning => Severity::Warning,
        _ => Severity::Info,
    };
    let location = diagnostic.location();
    Problem {
        source,
        severity,
        code: diagnostic.code().as_str().to_owned(),
        requirement: Some(diagnostic.requirement_id())
            .filter(|id| !id.is_empty())
            .map(str::to_owned),
        template: Some(diagnostic.template().to_owned()),
        arguments: diagnostic
            .arguments()
            .iter()
            .map(|argument| {
                let value = match argument.value() {
                    ArgumentValue::Count(count) => Value::Count(*count),
                    other => Value::Text(other.to_string()),
                };
                (argument.name().to_owned(), value)
            })
            .collect(),
        elements: diagnostic
            .affected()
            .iter()
            .map(|id| id.as_str().to_owned())
            .collect(),
        relationship: None,
        line: location.map(|location| location.line()),
        column: location.map(|location| location.column()),
    }
}

/// What the diagram does not draw, or draws neutral: the view's findings,
/// then the elements it does not place. The editor's own placeholder (R16)
/// is not a problem.
fn diagram(view: &View) -> impl Iterator<Item = Problem> + '_ {
    let findings = view.findings.iter().map(|finding| {
        let mut arguments = Vec::new();
        if let Some(end) = &finding.end {
            arguments.push(("end".to_owned(), Value::Text(end.clone())));
        }
        info(finding.code, finding.element.clone(), arguments)
    });
    let unplaced = view
        .unplaced
        .iter()
        .filter(|item| item.code != ViewCode::Placeholder)
        .map(|item| info(item.code, item.id.clone(), Vec::new()));
    findings.chain(unplaced)
}

fn info(code: ViewCode, element: String, arguments: Vec<(String, Value)>) -> Problem {
    Problem {
        source: Source::Diagram,
        severity: Severity::Info,
        code: code.as_str().to_owned(),
        requirement: None,
        template: None,
        arguments,
        elements: vec![element],
        relationship: None,
        line: None,
        column: None,
    }
}
