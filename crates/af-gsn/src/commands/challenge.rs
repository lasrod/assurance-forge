//! Adding a challenge: a counter-Goal or counter-evidence, from a node's or a
//! line's menu (W29 to W31 in `docs/standards/sacm-gsn-mapping.md`).
//!
//! A challenge is one `sacm::edit::batch`: the new challenger, made as an add
//! makes a node (W9, W10), and a relationship with `isCounter` from it to what
//! is challenged, which E5 reads back as a challenge.

use sacm::edit::{self, BatchError, CreateAssertedEvidence, CreateAssertedInference};
use sacm::model::{Document, ElementId, LangString};

use super::{
    AddError, Added, Choice, Identifiers, Refusal, argument_package, new_node, one_inference,
};
use crate::ElementKind;
use crate::view::{EdgeKind, NodeKind, View};

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
    CHALLENGERS
        .iter()
        .map(|&kind| Choice {
            kind,
            refusal: plan(document, view, at, kind).err(),
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
    let plan = plan(document, view, at, kind).map_err(AddError::Refused)?;
    let mut numbers = Identifiers::of(document, view);
    let mut made: Option<String> = None;
    let mutation = edit::batch(document, |tx| -> Result<(), BatchError> {
        let challenger = new_node(tx, &plan.package, kind, &mut numbers)?;
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

/// A challenge that may be made.
struct Plan {
    /// What the counter relationship points at.
    target: ElementId,
    /// The package the new elements go into: that of what is challenged (W9).
    package: ElementId,
}

/// W29: what is challenged, and W30: what challenges.
fn plan(
    document: &Document,
    view: &View,
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
            let mut drawn = view
                .edges
                .iter()
                .filter(|edge| edge.relationship == relationship);
            let first = drawn.next().ok_or(Refusal::NotANode)?;
            let line = std::iter::once(first)
                .chain(drawn)
                .find(|edge| edge.from == from && edge.to == to)
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
