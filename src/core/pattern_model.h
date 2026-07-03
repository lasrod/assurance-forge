#pragma once

#include "sacm/sacm_model.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// =============================================================================
// GSN pattern domain model (ADR-0006)
// -----------------------------------------------------------------------------
// UI-independent representation of GSN argument-pattern abstractions and the
// pure read/write helpers that map them onto the namespaced SACM TaggedValue
// entries defined in sacm/pattern_keys.h.
//
// Patterns themselves are ordinary abstract SACM ArgumentPackages; this layer
// adds the GSN-specific notation that SACM has no direct feature for:
//   - element abstraction (uninstantiated decorator),
//   - relationship abstraction (optionality / multiplicity + cardinality),
//   - choice groups.
//
// Layer note: lives in `core`, depends only on `sacm`. No UI / ImGui / i18n.
// =============================================================================

namespace core {

// ----- Generic tagged-value helpers (operate on any SACM element) -----

// Returns the value of the first tagged value with `key`, or std::nullopt.
std::optional<std::string> GetTaggedValue(const sacm::SacmElement& element, std::string_view key);

bool HasTaggedValue(const sacm::SacmElement& element, std::string_view key);

// Upsert: sets the value of the existing tagged value with `key`, or appends a
// new one. Keeps a single entry per key.
void SetTaggedValue(sacm::SacmElement& element, std::string_view key, const std::string& value);

// Removes every tagged value with `key`. Returns true if any were removed.
bool RemoveTaggedValue(sacm::SacmElement& element, std::string_view key);

// ----- Package classification -----

// True when `package` is a GSN pattern definition: abstract AND carrying the
// `assuranceforge.view.kind = gsn-pattern` tagged value. Never classify from
// the name or tree location alone (ADR-0006).
bool IsPatternPackage(const sacm::ArgumentPackage& package);

// True when no pattern package in `package` already uses `identifier`. The
// package whose id equals `exclude_package_id` is ignored (so a rename can keep
// its own identifier). Empty identifier is treated as not-unique-friendly: it
// returns true (the caller validates non-empty separately).
bool IsPatternIdentifierUnique(const sacm::AssuranceCasePackage& package,
                               const std::string& identifier,
                               const std::string& exclude_package_id = {});

// ----- Pattern package creation -----

struct PatternCreateResult {
    bool success = false;
    std::string package_id;
    std::string package_gid;
    std::string error;
};

// Create an abstract ArgumentPackage classified as a GSN pattern (ADR-0006):
// isAbstract = true, `view.kind = gsn-pattern`, and the supplied identifier.
// `name` and `identifier` are required; `identifier` must be unique among
// existing pattern packages. The created package is appended to
// `package.argumentPackages` and its assigned (id, gid) are returned.
PatternCreateResult CreatePatternPackage(sacm::AssuranceCasePackage& package,
                                         const std::string& name,
                                         const std::string& identifier,
                                         const std::string& description);

// Replay-friendly variant: forces the (id, gid) when non-empty instead of
// generating them. Used by the audit replayer and the command Apply method so
// recorded history reproduces the exact identities.
PatternCreateResult CreatePatternPackageWithIds(sacm::AssuranceCasePackage& package,
                                                const std::string& name,
                                                const std::string& identifier,
                                                const std::string& description,
                                                const std::string& forced_id,
                                                const std::string& forced_gid);

// ----- Element abstraction (uninstantiated decorator) -----

bool IsElementUninstantiated(const sacm::SacmElement& element);
void SetElementUninstantiated(sacm::SacmElement& element, bool uninstantiated);

// ----- Cardinality -----

// A single cardinality bound. A bound is either a concrete integer, a named
// parameter resolved at instantiation, or unbounded (the GSN "*").
struct PatternBound {
    enum class Kind {
        Integer,
        Parameter,
        Unbounded,
    };

    Kind kind = Kind::Integer;
    int integerValue = 1;
    std::string parameterName;
};

// Structured lower/upper bounds plus the author's display expression. Keeping
// structured bounds (rather than only free-form text) is what lets later plans
// instantiate a pattern without re-parsing arbitrary display strings.
struct PatternCardinality {
    PatternBound minimum;
    PatternBound maximum;
    std::string displayExpression;
};

// Canonical token form of a bound: an integer ("5"), a parameter name ("n"),
// or "unbounded". Used both for tagged-value storage and equality in tests.
std::string PatternBoundToToken(const PatternBound& bound);

// Inverse of PatternBoundToToken. "unbounded"/"*" -> Unbounded; an all-digit
// token -> Integer; anything else -> Parameter (the token is the name).
PatternBound PatternBoundFromToken(const std::string& token);

// Validates a single cardinality: integer bounds must be non-negative, and when
// both bounds are integers the minimum must not exceed the maximum. Bounds that
// are parameters or unbounded skip the numeric ordering check. Returns true if
// valid; otherwise writes a human-readable reason into out_error.
bool ValidateCardinality(const PatternCardinality& cardinality, std::string& out_error);

// Parse a user-entered cardinality expression into a structured cardinality,
// preserving the original text as the display expression. "lo..hi" splits into
// bounds; a single token (e.g. "n", "3", "*") sets both bounds to that token.
// Each side is interpreted by PatternBoundFromToken.
PatternCardinality ParseCardinalityExpression(const std::string& expression);

// ----- Relationship abstraction -----

enum class PatternRelationOperator {
    None,
    Optional,
    Multiplicity,
};

// Pattern abstraction attached to a single SupportedBy / InContextOf
// relationship. The underlying SACM relationship type is unchanged; this is
// metadata only.
struct PatternRelationshipData {
    PatternRelationOperator relationOperator = PatternRelationOperator::None;
    // Present only when relationOperator == Multiplicity.
    std::optional<PatternCardinality> multiplicity;
    // Set when this relationship is a member of a choice group.
    std::optional<std::string> choiceGroupId;
};

PatternRelationshipData ReadPatternRelationshipData(const sacm::AssertedRelationship& relationship);

// Writes `data` onto `relationship`'s tagged values, removing any pattern
// operator/cardinality/choice tagged values that no longer apply.
void WritePatternRelationshipData(sacm::AssertedRelationship& relationship, const PatternRelationshipData& data);

// Find the asserted relationship with id `relationship_id` anywhere in `package`
// and apply `data` to it (optionality / multiplicity + cardinality). When the
// operator is Multiplicity the cardinality is validated first. Returns false
// with `out_error` when the relationship is not found or the cardinality is
// invalid; the package is left unchanged in that case.
bool SetRelationshipPatternData(sacm::AssuranceCasePackage& package,
                                const std::string& relationship_id,
                                const PatternRelationshipData& data,
                                std::string& out_error);

// ----- Choice groups -----

// A choice group: one solid diamond representing a choice across several
// alternative relationships sharing a source and relationship type. It is NOT a
// SACM element; it is reconstructed from the member relationships' tagged
// values.
struct PatternChoiceGroup {
    std::string id;
    std::string sourceElement;
    std::string relationshipType; // normalized GSN/SACM relationship local name
    std::vector<std::string> alternatives; // member relationship ids
    PatternCardinality cardinality;
};

// Validates the self-consistent invariants of a choice group that can be
// checked without the surrounding model: at least two alternatives, a source
// element, a relationship type, no duplicate alternative ids, and a valid
// cardinality. Cross-relationship checks (shared source/type across the actual
// relationships) are performed by the model validation pass. Returns true if
// valid; otherwise writes a human-readable reason into out_error.
bool ValidateChoiceGroup(const PatternChoiceGroup& group, std::string& out_error);

// Reconstruct every choice group present in `package` from the member
// relationships' tagged values (the group is never a SACM element — ADR-0006).
// Source element, relationship type, and cardinality are taken from the group's
// members.
std::vector<PatternChoiceGroup> ReconstructChoiceGroups(const sacm::AssuranceCasePackage& package);

// Gather the relationship ids that are candidates for a new choice group rooted
// at `source_element_id`: its structural (SupportedBy) child relationships of a
// single dominant type, when at least two such relationships exist and none is
// already in a choice group. Writes the shared relationship type to
// `out_relationship_type`. Returns an empty vector when no eligible set exists.
std::vector<std::string> GatherChoiceCandidateRelationships(const sacm::AssuranceCasePackage& package,
                                                            const std::string& source_element_id,
                                                            std::string& out_relationship_type);

struct ChoiceGroupCreateResult {
    bool success = false;
    std::string group_id;
    std::string error;
};

// Group `relationship_ids` into a choice group: validates they exist, number at
// least two, share one source element and one relationship type, and that none
// already belongs to a choice group; then writes the group id + cardinality
// tagged values onto each member. `forced_group_id` (when non-empty) is used
// instead of generating one, for deterministic replay.
ChoiceGroupCreateResult CreateChoiceGroupFromRelationships(sacm::AssuranceCasePackage& package,
                                                           const std::vector<std::string>& relationship_ids,
                                                           const PatternCardinality& cardinality,
                                                           const std::string& forced_group_id = {});

// Remove the choice group `group_id` by clearing the group/cardinality tagged
// values from its member relationships; the relationships themselves remain.
// Returns false with out_error when no member carries the group id.
bool RemoveChoiceGroup(sacm::AssuranceCasePackage& package, const std::string& group_id, std::string& out_error);

// Update the cardinality of an existing choice group on every member.
bool SetChoiceCardinality(sacm::AssuranceCasePackage& package,
                          const std::string& group_id,
                          const PatternCardinality& cardinality,
                          std::string& out_error);

// ----- Pattern definition metadata -----

// The GSN-standard pattern-definition sections. Name maps to
// ArgumentPackage.name and summary to ArgumentPackage.description; the rest are
// stored as namespaced tagged values.
struct PatternDefinition {
    std::string identifier;
    std::string name;
    std::string summary;
    std::string aliases;
    std::string intent;
    std::string motivation;
    std::string participants;
    std::string collaboration;
    std::string applicability;
    std::string consequences;
    std::string implementation;
    std::string examples;
    std::string knownUses;
    std::string relatedPatterns;
};

PatternDefinition ReadPatternDefinition(const sacm::ArgumentPackage& package);

// Writes the definition onto `package`: name/description scalars plus the
// section tagged values (empty sections clear their tagged value). Does not
// touch the `view.kind` classification tagged value.
void WritePatternDefinition(sacm::ArgumentPackage& package, const PatternDefinition& definition);

} // namespace core
