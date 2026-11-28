//! Removing one line of the diagram, the nodes at its ends staying (W37 in
//! `docs/standards/sacm-gsn-mapping.md`; F2 and F3 of
//! `docs/roadmap/milestone-2-repair.md`).
//!
//! [`line_removal`] answers what a removal would take, which the interface
//! shows first when an ACP goes with the line; [`remove_line`] makes it, as
//! one `sacm::edit::batch`.
//!
//! A line other than a Strategy's first names one `source` of the
//! relationship it is drawn for: its lower end. That source alone, the
//! relationship goes, unless a Strategy keeps it (W20); one of several, it
//! is taken out of the relationship. No node goes, and none is drawn as
//! another kind than it was: that is checked on the result, which is rolled
//! back otherwise.

use std::collections::HashMap;

use sacm::edit::{
    self, AddReference, BatchError, BatchMutation, DeleteElement, EditError, RemoveReference,
};
use sacm::model::{AbstractClass, Document, ElementId, ReferenceEnd};

use super::{AddError, Line, Refusal, argument_package, changeable, placeholder};
use crate::view::{self, EdgeKind, NodeKind, View, can_hold_placeholder};

/// What removing a line would take with it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct LineRemoval {
    /// The ACPs that go with the line, as the relationship and the
    /// confidence Claim: each one E11 draws on it, when its relationship
    /// goes (W19).
    pub acps: Vec<(String, String)>,
    /// The descriptions (R17) that go with those ACPs: only the line's
    /// relationship names them.
    pub descriptions: Vec<String>,
}

/// What a line's removal made.
#[derive(Clone, Debug)]
pub struct LineRemoved {
    /// What libSACM changed.
    pub mutation: BatchMutation,
}

/// What removing `line` would take, where `view` is the view of `document`
/// as it is.
///
/// # Errors
///
/// What [`remove_line`] refuses, found by making the removal on a copy of
/// the document: [`Refusal::NotARemovableLine`],
/// [`Refusal::StrategyLine`], [`Refusal::LineChallenged`],
/// [`Refusal::Entangled`] and [`Refusal::KindFromLine`] (W37).
pub fn line_removal(
    document: &Document,
    view: &View,
    line: Line<'_>,
) -> Result<LineRemoval, Refusal> {
    let plan = Plan::of(document, view, line)?;
    let mut trial = document.clone();
    match apply(&mut trial, &plan) {
        Ok(_) => Ok(LineRemoval {
            acps: plan.acps,
            descriptions: plan
                .descriptions
                .iter()
                .map(|id| id.as_str().to_owned())
                .collect(),
        }),
        Err(AddError::Refused(refusal)) => Err(refusal),
        // A fault in these rules: the command says so; the menu refuses.
        Err(AddError::Edit(_)) => Err(Refusal::Entangled),
    }
}

/// Removes `line` (W37), as [`line_removal`] says, where `view` is the view
/// of `document` as it is.
///
/// # Errors
///
/// What [`line_removal`] refuses, as [`AddError::Refused`];
/// [`AddError::Edit`] when libSACM refused an operation, which is a fault
/// in these rules. The document is then unchanged.
pub fn remove_line(
    document: &mut Document,
    view: &View,
    line: Line<'_>,
) -> Result<LineRemoved, AddError> {
    let plan = Plan::of(document, view, line).map_err(AddError::Refused)?;
    let mutation = apply(document, &plan)?;
    Ok(LineRemoved { mutation })
}

/// What the batch of a removal does.
enum Change {
    /// The line's source is the relationship's only one: the relationship
    /// goes, with the descriptions only it names.
    Delete { relationship: ElementId },
    /// The source is the only one of a Strategy's inference, which stays,
    /// holding the place of the next Goal in a new placeholder in `package`
    /// (W20).
    HoldPlace {
        inference: ElementId,
        source: ElementId,
        package: ElementId,
    },
    /// The source is taken out of the relationship, which stays for the
    /// others.
    Scrub {
        relationship: ElementId,
        source: ElementId,
    },
}

/// A removal worked out, before anything is changed.
struct Plan {
    change: Change,
    acps: Vec<(String, String)>,
    descriptions: Vec<ElementId>,
}

impl Plan {
    fn of(document: &Document, view: &View, line: Line<'_>) -> Result<Plan, Refusal> {
        // A support or context line the view draws; not a challenge, which
        // goes with its challenger (W18), nor a neutral line.
        let drawn = view.edges.iter().find(|edge| {
            edge.relationship == line.relationship
                && edge.from == line.from
                && edge.to == line.to
                && !edge.to_relationship
        });
        let drawn = drawn.ok_or(Refusal::NotARemovableLine)?;
        if !matches!(drawn.kind, EdgeKind::SupportedBy | EdgeKind::InContextOf) {
            return Err(Refusal::NotARemovableLine);
        }
        let relationship = document
            .get(line.relationship)
            .filter(|element| element.kind().is_a(AbstractClass::AssertedRelationship))
            .ok_or(Refusal::NotARemovableLine)?;
        let is_strategy = |id: &str| {
            view.nodes
                .iter()
                .any(|node| node.id == id && node.kind == NodeKind::Strategy)
        };
        let reasoning = relationship
            .reasoning()
            .map(ElementId::as_str)
            .filter(|reasoning| is_strategy(reasoning));
        // F3: SACM 2.3 §11.13 gives an inference one target, so a Strategy
        // and its Goals cannot keep their place without the Goal above.
        let to_target = relationship
            .target()
            .is_some_and(|target| target.as_str() == line.from);
        if to_target && reasoning == Some(line.to) {
            return Err(Refusal::StrategyLine);
        }
        // Any other line runs from the target, or from the Strategy, to one
        // source, named once.
        if !(to_target || reasoning == Some(line.from)) {
            return Err(Refusal::NotARemovableLine);
        }
        let named = relationship
            .sources()
            .iter()
            .filter(|source| source.as_str() == line.to)
            .count();
        if named != 1 {
            return Err(Refusal::NotARemovableLine);
        }
        let source = relationship
            .sources()
            .iter()
            .find(|source| source.as_str() == line.to)
            .cloned()
            .ok_or(Refusal::NotARemovableLine)?;
        // W25: a claim about the relationship, a challenge of it above all,
        // is not one about what is left.
        let referrers = document.referrers(line.relationship);
        if referrers.iter().any(|referrer| {
            referrer.end() == ReferenceEnd::Target && referrer.holder().is_counter()
        }) {
            return Err(Refusal::LineChallenged);
        }
        if !referrers.is_empty() {
            return Err(Refusal::Entangled);
        }

        let id = relationship.id().clone();
        let sole = relationship.sources().len() == 1;
        if sole && reasoning == Some(line.from) {
            // W20: the Strategy keeps its inference, in which R16 hides the
            // placeholder; any other would draw it as a Goal nobody asked for.
            changeable(document, &id)?;
            if !can_hold_placeholder(document, relationship) {
                return Err(Refusal::Entangled);
            }
            let package = argument_package(relationship).ok_or(Refusal::NoPackage)?;
            return Ok(Plan {
                change: Change::HoldPlace {
                    inference: id,
                    source,
                    package,
                },
                acps: Vec::new(),
                descriptions: Vec::new(),
            });
        }
        if !sole {
            changeable(document, &id)?;
            return Ok(Plan {
                change: Change::Scrub {
                    relationship: id,
                    source,
                },
                acps: Vec::new(),
                descriptions: Vec::new(),
            });
        }

        // The relationship goes with what it holds, which is only its text.
        if relationship
            .children()
            .any(|child| !child.kind().is_a(AbstractClass::UtilityElement))
        {
            return Err(Refusal::Entangled);
        }
        // W19: its ACPs go with it and are listed; a metaClaim the diagram
        // does not draw would go unseen.
        let mut acps: Vec<(String, String)> = Vec::new();
        for claim in relationship.meta_claims() {
            let drawn = view
                .acps
                .iter()
                .any(|acp| acp.relationship == line.relationship && acp.claim == claim.as_str());
            if !drawn {
                return Err(Refusal::Entangled);
            }
            acps.push((line.relationship.to_owned(), claim.as_str().to_owned()));
        }
        // R17: a description only this relationship names would otherwise
        // be drawn as a Goal of its own.
        let mut descriptions: Vec<ElementId> = Vec::new();
        for description in &view.descriptions {
            let referrers = document.referrers(description.id.as_str());
            let only_ours = !referrers.is_empty()
                && referrers.iter().all(|referrer| {
                    referrer.end() == ReferenceEnd::MetaClaim
                        && referrer.holder().id().as_str() == line.relationship
                });
            if let (true, Some(element)) = (only_ours, document.get(description.id.as_str())) {
                descriptions.push(element.id().clone());
            }
        }
        Ok(Plan {
            change: Change::Delete { relationship: id },
            acps,
            descriptions,
        })
    }
}

/// Why a removal's batch was rolled back.
enum Failure {
    Edit(BatchError),
    /// A node is drawn as another kind than it was, or no longer drawn, or
    /// a new one is drawn.
    KindChanged,
}

impl From<BatchError> for Failure {
    fn from(error: BatchError) -> Self {
        Failure::Edit(error)
    }
}

/// Makes `plan` on `document`, and keeps it only when every node is drawn
/// as the kind it was.
fn apply(document: &mut Document, plan: &Plan) -> Result<BatchMutation, AddError> {
    let before = kinds(&view::view(document, "en"));
    edit::batch(document, |tx| -> Result<(), Failure> {
        match &plan.change {
            Change::Delete { relationship } => {
                tx.apply(DeleteElement::new(relationship.clone()))?;
                for description in &plan.descriptions {
                    tx.apply(DeleteElement::new(description.clone()))?;
                }
            }
            Change::HoldPlace {
                inference,
                source,
                package,
            } => {
                let premise = placeholder(tx, package)?;
                tx.apply(AddReference::new(
                    inference.clone(),
                    ReferenceEnd::Source,
                    premise,
                ))?;
                tx.apply(RemoveReference::new(
                    inference.clone(),
                    ReferenceEnd::Source,
                    source.clone(),
                ))?;
            }
            Change::Scrub {
                relationship,
                source,
            } => {
                tx.apply(RemoveReference::new(
                    relationship.clone(),
                    ReferenceEnd::Source,
                    source.clone(),
                ))?;
            }
        }
        if kinds(&view::view(tx.document(), "en")) == before {
            Ok(())
        } else {
            Err(Failure::KindChanged)
        }
    })
    .map_err(|failure| match failure {
        Failure::KindChanged => AddError::Refused(Refusal::KindFromLine),
        Failure::Edit(error) => match error.error() {
            // Something the plan did not see refers to what goes.
            EditError::Referenced { .. } | EditError::NotEmpty { .. } => {
                AddError::Refused(Refusal::Entangled)
            }
            _ => AddError::Edit(error),
        },
    })
}

/// Each node the view draws, by its SACM id, with its kind.
fn kinds(view: &View) -> HashMap<String, NodeKind> {
    view.nodes
        .iter()
        .map(|node| (node.id.clone(), node.kind))
        .collect()
}
