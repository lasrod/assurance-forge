//! The GSN view of a SACM document.
//!
//! [`view`] reads a [`Document`] and answers what the diagram draws: nodes,
//! edges, the elements it does not place, and a code for everything it draws
//! differently from the file or not at all. It follows the reading rules in
//! `docs/standards/sacm-gsn-mapping.md`, whose ids (R1 to R15, E1 to E12, T1 to
//! T3) the comments here cite. It never changes the document.
//!
//! Every argument element appears in the view exactly once (plan decision D4):
//! as a [`Node`], as the relationship of one or more [`Edge`]s, or as an
//! [`Unplaced`] item.

use std::collections::{HashMap, HashSet};

use sacm::model::{
    AssertionDeclaration, Document, ElementKind as SacmKind, MetamodelPackage, ReferenceEnd,
    SacmElement,
};

use crate::ElementKind;

/// What the view sends for a case it draws differently from the file, or does
/// not draw. The frontend words it; the core sends only the code and its ids.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum Code {
    /// AF-VIEW-001: a recorded GSN type was not used (R1).
    RecordedTypeNotUsed,
    /// AF-VIEW-002: a Claim's declaration leaves its GSN type unknown (R7).
    DeclarationHasNoShape,
    /// AF-VIEW-003: an ArtifactReference is neither plainly evidence nor plainly context (R11).
    ArtifactReferenceNotPlaced,
    /// AF-VIEW-004: a citation, drawn neutral until modules (R12, E9).
    Citation,
    /// AF-VIEW-005: the uninstantiated decorator is not drawn yet (R13).
    UninstantiatedNotDrawn,
    /// AF-VIEW-006: an element kind this step does not draw (R14, R15).
    KindNotDrawn,
    /// AF-VIEW-007: retired. It said a counter relationship was drawn
    /// neutral; a counter is drawn as a challenge now (E5), so it is no
    /// longer sent. The number is kept so the later codes keep theirs.
    Counter,
    /// AF-VIEW-008: a relationship's declaration is not drawn (E6).
    RelationshipDeclarationNotDrawn,
    /// AF-VIEW-009: a relationship end, or a reasoning, that cannot be placed (E5, E7, E8).
    EndNotPlaced,
    /// AF-VIEW-010: a relationship between artifacts, not drawn (E10).
    ArtifactRelationshipNotDrawn,
    /// AF-VIEW-011: a defeated relationship, drawn neutral (E5a).
    Defeated,
    /// AF-VIEW-012: an assertion's `metaClaim` link is not drawn (E11).
    MetaClaimNotDrawn,
    /// AF-VIEW-013: a Strategy's `structure` link is not drawn (E12).
    StructureNotDrawn,
    /// AF-VIEW-014: the place a Strategy's first Goal will take, held for
    /// SACM and not drawn (R16).
    Placeholder,
    /// AF-VIEW-015: a confidence Claim an ACP shows as its description, not
    /// drawn as a node (R17).
    AcpDescription,
}

impl Code {
    /// Every code, in the order of the mapping record's table.
    pub const ALL: [Code; 15] = [
        Code::RecordedTypeNotUsed,
        Code::DeclarationHasNoShape,
        Code::ArtifactReferenceNotPlaced,
        Code::Citation,
        Code::UninstantiatedNotDrawn,
        Code::KindNotDrawn,
        Code::Counter,
        Code::RelationshipDeclarationNotDrawn,
        Code::EndNotPlaced,
        Code::ArtifactRelationshipNotDrawn,
        Code::Defeated,
        Code::MetaClaimNotDrawn,
        Code::StructureNotDrawn,
        Code::Placeholder,
        Code::AcpDescription,
    ];

    /// The code as the mapping record writes it, such as `AF-VIEW-001`.
    pub fn as_str(self) -> &'static str {
        match self {
            Code::RecordedTypeNotUsed => "AF-VIEW-001",
            Code::DeclarationHasNoShape => "AF-VIEW-002",
            Code::ArtifactReferenceNotPlaced => "AF-VIEW-003",
            Code::Citation => "AF-VIEW-004",
            Code::UninstantiatedNotDrawn => "AF-VIEW-005",
            Code::KindNotDrawn => "AF-VIEW-006",
            Code::Counter => "AF-VIEW-007",
            Code::RelationshipDeclarationNotDrawn => "AF-VIEW-008",
            Code::EndNotPlaced => "AF-VIEW-009",
            Code::ArtifactRelationshipNotDrawn => "AF-VIEW-010",
            Code::Defeated => "AF-VIEW-011",
            Code::MetaClaimNotDrawn => "AF-VIEW-012",
            Code::StructureNotDrawn => "AF-VIEW-013",
            Code::Placeholder => "AF-VIEW-014",
            Code::AcpDescription => "AF-VIEW-015",
        }
    }
}

/// A code about one element, and for an end that cannot be placed, the id
/// that end names (which may name no element).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Finding {
    pub code: Code,
    /// The SACM id of the element or relationship the code is about.
    pub element: String,
    /// The id an end or reasoning names, for [`Code::EndNotPlaced`].
    pub end: Option<String>,
}

/// What a node is drawn as.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum NodeKind {
    Goal,
    Strategy,
    Solution,
    Context,
    Assumption,
    Justification,
    /// A neutral shape: the file does not say which GSN element it is, or
    /// says something this step does not draw. A [`Finding`] says which.
    Neutral,
}

impl NodeKind {
    /// The GSN element kind, or `None` for [`NodeKind::Neutral`].
    pub fn element_kind(self) -> Option<ElementKind> {
        match self {
            NodeKind::Goal => Some(ElementKind::Goal),
            NodeKind::Strategy => Some(ElementKind::Strategy),
            NodeKind::Solution => Some(ElementKind::Solution),
            NodeKind::Context => Some(ElementKind::Context),
            NodeKind::Assumption => Some(ElementKind::Assumption),
            NodeKind::Justification => Some(ElementKind::Justification),
            NodeKind::Neutral => None,
        }
    }

    fn from_element_kind(kind: ElementKind) -> Self {
        match kind {
            ElementKind::Goal => NodeKind::Goal,
            ElementKind::Strategy => NodeKind::Strategy,
            ElementKind::Solution => NodeKind::Solution,
            ElementKind::Context => NodeKind::Context,
            ElementKind::Assumption => NodeKind::Assumption,
            ElementKind::Justification => NodeKind::Justification,
        }
    }
}

/// A node of the diagram: one `Claim`, `ArgumentReasoning` or `ArtifactReference`.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Node {
    /// The SACM id.
    pub id: String,
    pub kind: NodeKind,
    /// The undeveloped decorator (R3).
    pub undeveloped: bool,
    /// The GSN identifier (T1).
    pub identifier: String,
    /// The name (T2).
    pub name: Option<String>,
    /// The statement (T3).
    pub statement: Option<String>,
    /// In a package's diagram, the other argument package a node drawn
    /// there belongs to (A6 of the ACP plan); `None` for a node of the
    /// package shown, and in the whole case.
    pub elsewhere: Option<Elsewhere>,
}

/// The argument package a node of another package's diagram belongs to.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Elsewhere {
    /// Its SACM id.
    pub package: String,
    /// Its name, or its id when it has none.
    pub name: String,
}

/// How an edge is drawn.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum EdgeKind {
    SupportedBy,
    InContextOf,
    /// A counter relationship: GSN's challenge (E5).
    Challenge,
    /// None of those: a defeated or cited relationship (E5a, E9).
    Neutral,
}

/// An edge of the diagram, from `from` to `to` as the arrow points.
///
/// SupportedBy and InContextOf run from the conclusion to the premise, the
/// opposite of SACM's source and target. A challenge runs from source to
/// target, at what it challenges (E5).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Edge {
    /// The SACM id of the node the edge leaves.
    pub from: String,
    /// The SACM id of what the arrow points at: a node, or, when
    /// `to_relationship` is set, a relationship.
    pub to: String,
    /// `to` names a relationship, whose first edge the arrow points at. Only
    /// a challenge has it (E5).
    pub to_relationship: bool,
    pub kind: EdgeKind,
    /// The SACM id of the relationship the edge draws.
    pub relationship: String,
}

/// An assurance claim point: a `metaClaim` of a relationship the view draws
/// as `SupportedBy` or `InContextOf`, naming a confidence `Claim` (E11; A1
/// of `docs/roadmap/milestone-2-acps.md`). Drawn on the relationship's first
/// edge.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Acp {
    /// The SACM id of the relationship.
    pub relationship: String,
    /// The ends of the relationship's first edge, which carries it: a
    /// package's diagram that does not draw that edge does not draw it.
    pub from: String,
    pub to: String,
    /// The SACM id of the confidence `Claim`.
    pub claim: String,
    /// The label: the Claim's GSN identifier, with `[package name]` when it
    /// is in another argument package than the relationship (GSN v3
    /// §1:5.2.3).
    pub label: String,
    /// The SACM id of the Claim's argument package when it is another than
    /// the relationship's: what the ACP opens.
    pub package: Option<String>,
}

/// An argument element the view does not place.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Unplaced {
    pub id: String,
    pub code: Code,
}

/// The GSN view of a document.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct View {
    /// In document order.
    pub nodes: Vec<Node>,
    /// In document order of their relationships.
    pub edges: Vec<Edge>,
    /// In document order.
    pub unplaced: Vec<Unplaced>,
    /// In document order of their elements.
    pub findings: Vec<Finding>,
    /// In document order of their relationships, then of each one's
    /// `metaClaim` references.
    pub acps: Vec<Acp>,
    /// The confidence Claims ACPs show as their description (R17), as a
    /// Goal would be drawn, though they are not: their text is edited as a
    /// node's is. In document order.
    pub descriptions: Vec<Node>,
}

/// The tagged value that records an element's GSN type on import.
const EXTENSION_TYPE_KEY: &str = "sacm.import.extensionType";
/// The C++ application's tag for a Justification.
const GSN_ROLE_KEY: &str = "assuranceForge.gsn.role";
/// The legacy declaration literal the C++ library recorded when it normalized it.
const IMPORT_DECLARATION_KEY: &str = "sacm.import.assertionDeclaration";
/// The GSN identifier, apart from the SACM id (T1).
const GSN_IDENTIFIER_KEY: &str = "assuranceForge.gsn.identifier";
/// The tagged value that marks the Claim a Strategy holds for its first Goal
/// (W2, R16), and its value.
pub(crate) const PLACEHOLDER_KEY: &str = "assuranceForge.gsn.placeholder";
pub(crate) const PLACEHOLDER_VALUE: &str = "subGoal";
/// The GSN namespaces a recorded type may name (mapping record, Namespaces).
const GSN_NAMESPACES: [&str; 3] = [
    "http://scsc.acwg.gsn/2.0",
    "http://acwg.org/3.0/gsn",
    "http://org.eclipse.acme/1.0/gsn",
];

/// Derives the GSN view of `document`, with node text in `language`, a BCP 47
/// tag such as `en` or `ja` (T3).
pub fn view(document: &Document, language: &str) -> View {
    let elements: Vec<SacmElement<'_>> = document
        .elements()
        .filter(|element| is_argument_element(element.kind()))
        .collect();
    let order: HashMap<&str, usize> = elements
        .iter()
        .enumerate()
        .map(|(index, element)| (element.id().as_str(), index))
        .collect();

    // R14, R15: groups, interfaces, bindings and what those two contain.
    let hidden: HashSet<&str> = elements
        .iter()
        .filter(|element| is_hidden(**element))
        .map(|element| element.id().as_str())
        .collect();
    // The relationships an edge may be drawn for, which a challenge may point
    // at (E5).
    let relationships: HashSet<&str> = elements
        .iter()
        .filter(|element| is_edge_kind(element.kind()) && !hidden.contains(element.id().as_str()))
        .map(|element| element.id().as_str())
        .collect();
    // R16: the Claims that hold a Strategy's place for its first Goal.
    let placeholders: HashSet<&str> = elements
        .iter()
        .filter(|element| {
            !hidden.contains(element.id().as_str()) && is_placeholder(document, **element)
        })
        .map(|element| element.id().as_str())
        .collect();
    // R17: the confidence Claims an ACP may show as its description. Only
    // those an ACP is drawn for are, which the relationships decide below.
    let candidates: HashSet<&str> = elements
        .iter()
        .filter(|element| {
            !hidden.contains(element.id().as_str()) && is_acp_description(document, **element)
        })
        .map(|element| element.id().as_str())
        .collect();
    // The elements a node is drawn for, and their SACM kind.
    let placeable: HashMap<&str, SacmKind> = elements
        .iter()
        .filter(|element| {
            let id = element.id().as_str();
            is_node_kind(element.kind()) && !hidden.contains(id) && !placeholders.contains(id)
        })
        .map(|element| (element.id().as_str(), element.kind()))
        .collect();

    let mut builder = Builder::default();
    let mut reach: HashMap<&str, Reach> = HashMap::new();

    for element in &elements {
        let id = element.id().as_str();
        if hidden.contains(id) {
            builder.unplaced(id, Code::KindNotDrawn);
        } else if placeholders.contains(id) {
            builder.unplaced(id, Code::Placeholder);
        } else if is_relationship(element.kind()) {
            relationship(
                *element,
                &placeable,
                &placeholders,
                &relationships,
                &mut reach,
                &mut builder,
            );
        }
    }
    builder.drop_challenges_of_nothing();
    // R17: a candidate is a description when an ACP is drawn for it, and
    // every relationship that names it draws one.
    let linked: HashSet<&str> = builder
        .acp_links
        .iter()
        .map(|(relationship, _)| relationship.as_str())
        .collect();
    let descriptions: HashSet<&str> = candidates
        .iter()
        .copied()
        .filter(|id| {
            document
                .referrers(*id)
                .iter()
                .all(|referrer| linked.contains(referrer.holder().id().as_str()))
        })
        .collect();
    for element in &elements {
        let id = element.id().as_str();
        if descriptions.contains(id) {
            builder.unplaced(id, Code::AcpDescription);
        }
    }
    for element in &elements {
        let id = element.id().as_str();
        if placeable.contains_key(id) && !descriptions.contains(id) {
            let reached = reach.get(id).copied().unwrap_or_default();
            node(*element, reached, language, &mut builder);
        } else if descriptions.contains(id) {
            // R17: built as a Goal is, and kept apart, not drawn; what the
            // building found is of no node, and is not said.
            let found = builder.findings.len();
            node(*element, Reach::default(), language, &mut builder);
            builder.findings.truncate(found);
            if let Some(described) = builder.nodes.pop() {
                builder.descriptions.push(described);
            }
        }
    }
    acps(document, &mut builder);

    builder.finish(&order)
}

/// E11: each `metaClaim` of a drawn live relationship that names a `Claim`
/// is an ACP; one naming anything else is not drawn.
fn acps(document: &Document, builder: &mut Builder) {
    let mut unnamed: Vec<String> = Vec::new();
    for (relationship, claim) in std::mem::take(&mut builder.acp_links) {
        let named = document
            .get(claim.as_str())
            .filter(|element| element.kind() == SacmKind::Claim);
        let Some(named) = named else {
            if !unnamed.contains(&relationship) {
                builder.finding(Code::MetaClaimNotDrawn, &relationship, None);
                unnamed.push(relationship);
            }
            continue;
        };
        let identifier = tagged_value(named, GSN_IDENTIFIER_KEY)
            .filter(|identifier| !identifier.is_empty())
            .unwrap_or(named.id().as_str());
        let held = |id: &str| document.get(id).and_then(package_of);
        let elsewhere = match (held(&claim), held(&relationship)) {
            (Some(claims), Some(lines)) if claims.id() != lines.id() => Some(claims),
            (Some(claims), None) => Some(claims),
            _ => None,
        };
        let label = match elsewhere {
            Some(package) => {
                let name = package
                    .name()
                    .and_then(|name| name.content())
                    .filter(|name| !name.trim().is_empty())
                    .unwrap_or(package.id().as_str());
                format!("{identifier}[{name}]")
            }
            None => identifier.to_owned(),
        };
        let Some((from, to)) = builder
            .edges
            .iter()
            .find(|edge| edge.relationship == relationship)
            .map(|edge| (edge.from.clone(), edge.to.clone()))
        else {
            continue;
        };
        builder.acps.push(Acp {
            relationship,
            from,
            to,
            claim,
            label,
            package: elsewhere.map(|package| package.id().as_str().to_owned()),
        });
    }
}

/// R17: a `Claim` an ACP shows as its description, and the diagram not as a
/// node. Only `metaClaim` references name it, each from a relationship of its
/// own argument package that is drawn live (asserted, or declared nothing;
/// not a counter or a citation); it is not cited, not abstract, and makes no
/// claim about anything itself. Anything that also supports it, or that it
/// supports, makes it a Goal of the argument, drawn as R2 says.
fn is_acp_description(document: &Document, element: SacmElement<'_>) -> bool {
    if element.kind() != SacmKind::Claim
        || element.is_citation()
        || element.is_abstract()
        || !element.meta_claims().is_empty()
    {
        return false;
    }
    let home = package_of(element).map(|package| package.id().clone());
    let referrers = document.referrers(element.id().as_str());
    !referrers.is_empty()
        && referrers.iter().all(|referrer| {
            let holder = referrer.holder();
            referrer.end() == ReferenceEnd::MetaClaim
                && matches!(
                    holder.kind(),
                    SacmKind::AssertedInference
                        | SacmKind::AssertedEvidence
                        | SacmKind::AssertedContext
                )
                && !holder.is_counter()
                && !holder.is_citation()
                && matches!(
                    holder.assertion_declaration(),
                    None | Some(AssertionDeclaration::Asserted)
                )
                && package_of(holder).map(|package| package.id().clone()) == home
        })
}

/// How the live relationships reach an element: only these count (the
/// mapping record's "Reached").
#[derive(Clone, Copy, Debug, Default)]
struct Reach {
    evidence_source: bool,
    context_source: bool,
    other: bool,
    /// The source of a counter `AssertedEvidence` the view draws: counter-
    /// evidence, a Solution (R9, decided 27 November 2026).
    counter_evidence_source: bool,
}

/// How a relationship's edges are drawn.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Style {
    /// E1 to E4, with E6's code or without.
    Live,
    /// E5a and E9: the edges E1 to E4 would give, drawn neutral.
    NeutralInGsnDirection,
    /// E5: from each source to the target.
    Counter,
}

#[derive(Default)]
struct Builder {
    nodes: Vec<Node>,
    edges: Vec<Edge>,
    unplaced: Vec<Unplaced>,
    findings: Vec<Finding>,
    /// Challenges of a relationship that made no edge (none of their sources
    /// can be placed), and the relationship each challenges: whether that is
    /// drawn is known only once every edge is.
    edgeless_challenges: Vec<(String, String)>,
    /// A drawn live relationship and a `metaClaim` it names: an ACP once the
    /// named element is known to be a `Claim` (E11).
    acp_links: Vec<(String, String)>,
    acps: Vec<Acp>,
    descriptions: Vec<Node>,
}

impl Builder {
    fn finding(&mut self, code: Code, element: &str, end: Option<&str>) {
        self.findings.push(Finding {
            code,
            element: element.to_owned(),
            end: end.map(str::to_owned),
        });
    }

    fn unplaced(&mut self, id: &str, code: Code) {
        self.unplaced.push(Unplaced {
            id: id.to_owned(),
            code,
        });
    }

    fn edge(&mut self, from: &str, to: &str, kind: EdgeKind, relationship: &str) {
        self.edges.push(Edge {
            from: from.to_owned(),
            to: to.to_owned(),
            to_relationship: false,
            kind,
            relationship: relationship.to_owned(),
        });
    }

    /// E5 and E7: a challenge of a relationship that has no edge has nothing
    /// to point at, and its finding names that relationship, whether or not
    /// the challenge's sources can be placed. Dropping one may leave another
    /// with nothing, so this runs until none is left.
    fn drop_challenges_of_nothing(&mut self) {
        self.drop_challenge_edges_of_nothing();
        for (id, target) in std::mem::take(&mut self.edgeless_challenges) {
            if !self.edges.iter().any(|edge| edge.relationship == target) {
                self.findings.push(Finding {
                    code: Code::EndNotPlaced,
                    element: id,
                    end: Some(target),
                });
            }
        }
    }

    fn drop_challenge_edges_of_nothing(&mut self) {
        loop {
            let drawn: HashSet<&str> = self
                .edges
                .iter()
                .map(|edge| edge.relationship.as_str())
                .collect();
            let dropped: Vec<Edge> = self
                .edges
                .iter()
                .filter(|edge| edge.to_relationship && !drawn.contains(edge.to.as_str()))
                .cloned()
                .collect();
            if dropped.is_empty() {
                return;
            }
            self.edges
                .retain(|edge| !(edge.to_relationship && dropped.contains(edge)));
            let mut reported: HashSet<&str> = HashSet::new();
            for edge in &dropped {
                let id = edge.relationship.as_str();
                if !reported.insert(id) {
                    continue;
                }
                self.findings.push(Finding {
                    code: Code::EndNotPlaced,
                    element: id.to_owned(),
                    end: Some(edge.to.clone()),
                });
                if !self.edges.iter().any(|kept| kept.relationship == id) {
                    self.unplaced.push(Unplaced {
                        id: id.to_owned(),
                        code: Code::EndNotPlaced,
                    });
                }
            }
        }
    }

    fn finish(mut self, order: &HashMap<&str, usize>) -> View {
        // Findings come from two passes; put them in document order, keeping
        // the order within one element.
        let position = |id: &str| order.get(id).copied().unwrap_or(usize::MAX);
        self.findings
            .sort_by_key(|finding| position(finding.element.as_str()));
        View {
            nodes: self.nodes,
            edges: self.edges,
            unplaced: self.unplaced,
            findings: self.findings,
            acps: self.acps,
            descriptions: self.descriptions,
        }
    }
}

/// Draws one relationship that is not hidden (E1 to E11).
fn relationship<'a>(
    element: SacmElement<'a>,
    placeable: &HashMap<&'a str, SacmKind>,
    placeholders: &HashSet<&'a str>,
    relationships: &HashSet<&'a str>,
    reach: &mut HashMap<&'a str, Reach>,
    builder: &mut Builder,
) {
    let id = element.id().as_str();
    let kind = element.kind();

    // E10.
    if matches!(
        kind,
        SacmKind::AssertedArtifactSupport | SacmKind::AssertedArtifactContext
    ) {
        builder.unplaced(id, Code::ArtifactRelationshipNotDrawn);
        return;
    }

    // The style: E5 for a counter, then E5a or E9 for a defeated or cited
    // relationship, then E1 to E4. Every code whose condition holds is sent,
    // whichever style won.
    let declaration = element
        .assertion_declaration()
        .unwrap_or(AssertionDeclaration::Asserted);
    let defeated = declaration == AssertionDeclaration::Defeated;
    let style = if element.is_counter() {
        Style::Counter
    } else if defeated || element.is_citation() {
        Style::NeutralInGsnDirection
    } else {
        Style::Live
    };
    if element.is_citation() {
        builder.finding(Code::Citation, id, None);
    }
    if defeated {
        builder.finding(Code::Defeated, id, None);
    } else if declaration != AssertionDeclaration::Asserted {
        builder.finding(Code::RelationshipDeclarationNotDrawn, id, None);
    }
    // R13, E11.
    if element.is_abstract() {
        builder.finding(Code::UninstantiatedNotDrawn, id, None);
    }

    // E7: the ends that can be placed. E5: a challenge may point at another
    // relationship, though not at itself.
    let mut target_is_relationship = false;
    let target = match element.target() {
        Some(target) if placeable.contains_key(target.as_str()) => Some(target.as_str()),
        Some(target)
            if style == Style::Counter
                && target.as_str() != id
                && relationships.contains(target.as_str()) =>
        {
            target_is_relationship = true;
            Some(target.as_str())
        }
        Some(target) => {
            builder.finding(Code::EndNotPlaced, id, Some(target.as_str()));
            None
        }
        None => {
            builder.finding(Code::EndNotPlaced, id, None);
            None
        }
    };
    // A source named twice is one premise, and one edge.
    let mut sources: Vec<&'a str> = Vec::new();
    if element.sources().is_empty() {
        builder.finding(Code::EndNotPlaced, id, None);
    }
    let mut seen: HashSet<&'a str> = HashSet::new();
    for source in element.sources() {
        let source = source.as_str();
        if !seen.insert(source) {
            continue;
        }
        if placeable.contains_key(source) {
            sources.push(source);
        } else if placeholders.contains(source) {
            // R16: a Strategy with no Goal yet. The placeholder has its own code.
        } else {
            builder.finding(Code::EndNotPlaced, id, Some(source));
        }
    }

    // E1 and E8: an inference's Strategy. A reasoning anywhere else, on a
    // counter inference (E5) or on another kind of relationship, is not linked.
    let strategy = element.reasoning().and_then(|reasoning| {
        let routable = kind == SacmKind::AssertedInference
            && style != Style::Counter
            && placeable.get(reasoning.as_str()) == Some(&SacmKind::ArgumentReasoning);
        if routable {
            Some(reasoning.as_str())
        } else {
            builder.finding(Code::EndNotPlaced, id, Some(reasoning.as_str()));
            None
        }
    });

    let edge_kind = match (style, kind) {
        (Style::Live, SacmKind::AssertedContext) => EdgeKind::InContextOf,
        (Style::Live, _) => EdgeKind::SupportedBy,
        (Style::Counter, _) => EdgeKind::Challenge,
        (Style::NeutralInGsnDirection, _) => EdgeKind::Neutral,
    };
    let edges_before = builder.edges.len();
    if let Some(target) = target {
        if style == Style::Counter {
            for source in &sources {
                builder.edge(source, target, edge_kind, id);
                if let Some(edge) = builder.edges.last_mut() {
                    edge.to_relationship = target_is_relationship;
                }
            }
        } else if let Some(strategy) = strategy {
            builder.edge(target, strategy, edge_kind, id);
            for source in &sources {
                builder.edge(strategy, source, edge_kind, id);
            }
        } else {
            for source in &sources {
                builder.edge(target, source, edge_kind, id);
            }
        }
    }
    if builder.edges.len() == edges_before {
        if let Some(target) = target.filter(|_| target_is_relationship) {
            builder
                .edgeless_challenges
                .push((id.to_owned(), target.to_owned()));
        }
        if !element.meta_claims().is_empty() {
            builder.finding(Code::MetaClaimNotDrawn, id, None);
        }
        builder.unplaced(id, Code::EndNotPlaced);
        return;
    }
    // E11: an ACP on a line GSN puts one on (Table 1:5-1); on a challenge or
    // a line drawn neutral, the link is not drawn.
    if !element.meta_claims().is_empty() {
        if style == Style::Live {
            for claim in element.meta_claims() {
                builder
                    .acp_links
                    .push((id.to_owned(), claim.as_str().to_owned()));
            }
        } else {
            builder.finding(Code::MetaClaimNotDrawn, id, None);
        }
    }

    // R9: counter-evidence is a Solution. It reaches nothing else: the
    // target of a challenge is not reached by it.
    if style == Style::Counter && kind == SacmKind::AssertedEvidence {
        for source in &sources {
            reach.entry(source).or_default().counter_evidence_source = true;
        }
    }
    // Reached: only live relationships count.
    if style == Style::Live {
        if let Some(target) = target {
            reach.entry(target).or_default().other = true;
        }
        for source in sources {
            let entry = reach.entry(source).or_default();
            match kind {
                SacmKind::AssertedEvidence => entry.evidence_source = true,
                SacmKind::AssertedContext => entry.context_source = true,
                _ => entry.other = true,
            }
        }
    }
}

/// Draws one node (R1 to R13) with its text (T1 to T3).
fn node(element: SacmElement<'_>, reached: Reach, language: &str, builder: &mut Builder) {
    let id = element.id().as_str();
    let declaration = element.assertion_declaration();

    // R12, then R1, then R2 to R11.
    let (kind, undeveloped) = if element.is_citation() {
        builder.finding(Code::Citation, id, None);
        (NodeKind::Neutral, false)
    } else {
        let recorded = match recorded_type(element) {
            Recorded::None => None,
            Recorded::Kind(kind) if agrees(kind, element.kind(), declaration) => Some(kind),
            Recorded::Kind(_) | Recorded::Unusable => {
                builder.finding(Code::RecordedTypeNotUsed, id, None);
                None
            }
        };
        match recorded {
            Some(kind) => (
                NodeKind::from_element_kind(kind),
                kind == ElementKind::Goal
                    && declaration == Some(AssertionDeclaration::NeedsSupport),
            ),
            None => by_class(element, declaration, reached, builder),
        }
    };

    // R13, E11.
    if element.is_abstract() {
        builder.finding(Code::UninstantiatedNotDrawn, id, None);
    }
    if !element.meta_claims().is_empty() {
        builder.finding(Code::MetaClaimNotDrawn, id, None);
    }
    // E12.
    if element.structure().is_some() {
        builder.finding(Code::StructureNotDrawn, id, None);
    }

    builder.nodes.push(Node {
        id: id.to_owned(),
        elsewhere: None,
        kind,
        undeveloped,
        identifier: tagged_value(element, GSN_IDENTIFIER_KEY)
            .filter(|identifier| !identifier.is_empty())
            .unwrap_or(id)
            .to_owned(),
        name: element
            .name()
            .and_then(|name| name.content())
            .map(str::to_owned),
        statement: element
            .descriptions()
            .next()
            .and_then(|description| description.content())
            .and_then(|content| in_language(content.values(), language))
            .map(str::to_owned),
    });
}

/// R2 to R11: the kind from the element's class, declaration and reach.
fn by_class(
    element: SacmElement<'_>,
    declaration: Option<AssertionDeclaration>,
    reached: Reach,
    builder: &mut Builder,
) -> (NodeKind, bool) {
    let id = element.id().as_str();
    match element.kind() {
        SacmKind::Claim => match declaration.unwrap_or(AssertionDeclaration::Asserted) {
            // R2, R6.
            AssertionDeclaration::Asserted => (NodeKind::Goal, false),
            // R3.
            AssertionDeclaration::NeedsSupport => (NodeKind::Goal, true),
            // R4.
            AssertionDeclaration::Assumed => (NodeKind::Assumption, false),
            // R5.
            AssertionDeclaration::Axiomatic => (NodeKind::Justification, false),
            // R7: `defeated`, `asCited`, and any literal a later SACM adds.
            _ => {
                builder.finding(Code::DeclarationHasNoShape, id, None);
                (NodeKind::Neutral, false)
            }
        },
        // R8.
        SacmKind::ArgumentReasoning => (NodeKind::Strategy, false),
        // R9, R10, R11. Evidence for, or against (counter-evidence), is a
        // Solution.
        _ => match (
            reached.evidence_source || reached.counter_evidence_source,
            reached.context_source,
            reached.other,
        ) {
            (true, false, false) => (NodeKind::Solution, false),
            (false, true, false) => (NodeKind::Context, false),
            _ => {
                builder.finding(Code::ArtifactReferenceNotPlaced, id, None);
                (NodeKind::Neutral, false)
            }
        },
    }
}

/// What an element's tagged values record about its GSN type (R1).
enum Recorded {
    None,
    Kind(ElementKind),
    /// A type this step does not draw, or tags that disagree.
    Unusable,
}

fn recorded_type(element: SacmElement<'_>) -> Recorded {
    let mut found: Vec<Option<ElementKind>> = Vec::new();
    for tag in element.tagged_values() {
        let Some(key) = first_content(tag.key()) else {
            continue;
        };
        let value = first_content(tag.content()).unwrap_or_default();
        match key {
            EXTENSION_TYPE_KEY => {
                if let Some(recorded) = extension_type(value) {
                    found.push(recorded);
                }
            }
            // Only `Justification` is a role the C++ application wrote.
            GSN_ROLE_KEY if value == "Justification" => {
                found.push(Some(ElementKind::Justification));
            }
            IMPORT_DECLARATION_KEY if value == "justification" => {
                found.push(Some(ElementKind::Justification));
            }
            _ => {}
        }
    }
    match found.split_first() {
        None => Recorded::None,
        Some((Some(first), rest)) if rest.iter().all(|kind| *kind == Some(*first)) => {
            Recorded::Kind(*first)
        }
        Some(_) => Recorded::Unusable,
    }
}

/// A Clark-notation type, `{namespace}Local`. `None` when it is no GSN type
/// at all, which R1 ignores; `Some(None)` for a GSN type this step does not
/// draw, such as an Away Goal.
fn extension_type(value: &str) -> Option<Option<ElementKind>> {
    let rest = value.strip_prefix('{')?;
    let (namespace, local) = rest.split_once('}')?;
    if !GSN_NAMESPACES.contains(&namespace) {
        return None;
    }
    Some(match local {
        "Goal" => Some(ElementKind::Goal),
        "Strategy" => Some(ElementKind::Strategy),
        "Solution" => Some(ElementKind::Solution),
        "Context" => Some(ElementKind::Context),
        "Assumption" => Some(ElementKind::Assumption),
        "Justification" => Some(ElementKind::Justification),
        _ => None,
    })
}

/// Whether a recorded type agrees with the element (R1).
fn agrees(kind: ElementKind, class: SacmKind, declaration: Option<AssertionDeclaration>) -> bool {
    use AssertionDeclaration as D;
    match kind {
        ElementKind::Goal => {
            class == SacmKind::Claim && matches!(declaration, Some(D::Asserted | D::NeedsSupport))
        }
        ElementKind::Assumption => class == SacmKind::Claim && declaration == Some(D::Assumed),
        ElementKind::Justification => class == SacmKind::Claim && declaration == Some(D::Axiomatic),
        ElementKind::Strategy => class == SacmKind::ArgumentReasoning,
        ElementKind::Solution | ElementKind::Context => class == SacmKind::ArtifactReference,
    }
}

/// The first value's content of a tagged value's key or content.
fn first_content(text: Option<&sacm::model::MultiLangString>) -> Option<&str> {
    text?.values().first()?.content()
}

/// The content of the tagged value with `key`, if the element has one.
pub(crate) fn tagged_value<'a>(element: SacmElement<'a>, key: &str) -> Option<&'a str> {
    tagged(element, key).and_then(|tag| first_content(tag.content()))
}

/// The tagged value with `key`, if the element has one.
pub(crate) fn tagged<'a>(element: SacmElement<'a>, key: &str) -> Option<SacmElement<'a>> {
    element
        .tagged_values()
        .find(|tag| first_content(tag.key()) == Some(key))
}

/// T3: the value in `language`, else one with the same primary subtag, else
/// the first.
fn in_language<'a>(values: &'a [sacm::model::LangString], language: &str) -> Option<&'a str> {
    in_language_index(values, language).and_then(|index| values[index].content())
}

/// T3: which of `values` is read, of those whose content is not empty.
pub(crate) fn in_language_index(
    values: &[sacm::model::LangString],
    language: &str,
) -> Option<usize> {
    let primary = |tag: &str| {
        tag.split('-')
            .next()
            .unwrap_or_default()
            .to_ascii_lowercase()
    };
    let wanted = primary(language);
    let values: Vec<(usize, &sacm::model::LangString)> = values
        .iter()
        .enumerate()
        .filter(|(_, value)| value.content().is_some_and(|content| !content.is_empty()))
        .collect();
    values
        .iter()
        .find(|(_, value)| {
            value
                .lang()
                .is_some_and(|lang| lang.eq_ignore_ascii_case(language))
        })
        .or_else(|| {
            values
                .iter()
                .find(|(_, value)| value.lang().is_some_and(|lang| primary(lang) == wanted))
        })
        .or_else(|| values.first())
        .map(|(index, _)| *index)
}

/// R16: whether `element` is the Claim a Strategy holds for its first Goal
/// (W2), which is not drawn. It is only while nothing about it says more than
/// that: a Claim with the placeholder tag and no other child, no text, and no
/// other declaration than `asserted`, that is the one source of one live
/// inference whose reasoning is a Strategy, and that nothing else refers to.
/// Anything else is drawn as what it is, so that nothing the file says is
/// hidden.
pub(crate) fn is_placeholder(document: &Document, element: SacmElement<'_>) -> bool {
    if element.kind() != SacmKind::Claim
        || tagged_value(element, PLACEHOLDER_KEY) != Some(PLACEHOLDER_VALUE)
    {
        return false;
    }
    let asserted = |declaration: Option<AssertionDeclaration>| {
        declaration.is_none_or(|declaration| declaration == AssertionDeclaration::Asserted)
    };
    let plain = asserted(element.assertion_declaration())
        && element
            .name()
            .and_then(|name| name.content())
            .unwrap_or_default()
            .is_empty()
        && element.tagged_values().count() == 1
        && element
            .children()
            .all(|child| child.kind() == SacmKind::TaggedValue)
        && !element.is_citation()
        && !element.is_abstract()
        && element.meta_claims().is_empty();
    if !plain {
        return false;
    }
    let referrers = document.referrers(element.id().as_str());
    let [referrer] = referrers.as_slice() else {
        return false;
    };
    let inference = referrer.holder();
    referrer.end() == sacm::model::ReferenceEnd::Source
        && inference.sources().len() == 1
        && can_hold_placeholder(document, inference)
}

/// R16: whether `inference` is one whose one source R16 would leave undrawn
/// as a placeholder: a live `AssertedInference`, asserted and not a counter,
/// cited or abstract, not hidden, with a target and a drawn
/// `ArgumentReasoning` as its reasoning. W20 holds a place only in such an
/// inference, so that the placeholder it adds is never drawn.
pub(crate) fn can_hold_placeholder(document: &Document, inference: SacmElement<'_>) -> bool {
    inference.kind() == SacmKind::AssertedInference
        && inference.target().is_some()
        && !inference.is_counter()
        && !inference.is_citation()
        && !inference.is_abstract()
        && inference
            .assertion_declaration()
            .is_none_or(|declaration| declaration == AssertionDeclaration::Asserted)
        && !is_hidden(inference)
        && inference
            .reasoning()
            .and_then(|reasoning| document.get(reasoning.as_str()))
            .is_some_and(|reasoning| {
                reasoning.kind() == SacmKind::ArgumentReasoning && !is_hidden(reasoning)
            })
}

/// The element kinds D4 accounts for: every kind of SACM's Argumentation
/// package but a plain `ArgumentPackage`, which is a container. A kind a later
/// libSACM adds is included, and is unplaced by R14 until a rule names it.
pub fn is_argument_element(kind: SacmKind) -> bool {
    kind.package() == MetamodelPackage::Argumentation && kind != SacmKind::ArgumentPackage
}

fn is_node_kind(kind: SacmKind) -> bool {
    matches!(
        kind,
        SacmKind::Claim | SacmKind::ArgumentReasoning | SacmKind::ArtifactReference
    )
}

/// The relationships an edge is drawn for (E1 to E9; not E10).
fn is_edge_kind(kind: SacmKind) -> bool {
    matches!(
        kind,
        SacmKind::AssertedInference | SacmKind::AssertedEvidence | SacmKind::AssertedContext
    )
}

fn is_relationship(kind: SacmKind) -> bool {
    matches!(
        kind,
        SacmKind::AssertedInference
            | SacmKind::AssertedEvidence
            | SacmKind::AssertedContext
            | SacmKind::AssertedArtifactSupport
            | SacmKind::AssertedArtifactContext
    )
}

/// R14 and R15: an argument element that is neither a node nor a
/// relationship (a group, an interface, a binding, or a kind no rule names), or
/// one inside an interface or binding at any depth, of an argument package or
/// of an assurance case package.
fn is_hidden(element: SacmElement<'_>) -> bool {
    let kind = element.kind();
    if is_argument_element(kind) && !is_node_kind(kind) && !is_relationship(kind) {
        return true;
    }
    let mut parent = element.parent();
    while let Some(ancestor) = parent {
        if matches!(
            ancestor.kind(),
            SacmKind::ArgumentPackageInterface
                | SacmKind::ArgumentPackageBinding
                | SacmKind::AssuranceCasePackageInterface
                | SacmKind::AssuranceCasePackageBinding
        ) {
            return true;
        }
        parent = ancestor.parent();
    }
    false
}

/// The SACM id of the argument package the element `id` is held in,
/// nearest first; `None` when it is in none.
pub fn argument_package_of(document: &Document, id: &str) -> Option<String> {
    document
        .get(id)
        .and_then(package_of)
        .map(|package| package.id().as_str().to_owned())
}

/// The argument package an element is held in, nearest first.
fn package_of(element: SacmElement<'_>) -> Option<SacmElement<'_>> {
    let mut holder = element.parent();
    while let Some(container) = holder {
        if container.kind() == SacmKind::ArgumentPackage {
            return Some(container);
        }
        holder = container.parent();
    }
    None
}

/// The diagram of the argument package `package` (A5 and A6 of the ACP
/// plan, `docs/roadmap/milestone-2-acps.md`): its nodes, every edge with an
/// end among them, and the nodes of other packages those edges reach, each
/// with the name of its package. A challenge of a relationship comes with
/// that relationship's edges. `None` when `package` is no argument package
/// of `document`.
///
/// Nothing is placed that the whole view does not place, and nothing of the
/// document changes: the package's name on a node is of the diagram.
pub fn of_package(document: &Document, whole: &View, package: &str) -> Option<View> {
    let shown = document.get(package)?;
    if shown.kind() != SacmKind::ArgumentPackage {
        return None;
    }
    let holder = |id: &str| document.get(id).and_then(package_of);
    let own: HashSet<&str> = whole
        .nodes
        .iter()
        .filter(|node| holder(&node.id).is_some_and(|found| found.id().as_str() == package))
        .map(|node| node.id.as_str())
        .collect();
    // The edges with an end among the package's nodes, then the challenges
    // of the relationships those edges draw, and of those, to any depth.
    let mut kept: Vec<bool> = whole
        .edges
        .iter()
        .map(|edge| {
            own.contains(edge.from.as_str())
                || (!edge.to_relationship && own.contains(edge.to.as_str()))
        })
        .collect();
    loop {
        let drawn: HashSet<&str> = whole
            .edges
            .iter()
            .zip(&kept)
            .filter(|(_, kept)| **kept)
            .map(|(edge, _)| edge.relationship.as_str())
            .collect();
        // What a kept challenge points at, so it has a line to point at.
        let challenged: HashSet<&str> = whole
            .edges
            .iter()
            .zip(&kept)
            .filter(|(edge, kept)| **kept && edge.to_relationship)
            .map(|(edge, _)| edge.to.as_str())
            .collect();
        let mut grew = false;
        for (edge, kept) in whole.edges.iter().zip(kept.iter_mut()) {
            let wanted = (edge.to_relationship && drawn.contains(edge.to.as_str()))
                || challenged.contains(edge.relationship.as_str());
            if !*kept && wanted {
                *kept = true;
                grew = true;
            }
        }
        if !grew {
            break;
        }
    }
    let edges: Vec<Edge> = whole
        .edges
        .iter()
        .zip(&kept)
        .filter(|(_, kept)| **kept)
        .map(|(edge, _)| edge.clone())
        .collect();
    let reached: HashSet<&str> = edges
        .iter()
        .flat_map(|edge| {
            let to = (!edge.to_relationship).then_some(edge.to.as_str());
            std::iter::once(edge.from.as_str()).chain(to)
        })
        .collect();
    let nodes = whole
        .nodes
        .iter()
        .filter(|node| own.contains(node.id.as_str()) || reached.contains(node.id.as_str()))
        .map(|node| {
            let mut node = node.clone();
            if !own.contains(node.id.as_str()) {
                node.elsewhere = holder(&node.id).map(|found| Elsewhere {
                    package: found.id().as_str().to_owned(),
                    name: found
                        .name()
                        .and_then(|name| name.content())
                        .filter(|name| !name.trim().is_empty())
                        .unwrap_or(found.id().as_str())
                        .to_owned(),
                });
            }
            node
        })
        .collect::<Vec<_>>();
    let ids: HashSet<&str> = nodes
        .iter()
        .map(|node| node.id.as_str())
        .chain(edges.iter().map(|edge| edge.relationship.as_str()))
        .collect();
    let acps: Vec<Acp> = whole
        .acps
        .iter()
        .filter(|acp| {
            edges.iter().any(|edge| {
                edge.relationship == acp.relationship && edge.from == acp.from && edge.to == acp.to
            })
        })
        .cloned()
        .collect();
    let descriptions = whole
        .descriptions
        .iter()
        .filter(|node| acps.iter().any(|acp: &Acp| acp.claim == node.id))
        .cloned()
        .collect();
    Some(View {
        acps,
        descriptions,
        findings: whole
            .findings
            .iter()
            .filter(|finding| ids.contains(finding.element.as_str()))
            .cloned()
            .collect(),
        unplaced: whole
            .unplaced
            .iter()
            .filter(|item| holder(&item.id).is_some_and(|found| found.id().as_str() == package))
            .cloned()
            .collect(),
        nodes,
        edges,
    })
}

#[cfg(test)]
mod tests {
    use super::{Code, in_language};
    use sacm::model::LangString;
    use std::collections::HashSet;

    #[test]
    fn codes_are_distinct_and_numbered_in_order() {
        let strings: Vec<&str> = Code::ALL.iter().map(|code| code.as_str()).collect();
        let distinct: HashSet<&&str> = strings.iter().collect();
        assert_eq!(distinct.len(), Code::ALL.len());
        for (index, code) in strings.iter().enumerate() {
            assert_eq!(*code, format!("AF-VIEW-{:03}", index + 1));
        }
    }

    #[test]
    fn t3_prefers_the_language_then_its_primary_subtag_then_the_first() {
        let values = [
            LangString::new("first"),
            LangString::new("English").with_lang("en"),
            LangString::new("日本語").with_lang("ja-JP"),
        ];
        assert_eq!(in_language(&values, "en"), Some("English"));
        assert_eq!(in_language(&values, "EN"), Some("English"));
        assert_eq!(in_language(&values, "ja"), Some("日本語"));
        assert_eq!(in_language(&values, "sv"), Some("first"));
        assert_eq!(in_language(&[], "en"), None);
    }
}
