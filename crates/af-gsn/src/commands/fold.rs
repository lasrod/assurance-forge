//! Making a Strategy that the file puts at an end of a support relationship
//! the reasoning of that relationship (W39 in
//! `docs/standards/sacm-gsn-mapping.md`; F6 of
//! `docs/roadmap/milestone-2-repair.md`).
//!
//! The file says "S supports G" and "these Goals support S"; GSN says "G is
//! supported, through S, by these Goals". [`strategy_fold`] answers whether
//! the one can be made the other without guessing, and between which Goals;
//! [`fold_strategy`] makes it, as one `sacm::edit::batch`: the inference
//! above keeps its id and target, takes the Goals below as its sources and S
//! as its reasoning, and those below go.

use sacm::edit::{self, BatchError, BatchMutation, DeleteElement, SetReferences};
use sacm::model::{
    AbstractClass, AssertionDeclaration, Document, ElementId, ElementKind as SacmKind,
    ReferenceEnd, SacmElement,
};

use super::{AddError, Refusal, argument_package, changeable, deletable, placeholder};
use crate::view::{NodeKind, View};

/// A fold worked out: the inference above the Strategy, the Goal it
/// supports, and the Goals that will be its premises, in order.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Fold {
    pub inference: String,
    pub goal: String,
    pub premises: Vec<String>,
    /// The inferences below, which go.
    pub below: Vec<String>,
}

/// What a fold made.
#[derive(Clone, Debug)]
pub struct Folded {
    /// What libSACM changed.
    pub mutation: BatchMutation,
}

/// Whether the Strategy `strategy` can be made the reasoning of the one
/// relationship above it (W39), and between which Goals, where `view` is
/// the view of `document` as it is.
///
/// # Errors
///
/// [`Refusal::NotANode`] for anything but a Strategy the view draws;
/// [`Refusal::StrategyNotAtOneGoal`], [`Refusal::NotOnlyGoalsBelow`],
/// [`Refusal::AlreadyReasoning`] and [`Refusal::Entangled`] as W39 says.
pub fn strategy_fold(document: &Document, view: &View, strategy: &str) -> Result<Fold, Refusal> {
    let kind = |id: &str| {
        view.nodes
            .iter()
            .find(|node| node.id == id)
            .map(|node| node.kind)
    };
    if kind(strategy) != Some(NodeKind::Strategy) {
        return Err(Refusal::NotANode);
    }
    let mut above: Vec<SacmElement<'_>> = Vec::new();
    let mut below: Vec<SacmElement<'_>> = Vec::new();
    for referrer in document.referrers(strategy) {
        let relationship = referrer.holder();
        if !relationship
            .kind()
            .is_a(AbstractClass::AssertedRelationship)
        {
            continue;
        }
        match referrer.end() {
            ReferenceEnd::Reasoning => return Err(Refusal::AlreadyReasoning),
            // A challenge of the Strategy, or one it makes, is no support.
            _ if relationship.is_counter() => {}
            ReferenceEnd::Source if relationship.kind() != SacmKind::AssertedContext => {
                above.push(relationship);
            }
            ReferenceEnd::Target if relationship.kind() != SacmKind::AssertedContext => {
                below.push(relationship);
            }
            // Context of the Strategy, and anything else, stays as it is.
            _ => {}
        }
    }
    let [up] = above.as_slice() else {
        return Err(Refusal::StrategyNotAtOneGoal);
    };
    let goal = up
        .target()
        .map(|target| target.as_str().to_owned())
        .filter(|target| kind(target) == Some(NodeKind::Goal));
    let alone = up.sources().len() == 1;
    let (Some(goal), true, SacmKind::AssertedInference, None) =
        (goal, alone, up.kind(), up.reasoning())
    else {
        return Err(Refusal::StrategyNotAtOneGoal);
    };

    let mut premises: Vec<String> = Vec::new();
    let mut below_ids: Vec<String> = Vec::new();
    for down in &below {
        if down.kind() != SacmKind::AssertedInference || down.reasoning().is_some() {
            return Err(Refusal::NotOnlyGoalsBelow);
        }
        for source in down.sources() {
            let source = source.as_str();
            if source == goal || kind(source) != Some(NodeKind::Goal) {
                return Err(Refusal::NotOnlyGoalsBelow);
            }
            if !premises.iter().any(|known| known == source) {
                premises.push(source.to_owned());
            }
        }
        deletable(document, down.id())?;
        below_ids.push(down.id().as_str().to_owned());
    }
    // W25: what refers to the inference above, or an ACP on it, is about
    // "S supports G", not about what it becomes.
    changeable(document, up.id())?;
    let plain = up
        .assertion_declaration()
        .is_none_or(|declaration| declaration == AssertionDeclaration::Asserted)
        && !up.is_abstract()
        && !up.is_citation();
    if !plain {
        return Err(Refusal::Entangled);
    }
    Ok(Fold {
        inference: up.id().as_str().to_owned(),
        goal,
        premises,
        below: below_ids,
    })
}

/// Makes the Strategy `strategy` the reasoning of the relationship above it
/// (W39), as [`strategy_fold`] says.
///
/// # Errors
///
/// What [`strategy_fold`] refuses, as [`AddError::Refused`];
/// [`AddError::Edit`] when libSACM refused an operation. The document is
/// then unchanged.
pub fn fold_strategy(
    document: &mut Document,
    view: &View,
    strategy: &str,
) -> Result<Folded, AddError> {
    let fold = strategy_fold(document, view, strategy).map_err(AddError::Refused)?;
    let id = |text: &str| -> Result<ElementId, AddError> {
        document
            .get(text)
            .map(|element| element.id().clone())
            .ok_or(AddError::Refused(Refusal::NotANode))
    };
    let inference = id(&fold.inference)?;
    let reasoning = id(strategy)?;
    let premises = fold
        .premises
        .iter()
        .map(|premise| id(premise))
        .collect::<Result<Vec<_>, _>>()?;
    let below = fold
        .below
        .iter()
        .map(|down| id(down))
        .collect::<Result<Vec<_>, _>>()?;
    let package = document
        .get(fold.inference.as_str())
        .and_then(argument_package)
        .ok_or(AddError::Refused(Refusal::NoPackage))?;
    let mutation = edit::batch(document, |tx| -> Result<(), BatchError> {
        // Those below go first: the Strategy is then the source of nothing
        // but the inference above.
        for down in below {
            tx.apply(DeleteElement::new(down))?;
        }
        let sources = if premises.is_empty() {
            // W2: a Strategy with no Goal yet holds the place of its first.
            vec![placeholder(tx, &package)?]
        } else {
            premises
        };
        tx.apply(SetReferences::new(
            inference.clone(),
            ReferenceEnd::Source,
            sources,
        ))?;
        tx.apply(SetReferences::new(
            inference,
            ReferenceEnd::Reasoning,
            [reasoning],
        ))?;
        Ok(())
    })
    .map_err(AddError::Edit)?;
    Ok(Folded { mutation })
}
