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
    AssertionDeclaration, Document, ElementKind as SacmKind, MetamodelPackage, SacmElement,
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
}

impl Code {
    /// Every code, in the order of the mapping record's table.
    pub const ALL: [Code; 13] = [
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
}

/// The tagged value that records an element's GSN type on import.
const EXTENSION_TYPE_KEY: &str = "sacm.import.extensionType";
/// The C++ application's tag for a Justification.
const GSN_ROLE_KEY: &str = "assuranceForge.gsn.role";
/// The legacy declaration literal the C++ library recorded when it normalized it.
const IMPORT_DECLARATION_KEY: &str = "sacm.import.assertionDeclaration";
/// The GSN identifier, apart from the SACM id (T1).
const GSN_IDENTIFIER_KEY: &str = "assuranceForge.gsn.identifier";
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
    // The elements a node is drawn for, and their SACM kind.
    let placeable: HashMap<&str, SacmKind> = elements
        .iter()
        .filter(|element| is_node_kind(element.kind()) && !hidden.contains(element.id().as_str()))
        .map(|element| (element.id().as_str(), element.kind()))
        .collect();

    let mut builder = Builder::default();
    let mut reach: HashMap<&str, Reach> = HashMap::new();

    for element in &elements {
        let id = element.id().as_str();
        if hidden.contains(id) {
            builder.unplaced(id, Code::KindNotDrawn);
        } else if is_relationship(element.kind()) {
            relationship(
                *element,
                &placeable,
                &relationships,
                &mut reach,
                &mut builder,
            );
        }
    }
    builder.drop_challenges_of_nothing();
    for element in &elements {
        let id = element.id().as_str();
        if placeable.contains_key(id) {
            let reached = reach.get(id).copied().unwrap_or_default();
            node(*element, reached, language, &mut builder);
        }
    }

    builder.finish(&order)
}

/// How the live relationships reach an element: only these count (the
/// mapping record's "Reached").
#[derive(Clone, Copy, Debug, Default)]
struct Reach {
    evidence_source: bool,
    context_source: bool,
    other: bool,
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
        }
    }
}

/// Draws one relationship that is not hidden (E1 to E11).
fn relationship<'a>(
    element: SacmElement<'a>,
    placeable: &HashMap<&'a str, SacmKind>,
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
    if !element.meta_claims().is_empty() {
        builder.finding(Code::MetaClaimNotDrawn, id, None);
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
        builder.unplaced(id, Code::EndNotPlaced);
        return;
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
        // R9, R10, R11.
        _ => match (
            reached.evidence_source,
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
    element
        .tagged_values()
        .find(|tag| first_content(tag.key()) == Some(key))
        .and_then(|tag| first_content(tag.content()))
}

/// T3: the value in `language`, else one with the same primary subtag, else
/// the first.
fn in_language<'a>(values: &'a [sacm::model::LangString], language: &str) -> Option<&'a str> {
    let primary = |tag: &str| {
        tag.split('-')
            .next()
            .unwrap_or_default()
            .to_ascii_lowercase()
    };
    let wanted = primary(language);
    let values: Vec<&sacm::model::LangString> = values
        .iter()
        .filter(|value| value.content().is_some_and(|content| !content.is_empty()))
        .collect();
    values
        .iter()
        .find(|value| {
            value
                .lang()
                .is_some_and(|lang| lang.eq_ignore_ascii_case(language))
        })
        .or_else(|| {
            values
                .iter()
                .find(|value| value.lang().is_some_and(|lang| primary(lang) == wanted))
        })
        .or_else(|| values.first())
        .and_then(|value| value.content())
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
