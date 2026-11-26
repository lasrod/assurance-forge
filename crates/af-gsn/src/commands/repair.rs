//! Setting the undeveloped mark, and repairing an element the file joins as
//! GSN does not allow (W26 to W28 in `docs/standards/sacm-gsn-mapping.md`).
//!
//! Each is one `sacm::edit::batch`, asked for by the user: the undeveloped
//! mark from Element Properties or a node's menu, the repair from the
//! problem that reports the join.

use sacm::edit::{
    self, BatchError, BatchMutation, CreateAssertedContext, DeleteElement, EditError,
    RemoveReference, SetAssertionDeclaration,
};
use sacm::model::{
    AssertionDeclaration, Document, ElementId, ElementKind as SacmKind, LangString, ReferenceEnd,
};

use super::{AddError, Refusal, TextSet, argument_package, changeable, deletable};
use crate::view::{EdgeKind, NodeKind, View};

/// W26: why the undeveloped mark of the node `id` cannot be set to
/// `undeveloped`, or `Ok` with whether that would change anything.
///
/// # Errors
///
/// [`Refusal::NotANode`]; [`Refusal::NotAGoal`] for anything but a Goal
/// whose declaration is `asserted` or `needsSupport`;
/// [`Refusal::GoalSupported`] when a supported Goal would be marked.
pub fn undeveloped_change(
    document: &Document,
    view: &View,
    id: &str,
    undeveloped: bool,
) -> Result<bool, Refusal> {
    let node = view
        .nodes
        .iter()
        .find(|node| node.id == id)
        .ok_or(Refusal::NotANode)?;
    let element = document.get(id).ok_or(Refusal::NotANode)?;
    // The declaration is the one slot: it says `needsSupport`, or it says
    // what else the Claim is.
    let free = element.kind() == SacmKind::Claim
        && matches!(
            element.assertion_declaration(),
            None | Some(AssertionDeclaration::Asserted | AssertionDeclaration::NeedsSupport)
        );
    if node.kind != NodeKind::Goal || !free {
        return Err(Refusal::NotAGoal);
    }
    if node.undeveloped == undeveloped {
        return Ok(false);
    }
    // GSN: an undeveloped element has no support. Clearing is always allowed.
    let supported = view
        .edges
        .iter()
        .any(|edge| edge.kind == EdgeKind::SupportedBy && edge.from == id);
    if undeveloped && supported {
        return Err(Refusal::GoalSupported);
    }
    Ok(true)
}

/// W26: marks the Goal `id` undeveloped, or clears the mark.
///
/// # Errors
///
/// What [`undeveloped_change`] refuses, as [`AddError::Refused`].
pub fn set_undeveloped(
    document: &mut Document,
    view: &View,
    id: &str,
    undeveloped: bool,
) -> Result<TextSet, AddError> {
    if !undeveloped_change(document, view, id, undeveloped).map_err(AddError::Refused)? {
        return Ok(TextSet::Unchanged);
    }
    let element = document
        .get(id)
        .ok_or(AddError::Refused(Refusal::NotANode))?
        .id()
        .clone();
    let declaration = if undeveloped {
        AssertionDeclaration::NeedsSupport
    } else {
        AssertionDeclaration::Asserted
    };
    let mutation = edit::batch(document, |tx| -> Result<(), BatchError> {
        tx.apply(SetAssertionDeclaration::new(element, declaration))?;
        Ok(())
    })
    .map_err(AddError::Edit)?;
    Ok(TextSet::Changed(mutation))
}

/// What a repair of a join made.
#[derive(Clone, Debug)]
pub struct Repaired {
    /// The SACM id of the `AssertedContext` that now joins the node: the one
    /// made, or the one the file already had.
    pub relationship: String,
    pub mutation: BatchMutation,
}

/// A repair worked out, before anything is changed.
struct Plan {
    node: ElementId,
    relationship: ElementId,
    /// The node is the relationship's only source: the relationship goes.
    sole: bool,
    target: ElementId,
    package: ElementId,
    name: LangString,
    /// A live `AssertedContext` from the node to the target the file already
    /// has: the repair only takes the support line away.
    joined: Option<ElementId>,
}

/// W27: the Context, Assumption or Justification `id` joined by the
/// `SupportedBy` line that `relationship` draws.
fn plan(document: &Document, view: &View, id: &str, relationship: &str) -> Result<Plan, Refusal> {
    let node = view
        .nodes
        .iter()
        .find(|node| node.id == id)
        .ok_or(Refusal::NotANode)?;
    if !matches!(
        node.kind,
        NodeKind::Context | NodeKind::Assumption | NodeKind::Justification
    ) {
        return Err(Refusal::NothingToRepair);
    }
    let edge = view
        .edges
        .iter()
        .find(|edge| {
            edge.relationship == relationship && edge.to == id && edge.kind == EdgeKind::SupportedBy
        })
        .ok_or(Refusal::NothingToRepair)?;
    let held = document.get(relationship).ok_or(Refusal::NothingToRepair)?;
    let element = document.get(id).ok_or(Refusal::NotANode)?;
    // Only a plain relationship from the node to what the line leaves: with
    // a Strategy as its reasoning the node is one of the Strategy's
    // premises, and whether it is context of the Strategy or of its Goal is
    // not the editor's to say.
    let target = held.target().ok_or(Refusal::JoinNotMoved)?;
    let once = held
        .sources()
        .iter()
        .filter(|source| source.as_str() == id)
        .count()
        == 1;
    if held.reasoning().is_some() || target.as_str() != edge.from || !once {
        return Err(Refusal::JoinNotMoved);
    }
    // What takes the context: a Goal or a Strategy.
    let parent = view
        .nodes
        .iter()
        .find(|node| node.id == edge.from)
        .ok_or(Refusal::JoinNotMoved)?;
    if !matches!(parent.kind, NodeKind::Goal | NodeKind::Strategy) {
        return Err(Refusal::JoinNotMoved);
    }
    let sole = held.sources().len() == 1;
    // W28, as W25 for a move: a relationship something refers to, that
    // holds more than its name, or that says more than that its sources
    // support its target, is not replaced, nor has a source taken out.
    if sole {
        deletable(document, held.id())?;
    } else {
        changeable(document, held.id())?;
    }
    // The node may already be context of the target as well: a context line
    // the diagram draws, so that the node stays joined where it is seen. One
    // it does not draw (cited, abstract, hidden, or not plain) is not leaned on.
    let joined = view
        .edges
        .iter()
        .filter(|line| {
            line.kind == EdgeKind::InContextOf && line.from == edge.from && line.to == id
        })
        .filter_map(|line| document.get(&line.relationship))
        .find(|other| {
            other.kind() == SacmKind::AssertedContext
                && other.target() == Some(target)
                && other.reasoning().is_none()
                && !other.is_abstract()
                && !other.is_citation()
                && other
                    .assertion_declaration()
                    .is_none_or(|declaration| declaration == AssertionDeclaration::Asserted)
        })
        .map(|other| other.id().clone());
    Ok(Plan {
        node: element.id().clone(),
        relationship: held.id().clone(),
        sole,
        target: target.clone(),
        package: argument_package(held).ok_or(Refusal::NoPackage)?,
        name: held.name().cloned().unwrap_or_default(),
        joined,
    })
}

/// Whether the join of the node `id` that `relationship` draws can be
/// repaired (W27), for the problem list to offer it.
///
/// # Errors
///
/// [`Refusal::NothingToRepair`] when it is not a Context, Assumption or
/// Justification joined by that `SupportedBy` line;
/// [`Refusal::JoinNotMoved`], [`Refusal::Entangled`] or
/// [`Refusal::NoPackage`] when it is, and cannot be repaired.
pub fn repairable(
    document: &Document,
    view: &View,
    id: &str,
    relationship: &str,
) -> Result<(), Refusal> {
    plan(document, view, id, relationship).map(|_| ())
}

/// W27: joins the node `id` by an `AssertedContext` in place of the
/// `SupportedBy` line that `relationship` draws.
///
/// # Errors
///
/// What [`repairable`] refuses, as [`AddError::Refused`]; the document is
/// then unchanged.
pub fn repair_join(
    document: &mut Document,
    view: &View,
    id: &str,
    relationship: &str,
) -> Result<Repaired, AddError> {
    let plan = plan(document, view, id, relationship).map_err(AddError::Refused)?;
    let mut made: Option<String> = None;
    let mutation = edit::batch(document, |tx| -> Result<(), BatchError> {
        if plan.sole {
            tx.apply(DeleteElement::new(plan.relationship.clone()))?;
        } else {
            tx.apply(RemoveReference::new(
                plan.relationship.clone(),
                ReferenceEnd::Source,
                plan.node.clone(),
            ))?;
        }
        if let Some(joined) = &plan.joined {
            made = Some(joined.as_str().to_owned());
            return Ok(());
        }
        let created = tx.apply(CreateAssertedContext::new(
            plan.package.clone(),
            // The relationship's name is kept when it takes the place of the
            // one deleted; a relationship that stays keeps its own.
            if plan.sole {
                plan.name.clone()
            } else {
                LangString::default()
            },
            [plan.node.clone()],
            plan.target.clone(),
        ))?;
        made = Some(created.created_ids()[0].as_str().to_owned());
        Ok(())
    })
    .map_err(|error| match error.error() {
        EditError::Referenced { .. } | EditError::NotEmpty { .. } => {
            AddError::Refused(Refusal::Entangled)
        }
        _ => AddError::Edit(error),
    })?;
    Ok(Repaired {
        relationship: made.unwrap_or_default(),
        mutation,
    })
}
