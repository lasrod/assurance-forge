//! Which repairs a GSN problem has (F1 and F4 of
//! `docs/roadmap/milestone-2-repair.md`).
//!
//! A repair is an edit the editor already makes, chosen by the rule the
//! argument breaks: joining an element as context (W27), removing a line
//! (W37) or clearing the undeveloped mark (W26). [`repairs`] lists those a
//! finding has, each with why it cannot be made here, so the problem list
//! can offer those that can and say why for the others. Nothing is changed.

use std::collections::HashSet;

use sacm::model::Document;

use super::{Line, Refusal, line_removal, repairable, undeveloped_change};
use crate::view::{EdgeKind, View};
use crate::wellformed::{Finding, Rule};

/// One repair of a problem.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Repair {
    /// Join `element`, which `relationship` joins by a support line, as
    /// context in its place (W27).
    MakeContext {
        element: String,
        relationship: String,
    },
    /// Remove the line from `from` to `to` of `relationship` (W37), or why
    /// it cannot be removed.
    RemoveLine {
        relationship: String,
        from: String,
        to: String,
        refusal: Option<Refusal>,
    },
    /// Clear the undeveloped mark of the Goal `element` (W26), or why it
    /// cannot be cleared.
    ClearUndeveloped {
        element: String,
        refusal: Option<Refusal>,
    },
}

/// The repairs of `finding`, where `view` is the view of `document` the
/// finding was made of (`wellformed::check`), in the order they are offered.
/// Empty for a rule that has none in this step: a Strategy at an end
/// (AF-GSN-007), a Strategy with no Goal (AF-GSN-008) and a shared
/// identifier (AF-GSN-009).
pub fn repairs(document: &Document, view: &View, finding: &Finding) -> Vec<Repair> {
    let line = || match (finding.relationship.as_deref(), finding.nodes.as_slice()) {
        (Some(relationship), [from, to]) => {
            Some(remove_line(document, view, relationship, from, to))
        }
        _ => None,
    };
    match finding.rule {
        // F1: the user chooses which line of the cycle goes.
        Rule::Cycle => {
            let members: HashSet<&str> = finding.nodes.iter().map(String::as_str).collect();
            let mut seen: HashSet<(&str, &str, &str)> = HashSet::new();
            view.edges
                .iter()
                .filter(|edge| {
                    edge.kind == EdgeKind::SupportedBy
                        && !edge.to_relationship
                        && members.contains(edge.from.as_str())
                        && members.contains(edge.to.as_str())
                        && seen.insert((&edge.relationship, &edge.from, &edge.to))
                })
                .map(|edge| remove_line(document, view, &edge.relationship, &edge.from, &edge.to))
                .collect()
        }
        Rule::LeafSupported
        | Rule::LeafHasContext
        | Rule::NotContextual
        | Rule::StrategySupportedByNonGoal => line().into_iter().collect(),
        // W27 where it can be made; under a Strategy, which W27 does not
        // repair (U7), and wherever else it cannot, the line goes.
        Rule::ContextualSupports => {
            let joined = finding.relationship.as_deref().and_then(|relationship| {
                finding
                    .nodes
                    .iter()
                    .find(|node| repairable(document, view, node, relationship).is_ok())
                    .map(|element| Repair::MakeContext {
                        element: element.clone(),
                        relationship: relationship.to_owned(),
                    })
            });
            match joined {
                Some(repair) => vec![repair],
                None => line().into_iter().collect(),
            }
        }
        Rule::UndevelopedSupported => finding
            .nodes
            .first()
            .map(|element| Repair::ClearUndeveloped {
                element: element.clone(),
                refusal: undeveloped_change(document, view, element, false).err(),
            })
            .into_iter()
            .collect(),
        Rule::StrategyAtEnd | Rule::StrategyUnsupported | Rule::DuplicateIdentifier => Vec::new(),
    }
}

fn remove_line(
    document: &Document,
    view: &View,
    relationship: &str,
    from: &str,
    to: &str,
) -> Repair {
    let line = Line {
        relationship,
        from,
        to,
    };
    Repair::RemoveLine {
        relationship: relationship.to_owned(),
        from: from.to_owned(),
        to: to.to_owned(),
        refusal: line_removal(document, view, line).err(),
    }
}
