#pragma once

#include <string_view>

// =============================================================================
// GSN pattern tagged-value keys (ADR-0006)
// -----------------------------------------------------------------------------
// GSN argument patterns (GSN Community Standard v3, Argument Pattern Extension)
// are persisted as abstract SACM ArgumentPackages. SACM has no dedicated
// pattern metamodel, so GSN-specific pattern notation and the pattern
// definition are carried as namespaced SACM TaggedValue entries (clause 8.10).
//
// These keys are the single source of truth for those tagged values and are
// shared by the parser, serializer, core pattern model, and the app layer.
// Never duplicate the literal strings elsewhere.
//
// Layer note: this header lives in `sacm` and contains data only (no UI / i18n
// dependency). The `ui` layer translates display labels separately; the raw
// keys and stored values stay in English.
// =============================================================================

namespace sacm::pattern_keys {

// ----- Package classification -----

// On the ArgumentPackage: marks the package as a GSN pattern definition. A
// package is a pattern only when it is abstract AND carries this tagged value
// (see ADR-0006); never infer "pattern" from the name or tree location alone.
inline constexpr std::string_view kViewKind = "assuranceforge.view.kind";
inline constexpr std::string_view kViewKindPatternValue = "gsn-pattern";

// On the ArgumentPackage: user-facing unique pattern identifier.
inline constexpr std::string_view kPatternIdentifier = "assuranceforge.gsn.pattern.identifier";

// ----- Element abstraction -----

// On a GSN element: the visible "uninstantiated" decorator. Distinct from
// SACMElement.isAbstract (every entity in a pattern is abstract; only selected
// elements visibly carry the uninstantiated decorator). Value: "true".
inline constexpr std::string_view kUninstantiated = "assuranceforge.gsn.pattern.uninstantiated";

// ----- Relationship abstraction (optionality / multiplicity) -----

// On a SupportedBy / InContextOf relationship: the pattern operator applied to
// the ordinary GSN relationship. The underlying relationship type is unchanged.
inline constexpr std::string_view kRelationOperator = "assuranceforge.gsn.pattern.operator";
inline constexpr std::string_view kOperatorOptional = "optional";
inline constexpr std::string_view kOperatorMultiplicity = "multiplicity";

// Structured multiplicity cardinality. Minimum/maximum carry a bound token
// (an integer, a parameter name, or kUnbounded); display preserves the
// author's expression (e.g. "1..*", "n", "2..5").
inline constexpr std::string_view kCardinalityMinimum = "assuranceforge.gsn.pattern.cardinality.minimum";
inline constexpr std::string_view kCardinalityMaximum = "assuranceforge.gsn.pattern.cardinality.maximum";
inline constexpr std::string_view kCardinalityDisplay = "assuranceforge.gsn.pattern.cardinality.display";

// Bound token meaning "unbounded" (the GSN "*"). Stored canonically as a word
// so cardinality bounds never collide with a literal numeric value.
inline constexpr std::string_view kUnbounded = "unbounded";

// ----- Choice groups -----

// On each member relationship of a choice group: the owning group id (a uuid).
// The choice diamond itself is never a SACM element (see ADR-0006).
inline constexpr std::string_view kChoiceGroup = "assuranceforge.gsn.pattern.choice.group";
inline constexpr std::string_view kChoiceCardinalityMinimum =
    "assuranceforge.gsn.pattern.choice.cardinality.minimum";
inline constexpr std::string_view kChoiceCardinalityMaximum =
    "assuranceforge.gsn.pattern.choice.cardinality.maximum";
inline constexpr std::string_view kChoiceCardinalityDisplay =
    "assuranceforge.gsn.pattern.choice.cardinality.display";

// ----- Pattern definition sections (on the ArgumentPackage) -----
//
// Name maps to ArgumentPackage.name and the general summary to
// ArgumentPackage.description; the remaining GSN-standard sections are stored
// as these tagged values.
inline constexpr std::string_view kAliases = "assuranceforge.gsn.pattern.aliases";
inline constexpr std::string_view kIntent = "assuranceforge.gsn.pattern.intent";
inline constexpr std::string_view kMotivation = "assuranceforge.gsn.pattern.motivation";
inline constexpr std::string_view kParticipants = "assuranceforge.gsn.pattern.participants";
inline constexpr std::string_view kCollaboration = "assuranceforge.gsn.pattern.collaboration";
inline constexpr std::string_view kApplicability = "assuranceforge.gsn.pattern.applicability";
inline constexpr std::string_view kConsequences = "assuranceforge.gsn.pattern.consequences";
inline constexpr std::string_view kImplementation = "assuranceforge.gsn.pattern.implementation";
inline constexpr std::string_view kExamples = "assuranceforge.gsn.pattern.examples";
inline constexpr std::string_view kKnownUses = "assuranceforge.gsn.pattern.knownUses";
inline constexpr std::string_view kRelatedPatterns = "assuranceforge.gsn.pattern.relatedPatterns";

// Canonical boolean value written for flag-style tagged values.
inline constexpr std::string_view kBooleanTrue = "true";

} // namespace sacm::pattern_keys
