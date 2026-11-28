//! Adding an assurance claim point (ACP) to a line, as a description or as a
//! confidence argument in a new argument package (W33 and W34 in
//! `docs/standards/sacm-gsn-mapping.md`; A1 to A4 of
//! `docs/roadmap/milestone-2-acps.md`).
//!
//! An ACP is one `sacm::edit::batch`: the confidence `Claim`, numbered `ACP1`,
//! `ACP2`…, made as an add makes a Goal (W9, W10), in the line's argument
//! package or in a new one beside it; and the relationship's `metaClaim`
//! gains it, which E11 reads back as the ACP.

use sacm::edit::{self, BatchError, CreateArgumentPackage, CreateTaggedValue, SetReferences};
use sacm::model::{Document, ElementId, ElementKind as SacmKind, LangString, ReferenceEnd};

use super::{AddError, Added, Identifiers, Refusal, argument_package, new_node};
use crate::ElementKind;
use crate::view::{ACP_DESCRIPTION, ACP_KEY, EdgeKind, NodeKind, View};

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

/// The line an ACP goes on: one line of a relationship, from `from` to `to`
/// as the view draws it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct AcpLine<'a> {
    pub relationship: &'a str,
    pub from: &'a str,
    pub to: &'a str,
}

/// What the line's menu offers: the identifier the next ACP takes, and each
/// kind with why it cannot be added, or `None`.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct AcpChoices {
    pub identifier: String,
    pub choices: Vec<(AcpKind, Option<Refusal>)>,
}

/// W33: the ACPs the line may take. Either both kinds may, or neither, for
/// the same reason.
pub fn acp_choices(document: &Document, view: &View, line: AcpLine<'_>) -> AcpChoices {
    let refusal = plan(document, view, line).err();
    AcpChoices {
        identifier: Identifiers::of(document, view).next_prefixed(ACP_PREFIX),
        choices: ACP_KINDS.iter().map(|kind| (*kind, refusal)).collect(),
    }
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
fn plan(document: &Document, view: &View, line: AcpLine<'_>) -> Result<Plan, Refusal> {
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
    line: AcpLine<'_>,
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
