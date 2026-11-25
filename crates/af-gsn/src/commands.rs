//! GSN commands: what may be added where, the SACM operations each add is,
//! and the SACM operations a change of a node's text is.
//!
//! [`choices`] answers, for a node of the view or the empty canvas, which of
//! the six GSN element kinds can be added there and why each other one cannot.
//! [`add`] adds one, as one `sacm::edit::batch`: every operation is applied or
//! none is. What is added reads back, by the reading rules, as the kind chosen
//! joined to its parent as chosen. The writing rules (W1 to W14) are in
//! `docs/standards/sacm-gsn-mapping.md` ("Writing GSN as SACM"), which the
//! comments here cite.
//!
//! [`set_text`] changes a node's identifier, name or statement: the inverse of
//! the reading rules T1 to T3, as W15 to W17 record. [`removal`] and
//! [`remove`] answer what removing a node deletes and remove it (W18 to W20).

use std::fmt;

use sacm::edit::{
    self, AddReference, BatchError, BatchMutation, CreateArgumentReasoning,
    CreateArtifactReference, CreateAssertedContext, CreateAssertedEvidence,
    CreateAssertedInference, CreateClaim, CreateDescription, CreateTaggedValue, DeleteElement,
    SetAssertionDeclaration, SetContent, SetName,
};
use sacm::model::{
    AssertionDeclaration, Document, ElementId, ElementKind as SacmKind, LangString,
    MultiLangString, ReferenceEnd, SacmElement,
};

use crate::ElementKind;
use crate::view::{Code, EdgeKind, NodeKind, PLACEHOLDER_KEY, PLACEHOLDER_VALUE, View};

mod remove;
pub use remove::{Removal, Removed, removal, remove};

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
    /// AF-EDIT-003: GSN does not allow it: a Strategy is supported only by
    /// Goals.
    NotUnderStrategy,
    /// AF-EDIT-004: the Strategy is the reasoning of no inference, or of
    /// several, so there is no one inference to add a premise to.
    NoOneInference,
    /// AF-EDIT-005: there is no `ArgumentPackage` to add to.
    NoPackage,
    /// AF-EDIT-006: only a Goal can be added at the top level.
    TopLevelGoalOnly,
    /// AF-EDIT-011: the id is of no node the diagram draws.
    NotANode,
    /// AF-EDIT-015: something the diagram does not draw refers to what a
    /// removal would delete, or it holds more than its text, so it is not
    /// removed (W19).
    Entangled,
    /// AF-EDIT-012: an identifier cannot be empty.
    IdentifierEmpty,
    /// AF-EDIT-013: another element of the file has the identifier.
    IdentifierTaken,
    /// AF-EDIT-014: the text is the value of a terminology expression, which
    /// is not edited as text.
    TextIsExpression,
}

impl Refusal {
    /// The stable code the frontend words.
    pub fn code(self) -> &'static str {
        match self {
            Refusal::Leaf => "AF-EDIT-001",
            Refusal::KindUnknown => "AF-EDIT-002",
            Refusal::NotUnderStrategy => "AF-EDIT-003",
            Refusal::NoOneInference => "AF-EDIT-004",
            Refusal::NoPackage => "AF-EDIT-005",
            Refusal::TopLevelGoalOnly => "AF-EDIT-006",
            Refusal::NotANode => "AF-EDIT-011",
            Refusal::Entangled => "AF-EDIT-015",
            Refusal::IdentifierEmpty => "AF-EDIT-012",
            Refusal::IdentifierTaken => "AF-EDIT-013",
            Refusal::TextIsExpression => "AF-EDIT-014",
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
    /// The SACM id of the element of the kind chosen.
    pub element: String,
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

/// The language a text is taken to be in when the file does not say: a name
/// or a statement written where there was none is tagged with it (W16, W17).
pub const DEFAULT_LANGUAGE: &str = "en";

/// Tags `value` with [`DEFAULT_LANGUAGE`] when it is being written for the
/// first time: it has no language and no content yet. A text the file
/// already holds keeps the language it has, or none.
fn first_written(value: &mut LangString) {
    if value.lang().is_none() && value.content().unwrap_or_default().is_empty() {
        value.set_lang(Some(DEFAULT_LANGUAGE.to_owned()));
    }
}

/// A text of a node that can be changed.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum TextField {
    /// The GSN identifier (T1).
    Identifier,
    /// The name (T2).
    Name,
    /// The statement (T3).
    Statement,
}

/// What a change of text made.
#[derive(Clone, Debug)]
pub enum TextSet {
    /// The document changed.
    Changed(BatchMutation),
    /// The text was already that: the document is as it was.
    Unchanged,
}

/// Sets the identifier, name or statement of the node `id` to `text`, where
/// `view` is the view of `document` as it is, made for `language`. The text
/// then reads back as `text` by T1 to T3, but for an emptied statement, which
/// reads back as T3 gives it from what the file still holds.
///
/// # Errors
///
/// [`AddError::Refused`] when `id` is no node of the view, the identifier is
/// empty or another element's, or the text is an expression's value.
pub fn set_text(
    document: &mut Document,
    view: &View,
    id: &str,
    field: TextField,
    text: &str,
    language: &str,
) -> Result<TextSet, AddError> {
    if !view.nodes.iter().any(|node| node.id == id) {
        return Err(AddError::Refused(Refusal::NotANode));
    }
    let element = document
        .get(id)
        .ok_or(AddError::Refused(Refusal::NotANode))?;
    let operation: Option<edit::Operation> = match field {
        TextField::Identifier => identifier_operation(document, view, element, text)?,
        TextField::Name => name_operation(element, text)?,
        TextField::Statement => statement_operation(element, text, language)?,
    };
    let Some(operation) = operation else {
        return Ok(TextSet::Unchanged);
    };
    let mutation = edit::batch(document, |tx| -> Result<(), BatchError> {
        tx.apply(operation)?;
        Ok(())
    })
    .map_err(AddError::Edit)?;
    if mutation.created_ids().is_empty()
        && mutation.changed_ids().is_empty()
        && mutation.deleted_ids().is_empty()
    {
        return Ok(TextSet::Unchanged);
    }
    Ok(TextSet::Changed(mutation))
}

/// W15: the identifier's tagged value is set, or made when there is none.
/// It is set without the space around it, which no identifier has meaning in
/// and which would let two read alike.
fn identifier_operation(
    document: &Document,
    view: &View,
    element: SacmElement<'_>,
    text: &str,
) -> Result<Option<edit::Operation>, AddError> {
    let text = text.trim();
    if text.is_empty() {
        return Err(AddError::Refused(Refusal::IdentifierEmpty));
    }
    let id = element.id().as_str();
    // As W10 counts them: a drawn node's identifier, and every element's
    // tagged identifier and SACM id, drawn or not.
    let taken = view
        .nodes
        .iter()
        .any(|node| node.id != id && node.identifier == text)
        || document.elements().any(|other| {
            other.id().as_str() != id
                && (other.id().as_str() == text
                    || crate::view::tagged_value(other, GSN_IDENTIFIER_KEY) == Some(text))
        });
    if taken {
        return Err(AddError::Refused(Refusal::IdentifierTaken));
    }
    let Some(tag) = crate::view::tagged(element, GSN_IDENTIFIER_KEY) else {
        return Ok(Some(
            CreateTaggedValue::new(
                element.id().clone(),
                LangString::new(GSN_IDENTIFIER_KEY),
                LangString::new(text),
            )
            .into(),
        ));
    };
    // T1 reads the first value: it is the one replaced, and the others stay.
    let mut values = tag
        .content()
        .map(|content| content.values().to_vec())
        .unwrap_or_default();
    match values.first_mut() {
        Some(first) if first.is_expression_lang_string() => {
            return Err(AddError::Refused(Refusal::TextIsExpression));
        }
        Some(first) => first.set_content(Some(text.to_owned())),
        None => values.push(LangString::new(text)),
    }
    Ok(Some(
        SetContent::new(
            tag.id().clone(),
            values.into_iter().collect::<MultiLangString>(),
        )
        .into(),
    ))
}

/// W16: the name's content is set, and its language kept.
fn name_operation(
    element: SacmElement<'_>,
    text: &str,
) -> Result<Option<edit::Operation>, AddError> {
    let mut name = element.name().cloned().unwrap_or_default();
    if name.is_expression_lang_string() {
        return Err(AddError::Refused(Refusal::TextIsExpression));
    }
    if name.content().unwrap_or_default() == text {
        return Ok(None);
    }
    first_written(&mut name);
    name.set_content(Some(text.to_owned()));
    Ok(Some(SetName::new(element.id().clone(), name).into()))
}

/// W17: the value T3 reads is set, in the first description; with no
/// description, one is made.
fn statement_operation(
    element: SacmElement<'_>,
    text: &str,
    language: &str,
) -> Result<Option<edit::Operation>, AddError> {
    let Some(description) = element.descriptions().next() else {
        if text.is_empty() {
            return Ok(None);
        }
        return Ok(Some(
            CreateDescription::new(
                element.id().clone(),
                LangString::new(text).with_lang(DEFAULT_LANGUAGE),
            )
            .into(),
        ));
    };
    let mut values = description
        .content()
        .map(|content| content.values().to_vec())
        .unwrap_or_default();
    // The value shown; with none shown, the first value, which is empty; with
    // no value, a new one.
    let index = match crate::view::in_language_index(&values, language) {
        Some(index) => index,
        None if text.is_empty() => return Ok(None),
        None if values.is_empty() => {
            values.push(LangString::default());
            0
        }
        None => 0,
    };
    if values[index].is_expression_lang_string() {
        return Err(AddError::Refused(Refusal::TextIsExpression));
    }
    first_written(&mut values[index]);
    values[index].set_content(Some(text.to_owned()));
    Ok(Some(
        SetContent::new(
            description.id().clone(),
            values.into_iter().collect::<MultiLangString>(),
        )
        .into(),
    ))
}

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
    let mut numbers = Identifiers::of(document, view);
    let mut made: Option<String> = None;
    let mutation = edit::batch(document, |tx| -> Result<(), BatchError> {
        let package = plan.package.clone();
        // W3: the place a Strategy holds for its first Goal becomes that Goal.
        if let Parent::Strategy {
            placeholder: Some((claim, tag)),
            ..
        } = &plan.parent
        {
            tx.apply(DeleteElement::new(tag.clone()))?;
            tx.apply(CreateTaggedValue::new(
                claim.clone(),
                LangString::new(GSN_IDENTIFIER_KEY),
                LangString::new(numbers.next(ElementKind::Goal)),
            ))?;
            made = Some(claim.as_str().to_owned());
            return Ok(());
        }
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
        match (&plan.parent, kind) {
            // W8.
            (Parent::Canvas, _) => {}
            // W2: a Strategy, and the place its first Goal will take.
            (Parent::Goal(goal), ElementKind::Strategy) => {
                let premise = placeholder(tx, &package)?;
                tx.apply(
                    CreateAssertedInference::new(
                        package.clone(),
                        LangString::default(),
                        [premise],
                        goal.clone(),
                    )
                    .with_reasoning(element.clone()),
                )?;
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
        made = Some(element.as_str().to_owned());
        Ok(())
    })
    .map_err(AddError::Edit)?;
    Ok(Added {
        element: made.unwrap_or_default(),
        mutation,
    })
}

/// W2: the Claim that holds a Strategy's place for its first Goal, which R16
/// does not draw. It has no identifier: it takes one when it becomes the
/// Goal (W3).
pub(crate) fn placeholder(
    tx: &mut edit::Batch<'_>,
    package: &ElementId,
) -> Result<ElementId, BatchError> {
    let created = tx.apply(CreateClaim::new(package.clone(), LangString::default()))?;
    let id = created.created_ids()[0].clone();
    tx.apply(CreateTaggedValue::new(
        id.clone(),
        LangString::new(PLACEHOLDER_KEY),
        LangString::new(PLACEHOLDER_VALUE),
    ))?;
    Ok(id)
}

/// What a node is to an add.
#[derive(Clone, Debug)]
enum Parent {
    Canvas,
    Goal(ElementId),
    /// A Strategy, and for a sub-goal the one live inference it is the
    /// reasoning of (W3), and the placeholder that inference holds for its
    /// first Goal, with the placeholder's tag, when it has no Goal yet.
    Strategy {
        node: ElementId,
        inference: Option<ElementId>,
        placeholder: Option<(ElementId, ElementId)>,
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
            // GSN: a Strategy is supported only by Goals.
            ElementKind::Strategy | ElementKind::Solution => {
                return Err(Refusal::NotUnderStrategy);
            }
            ElementKind::Goal => {
                let inference = one_inference(document, view, id).ok_or(Refusal::NoOneInference)?;
                (
                    Parent::Strategy {
                        node: element.id().clone(),
                        placeholder: held_place(document, view, &inference),
                        inference: Some(inference),
                    },
                    false,
                )
            }
            ElementKind::Context | ElementKind::Assumption | ElementKind::Justification => (
                Parent::Strategy {
                    node: element.id().clone(),
                    inference: None,
                    placeholder: None,
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

/// The placeholder `inference` holds for a Strategy's first Goal (R16), and
/// its tag, when the view did not draw it for that reason.
fn held_place(
    document: &Document,
    view: &View,
    inference: &ElementId,
) -> Option<(ElementId, ElementId)> {
    let source = document.get(inference.as_str())?.sources().first()?.clone();
    let unplaced = view
        .unplaced
        .iter()
        .any(|item| item.id == source.as_str() && item.code == Code::Placeholder);
    if !unplaced {
        return None;
    }
    let claim = document.get(source.as_str())?;
    let tag = crate::view::tagged(claim, PLACEHOLDER_KEY)?.id().clone();
    Some((source, tag))
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
    /// Every identifier the file already uses: each drawn node's, and each
    /// element's tagged identifier and SACM id, so that an element the view
    /// does not draw keeps its identifier to itself.
    fn of(document: &Document, view: &View) -> Self {
        let mut taken: Vec<String> = view
            .nodes
            .iter()
            .map(|node| node.identifier.clone())
            .collect();
        for element in document.elements() {
            taken.push(element.id().as_str().to_owned());
            if let Some(identifier) = crate::view::tagged_value(element, GSN_IDENTIFIER_KEY) {
                taken.push(identifier.to_owned());
            }
        }
        Self { taken }
    }

    /// The kind's prefix and the lowest number above every identifier of
    /// that prefix; then that one is taken too.
    fn next(&mut self, kind: ElementKind) -> String {
        let prefix = kind.identifier_prefix();
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
