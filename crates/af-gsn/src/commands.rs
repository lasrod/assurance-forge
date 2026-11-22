//! GSN commands: what may be added where, and the SACM operations each add is.
//!
//! [`choices`] answers, for a node of the view or the empty canvas, which of
//! the six GSN element kinds can be added there and why each other one cannot.
//! [`add`] adds one, as one `sacm::edit::batch`: every operation is applied or
//! none is. What is added reads back, by the reading rules, as the kind chosen
//! joined to its parent as chosen. The writing rules (W1 to W14) are in
//! `docs/standards/sacm-gsn-mapping.md` ("Writing GSN as SACM"), which the
//! comments here cite.

use std::fmt;

use sacm::edit::{
    self, AddReference, BatchError, BatchMutation, CreateArgumentReasoning,
    CreateArtifactReference, CreateAssertedContext, CreateAssertedEvidence,
    CreateAssertedInference, CreateClaim, CreateTaggedValue, SetAssertionDeclaration,
};
use sacm::model::{
    AssertionDeclaration, Document, ElementId, ElementKind as SacmKind, LangString, ReferenceEnd,
    SacmElement,
};

use crate::ElementKind;
use crate::view::{EdgeKind, NodeKind, View};

/// Where an element is to be added.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Target<'a> {
    /// The empty canvas: a top-level Goal (W8).
    Canvas,
    /// Under the node with this SACM id.
    Node(&'a str),
}

/// Why a kind cannot be added where it was asked for. The frontend words it.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
#[non_exhaustive]
pub enum Refusal {
    /// AF-EDIT-001: a Solution, Context, Assumption or Justification is a leaf.
    Leaf,
    /// AF-EDIT-002: the node's GSN kind is not known (a neutral node).
    KindUnknown,
    /// AF-EDIT-003: GSN allows it, but no SACM form is decided for it yet.
    NotMapped,
    /// AF-EDIT-004: the Strategy is the reasoning of no inference, or of
    /// several, so there is no one inference to add a premise to.
    NoOneInference,
    /// AF-EDIT-005: there is no `ArgumentPackage` to add to.
    NoPackage,
    /// AF-EDIT-006: only a Goal can be added at the top level.
    TopLevelGoalOnly,
}

impl Refusal {
    /// The stable code the frontend words.
    pub fn code(self) -> &'static str {
        match self {
            Refusal::Leaf => "AF-EDIT-001",
            Refusal::KindUnknown => "AF-EDIT-002",
            Refusal::NotMapped => "AF-EDIT-003",
            Refusal::NoOneInference => "AF-EDIT-004",
            Refusal::NoPackage => "AF-EDIT-005",
            Refusal::TopLevelGoalOnly => "AF-EDIT-006",
        }
    }
}

/// One kind the menu lists.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Choice {
    pub kind: ElementKind,
    /// Why it cannot be added, or `None` when it can.
    pub refusal: Option<Refusal>,
    /// Adding it also clears the parent Goal's undeveloped decorator (W11).
    pub develops: bool,
}

/// What an add made.
#[derive(Clone, Debug)]
pub struct Added {
    /// The SACM id of the element of the kind chosen (for a Strategy, the
    /// Strategy).
    pub element: String,
    /// The first sub-goal added with a Strategy (W2).
    pub sub_goal: Option<String>,
    /// What libSACM changed: the ids created and changed.
    pub mutation: BatchMutation,
}

/// Why an add did not happen. The document is unchanged.
#[derive(Debug)]
pub enum AddError {
    /// The kind cannot be added there; [`choices`] says the same.
    Refused(Refusal),
    /// libSACM refused an operation. Not expected: it is a bug in these rules.
    Edit(BatchError),
}

impl fmt::Display for AddError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            AddError::Refused(refusal) => write!(f, "{}", refusal.code()),
            AddError::Edit(error) => write!(f, "{error}"),
        }
    }
}

impl std::error::Error for AddError {}

/// The tagged value that holds the GSN identifier (T1, W10).
const GSN_IDENTIFIER_KEY: &str = "assuranceForge.gsn.identifier";

/// The six kinds, in [`ElementKind::ALL`] order, for `target` in `view` of
/// `document`.
pub fn choices(document: &Document, view: &View, target: Target<'_>) -> Vec<Choice> {
    ElementKind::ALL
        .iter()
        .map(|&kind| {
            let plan = plan(document, view, target, kind);
            Choice {
                kind,
                refusal: plan.as_ref().err().copied(),
                develops: plan.is_ok_and(|plan| plan.develops),
            }
        })
        .collect()
}

/// Adds an element of `kind` at `target`, where `view` is the view of
/// `document` as it is.
///
/// # Errors
///
/// [`AddError::Refused`] when [`choices`] greys the kind out there.
pub fn add(
    document: &mut Document,
    view: &View,
    target: Target<'_>,
    kind: ElementKind,
) -> Result<Added, AddError> {
    let plan = plan(document, view, target, kind).map_err(AddError::Refused)?;
    let mut numbers = Identifiers::of(view);
    let mut made: Option<(String, Option<String>)> = None;
    let mutation = edit::batch(document, |tx| -> Result<(), BatchError> {
        let package = plan.package.clone();
        let mut create =
            |tx: &mut edit::Batch<'_>, kind: ElementKind| -> Result<ElementId, BatchError> {
                let created = match kind {
                    ElementKind::Strategy => tx.apply(CreateArgumentReasoning::new(
                        package.clone(),
                        LangString::default(),
                    ))?,
                    ElementKind::Solution | ElementKind::Context => tx.apply(
                        CreateArtifactReference::new(package.clone(), LangString::default()),
                    )?,
                    ElementKind::Goal | ElementKind::Assumption | ElementKind::Justification => {
                        tx.apply(CreateClaim::new(package.clone(), LangString::default()))?
                    }
                };
                let id = created.created_ids()[0].clone();
                // R4, R5: the declaration tells an Assumption and a Justification from a Goal.
                let declaration = match kind {
                    ElementKind::Assumption => Some(AssertionDeclaration::Assumed),
                    ElementKind::Justification => Some(AssertionDeclaration::Axiomatic),
                    _ => None,
                };
                if let Some(declaration) = declaration {
                    tx.apply(SetAssertionDeclaration::new(id.clone(), declaration))?;
                }
                // W10.
                tx.apply(CreateTaggedValue::new(
                    id.clone(),
                    LangString::new(GSN_IDENTIFIER_KEY),
                    LangString::new(numbers.next(kind)),
                ))?;
                Ok(id)
            };

        let element = create(tx, kind)?;
        let mut sub_goal = None;
        match (&plan.parent, kind) {
            // W8.
            (Parent::Canvas, _) => {}
            // W2: a Strategy comes with its first sub-goal.
            (Parent::Goal(goal), ElementKind::Strategy) => {
                let premise = create(tx, ElementKind::Goal)?;
                tx.apply(
                    CreateAssertedInference::new(
                        package.clone(),
                        LangString::default(),
                        [premise.clone()],
                        goal.clone(),
                    )
                    .with_reasoning(element.clone()),
                )?;
                sub_goal = Some(premise.as_str().to_owned());
            }
            // W1.
            (Parent::Goal(goal), ElementKind::Goal) => {
                tx.apply(CreateAssertedInference::new(
                    package.clone(),
                    LangString::default(),
                    [element.clone()],
                    goal.clone(),
                ))?;
            }
            // W4.
            (Parent::Goal(goal), ElementKind::Solution) => {
                tx.apply(CreateAssertedEvidence::new(
                    package.clone(),
                    LangString::default(),
                    [element.clone()],
                    goal.clone(),
                ))?;
            }
            // W3.
            (
                Parent::Strategy {
                    inference: Some(inference),
                    ..
                },
                ElementKind::Goal,
            ) => {
                tx.apply(AddReference::new(
                    inference.clone(),
                    ReferenceEnd::Source,
                    element.clone(),
                ))?;
            }
            // W5 to W7.
            (Parent::Goal(node) | Parent::Strategy { node, .. }, _) => {
                tx.apply(CreateAssertedContext::new(
                    package.clone(),
                    LangString::default(),
                    [element.clone()],
                    node.clone(),
                ))?;
            }
        }
        // W11.
        if plan.develops {
            if let Parent::Goal(goal) = &plan.parent {
                tx.apply(SetAssertionDeclaration::new(
                    goal.clone(),
                    AssertionDeclaration::Asserted,
                ))?;
            }
        }
        made = Some((element.as_str().to_owned(), sub_goal));
        Ok(())
    })
    .map_err(AddError::Edit)?;
    let (element, sub_goal) = made.unwrap_or_default();
    Ok(Added {
        element,
        sub_goal,
        mutation,
    })
}

/// What a node is to an add.
#[derive(Clone, Debug)]
enum Parent {
    Canvas,
    Goal(ElementId),
    /// A Strategy, and for a sub-goal the one live inference it is the
    /// reasoning of (W3).
    Strategy {
        node: ElementId,
        inference: Option<ElementId>,
    },
}

/// An add that may be made.
#[derive(Clone, Debug)]
struct Plan {
    parent: Parent,
    /// The package the new elements go into (W9).
    package: ElementId,
    develops: bool,
}

/// The rules of "What may be added where" in the plan, and W9.
fn plan(
    document: &Document,
    view: &View,
    target: Target<'_>,
    kind: ElementKind,
) -> Result<Plan, Refusal> {
    let Target::Node(id) = target else {
        if kind != ElementKind::Goal {
            return Err(Refusal::TopLevelGoalOnly);
        }
        // W9: the first ArgumentPackage in document order.
        let package = document
            .elements()
            .find(|element| element.kind() == SacmKind::ArgumentPackage)
            .ok_or(Refusal::NoPackage)?;
        return Ok(Plan {
            parent: Parent::Canvas,
            package: package.id().clone(),
            develops: false,
        });
    };
    let node = view
        .nodes
        .iter()
        .find(|node| node.id == id)
        .ok_or(Refusal::KindUnknown)?;
    let element = document.get(id).ok_or(Refusal::KindUnknown)?;
    let supports = matches!(
        kind,
        ElementKind::Goal | ElementKind::Strategy | ElementKind::Solution
    );
    let (parent, develops) = match node.kind {
        NodeKind::Neutral => return Err(Refusal::KindUnknown),
        NodeKind::Solution | NodeKind::Context | NodeKind::Assumption | NodeKind::Justification => {
            return Err(Refusal::Leaf);
        }
        NodeKind::Goal => (
            Parent::Goal(element.id().clone()),
            supports && node.undeveloped,
        ),
        NodeKind::Strategy => match kind {
            // GSN allows both; no SACM form is decided (the plan's open questions).
            ElementKind::Strategy | ElementKind::Solution => return Err(Refusal::NotMapped),
            ElementKind::Goal => (
                Parent::Strategy {
                    node: element.id().clone(),
                    inference: Some(
                        one_inference(document, view, id).ok_or(Refusal::NoOneInference)?,
                    ),
                },
                false,
            ),
            ElementKind::Context | ElementKind::Assumption | ElementKind::Justification => (
                Parent::Strategy {
                    node: element.id().clone(),
                    inference: None,
                },
                false,
            ),
        },
    };
    let package = argument_package(element).ok_or(Refusal::NoPackage)?;
    Ok(Plan {
        parent,
        package,
        develops,
    })
}

/// The one live inference whose reasoning is the Strategy `id` and that the
/// view draws through it (E1), or `None` when there is not exactly one.
fn one_inference(document: &Document, view: &View, id: &str) -> Option<ElementId> {
    let mut found = document.elements().filter(|element| {
        element.kind() == SacmKind::AssertedInference
            && element
                .reasoning()
                .is_some_and(|reasoning| reasoning.as_str() == id)
            && view.edges.iter().any(|edge| {
                edge.relationship == element.id().as_str()
                    && edge.kind == EdgeKind::SupportedBy
                    && edge.to == id
            })
    });
    let first = found.next()?;
    if found.next().is_some() {
        return None;
    }
    Some(first.id().clone())
}

/// The `ArgumentPackage` that holds `element`, at any depth (W9).
fn argument_package(element: SacmElement<'_>) -> Option<ElementId> {
    let mut holder = element.parent();
    while let Some(container) = holder {
        if container.kind() == SacmKind::ArgumentPackage {
            return Some(container.id().clone());
        }
        holder = container.parent();
    }
    None
}

/// The next free GSN identifier of each kind (W10).
struct Identifiers {
    taken: Vec<String>,
}

impl Identifiers {
    fn of(view: &View) -> Self {
        Self {
            taken: view
                .nodes
                .iter()
                .map(|node| node.identifier.clone())
                .collect(),
        }
    }

    /// The kind's prefix and the lowest number above every identifier of
    /// that prefix; then that one is taken too.
    fn next(&mut self, kind: ElementKind) -> String {
        let prefix = prefix(kind);
        let highest = self
            .taken
            .iter()
            .filter_map(|identifier| identifier.strip_prefix(prefix))
            .filter(|digits| !digits.is_empty() && digits.bytes().all(|byte| byte.is_ascii_digit()))
            .filter_map(|digits| digits.parse::<u64>().ok())
            .max()
            .unwrap_or(0);
        let next = format!("{prefix}{}", highest + 1);
        self.taken.push(next.clone());
        next
    }
}

/// The GSN identifier prefix of each kind (GSN v3 examples, and the C++
/// application's).
fn prefix(kind: ElementKind) -> &'static str {
    match kind {
        ElementKind::Goal => "G",
        ElementKind::Strategy => "S",
        ElementKind::Solution => "Sn",
        ElementKind::Context => "C",
        ElementKind::Assumption => "A",
        ElementKind::Justification => "J",
    }
}
