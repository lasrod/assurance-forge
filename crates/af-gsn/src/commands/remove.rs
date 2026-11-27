//! Removing a node and what is under it (W18 to W20 in
//! `docs/standards/sacm-gsn-mapping.md`).
//!
//! [`removal`] answers what a removal would delete, which the interface shows
//! before it is made; [`remove`] makes it, as one `sacm::edit::batch`.
//!
//! What is under a node is what the diagram draws below it: what it is
//! supported by and in the context of, and what challenges it. A node under
//! it goes with it only when everything it is drawn under goes too; a node
//! also drawn under one that stays, stays, and loses only its line to what
//! goes.

use std::collections::{HashMap, HashSet};

use sacm::edit::{
    self, AddReference, BatchError, BatchMutation, CrossPackageReferencePolicy, DeleteElement,
    ReferenceDeletePolicy, RemoveReference,
};
use sacm::model::{AbstractClass, Document, ElementId, ReferenceEnd};

use super::{AddError, Refusal, argument_package, placeholder};
use crate::view::{Code, EdgeKind, NodeKind, View, can_hold_placeholder};

/// What removing a node would delete, as the diagram draws it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Removal {
    /// The SACM ids of the nodes it deletes: the node chosen first, then
    /// what goes with it, each after a node it is drawn under.
    pub nodes: Vec<String>,
}

/// What a removal made.
#[derive(Clone, Debug)]
pub struct Removed {
    /// A node the removed one was drawn under, which stays: the interface
    /// selects it. `None` for a node at the top level.
    pub parent: Option<String>,
    /// What libSACM changed.
    pub mutation: BatchMutation,
}

/// What removing the node `id` would delete, where `view` is the view of
/// `document` as it is.
///
/// # Errors
///
/// [`Refusal::NotANode`] when the diagram draws no node `id`, and
/// [`Refusal::Entangled`] when something the diagram does not draw refers to
/// what would be deleted, or a node holds more than its text (W19).
pub fn removal(document: &Document, view: &View, id: &str) -> Result<Removal, Refusal> {
    Plan::of(document, view, id).map(|plan| Removal { nodes: plan.nodes })
}

/// Removes the node `id` and what goes with it (W18 to W20), as
/// [`removal`] says.
///
/// # Errors
///
/// What [`removal`] refuses, as [`AddError::Refused`]; [`AddError::Edit`]
/// when libSACM refused an operation, which is a fault in these rules. The
/// document is then unchanged.
pub fn remove(document: &mut Document, view: &View, id: &str) -> Result<Removed, AddError> {
    let plan = Plan::of(document, view, id).map_err(AddError::Refused)?;
    let mutation = edit::batch(document, |tx| -> Result<(), Failure> {
        // W20: an inference that keeps its Strategy and loses every Goal
        // holds the place of the next one, before the last is taken out.
        for (inference, package) in &plan.held {
            let premise = placeholder(tx, package)?;
            tx.apply(AddReference::new(
                inference.clone(),
                ReferenceEnd::Source,
                premise,
            ))?;
        }
        // W18: a relationship that stays loses only the sources that go.
        for (relationship, source) in &plan.scrubbed {
            tx.apply(RemoveReference::new(
                relationship.clone(),
                ReferenceEnd::Source,
                source.clone(),
            ))?;
        }
        // Relationships first, so that what they name is no longer referred
        // to; one already deleted with a relationship it named is skipped.
        let mut deleted: HashSet<String> = HashSet::new();
        for element in &plan.deleted {
            if !tx.document().contains(element.as_str()) {
                continue;
            }
            let made = tx.apply(
                DeleteElement::new(element.clone())
                    .with_reference_policy(ReferenceDeletePolicy::DeleteReferencingRelationships)
                    .with_cross_package_policy(CrossPackageReferencePolicy::ApplyReferencePolicy),
            )?;
            deleted.extend(made.deleted_ids().iter().map(|id| id.as_str().to_owned()));
        }
        // Exactly what was shown is deleted, with what each element holds.
        let exact = plan
            .deleted
            .iter()
            .all(|element| deleted.contains(element.as_str()))
            && deleted
                .iter()
                .all(|element| plan.held_within.contains(element));
        if exact {
            Ok(())
        } else {
            Err(Failure::Unexpected)
        }
    })
    .map_err(|failure| match failure {
        Failure::Edit(error) => AddError::Edit(error),
        Failure::Unexpected => AddError::Refused(Refusal::Entangled),
    })?;
    Ok(Removed {
        parent: plan.parent,
        mutation,
    })
}

/// Why a removal's batch was rolled back.
enum Failure {
    Edit(BatchError),
    /// libSACM would delete more, or less, than the plan: something refers
    /// to it that the plan did not see.
    Unexpected,
}

impl From<BatchError> for Failure {
    fn from(error: BatchError) -> Self {
        Failure::Edit(error)
    }
}

/// A removal worked out, before anything is changed.
struct Plan {
    /// The nodes, as [`Removal::nodes`].
    nodes: Vec<String>,
    /// Every element to delete: the relationships, then the nodes, then the
    /// placeholders of the Strategies that go.
    deleted: Vec<ElementId>,
    /// The ids of those elements and of everything they hold: what libSACM
    /// may delete.
    held_within: HashSet<String>,
    /// A relationship that stays, and a source of it that goes.
    scrubbed: Vec<(ElementId, ElementId)>,
    /// An inference that stays with its Strategy and loses every Goal, and
    /// the package its placeholder goes into.
    held: Vec<(ElementId, ElementId)>,
    parent: Option<String>,
}

impl Plan {
    fn of(document: &Document, view: &View, id: &str) -> Result<Plan, Refusal> {
        if !view.nodes.iter().any(|node| node.id == id) {
            return Err(Refusal::NotANode);
        }
        let strategies: HashSet<&str> = view
            .nodes
            .iter()
            .filter(|node| node.kind == NodeKind::Strategy)
            .map(|node| node.id.as_str())
            .collect();
        let placeholders: HashSet<&str> = view
            .unplaced
            .iter()
            .filter(|item| item.code == Code::Placeholder)
            .map(|item| item.id.as_str())
            .collect();

        // What each node is drawn under: the node an edge leaves, or for a
        // challenge, what it challenges, a node or a relationship; with the
        // relationship that draws the edge, since the line is gone when that
        // relationship goes even if the node it leaves stays.
        let mut parents: HashMap<&str, Vec<(&str, &str)>> = HashMap::new();
        for edge in &view.edges {
            let (child, parent) = if edge.kind == EdgeKind::Challenge {
                (edge.from.as_str(), edge.to.as_str())
            } else {
                (edge.to.as_str(), edge.from.as_str())
            };
            if child != parent {
                parents
                    .entry(child)
                    .or_default()
                    .push((parent, edge.relationship.as_str()));
            }
        }

        // W18: the node, then each node all of whose parents go, until no
        // more do.
        let mut nodes: Vec<&str> = vec![id];
        let mut going: HashSet<&str> = HashSet::from([id]);
        let mut relationships = Relationships::of(document, &strategies, &going);
        loop {
            let mut grew = false;
            for node in &view.nodes {
                let candidate = node.id.as_str();
                if going.contains(candidate) {
                    continue;
                }
                let Some(above) = parents.get(candidate) else {
                    continue;
                };
                // A line goes with the relationship that draws it. A
                // Strategy's inference that holds a place (W20) stays, so a
                // Strategy goes this way only when nothing would draw it.
                let gone = |(parent, line): &(&str, &str)| {
                    going.contains(parent)
                        || relationships.deleted.contains(*parent)
                        || relationships.deleted.contains(*line)
                };
                if above.iter().all(gone) {
                    nodes.push(candidate);
                    going.insert(candidate);
                    grew = true;
                }
            }
            if !grew {
                break;
            }
            relationships = Relationships::of(document, &strategies, &going);
        }

        // Listed from the node chosen down, each after a node it is drawn
        // under that also goes; one whose line went with its relationship
        // and whose parents stay comes after them.
        let mut ordered: Vec<&str> = vec![id];
        let mut next = 0;
        while next < ordered.len() {
            let above = ordered[next];
            next += 1;
            for candidate in &nodes {
                let under = parents
                    .get(candidate)
                    .is_some_and(|list| list.iter().any(|(parent, _)| *parent == above));
                if under && !ordered.contains(candidate) {
                    ordered.push(candidate);
                }
            }
        }
        for candidate in &nodes {
            if !ordered.contains(candidate) {
                ordered.push(candidate);
            }
        }
        let nodes = ordered;

        // The relationships in document order, then the nodes, then the
        // placeholders the deleted inferences held.
        let mut deleted: Vec<ElementId> = Vec::new();
        let mut placeholders_going: Vec<ElementId> = Vec::new();
        for element in document.elements() {
            let element_id = element.id().as_str();
            if relationships.deleted.contains(element_id) {
                deleted.push(element.id().clone());
                for source in element.sources() {
                    if placeholders.contains(source.as_str()) {
                        placeholders_going.push(source.clone());
                    }
                }
            }
        }
        for node in &nodes {
            if let Some(element) = document.get(*node) {
                deleted.push(element.id().clone());
            }
        }
        deleted.extend(placeholders_going);
        // R17: a description only the deleted relationships named goes with
        // them, and is listed: it would otherwise be drawn as a Goal of its
        // own, which nobody asked for.
        let mut described: Vec<&str> = Vec::new();
        for description in &view.descriptions {
            let referrers = document.referrers(description.id.as_str());
            let only_ours = !referrers.is_empty()
                && referrers.iter().all(|referrer| {
                    referrer.end() == ReferenceEnd::MetaClaim
                        && relationships
                            .deleted
                            .contains(referrer.holder().id().as_str())
                });
            if let (true, Some(element)) = (only_ours, document.get(description.id.as_str())) {
                deleted.push(element.id().clone());
                described.push(description.id.as_str());
            }
        }
        let all: HashSet<&str> = deleted.iter().map(ElementId::as_str).collect();

        // W19: what refers to what goes. A relationship that stays loses the
        // source; anything else, which the diagram does not draw, refuses.
        let mut scrubbed: Vec<(ElementId, ElementId)> = Vec::new();
        let mut held_within: HashSet<String> = HashSet::new();
        for element_id in &deleted {
            let Some(element) = document.get(element_id.as_str()) else {
                continue;
            };
            if element
                .children()
                .any(|child| !child.kind().is_a(AbstractClass::UtilityElement))
            {
                return Err(Refusal::Entangled);
            }
            let mut stack = vec![element];
            while let Some(inner) = stack.pop() {
                held_within.insert(inner.id().as_str().to_owned());
                stack.extend(inner.children());
            }
            for referrer in document.referrers(element_id.as_str()) {
                let holder = referrer.holder();
                let mut within = Some(holder);
                let mut goes = false;
                while let Some(ancestor) = within {
                    if all.contains(ancestor.id().as_str()) {
                        goes = true;
                        break;
                    }
                    within = ancestor.parent();
                }
                if goes {
                    continue;
                }
                if holder.kind().is_a(AbstractClass::AssertedRelationship)
                    && referrer.end() == ReferenceEnd::Source
                {
                    let pair = (holder.id().clone(), element_id.clone());
                    if !scrubbed.contains(&pair) {
                        scrubbed.push(pair);
                    }
                } else {
                    return Err(Refusal::Entangled);
                }
            }
        }

        let mut held: Vec<(ElementId, ElementId)> = Vec::new();
        for inference in &relationships.held {
            let element = document.get(inference.as_str()).ok_or(Refusal::Entangled)?;
            let package = argument_package(element).ok_or(Refusal::NoPackage)?;
            held.push((element.id().clone(), package));
        }

        let parent = parents.get(id).and_then(|above| {
            above
                .iter()
                .find(|(parent, _)| {
                    !going.contains(*parent) && view.nodes.iter().any(|node| node.id == *parent)
                })
                .map(|(parent, _)| (*parent).to_owned())
        });

        Ok(Plan {
            nodes: nodes
                .into_iter()
                .chain(described)
                .map(str::to_owned)
                .collect(),
            deleted,
            held_within,
            scrubbed,
            held,
            parent,
        })
    }
}

/// The relationships a removal of `nodes` deletes, and those it keeps for
/// their Strategy (W20).
struct Relationships {
    deleted: HashSet<String>,
    /// Inferences whose reasoning is a Strategy that stays, and whose every
    /// source goes.
    held: Vec<String>,
}

impl Relationships {
    /// W18: a relationship goes when its target or its reasoning goes, or
    /// every source goes; a relationship that a deleted one names goes on
    /// the same terms, until no more do. An inference that loses every
    /// source but keeps a Strategy stays (W20).
    fn of(document: &Document, strategies: &HashSet<&str>, nodes: &HashSet<&str>) -> Self {
        let mut dead: HashSet<String> = nodes.iter().map(|node| (*node).to_owned()).collect();
        let mut deleted: HashSet<String> = HashSet::new();
        let mut touched: Vec<String> = Vec::new();
        let mut queue: Vec<String> = dead.iter().cloned().collect();
        while let Some(id) = queue.pop() {
            for referrer in document.referrers(id.as_str()) {
                let relationship = referrer.holder();
                if !relationship
                    .kind()
                    .is_a(AbstractClass::AssertedRelationship)
                {
                    continue;
                }
                let relationship_id = relationship.id().as_str();
                if dead.contains(relationship_id) {
                    continue;
                }
                let is_dead = |end: &ElementId| dead.contains(end.as_str());
                let end_dead = relationship.target().is_some_and(is_dead)
                    || relationship.reasoning().is_some_and(is_dead);
                let sources_dead = !relationship.sources().is_empty()
                    && relationship.sources().iter().all(is_dead);
                // Only an inference R16 would hide a placeholder in: any other
                // would draw the placeholder W20 adds as a Goal nobody asked for.
                let keeps_strategy = relationship
                    .reasoning()
                    .is_some_and(|reasoning| strategies.contains(reasoning.as_str()))
                    && can_hold_placeholder(document, relationship);
                if end_dead || (sources_dead && !keeps_strategy) {
                    dead.insert(relationship_id.to_owned());
                    deleted.insert(relationship_id.to_owned());
                    queue.push(relationship_id.to_owned());
                } else if sources_dead {
                    touched.push(relationship_id.to_owned());
                }
            }
        }
        let mut held: Vec<String> = Vec::new();
        for id in touched {
            if !deleted.contains(&id) && !held.contains(&id) {
                held.push(id);
            }
        }
        Self { deleted, held }
    }
}
