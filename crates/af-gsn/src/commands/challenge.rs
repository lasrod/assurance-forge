//! Adding a challenge: a counter-Goal or counter-evidence, from a node's or a
//! line's menu (W29 to W31 in `docs/standards/sacm-gsn-mapping.md`).
//!
//! A challenge is one `sacm::edit::batch`: the new challenger, made as an add
//! makes a node (W9, W10), and a relationship with `isCounter` from it to what
//! is challenged, which E5 reads back as a challenge.

use sacm::edit::{
    self, BatchError, CreateAssertedEvidence, CreateAssertedInference, EditError, SetReferences,
};
use std::collections::HashMap;

use sacm::model::{Document, ElementId, LangString, ReferenceEnd};

use super::move_node::{MoveTarget, MoveTargets, Moved, below};
use super::{
    AddError, Added, Choice, Identifiers, Refusal, argument_package, new_node, one_inference,
    retargetable,
};
use crate::ElementKind;
use crate::view::{Edge, EdgeKind, NodeKind, View};

/// What a challenge is added to.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Challenged<'a> {
    /// The node with this SACM id.
    Node(&'a str),
    /// One line of the relationship with this SACM id: the one from the node
    /// `from` to `to`, as the view draws it.
    Line {
        relationship: &'a str,
        from: &'a str,
        to: &'a str,
    },
}

/// The kinds that challenge, in menu order (W30): a counter-Goal and
/// counter-evidence.
pub const CHALLENGERS: [ElementKind; 2] = [ElementKind::Goal, ElementKind::Solution];

/// W29, W30: whether `kind` may challenge `at`, for the menu: the two kinds
/// of [`CHALLENGERS`], each with why it cannot, or `None` when it can.
pub fn challenge_choices(document: &Document, view: &View, at: Challenged<'_>) -> Vec<Choice> {
    let lines = Lines::of(view);
    CHALLENGERS
        .iter()
        .map(|&kind| Choice {
            kind,
            refusal: plan(document, view, &lines, at, kind).err(),
            develops: false,
        })
        .collect()
}

/// W29 to W31: challenges `at` with a new element of `kind`, where `view`
/// is the view of `document` as it is.
///
/// # Errors
///
/// What [`challenge_choices`] refuses, as [`AddError::Refused`]; the
/// document is then unchanged.
pub fn challenge(
    document: &mut Document,
    view: &View,
    at: Challenged<'_>,
    kind: ElementKind,
) -> Result<Added, AddError> {
    let plan = plan(document, view, &Lines::of(view), at, kind).map_err(AddError::Refused)?;
    let mut numbers = Identifiers::of(document, view);
    let mut made: Option<String> = None;
    let mutation = edit::batch(document, |tx| -> Result<(), BatchError> {
        let challenger = new_node(tx, &plan.package, kind, numbers.next_prefixed(prefix(kind)))?;
        // W31: the counter relationship, from the challenger to what it
        // challenges.
        if kind == ElementKind::Solution {
            tx.apply(
                CreateAssertedEvidence::new(
                    plan.package.clone(),
                    LangString::default(),
                    [challenger.clone()],
                    plan.target.clone(),
                )
                .with_is_counter(true),
            )?;
        } else {
            tx.apply(
                CreateAssertedInference::new(
                    plan.package.clone(),
                    LangString::default(),
                    [challenger.clone()],
                    plan.target.clone(),
                )
                .with_is_counter(true),
            )?;
        }
        made = Some(challenger.as_str().to_owned());
        Ok(())
    })
    .map_err(AddError::Edit)?;
    Ok(Added {
        element: made.unwrap_or_default(),
        mutation,
    })
}

/// W31: the identifier prefix of a challenger: `CG` for a counter goal and
/// `CSn` for counter evidence, as GSN v3's dialectic figures number them
/// (Figures 1:6-1 to 1:6-5).
pub fn prefix(kind: ElementKind) -> &'static str {
    if kind == ElementKind::Solution {
        "CSn"
    } else {
        "CG"
    }
}

/// A challenge that may be made.
struct Plan {
    /// What the counter relationship points at.
    target: ElementId,
    /// The package the new elements go into: that of what is challenged (W9).
    package: ElementId,
}

/// The lines of a view by their ends, and the first line of each
/// relationship: made once, for the many places a move asks about.
pub(crate) struct Lines<'a> {
    by_ends: HashMap<(&'a str, &'a str, &'a str), &'a Edge>,
    first: HashMap<&'a str, &'a Edge>,
}

impl<'a> Lines<'a> {
    pub(crate) fn of(view: &'a View) -> Self {
        let mut lines = Lines {
            by_ends: HashMap::with_capacity(view.edges.len()),
            first: HashMap::new(),
        };
        for edge in &view.edges {
            let relationship = edge.relationship.as_str();
            lines
                .by_ends
                .entry((relationship, edge.from.as_str(), edge.to.as_str()))
                .or_insert(edge);
            lines.first.entry(relationship).or_insert(edge);
        }
        lines
    }
}

/// W29: what is challenged, and W30: what challenges.
fn plan(
    document: &Document,
    view: &View,
    lines: &Lines<'_>,
    at: Challenged<'_>,
    kind: ElementKind,
) -> Result<Plan, Refusal> {
    let target = match at {
        Challenged::Node(id) => {
            let node = view
                .nodes
                .iter()
                .find(|node| node.id == id)
                .ok_or(Refusal::NotANode)?;
            match node.kind {
                NodeKind::Neutral => return Err(Refusal::KindUnknown),
                // A Strategy is challenged through the inference it is the
                // reasoning of, as E5 draws a challenge of that inference at
                // the Strategy (GSN v3 §1:6.3.12).
                NodeKind::Strategy => {
                    one_inference(document, view, id).ok_or(Refusal::NoOneInference)?
                }
                NodeKind::Goal
                | NodeKind::Assumption
                | NodeKind::Justification
                | NodeKind::Solution
                | NodeKind::Context => document.get(id).ok_or(Refusal::NotANode)?.id().clone(),
            }
        }
        Challenged::Line {
            relationship,
            from,
            to,
        } => {
            let first = *lines.first.get(relationship).ok_or(Refusal::NotANode)?;
            let line = *lines
                .by_ends
                .get(&(relationship, from, to))
                .ok_or(Refusal::NotANode)?;
            // A line drawn neutral is of no GSN kind to challenge.
            if line.kind == EdgeKind::Neutral {
                return Err(Refusal::KindUnknown);
            }
            // The relationship is challenged, and E5 draws that at its first
            // line: another of its lines, such as a premise's under a
            // Strategy, has no relationship of its own to challenge.
            if !std::ptr::eq(line, first) {
                return Err(Refusal::NotTheFirstLine);
            }
            document
                .get(relationship)
                .ok_or(Refusal::NotANode)?
                .id()
                .clone()
        }
    };
    if !CHALLENGERS.contains(&kind) {
        return Err(Refusal::NotAChallenger);
    }
    let held = document.get(target.as_str()).ok_or(Refusal::NotANode)?;
    let package = argument_package(held).ok_or(Refusal::NoPackage)?;
    Ok(Plan { target, package })
}

/// Whether a line can take a moved challenge (W32).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct LineTarget {
    /// The SACM id of the relationship the line is drawn for.
    pub relationship: String,
    /// The nodes the line runs from and to.
    pub from: String,
    pub to: String,
    /// Why the challenge cannot go there, or `None` when it can.
    pub refusal: Option<Refusal>,
}

/// W32: the challenge a node makes, which a move gives a new target.
pub(crate) struct Counter {
    id: ElementId,
    /// The counter relationship, whose `target` the move sets.
    relationship: ElementId,
    /// What it challenges now.
    target: ElementId,
    package: ElementId,
    kind: ElementKind,
}

/// W32: the challenge the node `id` makes, when it makes one: `None` when
/// it challenges nothing; an error when a move cannot carry it. It is
/// carried when the node is a counter goal or counter evidence, the only
/// source of one counter relationship, drawn under nothing but what it
/// challenges, and the relationship may get a new target (W25).
pub(crate) fn counter_of(
    document: &Document,
    view: &View,
    id: &str,
) -> Option<Result<Counter, Refusal>> {
    let made: Vec<&str> = view
        .edges
        .iter()
        .filter(|edge| edge.kind == EdgeKind::Challenge && edge.from == id)
        .map(|edge| edge.relationship.as_str())
        .collect();
    let first = *made.first()?;
    Some((|| {
        let node = view
            .nodes
            .iter()
            .find(|node| node.id == id)
            .ok_or(Refusal::NotANode)?;
        let kind = match node.kind {
            NodeKind::Neutral => return Err(Refusal::KindUnknown),
            NodeKind::Goal => ElementKind::Goal,
            NodeKind::Solution => ElementKind::Solution,
            // A file's own challenge from another kind: it challenges, and is
            // joined in a way a move does not carry.
            _ => return Err(Refusal::JoinNotMoved),
        };
        // Nothing else draws the node under anything: under several, as
        // any node, there is no one line to move (W21); under one, it is
        // joined in a way a move does not carry. And one challenge.
        let parents = view
            .edges
            .iter()
            .filter(|edge| edge.kind != EdgeKind::Challenge && edge.to == id)
            .count();
        if parents > 1 {
            return Err(Refusal::SeveralParents);
        }
        if made.iter().any(|relationship| *relationship != first) || parents == 1 {
            return Err(Refusal::JoinNotMoved);
        }
        let relationship = document.get(first).ok_or(Refusal::NotANode)?;
        if relationship.sources().len() != 1 {
            return Err(Refusal::JoinNotMoved);
        }
        retargetable(document, relationship.id())?;
        Ok(Counter {
            id: document.get(id).ok_or(Refusal::NotANode)?.id().clone(),
            relationship: relationship.id().clone(),
            target: relationship.target().ok_or(Refusal::JoinNotMoved)?.clone(),
            package: argument_package(relationship).ok_or(Refusal::NoPackage)?,
            kind,
        })
    })())
}

/// W32: where the challenge of `counter` may be moved: every node and every
/// line, each with why not. It stays a challenge, so not the top level
/// (AF-EDIT-028).
pub(crate) fn counter_targets(document: &Document, view: &View, counter: &Counter) -> MoveTargets {
    let under = below(view, counter.id.as_str());
    let lines = Lines::of(view);
    let nodes = view
        .nodes
        .iter()
        .map(|node| MoveTarget {
            node: node.id.clone(),
            refusal: place_refusal(
                document,
                view,
                &lines,
                counter,
                &under,
                Challenged::Node(&node.id),
            ),
            develops: false,
        })
        .collect();
    let places = view
        .edges
        .iter()
        .map(|edge| LineTarget {
            relationship: edge.relationship.clone(),
            from: edge.from.clone(),
            to: edge.to.clone(),
            refusal: place_refusal(
                document,
                view,
                &lines,
                counter,
                &under,
                Challenged::Line {
                    relationship: &edge.relationship,
                    from: &edge.from,
                    to: &edge.to,
                },
            ),
        })
        .collect();
    MoveTargets {
        canvas: Some(Refusal::ChallengeStays),
        nodes,
        lines: places,
    }
}

/// W32: why the challenge of `counter` cannot go to `at`, or `None`.
fn place_refusal(
    document: &Document,
    view: &View,
    lines: &Lines<'_>,
    counter: &Counter,
    under: &super::move_node::Below<'_>,
    at: Challenged<'_>,
) -> Option<Refusal> {
    let plan = match plan(document, view, lines, at, counter.kind) {
        Ok(plan) => plan,
        Err(refusal) => return Some(refusal),
    };
    // Not onto itself or what is under it, nor its own line or a line under it.
    let beneath = match at {
        Challenged::Node(id) => under.nodes.contains(id),
        Challenged::Line { relationship, .. } => {
            relationship == counter.relationship.as_str()
                || under.hangs.get(relationship).is_some_and(|parents| {
                    parents.iter().any(|parent| under.nodes.contains(parent))
                })
        }
    };
    if beneath {
        return Some(Refusal::UnderItself);
    }
    if plan.target == counter.target {
        return Some(Refusal::AlreadyThere);
    }
    if plan.package != counter.package {
        return Some(Refusal::OtherPackage);
    }
    None
}

/// W32: moves the challenge the node `id` makes to `to`: its counter
/// relationship gets `to` as its `target`, as one batch. The node, its text
/// and what is under it stay as they are.
///
/// # Errors
///
/// What [`super::move_targets`] refuses for it, as [`AddError::Refused`]; the
/// document is then unchanged.
pub fn move_challenge(
    document: &mut Document,
    view: &View,
    id: &str,
    to: Challenged<'_>,
) -> Result<Moved, AddError> {
    let counter = counter_of(document, view, id)
        .ok_or(AddError::Refused(Refusal::NotAChallenger))?
        .map_err(AddError::Refused)?;
    let under = below(view, counter.id.as_str());
    let lines = Lines::of(view);
    if let Some(refusal) = place_refusal(document, view, &lines, &counter, &under, to) {
        return Err(AddError::Refused(refusal));
    }
    let target = plan(document, view, &lines, to, counter.kind)
        .map_err(AddError::Refused)?
        .target;
    let mutation = edit::batch(document, |tx| -> Result<(), BatchError> {
        tx.apply(SetReferences::new(
            counter.relationship.clone(),
            ReferenceEnd::Target,
            [target],
        ))?;
        Ok(())
    })
    .map_err(|error| match error.error() {
        EditError::Referenced { .. } | EditError::NotEmpty { .. } => {
            AddError::Refused(Refusal::Entangled)
        }
        _ => AddError::Edit(error),
    })?;
    Ok(Moved { mutation })
}
