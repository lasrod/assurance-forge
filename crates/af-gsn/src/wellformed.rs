//! GSN well-formedness: where the argument, as the view reads it, breaks the
//! rules of the GSN Community Standard v3.
//!
//! The view draws what the file says, even where GSN would not allow it
//! (principle 1 and D3 of `docs/standards/sacm-gsn-mapping.md`). These checks
//! say where that is. Each [`Rule`] names the GSN v3 requirement it enforces,
//! so a reader can take a problem back to the standard (GSN3-VAL-001). The
//! rules and their severities are the problem-list plan's (P2 to P4,
//! `docs/roadmap/milestone-1-problems.md`).
//!
//! Nothing is reported at a neutral node, whose GSN type the file leaves
//! unknown: a false structural error is worse than a missing one. A node
//! under more than one parent is not reported: GSN3-CORE-014 makes an
//! argument a directed acyclic support graph, not a tree.

use std::collections::{HashMap, HashSet};

use sacm::model::{Document, ElementKind as SacmKind};

use crate::view::{EdgeKind, NodeKind, View};

/// A GSN rule this module checks.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Rule {
    /// AF-GSN-001: nodes support each other in a circle.
    Cycle,
    /// AF-GSN-002: a Solution, Context, Assumption or Justification is supported.
    LeafSupported,
    /// AF-GSN-003: a Solution, Context, Assumption or Justification has context.
    LeafHasContext,
    /// AF-GSN-004: a Context, Assumption or Justification supports.
    ContextualSupports,
    /// AF-GSN-005: a Goal, Strategy or Solution is given as context.
    NotContextual,
    /// AF-GSN-006: a Strategy is supported by something other than a Goal.
    StrategySupportedByNonGoal,
    /// AF-GSN-007: a Strategy stands at an end of a relationship instead of
    /// as its reasoning.
    StrategyAtEnd,
    /// AF-GSN-008: a Strategy supports nothing.
    StrategyUnsupported,
    /// AF-GSN-009: two or more nodes have one identifier.
    DuplicateIdentifier,
    /// AF-GSN-010: an undeveloped element is supported.
    UndevelopedSupported,
}

impl Rule {
    /// Every rule, in the order of their codes.
    pub const ALL: [Rule; 10] = [
        Rule::Cycle,
        Rule::LeafSupported,
        Rule::LeafHasContext,
        Rule::ContextualSupports,
        Rule::NotContextual,
        Rule::StrategySupportedByNonGoal,
        Rule::StrategyAtEnd,
        Rule::StrategyUnsupported,
        Rule::DuplicateIdentifier,
        Rule::UndevelopedSupported,
    ];

    /// The code, such as `AF-GSN-001`.
    pub fn code(self) -> &'static str {
        match self {
            Rule::Cycle => "AF-GSN-001",
            Rule::LeafSupported => "AF-GSN-002",
            Rule::LeafHasContext => "AF-GSN-003",
            Rule::ContextualSupports => "AF-GSN-004",
            Rule::NotContextual => "AF-GSN-005",
            Rule::StrategySupportedByNonGoal => "AF-GSN-006",
            Rule::StrategyAtEnd => "AF-GSN-007",
            Rule::StrategyUnsupported => "AF-GSN-008",
            Rule::DuplicateIdentifier => "AF-GSN-009",
            Rule::UndevelopedSupported => "AF-GSN-010",
        }
    }

    /// The GSN Community Standard v3 requirement it enforces, as
    /// `docs/reference/cpp-gsn-v3-conformance-matrix.md` names it.
    pub fn requirement(self) -> &'static str {
        match self {
            Rule::Cycle => "GSN3-CORE-014",
            Rule::LeafSupported
            | Rule::LeafHasContext
            | Rule::ContextualSupports
            | Rule::NotContextual
            | Rule::StrategySupportedByNonGoal => "GSN3-CORE-015",
            Rule::StrategyAtEnd | Rule::StrategyUnsupported => "GSN3-CORE-002",
            Rule::DuplicateIdentifier => "GSN3-CORE-010",
            Rule::UndevelopedSupported => "GSN3-CORE-009",
        }
    }

    /// Whether it makes the argument wrong (an error), rather than unfinished
    /// or stale (a warning): a Strategy with no Goal yet is how the editor
    /// adds one (W2), and an undeveloped decorator on a supported element is
    /// stale.
    pub fn is_error(self) -> bool {
        !matches!(self, Rule::StrategyUnsupported | Rule::UndevelopedSupported)
    }
}

/// One place the argument breaks a [`Rule`].
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Finding {
    pub rule: Rule,
    /// The SACM ids of the nodes it is about. For a relationship, the node
    /// the arrow leaves and the one it points at; for a cycle or a shared
    /// identifier, every node, in document order.
    pub nodes: Vec<String>,
    /// The SACM id of the relationship it is about, if it is about one.
    pub relationship: Option<String>,
}

/// Where the argument `view` reads from `document` breaks the GSN rules: per
/// relationship, then per node, then cycles, each in document order.
pub fn check(document: &Document, view: &View) -> Vec<Finding> {
    let kinds: HashMap<&str, NodeKind> = view
        .nodes
        .iter()
        .map(|node| (node.id.as_str(), node.kind))
        .collect();
    let kind = |id: &str| kinds.get(id).copied().unwrap_or(NodeKind::Neutral);
    let mut findings = Vec::new();

    // Per relationship: the first rule an edge breaks (AF-GSN-002 to 006).
    // A relationship drawn as several edges is reported once.
    let mut reported: HashSet<&str> = HashSet::new();
    for edge in &view.edges {
        if edge.to_relationship || reported.contains(edge.relationship.as_str()) {
            continue;
        }
        let (from, to) = (kind(&edge.from), kind(&edge.to));
        if from == NodeKind::Neutral || to == NodeKind::Neutral {
            continue;
        }
        let rule = match edge.kind {
            EdgeKind::SupportedBy if is_leaf(from) => Some(Rule::LeafSupported),
            EdgeKind::SupportedBy if is_contextual(to) => Some(Rule::ContextualSupports),
            EdgeKind::SupportedBy if from == NodeKind::Strategy && to != NodeKind::Goal => {
                Some(Rule::StrategySupportedByNonGoal)
            }
            EdgeKind::InContextOf if is_leaf(from) => Some(Rule::LeafHasContext),
            EdgeKind::InContextOf if !is_contextual(to) => Some(Rule::NotContextual),
            _ => None,
        };
        if let Some(rule) = rule {
            reported.insert(edge.relationship.as_str());
            findings.push(Finding {
                rule,
                nodes: vec![edge.from.clone(), edge.to.clone()],
                relationship: Some(edge.relationship.clone()),
            });
        }
    }

    // AF-GSN-007 (R8): a Strategy at an end of an inference or of evidence,
    // where it is a premise or a conclusion. Its reasoning is where a
    // Strategy belongs. A Strategy as the target of an `AssertedContext` is
    // the Strategy's context, which GSN allows (R8); one as its source is
    // AF-GSN-005.
    let elements: HashMap<&str, sacm::model::SacmElement<'_>> = document
        .elements()
        .map(|element| (element.id().as_str(), element))
        .collect();
    let mut seen: HashSet<&str> = HashSet::new();
    for edge in &view.edges {
        if edge.kind != EdgeKind::SupportedBy || !seen.insert(edge.relationship.as_str()) {
            continue;
        }
        let Some(relationship) = elements.get(edge.relationship.as_str()).filter(|element| {
            matches!(
                element.kind(),
                SacmKind::AssertedInference | SacmKind::AssertedEvidence
            )
        }) else {
            continue;
        };
        let mut strategies: Vec<String> = Vec::new();
        for end in relationship
            .target()
            .into_iter()
            .chain(relationship.sources().iter())
        {
            let is_strategy = elements
                .get(end.as_str())
                .is_some_and(|element| element.kind() == SacmKind::ArgumentReasoning);
            if is_strategy && !strategies.iter().any(|known| known == end.as_str()) {
                strategies.push(end.as_str().to_owned());
            }
        }
        if !strategies.is_empty() {
            findings.push(Finding {
                rule: Rule::StrategyAtEnd,
                nodes: strategies,
                relationship: Some(edge.relationship.clone()),
            });
        }
    }

    // Per node.
    let supports: HashSet<&str> = view
        .edges
        .iter()
        .filter(|edge| edge.kind == EdgeKind::SupportedBy)
        .map(|edge| edge.from.as_str())
        .collect();
    for node in &view.nodes {
        if node.kind == NodeKind::Strategy && !supports.contains(node.id.as_str()) {
            findings.push(Finding {
                rule: Rule::StrategyUnsupported,
                nodes: vec![node.id.clone()],
                relationship: None,
            });
        }
        // Once per element, however many relationships support it.
        if node.undeveloped && supports.contains(node.id.as_str()) {
            findings.push(Finding {
                rule: Rule::UndevelopedSupported,
                nodes: vec![node.id.clone()],
                relationship: None,
            });
        }
    }

    // AF-GSN-009: identifiers, once per identifier shared.
    let mut by_identifier: HashMap<&str, Vec<&str>> = HashMap::new();
    let mut identifiers: Vec<&str> = Vec::new();
    for node in &view.nodes {
        let holders = by_identifier.entry(node.identifier.as_str()).or_default();
        if holders.is_empty() {
            identifiers.push(node.identifier.as_str());
        }
        holders.push(node.id.as_str());
    }
    for identifier in identifiers {
        let holders = &by_identifier[identifier];
        if holders.len() > 1 {
            findings.push(Finding {
                rule: Rule::DuplicateIdentifier,
                nodes: holders.iter().map(|id| (*id).to_owned()).collect(),
                relationship: None,
            });
        }
    }

    // AF-GSN-001: cycles of support (P4).
    findings.extend(cycles(view).into_iter().map(|nodes| Finding {
        rule: Rule::Cycle,
        nodes,
        relationship: None,
    }));
    findings
}

/// A Solution, Context, Assumption or Justification: nothing goes under it.
fn is_leaf(kind: NodeKind) -> bool {
    matches!(
        kind,
        NodeKind::Solution | NodeKind::Context | NodeKind::Assumption | NodeKind::Justification
    )
}

/// What `InContextOf` may point at.
fn is_contextual(kind: NodeKind) -> bool {
    matches!(
        kind,
        NodeKind::Context | NodeKind::Assumption | NodeKind::Justification
    )
}

/// The sets of nodes that support each other in a circle: the strongly
/// connected parts of the `SupportedBy` graph with more than one node, or one
/// node that supports itself. Each set in document order, the sets in the
/// document order of their first node.
fn cycles(view: &View) -> Vec<Vec<String>> {
    let index: HashMap<&str, usize> = view
        .nodes
        .iter()
        .enumerate()
        .map(|(at, node)| (node.id.as_str(), at))
        .collect();
    let count = view.nodes.len();
    let mut next: Vec<Vec<usize>> = vec![Vec::new(); count];
    let mut looped = vec![false; count];
    for edge in &view.edges {
        if edge.kind != EdgeKind::SupportedBy {
            continue;
        }
        if let (Some(&from), Some(&to)) =
            (index.get(edge.from.as_str()), index.get(edge.to.as_str()))
        {
            if from == to {
                looped[from] = true;
            }
            next[from].push(to);
        }
    }
    let mut sets: Vec<Vec<usize>> = strongly_connected(&next)
        .into_iter()
        .filter(|set| set.len() > 1 || looped[set[0]])
        .map(|mut set| {
            set.sort_unstable();
            set
        })
        .collect();
    sets.sort_unstable_by_key(|set| set[0]);
    sets.into_iter()
        .map(|set| {
            set.into_iter()
                .map(|at| view.nodes[at].id.clone())
                .collect()
        })
        .collect()
}

/// Tarjan's strongly connected components, without recursion: a large case
/// must not overflow the stack.
fn strongly_connected(next: &[Vec<usize>]) -> Vec<Vec<usize>> {
    const UNSEEN: usize = usize::MAX;
    let count = next.len();
    let mut order = vec![UNSEEN; count];
    let mut low = vec![0; count];
    let mut on_stack = vec![false; count];
    let mut stack: Vec<usize> = Vec::new();
    let mut sets = Vec::new();
    let mut counter = 0;
    for start in 0..count {
        if order[start] != UNSEEN {
            continue;
        }
        // (node, the next of its successors to look at)
        let mut path: Vec<(usize, usize)> = vec![(start, 0)];
        order[start] = counter;
        low[start] = counter;
        counter += 1;
        stack.push(start);
        on_stack[start] = true;
        while let Some(&mut (node, ref mut at)) = path.last_mut() {
            if let Some(&successor) = next[node].get(*at) {
                *at += 1;
                if order[successor] == UNSEEN {
                    order[successor] = counter;
                    low[successor] = counter;
                    counter += 1;
                    stack.push(successor);
                    on_stack[successor] = true;
                    path.push((successor, 0));
                } else if on_stack[successor] {
                    low[node] = low[node].min(order[successor]);
                }
                continue;
            }
            path.pop();
            if let Some(&(parent, _)) = path.last() {
                low[parent] = low[parent].min(low[node]);
            }
            if low[node] == order[node] {
                let mut set = Vec::new();
                while let Some(member) = stack.pop() {
                    on_stack[member] = false;
                    set.push(member);
                    if member == node {
                        break;
                    }
                }
                sets.push(set);
            }
        }
    }
    sets
}
