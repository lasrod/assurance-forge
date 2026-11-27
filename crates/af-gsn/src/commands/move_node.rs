//! Moving a node, with what is under it, to another parent (W21 to W25 in
//! `docs/standards/sacm-gsn-mapping.md`).
//!
//! [`move_targets`] answers, for a node, where it may go and why not
//! elsewhere, which the interface shows while the user chooses;
//! [`move_node`] makes the move, as one `sacm::edit::batch`.
//!
//! A move changes one line of the diagram: the one the node is drawn under.
//! The node, its text and everything under it stay as they are. The node
//! keeps how it is joined: what supports stays support, and what is context
//! stays context.

use std::collections::{HashMap, HashSet};

use sacm::edit::{
    self, AddReference, BatchError, BatchMutation, CreateAssertedContext, CreateAssertedEvidence,
    CreateAssertedInference, DeleteElement, EditError, RemoveReference, SetAssertionDeclaration,
    SetReferences,
};
use sacm::model::{
    AssertionDeclaration, Document, ElementId, ElementKind as SacmKind, LangString, ReferenceEnd,
    SacmElement,
};

use super::{
    AddError, Refusal, Target, argument_package, changeable, deletable, held_place, placeholder,
    retargetable,
};
use crate::view::{EdgeKind, NodeKind, View, can_hold_placeholder};

/// Whether one node can take the moved one.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MoveTarget {
    /// The SACM id of the node.
    pub node: String,
    /// Why the moved node cannot go under it, or `None` when it can.
    pub refusal: Option<Refusal>,
    /// Moving there also clears this Goal's undeveloped decorator (W11).
    pub develops: bool,
}

/// Where a node may be moved.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MoveTargets {
    /// Why it cannot be moved to the top level, or `None` when it can.
    pub canvas: Option<Refusal>,
    /// Every node of the view, in its order.
    pub nodes: Vec<MoveTarget>,
    /// For a challenger, every line of the view, in its order (W32); empty
    /// for any other node, which goes under a node.
    pub lines: Vec<super::challenge::LineTarget>,
}

/// What a move made.
#[derive(Clone, Debug)]
pub struct Moved {
    /// What libSACM changed.
    pub mutation: BatchMutation,
}

/// Where the node `id` may be moved, where `view` is the view of `document`
/// as it is.
///
/// # Errors
///
/// Why the node cannot be moved at all: [`Refusal::NotANode`],
/// [`Refusal::KindUnknown`], [`Refusal::SeveralParents`],
/// [`Refusal::JoinNotMoved`], [`Refusal::NoOneInference`] or
/// [`Refusal::StrategyAlone`].
pub fn move_targets(document: &Document, view: &View, id: &str) -> Result<MoveTargets, Refusal> {
    // W32: a counter goal or counter evidence moves its challenge.
    if let Some(counter) = super::challenge::counter_of(document, view, id) {
        return counter.map(|counter| super::challenge::counter_targets(document, view, &counter));
    }
    let mover = Mover::of(document, view, id)?;
    let under = mover.under(view);
    let strategies = Strategies::of(document, view);
    // What a place is offered with is what the move would do: the steps are
    // worked out for it too, so a line W25 would not change, or a place W20
    // could not hold, is refused here and not only once the move is made.
    let answer = |target: Target<'_>| {
        mover
            .onto(document, view, &under, &strategies, target)
            .and_then(|develops| {
                mover
                    .steps(document, view, &strategies, target, develops)
                    .map(|_| develops)
            })
    };
    let nodes = view
        .nodes
        .iter()
        .map(|node| {
            let answer = answer(Target::Node(&node.id));
            MoveTarget {
                node: node.id.clone(),
                refusal: answer.err(),
                develops: answer.is_ok_and(|develops| develops),
            }
        })
        .collect();
    Ok(MoveTargets {
        canvas: answer(Target::Canvas).err(),
        nodes,
        lines: Vec::new(),
    })
}

/// Moves the node `id` to `target`, as [`move_targets`] says.
///
/// # Errors
///
/// What [`move_targets`] refuses, as [`AddError::Refused`], among them
/// [`Refusal::Entangled`] when the line that would be replaced holds or is
/// referred to by something the diagram does not draw (W25);
/// [`AddError::Edit`] when libSACM refused an operation, which is a fault in
/// these rules. The document is then unchanged.
pub fn move_node(
    document: &mut Document,
    view: &View,
    id: &str,
    target: Target<'_>,
) -> Result<Moved, AddError> {
    // W32: a counter goal or counter evidence moves its challenge, to a node.
    if let Some(counter) = super::challenge::counter_of(document, view, id) {
        // One that cannot be moved at all says why, wherever it was asked to go.
        if let Err(refusal) = counter {
            return Err(AddError::Refused(refusal));
        }
        return match target {
            Target::Canvas => Err(AddError::Refused(Refusal::ChallengeStays)),
            Target::Node(to) => {
                super::challenge::move_challenge(document, view, id, super::Challenged::Node(to))
            }
        };
    }
    let mover = Mover::of(document, view, id).map_err(AddError::Refused)?;
    let under = mover.under(view);
    let strategies = Strategies::of(document, view);
    let develops = mover
        .onto(document, view, &under, &strategies, target)
        .map_err(AddError::Refused)?;
    let steps = mover
        .steps(document, view, &strategies, target, develops)
        .map_err(AddError::Refused)?;
    let mutation = edit::batch(document, |tx| -> Result<(), BatchError> {
        for step in steps {
            match step {
                Step::Apply(operation) => {
                    tx.apply(operation)?;
                }
                Step::HoldPlace { inference, package } => {
                    let premise = placeholder(tx, &package)?;
                    tx.apply(AddReference::new(inference, ReferenceEnd::Source, premise))?;
                }
            }
        }
        Ok(())
    })
    .map_err(|error| match error.error() {
        // W25: something the plan did not see refers to the line, or it
        // holds more than its text.
        EditError::Referenced { .. } | EditError::NotEmpty { .. } => {
            AddError::Refused(Refusal::Entangled)
        }
        _ => AddError::Edit(error),
    })?;
    Ok(Moved { mutation })
}

/// One thing the batch of a move does.
enum Step {
    Apply(edit::Operation),
    /// W20: the inference of a Strategy that loses its last Goal holds the
    /// place of the next.
    HoldPlace {
        inference: ElementId,
        package: ElementId,
    },
}

/// How the moved node is joined to what it is drawn under.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Role {
    /// By `SupportedBy`, or a top-level node that would be.
    Support,
    /// By `InContextOf`, or a top-level node that would be.
    Context,
    /// By a `SupportedBy` line that GSN does not allow it: a Context, an
    /// Assumption or a Justification joined as support. The line is kept as
    /// it is and given a new end (W21): the move does not repair it.
    Misjoined,
}

/// The line the moved node is drawn under.
#[derive(Clone, Debug)]
enum Link {
    /// None: the node is at the top level.
    None,
    /// The node is a Strategy, and `inference` the one inference it is the
    /// reasoning of (E1), whose target is the Goal it is under.
    OfStrategy { inference: ElementId },
    /// The node is a source of `relationship`, whose target is its parent.
    /// `sole`: it is the only source.
    Direct { relationship: ElementId, sole: bool },
    /// The node is a source of `inference`, whose reasoning is the Strategy
    /// it is under. `sole`: it is the only source.
    UnderStrategy { inference: ElementId, sole: bool },
}

/// A node that may be moved, and how it is joined now.
struct Mover {
    id: ElementId,
    kind: NodeKind,
    role: Role,
    /// The node it is drawn under, or `None` at the top level.
    parent: Option<String>,
    link: Link,
    package: ElementId,
}

impl Mover {
    /// W21: the node, and the one line it is drawn under.
    fn of(document: &Document, view: &View, id: &str) -> Result<Mover, Refusal> {
        let node = view
            .nodes
            .iter()
            .find(|node| node.id == id)
            .ok_or(Refusal::NotANode)?;
        if node.kind == NodeKind::Neutral {
            return Err(Refusal::KindUnknown);
        }
        let element = document.get(id).ok_or(Refusal::NotANode)?;
        let package = argument_package(element).ok_or(Refusal::NoPackage)?;
        // A challenge is joined from the node to what it challenges, and a
        // neutral line is one the reading rules could not name: neither is
        // a line a move knows how to carry.
        let challenges = view
            .edges
            .iter()
            .any(|edge| edge.kind == EdgeKind::Challenge && edge.from == id);
        let mut above = view
            .edges
            .iter()
            .filter(|edge| edge.kind != EdgeKind::Challenge && edge.to == id && edge.from != id);
        let first = above.next();
        if above.next().is_some() {
            return Err(Refusal::SeveralParents);
        }
        if challenges {
            return Err(Refusal::JoinNotMoved);
        }
        let Some(edge) = first else {
            let role = match node.kind {
                NodeKind::Goal | NodeKind::Solution => Role::Support,
                NodeKind::Context | NodeKind::Assumption | NodeKind::Justification => Role::Context,
                // A Strategy at the top level is the reasoning of no
                // inference: there is none to give a target.
                NodeKind::Strategy => return Err(Refusal::StrategyAlone),
                NodeKind::Neutral => return Err(Refusal::KindUnknown),
            };
            return Ok(Mover {
                id: element.id().clone(),
                kind: node.kind,
                role,
                parent: None,
                link: Link::None,
                package,
            });
        };
        let role = match (edge.kind, node.kind) {
            (EdgeKind::SupportedBy, NodeKind::Goal | NodeKind::Strategy | NodeKind::Solution) => {
                Role::Support
            }
            (EdgeKind::InContextOf, kind) if kind != NodeKind::Strategy => Role::Context,
            (
                EdgeKind::SupportedBy,
                NodeKind::Context | NodeKind::Assumption | NodeKind::Justification,
            ) => Role::Misjoined,
            _ => return Err(Refusal::JoinNotMoved),
        };
        let relationship = document
            .get(&edge.relationship)
            .ok_or(Refusal::JoinNotMoved)?;
        let holds_once = |relationship: SacmElement<'_>| {
            relationship
                .sources()
                .iter()
                .filter(|source| source.as_str() == id)
                .count()
                == 1
        };
        let target_is_parent = relationship
            .target()
            .is_some_and(|target| target.as_str() == edge.from);
        let link = if node.kind == NodeKind::Strategy {
            let inference = super::one_inference(document, view, id)
                .filter(|inference| inference.as_str() == edge.relationship)
                .ok_or(Refusal::NoOneInference)?;
            if !target_is_parent {
                return Err(Refusal::JoinNotMoved);
            }
            Link::OfStrategy { inference }
        } else if relationship
            .reasoning()
            .is_some_and(|reasoning| reasoning.as_str() == edge.from)
        {
            if !holds_once(relationship) {
                return Err(Refusal::JoinNotMoved);
            }
            Link::UnderStrategy {
                inference: relationship.id().clone(),
                sole: relationship.sources().len() == 1,
            }
        } else if relationship.reasoning().is_none() && target_is_parent && holds_once(relationship)
        {
            Link::Direct {
                relationship: relationship.id().clone(),
                sole: relationship.sources().len() == 1,
            }
        } else {
            return Err(Refusal::JoinNotMoved);
        };
        // A line GSN does not allow is only given a new end: no new one is
        // made, so the node must be alone in a relationship of its own.
        if role == Role::Misjoined && !matches!(link, Link::Direct { sole: true, .. }) {
            return Err(Refusal::JoinNotMoved);
        }
        Ok(Mover {
            id: element.id().clone(),
            kind: node.kind,
            role,
            parent: Some(edge.from.clone()),
            link,
            package,
        })
    }

    /// The node and every node drawn under it: where it cannot go (W22).
    /// What challenges a node counts as under it, as a removal counts it.
    fn under<'a>(&'a self, view: &'a View) -> HashSet<&'a str> {
        below(view, self.id.as_str()).nodes
    }

    /// W22: whether the node may go to `target`, and whether that develops
    /// the Goal it goes under (W11).
    fn onto(
        &self,
        document: &Document,
        view: &View,
        under: &HashSet<&str>,
        strategies: &Strategies,
        target: Target<'_>,
    ) -> Result<bool, Refusal> {
        let Target::Node(to) = target else {
            if self.kind != NodeKind::Goal {
                return Err(Refusal::TopLevelGoalOnly);
            }
            if self.parent.is_none() {
                return Err(Refusal::AlreadyThere);
            }
            return Ok(false);
        };
        if under.contains(to) {
            return Err(Refusal::UnderItself);
        }
        if self.parent.as_deref() == Some(to) {
            return Err(Refusal::AlreadyThere);
        }
        let node = view
            .nodes
            .iter()
            .find(|node| node.id == to)
            .ok_or(Refusal::KindUnknown)?;
        let develops = match node.kind {
            NodeKind::Neutral => return Err(Refusal::KindUnknown),
            NodeKind::Solution
            | NodeKind::Context
            | NodeKind::Assumption
            | NodeKind::Justification => return Err(Refusal::Leaf),
            // The line a misjoined element keeps is a support line: under
            // an undeveloped Goal it would support it, and the move does not
            // develop a Goal by a line GSN does not allow (W21).
            NodeKind::Goal if self.role == Role::Misjoined && node.undeveloped => {
                return Err(Refusal::WouldSupportUndeveloped);
            }
            NodeKind::Goal => self.role == Role::Support && node.undeveloped,
            NodeKind::Strategy => {
                // The line kept is a support line, which a Strategy takes
                // only from a Goal.
                if self.role == Role::Misjoined {
                    return Err(Refusal::NotUnderStrategy);
                }
                if self.role == Role::Support {
                    // GSN: a Strategy is supported only by Goals.
                    if self.kind != NodeKind::Goal {
                        return Err(Refusal::NotUnderStrategy);
                    }
                    if !strategies.one.contains_key(to) {
                        return Err(Refusal::NoOneInference);
                    }
                }
                false
            }
        };
        let element = document.get(to).ok_or(Refusal::KindUnknown)?;
        if argument_package(element).as_ref() != Some(&self.package) {
            return Err(Refusal::OtherPackage);
        }
        Ok(develops)
    }

    /// W23, W24: the operations of the move, in order.
    fn steps(
        &self,
        document: &Document,
        view: &View,
        strategies: &Strategies,
        target: Target<'_>,
        develops: bool,
    ) -> Result<Vec<Step>, Refusal> {
        let to = match target {
            Target::Canvas => None,
            Target::Node(to) => Some(document.get(to).ok_or(Refusal::KindUnknown)?.id().clone()),
        };
        let to_strategy = to.as_ref().and_then(|to| strategies.one.get(to.as_str()));
        let to_strategy = match (self.role, self.kind) {
            (Role::Support, NodeKind::Goal) => to_strategy,
            _ => None,
        };
        let mut steps: Vec<Step> = Vec::new();

        // W23: the line the node has is given its new end where it can be;
        // otherwise the node is taken out of it.
        let mut joined = false;
        match &self.link {
            Link::None => {}
            Link::OfStrategy { inference } => {
                let to = to.clone().ok_or(Refusal::TopLevelGoalOnly)?;
                retargetable(document, inference)?;
                steps.push(Step::Apply(
                    SetReferences::new(inference.clone(), ReferenceEnd::Target, [to]).into(),
                ));
                joined = true;
            }
            Link::Direct { relationship, sole } => match (&to, to_strategy, sole) {
                (Some(to), None, true) => {
                    retargetable(document, relationship)?;
                    steps.push(Step::Apply(
                        SetReferences::new(
                            relationship.clone(),
                            ReferenceEnd::Target,
                            [to.clone()],
                        )
                        .into(),
                    ));
                    joined = true;
                }
                (_, _, true) => {
                    deletable(document, relationship)?;
                    steps.push(Step::Apply(DeleteElement::new(relationship.clone()).into()));
                }
                (_, _, false) => {
                    changeable(document, relationship)?;
                    steps.push(Step::Apply(
                        RemoveReference::new(
                            relationship.clone(),
                            ReferenceEnd::Source,
                            self.id.clone(),
                        )
                        .into(),
                    ));
                }
            },
            Link::UnderStrategy { inference, sole } => {
                changeable(document, inference)?;
                if *sole {
                    // W20: the Strategy keeps its inference.
                    let held = document
                        .get(inference.as_str())
                        .filter(|element| can_hold_placeholder(document, *element))
                        .ok_or(Refusal::JoinNotMoved)?;
                    let package = argument_package(held).ok_or(Refusal::NoPackage)?;
                    steps.push(Step::HoldPlace {
                        inference: inference.clone(),
                        package,
                    });
                }
                steps.push(Step::Apply(
                    RemoveReference::new(inference.clone(), ReferenceEnd::Source, self.id.clone())
                        .into(),
                ));
            }
        }

        // W24: a new line, as an add would make it (W1, W3 to W7).
        if let (false, Some(to)) = (joined, &to) {
            let name = LangString::default;
            let package = self.package.clone();
            let source = [self.id.clone()];
            if let Some(inference) = to_strategy {
                changeable(document, inference)?;
                steps.push(Step::Apply(
                    AddReference::new(inference.clone(), ReferenceEnd::Source, self.id.clone())
                        .into(),
                ));
                // W3: the place the Strategy held is taken.
                if let Some((claim, _tag)) = held_place(document, view, inference) {
                    steps.push(Step::Apply(
                        RemoveReference::new(
                            inference.clone(),
                            ReferenceEnd::Source,
                            claim.clone(),
                        )
                        .into(),
                    ));
                    steps.push(Step::Apply(DeleteElement::new(claim).into()));
                }
            } else {
                let operation: edit::Operation = match (self.role, self.kind) {
                    // Never reached: its line is always kept (W23).
                    (Role::Misjoined, _) => return Err(Refusal::JoinNotMoved),
                    (Role::Context, _) => {
                        CreateAssertedContext::new(package, name(), source, to.clone()).into()
                    }
                    (Role::Support, NodeKind::Solution) => {
                        CreateAssertedEvidence::new(package, name(), source, to.clone()).into()
                    }
                    (Role::Support, _) => {
                        CreateAssertedInference::new(package, name(), source, to.clone()).into()
                    }
                };
                steps.push(Step::Apply(operation));
            }
        }

        // W11.
        if let (true, Some(to)) = (develops, to) {
            steps.push(Step::Apply(
                SetAssertionDeclaration::new(to, AssertionDeclaration::Asserted).into(),
            ));
        }
        Ok(steps)
    }
}

/// The Strategies a Goal can be added under: each with the one live
/// inference it is the reasoning of (W3), found in one pass.
struct Strategies {
    one: HashMap<String, ElementId>,
}

impl Strategies {
    fn of(document: &Document, view: &View) -> Self {
        let strategies: HashSet<&str> = view
            .nodes
            .iter()
            .filter(|node| node.kind == NodeKind::Strategy)
            .map(|node| node.id.as_str())
            .collect();
        let mut found: HashMap<&str, Vec<&str>> = HashMap::new();
        for edge in &view.edges {
            if edge.kind != EdgeKind::SupportedBy
                || edge.to_relationship
                || !strategies.contains(edge.to.as_str())
            {
                continue;
            }
            let is_its_inference = document.get(&edge.relationship).is_some_and(|element| {
                element.kind() == SacmKind::AssertedInference
                    && element
                        .reasoning()
                        .is_some_and(|reasoning| reasoning.as_str() == edge.to)
            });
            let list = found.entry(edge.to.as_str()).or_default();
            if is_its_inference && !list.contains(&edge.relationship.as_str()) {
                list.push(edge.relationship.as_str());
            }
        }
        let one = found
            .into_iter()
            .filter(|(_, inferences)| inferences.len() == 1)
            .filter_map(|(strategy, inferences)| {
                let inference = document.get(inferences[0])?.id().clone();
                Some((strategy.to_owned(), inference))
            })
            .collect();
        Self { one }
    }
}

/// What is under a node: the nodes, and the node each relationship's line
/// hangs from.
pub(crate) struct Below<'a> {
    pub(crate) nodes: HashSet<&'a str>,
    pub(crate) hangs: HashMap<&'a str, Vec<&'a str>>,
}

/// The node `id` and every node drawn under it, and the node each
/// relationship's line hangs from (W22). What challenges a node, or a line
/// under it, counts as under it, as a removal counts it.
pub(crate) fn below<'a>(view: &'a View, id: &'a str) -> Below<'a> {
    let mut below: HashMap<&str, Vec<&str>> = HashMap::new();
    // The node each relationship's line hangs from: a line is drawn
    // under its conclusion, a challenge under what it challenges.
    let mut hangs: HashMap<&str, Vec<&str>> = HashMap::new();
    for edge in &view.edges {
        if edge.to_relationship {
            continue;
        }
        let (parent, child) = if edge.kind == EdgeKind::Challenge {
            (edge.to.as_str(), edge.from.as_str())
        } else {
            (edge.from.as_str(), edge.to.as_str())
        };
        below.entry(parent).or_default().push(child);
        hangs
            .entry(edge.relationship.as_str())
            .or_default()
            .push(parent);
    }
    // A challenge of a line is under the node the line hangs from, as a
    // removal counts it (W18): a node is not moved under its own
    // challenger's line. A challenge of such a challenge, in turn.
    let mut grew = true;
    while grew {
        grew = false;
        for edge in view.edges.iter().filter(|edge| edge.to_relationship) {
            let parents = hangs.get(edge.to.as_str()).cloned().unwrap_or_default();
            for parent in parents {
                let children = below.entry(parent).or_default();
                if !children.contains(&edge.from.as_str()) {
                    children.push(edge.from.as_str());
                    grew = true;
                }
                let held = hangs.entry(edge.relationship.as_str()).or_default();
                if !held.contains(&parent) {
                    held.push(parent);
                    grew = true;
                }
            }
        }
    }
    let mut under: HashSet<&str> = HashSet::from([id]);
    let mut queue: Vec<&str> = vec![id];
    while let Some(node) = queue.pop() {
        for child in below.get(node).into_iter().flatten() {
            if under.insert(child) {
                queue.push(child);
            }
        }
    }
    Below {
        nodes: under,
        hangs,
    }
}
