#pragma once

#include "core/sacm_model.h"

#include <string>
#include <vector>

namespace core {

// Which GSN element a SACM element presents as.
//
// The classification is *intrinsic*: it is derived from the element alone and
// never from how the graph happens to wire it, because judging that wiring is
// exactly what the checker below exists to do. Classifying a node by its
// position would make every structure self-consistent by construction.
//
// `ArtifactBacked` is deliberately not split into Solution and Context. GSN
// Metamodel v2.2 §2.1: a Context "may be either of type Axiomatic Claim or of
// type ArtifactReference. The type of the Context element is determined by the
// nature of its content" — and no property distinguishes them. Picking one
// would reinterpret the argument; see docs/sacm/sacm-gsn-mapping.md.
enum class GsnElementKind {
    Goal,
    Strategy,
    Assumption,
    Justification,
    ArtifactBacked, // a Solution, or a Context stored as an ArtifactReference
    Relationship,
    Unrecognized, // preserved foreign content — never judged
};

GsnElementKind GsnKindOf(const parser::SacmElement& element);

// GSN v3 Part 1: `SupportedBy` runs from a Goal or a Strategy to the element
// that supports it. Nothing else can be the supported end — a Solution is a
// leaf, and Context, Assumption and Justification are attached, not supported.
bool GsnCanBeSupported(GsnElementKind kind);

// GSN v3 Part 1: `InContextOf` runs from a Goal or a Strategy to a Context,
// Assumption or Justification. Only those two element types declare a context.
bool GsnCanBeContextualized(GsnElementKind kind);

// A GSN v3 well-formedness rule. Each maps to one row of
// docs/gsn/gsn-v3-conformance-matrix.md, and `GsnRequirementId` is what makes a
// diagnostic traceable back to the normative requirement it enforces
// (GSN3-VAL-001).
enum class GsnRule {
    UnresolvedEndpoint,           // GSN3-CORE-007
    ChallengeTargetUnresolved,    // GSN3-DIA-003
    SupportedElementIsALeaf,      // GSN3-CORE-015
    ContextualizedElementIsALeaf, // GSN3-CORE-015
    StrategyUsedAsAssertion,      // GSN3-CORE-002
    EvidenceSourceIsNotASolution, // GSN3-CORE-003
    DuplicateNotationIdentifier,  // GSN3-CORE-010
    UndevelopedElementHasSupport, // GSN3-CORE-009
};

struct GsnFinding {
    GsnRule rule = GsnRule::UnresolvedEndpoint;
    // The element the finding is about, and the anchor a reader should be taken
    // to. Always an element that exists in the case, or empty when the defect
    // leaves nothing to navigate to.
    std::string element_id;
    // A second existing element the finding involves — the peer holding a
    // duplicated identifier, for instance. Empty when there is none.
    std::string related_id;
    // The relationship the defect was found on. Empty for defects that are not
    // about a relationship.
    std::string relationship_id;
    // An offending literal that is not an element id: an unresolved reference,
    // or the identifier two elements share. Empty when not applicable.
    std::string detail;
};

// Row id in docs/gsn/gsn-v3-conformance-matrix.md, e.g. "GSN3-CORE-015".
const char* GsnRequirementId(GsnRule rule);

// Stable machine token naming the rule, e.g. "SupportedElementIsALeaf". Used as
// the problem type, so it is part of the persisted problem id — do not rename
// one without accepting that stored selections move.
const char* GsnRuleName(GsnRule rule);

// Checks the case against the GSN v3 Core and Dialectic structural rules that
// are decidable from the model. Circular support is checked separately by
// `FindSupportCycles` (GSN3-CORE-014).
//
// The checker reports only what is *definitely* wrong. Anything it cannot
// classify — a foreign element type, a relationship endpoint whose GSN role is
// undecidable — is left alone rather than guessed at, because a false structural
// error against an imported safety argument is worse than a missing one.
//
// Argument *quality* (a topic-shaped goal, an unsupported leaf, a strategy that
// restates its parent) is deliberately absent: those are judgement calls, and
// they belong in the SCCG guideline catalog where a human resolves them.
//
// Findings are ordered deterministically, so the same model always produces the
// same list regardless of document order.
std::vector<GsnFinding> CheckGsnWellFormedness(const parser::AssuranceCase& model);

} // namespace core
