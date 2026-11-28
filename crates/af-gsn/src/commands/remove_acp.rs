//! Removing an assurance claim point (ACP) from its line (W35 in
//! `docs/standards/sacm-gsn-mapping.md`).
//!
//! [`acp_removal`] answers what removing an ACP deletes, which the interface
//! shows before it is made; [`remove_acp`] makes it, as one
//! `sacm::edit::batch`.
//!
//! The relationship's `metaClaim` loses the confidence Claim. A description
//! (R17) goes with it, since nothing else would show it. A confidence
//! argument in a package of its own is kept or deleted with the ACP, as the
//! user chooses. A goal of the line's own argument stays: only the ACP goes.

use std::collections::HashSet;

use sacm::edit::{
    self, BatchError, BatchMutation, DeleteElement, PackageDeletePolicy, RemoveReference,
};
use sacm::model::{AbstractClass, Document, ElementId, ReferenceEnd, SacmElement};

use super::{AddError, Refusal};
use crate::view::{Code, View};

/// What removing an ACP deletes, as the diagram draws it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct AcpRemoval {
    /// The description the ACP shows (R17), which goes with it. `None` for a
    /// confidence argument, a goal of the line's argument, or a description
    /// another ACP also shows, which stays for that one.
    pub description: Option<String>,
    /// The confidence argument in an argument package of its own, which may
    /// be kept or deleted with the ACP. `None` when the Claim is in the
    /// line's package.
    pub argument: Option<ConfidenceArgument>,
}

/// A confidence argument in a package of its own (W34's "confidence
/// argument").
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ConfidenceArgument {
    /// The SACM id of the package.
    pub package: String,
    /// The nodes deleting it deletes: the confidence Claim, then the other
    /// nodes the view draws in it, then the descriptions it holds, each in
    /// document order.
    pub nodes: Vec<String>,
    /// Why it cannot be deleted with the ACP, or `None`. The ACP may still be
    /// removed alone.
    pub refusal: Option<Refusal>,
}

/// W35: what removing the ACP that `relationship` carries for the Claim
/// `claim` would delete, where `view` is the view of `document` as it is.
///
/// # Errors
///
/// [`Refusal::NotAnAcp`] when the view draws no such ACP.
pub fn acp_removal(
    document: &Document,
    view: &View,
    relationship: &str,
    claim: &str,
) -> Result<AcpRemoval, Refusal> {
    let acp = view
        .acps
        .iter()
        .find(|acp| acp.relationship == relationship && acp.claim == claim)
        .ok_or(Refusal::NotAnAcp)?;
    // R17: a description only this relationship names goes with its ACP; it
    // would otherwise be drawn as a Goal nobody asked for.
    let described = view.descriptions.iter().any(|node| node.id == claim);
    let only_ours = document.referrers(claim).iter().all(|referrer| {
        referrer.end() == ReferenceEnd::MetaClaim && referrer.holder().id().as_str() == relationship
    });
    let description = (described && only_ours).then(|| claim.to_owned());
    let argument = match &acp.package {
        Some(package) => Some(confidence_argument(
            document,
            view,
            relationship,
            claim,
            package,
        )?),
        None => None,
    };
    Ok(AcpRemoval {
        description,
        argument,
    })
}

/// Removes the ACP that `relationship` carries for the Claim `claim`, as
/// [`acp_removal`] says: with its description, and with its confidence
/// argument when `with_argument` is set and it has one.
///
/// # Errors
///
/// What [`acp_removal`] refuses, and the confidence argument's refusal when
/// it is to be deleted, as [`AddError::Refused`]; [`AddError::Edit`] when
/// libSACM refused an operation, which is a fault in these rules. The
/// document is then unchanged.
pub fn remove_acp(
    document: &mut Document,
    view: &View,
    relationship: &str,
    claim: &str,
    with_argument: bool,
) -> Result<BatchMutation, AddError> {
    let plan = acp_removal(document, view, relationship, claim).map_err(AddError::Refused)?;
    let argument = match plan.argument {
        Some(argument) if with_argument => {
            if let Some(refusal) = argument.refusal {
                return Err(AddError::Refused(refusal));
            }
            Some(argument.package)
        }
        _ => None,
    };
    let holder = document
        .get(relationship)
        .map(|element| element.id().clone())
        .ok_or(AddError::Refused(Refusal::NotAnAcp))?;
    let named = document
        .get(claim)
        .map(|element| element.id().clone())
        .ok_or(AddError::Refused(Refusal::NotAnAcp))?;
    // What may be deleted: the description and the package, with all they
    // hold.
    let mut expected: HashSet<String> = HashSet::new();
    let mut roots: Vec<ElementId> = Vec::new();
    for id in plan.description.iter().chain(&argument) {
        if let Some(element) = document.get(id.as_str()) {
            expected.extend(held_within(element));
            roots.push(element.id().clone());
        }
    }
    edit::batch(document, |tx| -> Result<(), Failure> {
        tx.apply(RemoveReference::new(
            holder.clone(),
            ReferenceEnd::MetaClaim,
            named.clone(),
        ))?;
        let mut deleted: HashSet<String> = HashSet::new();
        for root in &roots {
            // Nothing else refers to it now: libSACM refuses if something
            // does, and the batch is rolled back.
            let made = tx.apply(
                DeleteElement::new(root.clone())
                    .with_package_policy(PackageDeletePolicy::DeleteRecursively),
            )?;
            deleted.extend(made.deleted_ids().iter().map(|id| id.as_str().to_owned()));
        }
        // Exactly what was shown is deleted.
        let exact = roots.iter().all(|root| deleted.contains(root.as_str()))
            && deleted.iter().all(|id| expected.contains(id));
        if exact {
            Ok(())
        } else {
            Err(Failure::Unexpected)
        }
    })
    .map_err(|failure| match failure {
        Failure::Edit(error) => AddError::Edit(error),
        Failure::Unexpected => AddError::Refused(Refusal::Entangled),
    })
}

/// Why a removal's batch was rolled back.
enum Failure {
    Edit(BatchError),
    /// libSACM would delete more, or less, than the plan.
    Unexpected,
}

impl From<BatchError> for Failure {
    fn from(error: BatchError) -> Self {
        Failure::Edit(error)
    }
}

/// The confidence argument in `package`, and whether it may be deleted with
/// the ACP: only when it holds nothing but the argument the diagram draws,
/// and nothing outside it refers into it but this ACP, or is referred to from
/// it but by a citation (W35).
fn confidence_argument(
    document: &Document,
    view: &View,
    relationship: &str,
    claim: &str,
    package: &str,
) -> Result<ConfidenceArgument, Refusal> {
    let element = document.get(package).ok_or(Refusal::NotAnAcp)?;
    let held = held_within(element);
    let drawn: HashSet<&str> = view
        .nodes
        .iter()
        .chain(&view.descriptions)
        .map(|node| node.id.as_str())
        .chain(
            view.unplaced
                .iter()
                .filter(|item| item.code == Code::Placeholder)
                .map(|item| item.id.as_str()),
        )
        .collect();

    let mut nodes: Vec<String> = vec![claim.to_owned()];
    nodes.extend(
        view.nodes
            .iter()
            .chain(&view.descriptions)
            .filter(|node| node.id != claim && held.contains(node.id.as_str()))
            .map(|node| node.id.clone()),
    );

    // What the dialog lists, the lines between them, and what the diagram
    // never shows on its own: the placeholders W20 adds (which a node's
    // removal takes unlisted too) and utility elements. A relationship the
    // diagram does not draw would go unseen: refused.
    let lines: HashSet<&str> = view
        .edges
        .iter()
        .map(|edge| edge.relationship.as_str())
        .collect();
    let shown = |element: SacmElement<'_>| {
        drawn.contains(element.id().as_str())
            || lines.contains(element.id().as_str())
            || element.kind().is_a(AbstractClass::UtilityElement)
    };
    let contents = element.children().all(shown);
    let incoming = held.iter().all(|id| {
        document.referrers(id.as_str()).iter().all(|referrer| {
            let holder = referrer.holder().id().as_str();
            held.contains(holder)
                || (holder == relationship
                    && referrer.end() == ReferenceEnd::MetaClaim
                    && id == claim)
        })
    });
    // A line from the confidence argument to a node outside it is drawn
    // there too; a citation of one only names it.
    let outgoing = held.iter().all(|id| {
        document.get(id.as_str()).is_none_or(|inner| {
            ReferenceEnd::ALL
                .iter()
                .filter(|end| **end != ReferenceEnd::CitedElement)
                .all(|end| {
                    inner
                        .references(*end)
                        .iter()
                        .all(|named| held.contains(named.as_str()))
                })
        })
    });
    Ok(ConfidenceArgument {
        package: package.to_owned(),
        nodes,
        refusal: (!(contents && incoming && outgoing)).then_some(Refusal::Entangled),
    })
}

/// The ids of `element` and of everything it holds.
fn held_within(element: SacmElement<'_>) -> HashSet<String> {
    let mut held = HashSet::new();
    let mut stack = vec![element];
    while let Some(inner) = stack.pop() {
        held.insert(inner.id().as_str().to_owned());
        stack.extend(inner.children());
    }
    held
}
