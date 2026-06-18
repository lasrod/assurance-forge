#include "core/pattern_model.h"

#include "core/terminology_internal.h"
#include "sacm/pattern_keys.h"

#include <algorithm>
#include <cctype>

namespace core {
namespace {

namespace keys = sacm::pattern_keys;

bool IsAllDigits(const std::string& token) {
    if (token.empty())
        return false;
    return std::all_of(token.begin(), token.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

// Writes `value` under `key` when non-empty, otherwise removes the tagged value.
void SetOrClearTaggedValue(sacm::SacmElement& element, std::string_view key, const std::string& value) {
    if (value.empty())
        RemoveTaggedValue(element, key);
    else
        SetTaggedValue(element, key, value);
}

} // namespace

// ----- Generic tagged-value helpers -----

std::optional<std::string> GetTaggedValue(const sacm::SacmElement& element, std::string_view key) {
    for (const sacm::TaggedValue& tag : element.taggedValues) {
        if (key == tag.key)
            return tag.value;
    }
    return std::nullopt;
}

bool HasTaggedValue(const sacm::SacmElement& element, std::string_view key) {
    return GetTaggedValue(element, key).has_value();
}

void SetTaggedValue(sacm::SacmElement& element, std::string_view key, const std::string& value) {
    for (sacm::TaggedValue& tag : element.taggedValues) {
        if (key == tag.key) {
            tag.value = value;
            return;
        }
    }
    sacm::TaggedValue tag;
    tag.key = std::string(key);
    tag.value = value;
    element.taggedValues.push_back(std::move(tag));
}

bool RemoveTaggedValue(sacm::SacmElement& element, std::string_view key) {
    const auto before = element.taggedValues.size();
    element.taggedValues.erase(
        std::remove_if(element.taggedValues.begin(), element.taggedValues.end(),
                       [key](const sacm::TaggedValue& tag) { return key == tag.key; }),
        element.taggedValues.end());
    return element.taggedValues.size() != before;
}

// ----- Package classification -----

bool IsPatternPackage(const sacm::ArgumentPackage& package) {
    if (!package.isAbstract)
        return false;
    const std::optional<std::string> view_kind = GetTaggedValue(package, keys::kViewKind);
    return view_kind.has_value() && keys::kViewKindPatternValue == *view_kind;
}

bool IsPatternIdentifierUnique(const sacm::AssuranceCasePackage& package,
                               const std::string& identifier,
                               const std::string& exclude_package_id) {
    if (identifier.empty())
        return true;
    for (const sacm::ArgumentPackage& candidate : package.argumentPackages) {
        if (!candidate.id.empty() && candidate.id == exclude_package_id)
            continue;
        if (!IsPatternPackage(candidate))
            continue;
        const std::optional<std::string> existing = GetTaggedValue(candidate, keys::kPatternIdentifier);
        if (existing.has_value() && *existing == identifier)
            return false;
    }
    return true;
}

PatternCreateResult CreatePatternPackageWithIds(sacm::AssuranceCasePackage& package,
                                                const std::string& name,
                                                const std::string& identifier,
                                                const std::string& description,
                                                const std::string& forced_id,
                                                const std::string& forced_gid) {
    PatternCreateResult result;
    if (name.empty()) {
        result.error = "Pattern name is required.";
        return result;
    }
    if (identifier.empty()) {
        result.error = "Pattern identifier is required.";
        return result;
    }
    if (!IsPatternIdentifierUnique(package, identifier)) {
        result.error = "Pattern identifier '" + identifier + "' is already used by another pattern.";
        return result;
    }

    sacm::ArgumentPackage pattern;
    pattern.id = forced_id.empty() ? core::detail::GenerateUniqueId(package, "PAT") : forced_id;
    pattern.gid = forced_gid.empty() ? core::detail::GenerateUniqueGid(package, pattern.id) : forced_gid;
    pattern.isAbstract = true;
    pattern.name = name;
    pattern.name_ml.set("en", name);
    pattern.description = description;
    if (!description.empty())
        pattern.description_ml.set("en", description);
    SetTaggedValue(pattern, keys::kViewKind, std::string(keys::kViewKindPatternValue));
    SetTaggedValue(pattern, keys::kPatternIdentifier, identifier);

    result.package_id = pattern.id;
    result.package_gid = pattern.gid;
    package.argumentPackages.push_back(std::move(pattern));
    result.success = true;
    return result;
}

PatternCreateResult CreatePatternPackage(sacm::AssuranceCasePackage& package,
                                         const std::string& name,
                                         const std::string& identifier,
                                         const std::string& description) {
    return CreatePatternPackageWithIds(package, name, identifier, description, {}, {});
}

// ----- Element abstraction -----

bool IsElementUninstantiated(const sacm::SacmElement& element) {
    const std::optional<std::string> value = GetTaggedValue(element, keys::kUninstantiated);
    return value.has_value() && keys::kBooleanTrue == *value;
}

void SetElementUninstantiated(sacm::SacmElement& element, bool uninstantiated) {
    if (uninstantiated)
        SetTaggedValue(element, keys::kUninstantiated, std::string(keys::kBooleanTrue));
    else
        RemoveTaggedValue(element, keys::kUninstantiated);
}

// ----- Cardinality -----

std::string PatternBoundToToken(const PatternBound& bound) {
    switch (bound.kind) {
    case PatternBound::Kind::Integer:
        return std::to_string(bound.integerValue);
    case PatternBound::Kind::Parameter:
        return bound.parameterName;
    case PatternBound::Kind::Unbounded:
        return std::string(keys::kUnbounded);
    }
    return std::string(keys::kUnbounded);
}

PatternBound PatternBoundFromToken(const std::string& token) {
    PatternBound bound;
    if (token == keys::kUnbounded || token == "*") {
        bound.kind = PatternBound::Kind::Unbounded;
        return bound;
    }
    if (IsAllDigits(token)) {
        bound.kind = PatternBound::Kind::Integer;
        bound.integerValue = std::stoi(token);
        return bound;
    }
    bound.kind = PatternBound::Kind::Parameter;
    bound.parameterName = token;
    return bound;
}

bool ValidateCardinality(const PatternCardinality& cardinality, std::string& out_error) {
    const PatternBound& lo = cardinality.minimum;
    const PatternBound& hi = cardinality.maximum;

    if (lo.kind == PatternBound::Kind::Integer && lo.integerValue < 0) {
        out_error = "Cardinality minimum must not be negative.";
        return false;
    }
    if (hi.kind == PatternBound::Kind::Integer && hi.integerValue < 0) {
        out_error = "Cardinality maximum must not be negative.";
        return false;
    }
    if (lo.kind == PatternBound::Kind::Integer && hi.kind == PatternBound::Kind::Integer &&
        lo.integerValue > hi.integerValue) {
        out_error = "Cardinality minimum must not be greater than maximum.";
        return false;
    }
    return true;
}

// ----- Relationship abstraction -----

PatternRelationshipData ReadPatternRelationshipData(const sacm::AssertedRelationship& relationship) {
    PatternRelationshipData data;

    const std::optional<std::string> op = GetTaggedValue(relationship, keys::kRelationOperator);
    if (op.has_value()) {
        if (keys::kOperatorOptional == *op)
            data.relationOperator = PatternRelationOperator::Optional;
        else if (keys::kOperatorMultiplicity == *op)
            data.relationOperator = PatternRelationOperator::Multiplicity;
    }

    if (data.relationOperator == PatternRelationOperator::Multiplicity) {
        PatternCardinality cardinality;
        if (const std::optional<std::string> lo = GetTaggedValue(relationship, keys::kCardinalityMinimum))
            cardinality.minimum = PatternBoundFromToken(*lo);
        if (const std::optional<std::string> hi = GetTaggedValue(relationship, keys::kCardinalityMaximum))
            cardinality.maximum = PatternBoundFromToken(*hi);
        if (const std::optional<std::string> disp = GetTaggedValue(relationship, keys::kCardinalityDisplay))
            cardinality.displayExpression = *disp;
        data.multiplicity = cardinality;
    }

    if (const std::optional<std::string> group = GetTaggedValue(relationship, keys::kChoiceGroup))
        data.choiceGroupId = *group;

    return data;
}

void WritePatternRelationshipData(sacm::AssertedRelationship& relationship, const PatternRelationshipData& data) {
    switch (data.relationOperator) {
    case PatternRelationOperator::None:
        RemoveTaggedValue(relationship, keys::kRelationOperator);
        break;
    case PatternRelationOperator::Optional:
        SetTaggedValue(relationship, keys::kRelationOperator, std::string(keys::kOperatorOptional));
        break;
    case PatternRelationOperator::Multiplicity:
        SetTaggedValue(relationship, keys::kRelationOperator, std::string(keys::kOperatorMultiplicity));
        break;
    }

    if (data.relationOperator == PatternRelationOperator::Multiplicity && data.multiplicity.has_value()) {
        SetTaggedValue(relationship, keys::kCardinalityMinimum, PatternBoundToToken(data.multiplicity->minimum));
        SetTaggedValue(relationship, keys::kCardinalityMaximum, PatternBoundToToken(data.multiplicity->maximum));
        SetOrClearTaggedValue(relationship, keys::kCardinalityDisplay, data.multiplicity->displayExpression);
    } else {
        RemoveTaggedValue(relationship, keys::kCardinalityMinimum);
        RemoveTaggedValue(relationship, keys::kCardinalityMaximum);
        RemoveTaggedValue(relationship, keys::kCardinalityDisplay);
    }

    if (data.choiceGroupId.has_value())
        SetTaggedValue(relationship, keys::kChoiceGroup, *data.choiceGroupId);
    else
        RemoveTaggedValue(relationship, keys::kChoiceGroup);
}

// ----- Choice groups -----

bool ValidateChoiceGroup(const PatternChoiceGroup& group, std::string& out_error) {
    if (group.sourceElement.empty()) {
        out_error = "Choice group has no source element.";
        return false;
    }
    if (group.relationshipType.empty()) {
        out_error = "Choice group has no relationship type.";
        return false;
    }
    if (group.alternatives.size() < 2) {
        out_error = "Choice group must contain at least two alternatives.";
        return false;
    }
    std::vector<std::string> sorted = group.alternatives;
    std::sort(sorted.begin(), sorted.end());
    if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) {
        out_error = "Choice group contains a duplicate alternative.";
        return false;
    }
    return ValidateCardinality(group.cardinality, out_error);
}

// ----- Pattern definition metadata -----

PatternDefinition ReadPatternDefinition(const sacm::ArgumentPackage& package) {
    PatternDefinition definition;
    definition.name = package.name;
    definition.summary = package.description;
    definition.identifier = GetTaggedValue(package, keys::kPatternIdentifier).value_or("");
    definition.aliases = GetTaggedValue(package, keys::kAliases).value_or("");
    definition.intent = GetTaggedValue(package, keys::kIntent).value_or("");
    definition.motivation = GetTaggedValue(package, keys::kMotivation).value_or("");
    definition.participants = GetTaggedValue(package, keys::kParticipants).value_or("");
    definition.collaboration = GetTaggedValue(package, keys::kCollaboration).value_or("");
    definition.applicability = GetTaggedValue(package, keys::kApplicability).value_or("");
    definition.consequences = GetTaggedValue(package, keys::kConsequences).value_or("");
    definition.implementation = GetTaggedValue(package, keys::kImplementation).value_or("");
    definition.examples = GetTaggedValue(package, keys::kExamples).value_or("");
    definition.knownUses = GetTaggedValue(package, keys::kKnownUses).value_or("");
    definition.relatedPatterns = GetTaggedValue(package, keys::kRelatedPatterns).value_or("");
    return definition;
}

void WritePatternDefinition(sacm::ArgumentPackage& package, const PatternDefinition& definition) {
    package.name = definition.name;
    package.description = definition.summary;
    SetOrClearTaggedValue(package, keys::kPatternIdentifier, definition.identifier);
    SetOrClearTaggedValue(package, keys::kAliases, definition.aliases);
    SetOrClearTaggedValue(package, keys::kIntent, definition.intent);
    SetOrClearTaggedValue(package, keys::kMotivation, definition.motivation);
    SetOrClearTaggedValue(package, keys::kParticipants, definition.participants);
    SetOrClearTaggedValue(package, keys::kCollaboration, definition.collaboration);
    SetOrClearTaggedValue(package, keys::kApplicability, definition.applicability);
    SetOrClearTaggedValue(package, keys::kConsequences, definition.consequences);
    SetOrClearTaggedValue(package, keys::kImplementation, definition.implementation);
    SetOrClearTaggedValue(package, keys::kExamples, definition.examples);
    SetOrClearTaggedValue(package, keys::kKnownUses, definition.knownUses);
    SetOrClearTaggedValue(package, keys::kRelatedPatterns, definition.relatedPatterns);
}

} // namespace core
