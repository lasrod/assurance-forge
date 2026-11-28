//! Adding an assurance claim point (ACP) to a line, as a description or as a
//! confidence argument in a new argument package (W33 and W34 in
//! `docs/standards/sacm-gsn-mapping.md`; A1 to A4 of
//! `docs/roadmap/milestone-2-acps.md`).
//!
//! An ACP is one `sacm::edit::batch`: the confidence `Claim`, numbered `ACP1`,
//! `ACP2`…, made as an add makes a Goal (W9, W10), in the line's argument
//! package or in a new one beside it; and the relationship's `metaClaim`
//! gains it, which E11 reads back as the ACP.
//!
//! An ACP may also name a goal that exists: the top goal of another argument
//! package, such as a confidence argument kept when its ACP was removed
//! (W36; A8). Then only the relationship's `metaClaim` changes.

use std::collections::HashSet;

use sacm::edit::{
    self, BatchError, BatchMutation, CreateArgumentPackage, CreateTaggedValue, SetReferences,
};
use sacm::model::{Document, ElementId, ElementKind as SacmKind, LangString, ReferenceEnd};

use super::{AddError, Added, Identifiers, Line, Refusal, argument_package, new_node};
use crate::ElementKind;
use crate::view::{ACP_DESCRIPTION, ACP_KEY, EdgeKind, NodeKind, View, acp_label};

/// The prefix of a confidence Claim's identifier (A4).
pub const ACP_PREFIX: &str = "ACP";

/// What an ACP's argument is (A3).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AcpKind {
    /// A `Claim` in the line's package holding the text, drawn only as the
    /// ACP (R17): GSN v3 §1:5.2.3's "paragraph of accompanying text".
    Description,
    /// A new argument package whose top goal is the `Claim`: §1:5.2.3's "goal
    /// in a separate module".
    Argument,
}

/// The two kinds, in the menu's order.
pub const ACP_KINDS: [AcpKind; 2] = [AcpKind::Description, AcpKind::Argument];

/// What the line's menu offers: the identifier the next ACP takes, each
/// kind with why it cannot be added, or `None`, and the goals that exist an
/// ACP of it may name.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct AcpChoices {
    pub identifier: String,
    pub choices: Vec<(AcpKind, Option<Refusal>)>,
    pub goals: Vec<AcpGoal>,
}

/// A goal that exists, which an ACP of the line may name (W36): the top
/// goal of another argument package.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct AcpGoal {
    /// The SACM id of its `Claim`.
    pub claim: String,
    /// The label the ACP would have, as E11 draws it: `ACP1[Confidence ACP1]`.
    pub label: String,
    /// Why the line's ACP cannot name it, or `None`.
    pub refusal: Option<Refusal>,
}

/// W33 and W36: the ACPs the line may take. Either both new kinds may, or
/// neither, for the same reason, which every goal then also carries.
pub fn acp_choices(document: &Document, view: &View, line: Line<'_>) -> AcpChoices {
    let refusal = plan(document, view, line).err();
    AcpChoices {
        identifier: Identifiers::of(document, view).next_prefixed(ACP_PREFIX),
        choices: ACP_KINDS.iter().map(|kind| (*kind, refusal)).collect(),
        goals: acp_goals(document, view, line, refusal),
    }
}

/// W36: the top goals of the argument packages other than the line's, in
/// document order, with why the line's ACP cannot name one: what refuses
/// the line (`refusal`), the goal already being an ACP of it
/// (AF-EDIT-033), or the line being part of the goal's own argument
/// (AF-EDIT-034), which would make that argument its own confidence
/// argument.
///
/// A top goal is a Goal the view draws that no `SupportedBy`, `InContextOf`
/// or neutral line reaches, that challenges nothing, and that is not a
/// citation. `view` is the whole case.
fn acp_goals(
    document: &Document,
    view: &View,
    line: Line<'_>,
    refusal: Option<Refusal>,
) -> Vec<AcpGoal> {
    let Some(relationship) = document.get(line.relationship) else {
        return Vec::new();
    };
    let home = argument_package(relationship);
    let named: HashSet<&str> = relationship
        .meta_claims()
        .iter()
        .map(ElementId::as_str)
        .collect();
    // Reached by a line: not a top goal. A challenger is a counter argument.
    let reached: HashSet<&str> = view
        .edges
        .iter()
        .filter(|edge| edge.kind != EdgeKind::Challenge)
        .map(|edge| edge.to.as_str())
        .collect();
    let challengers: HashSet<&str> = view
        .edges
        .iter()
        .filter(|edge| edge.kind == EdgeKind::Challenge)
        .map(|edge| edge.from.as_str())
        .collect();
    let above = above(view, line.from);
    view.nodes
        .iter()
        .filter(|node| {
            node.kind == NodeKind::Goal
                && !reached.contains(node.id.as_str())
                && !challengers.contains(node.id.as_str())
        })
        .filter_map(|node| {
            let claim = document
                .get(node.id.as_str())
                .filter(|claim| claim.kind() == SacmKind::Claim && !claim.is_citation())?;
            let package = argument_package(claim)?;
            if home.as_ref() == Some(&package) {
                return None;
            }
            let refusal = refusal.or_else(|| {
                if named.contains(node.id.as_str()) {
                    Some(Refusal::AlreadyAnAcp)
                } else if above.contains(node.id.as_str()) {
                    Some(Refusal::AcpOfItself)
                } else {
                    None
                }
            });
            Some(AcpGoal {
                claim: node.id.clone(),
                label: acp_label(document, line.relationship, node.id.as_str())?,
                refusal,
            })
        })
        .collect()
}

/// The node `id` and every node above it by `SupportedBy`, `InContextOf` or
/// neutral lines: the argument it is part of.
fn above<'v>(view: &'v View, id: &'v str) -> HashSet<&'v str> {
    let mut found: HashSet<&str> = HashSet::from([id]);
    let mut todo = vec![id];
    while let Some(at) = todo.pop() {
        for edge in &view.edges {
            if edge.kind != EdgeKind::Challenge && edge.to == at && found.insert(edge.from.as_str())
            {
                todo.push(edge.from.as_str());
            }
        }
    }
    found
}

/// W33: why a node cannot take an ACP. A Solution or a Context can in GSN v3
/// (Table 1:5-2), but not in SACM 2.3 (gap row 7): it comes with SACM 2.4
/// (AF-EDIT-030). Any other kind has none in GSN v3 (AF-EDIT-029).
pub fn acp_node_refusal(view: &View, id: &str) -> Refusal {
    match view
        .nodes
        .iter()
        .find(|node| node.id == id)
        .map(|node| node.kind)
    {
        Some(NodeKind::Solution | NodeKind::Context) => Refusal::AcpNeedsSacm24,
        Some(_) => Refusal::NotAnAcpPlace,
        None => Refusal::NotANode,
    }
}

/// Where an ACP of `line` goes: the relationship, and its argument package.
struct Plan {
    relationship: ElementId,
    package: ElementId,
}

/// W33: a `SupportedBy` or `InContextOf` line (GSN v3 Table 1:5-1), the
/// first of its relationship (§1:5.3.3), in an argument package.
fn plan(document: &Document, view: &View, line: Line<'_>) -> Result<Plan, Refusal> {
    let mut drawn = view
        .edges
        .iter()
        .filter(|edge| edge.relationship == line.relationship);
    let first = drawn.next().ok_or(Refusal::NotANode)?;
    let edge = std::iter::once(first)
        .chain(drawn)
        .find(|edge| edge.from == line.from && edge.to == line.to)
        .ok_or(Refusal::NotANode)?;
    match edge.kind {
        EdgeKind::SupportedBy | EdgeKind::InContextOf => {}
        // GSN puts none on a challenge, nor on a line drawn as none of its
        // relationships (a cited or defeated one).
        EdgeKind::Neutral | EdgeKind::Challenge => return Err(Refusal::NotAnAcpPlace),
    }
    if !std::ptr::eq(edge, first) {
        return Err(Refusal::NotTheFirstLine);
    }
    let relationship = document.get(line.relationship).ok_or(Refusal::NotANode)?;
    Ok(Plan {
        relationship: relationship.id().clone(),
        package: argument_package(relationship).ok_or(Refusal::NoPackage)?,
    })
}

/// W34: adds an ACP of `kind` to `line`, as one batch. The confidence
/// `Claim` is the element answered. A confidence argument's package is named
/// `package_name`, which the interface words; a description makes none.
///
/// # Errors
///
/// What [`acp_choices`] refuses, as [`AddError::Refused`]; [`AddError::Edit`]
/// when libSACM refused an operation, which is a fault in these rules. The
/// document is then unchanged.
pub fn add_acp(
    document: &mut Document,
    view: &View,
    line: Line<'_>,
    kind: AcpKind,
    package_name: &str,
) -> Result<Added, AddError> {
    let plan = plan(document, view, line).map_err(AddError::Refused)?;
    let identifier = Identifiers::of(document, view).next_prefixed(ACP_PREFIX);
    let named: Vec<ElementId> = document
        .get(plan.relationship.as_str())
        .map(|relationship| relationship.meta_claims().to_vec())
        .unwrap_or_default();
    // A confidence argument's package goes beside the line's, under what
    // holds it, or at the top of the file.
    let holder = document
        .get(plan.package.as_str())
        .and_then(|package| package.parent())
        .map(|parent| parent.id().clone());
    let mut made: Option<String> = None;
    let mutation = edit::batch(document, |tx| -> Result<(), BatchError> {
        let package = match kind {
            AcpKind::Description => plan.package.clone(),
            AcpKind::Argument => {
                let name = LangString::new(package_name);
                let created = tx.apply(match holder.clone() {
                    Some(parent) => CreateArgumentPackage::new(parent, name),
                    None => CreateArgumentPackage::root(name),
                })?;
                created.created_ids()[0].clone()
            }
        };
        let claim = new_node(tx, &package, ElementKind::Goal, identifier.clone())?;
        // R17: a description, marked so, which the view shows as the ACP.
        if kind == AcpKind::Description {
            tx.apply(CreateTaggedValue::new(
                claim.clone(),
                LangString::new(ACP_KEY),
                LangString::new(ACP_DESCRIPTION),
            ))?;
        }
        let mut claims = named.clone();
        claims.push(claim.clone());
        tx.apply(SetReferences::new(
            plan.relationship.clone(),
            ReferenceEnd::MetaClaim,
            claims,
        ))?;
        made = Some(claim.as_str().to_owned());
        Ok(())
    })
    .map_err(AddError::Edit)?;
    let element = made.ok_or(AddError::Refused(Refusal::NotANode))?;
    debug_assert!(
        document
            .get(element.as_str())
            .is_some_and(|claim| claim.kind() == SacmKind::Claim)
    );
    Ok(Added { element, mutation })
}

/// W36: names the goal `claim`, the top goal of another argument package, as
/// an ACP of `line`: the relationship's `metaClaim` gains it, after any it
/// names. Nothing else changes.
///
/// # Errors
///
/// What [`acp_choices`] refuses for the goal, and AF-EDIT-035 for a `claim`
/// it does not list, as [`AddError::Refused`]; [`AddError::Edit`] when
/// libSACM refused the operation, which is a fault in these rules. The
/// document is then unchanged.
pub fn link_acp(
    document: &mut Document,
    view: &View,
    line: Line<'_>,
    claim: &str,
) -> Result<BatchMutation, AddError> {
    let plan = plan(document, view, line).map_err(AddError::Refused)?;
    let goal = acp_goals(document, view, line, None)
        .into_iter()
        .find(|goal| goal.claim == claim)
        .ok_or(AddError::Refused(Refusal::NotATopGoalElsewhere))?;
    if let Some(refusal) = goal.refusal {
        return Err(AddError::Refused(refusal));
    }
    let mut claims: Vec<ElementId> = document
        .get(plan.relationship.as_str())
        .map(|relationship| relationship.meta_claims().to_vec())
        .unwrap_or_default();
    let named = document
        .get(claim)
        .map(|element| element.id().clone())
        .ok_or(AddError::Refused(Refusal::NotATopGoalElsewhere))?;
    claims.push(named);
    edit::batch(document, |tx| -> Result<(), BatchError> {
        tx.apply(SetReferences::new(
            plan.relationship.clone(),
            ReferenceEnd::MetaClaim,
            claims,
        ))?;
        Ok(())
    })
    .map_err(AddError::Edit)
}
