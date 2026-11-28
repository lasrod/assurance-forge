//! Renumbering nodes that share one identifier (W38 in
//! `docs/standards/sacm-gsn-mapping.md`; F5 of
//! `docs/roadmap/milestone-2-repair.md`).
//!
//! [`renumbering`] answers the identifiers a repair of AF-GSN-009 would give,
//! which the problem shows; [`renumber`] gives them, as one
//! `sacm::edit::batch`: the first node keeps the identifier, and each other
//! is given the next of its own prefix, by W15.

use sacm::edit::{self, BatchError, BatchMutation};
use sacm::model::Document;

use super::{AddError, Identifiers, Refusal, identifier_operation};
use crate::ElementKind;
use crate::view::{NodeKind, View};

/// What a renumbering made.
#[derive(Clone, Debug)]
pub struct Renumbered {
    /// What libSACM changed.
    pub mutation: BatchMutation,
}

/// The identifiers renumbering `nodes` would give, where `nodes` are the
/// nodes AF-GSN-009 reports in document order and `view` is the view of
/// `document` as it is: each node but the first, with its new identifier.
///
/// # Errors
///
/// [`Refusal::NothingToRenumber`] when fewer than two nodes are given, one
/// is not drawn, or they do not share one identifier.
pub fn renumbering(
    document: &Document,
    view: &View,
    nodes: &[String],
) -> Result<Vec<(String, String)>, Refusal> {
    let drawn = nodes
        .iter()
        .map(|id| view.nodes.iter().find(|node| node.id == *id))
        .collect::<Option<Vec<_>>>()
        .ok_or(Refusal::NothingToRenumber)?;
    let [first, others @ ..] = drawn.as_slice() else {
        return Err(Refusal::NothingToRenumber);
    };
    if others.is_empty()
        || others
            .iter()
            .any(|node| node.identifier != first.identifier)
    {
        return Err(Refusal::NothingToRenumber);
    }
    let mut identifiers = Identifiers::of(document, view);
    Ok(others
        .iter()
        .map(|node| {
            let prefix = prefix(&node.identifier, node.kind);
            (node.id.clone(), identifiers.next_prefixed(&prefix))
        })
        .collect())
}

/// Gives each node but the first of `nodes` the identifier [`renumbering`]
/// says (W38).
///
/// # Errors
///
/// What [`renumbering`] refuses, and what W15 refuses for an identifier
/// (such as [`Refusal::TextIsExpression`]), as [`AddError::Refused`];
/// [`AddError::Edit`] when libSACM refused an operation. The document is
/// then unchanged.
pub fn renumber(
    document: &mut Document,
    view: &View,
    nodes: &[String],
) -> Result<Renumbered, AddError> {
    let given = renumbering(document, view, nodes).map_err(AddError::Refused)?;
    let mut operations: Vec<edit::Operation> = Vec::new();
    for (id, identifier) in &given {
        let element = document
            .get(id.as_str())
            .ok_or(AddError::Refused(Refusal::NothingToRenumber))?;
        // A new identifier always changes the node.
        let operation = identifier_operation(document, view, element, identifier)?
            .ok_or(AddError::Refused(Refusal::NothingToRenumber))?;
        operations.push(operation);
    }
    let mutation = edit::batch(document, |tx| -> Result<(), BatchError> {
        for operation in operations {
            tx.apply(operation)?;
        }
        Ok(())
    })
    .map_err(AddError::Edit)?;
    Ok(Renumbered { mutation })
}

/// The prefix a node is renumbered under: its identifier without the digits
/// it ends in, or its kind's (W10) when it ends in none or is only digits.
fn prefix(identifier: &str, kind: NodeKind) -> String {
    let stem = identifier.trim_end_matches(|character: char| character.is_ascii_digit());
    if !stem.is_empty() && stem.len() < identifier.len() {
        return stem.to_owned();
    }
    let kind = match kind {
        NodeKind::Strategy => ElementKind::Strategy,
        NodeKind::Solution => ElementKind::Solution,
        NodeKind::Context => ElementKind::Context,
        NodeKind::Assumption => ElementKind::Assumption,
        NodeKind::Justification => ElementKind::Justification,
        NodeKind::Goal | NodeKind::Neutral => ElementKind::Goal,
    };
    kind.identifier_prefix().to_owned()
}
