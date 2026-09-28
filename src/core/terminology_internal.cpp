#include "core/terminology_internal.h"

#include "core/string_utils.h"

#include <algorithm>

namespace core::detail {

namespace {

// Every element a unique id or gid must not collide with.
template <typename Visitor>
void ForEachIdentifiedElement(const sacm::AssuranceCasePackage& package, Visitor visit) {
    auto visit_terminology_package = [&](const sacm::TerminologyPackage& terminology_package) {
        visit(terminology_package);
        for (const auto& category : terminology_package.categories)
            visit(category);
        for (const auto& term : terminology_package.terms)
            visit(term);
        for (const auto& expression : terminology_package.expressions)
            visit(expression);
    };

    visit(package);
    for (const auto& terminology_package : package.terminologyPackages) {
        visit_terminology_package(terminology_package);
    }
    for (const auto& artifact_package : package.artifactPackages) {
        visit(artifact_package);
        for (const auto& artifact : artifact_package.artifacts)
            visit(artifact);
    }
    for (const auto& argument_package : package.argumentPackages) {
        visit(argument_package);
        for (const auto& terminology_package : argument_package.terminologyPackages)
            visit_terminology_package(terminology_package);
        for (const auto& claim : argument_package.claims)
            visit(claim);
        for (const auto& reasoning : argument_package.argumentReasonings)
            visit(reasoning);
        for (const auto& artifact_reference : argument_package.artifactReferences)
            visit(artifact_reference);
        for (const auto& relationship : argument_package.assertedInferences)
            visit(relationship);
        for (const auto& relationship : argument_package.assertedContexts)
            visit(relationship);
        for (const auto& relationship : argument_package.assertedEvidences)
            visit(relationship);
    }
}

} // namespace

std::unordered_set<std::string> CollectElementIds(const sacm::AssuranceCasePackage& package) {
    std::unordered_set<std::string> ids;
    ForEachIdentifiedElement(package, [&](const sacm::SacmElement& element) {
        if (!element.id.empty())
            ids.insert(element.id);
    });
    return ids;
}

std::unordered_set<std::string> CollectGids(const sacm::AssuranceCasePackage& package) {
    std::unordered_set<std::string> gids;
    ForEachIdentifiedElement(package, [&](const sacm::SacmElement& element) {
        if (!element.gid.empty())
            gids.insert(element.gid);
    });
    return gids;
}

std::string GenerateUniqueId(const sacm::AssuranceCasePackage& package, const std::string& prefix) {
    const auto existing = CollectElementIds(package);
    for (int index = 1; index < 100000; ++index) {
        std::string candidate = prefix + std::to_string(index);
        if (existing.find(candidate) == existing.end())
            return candidate;
    }
    return prefix + "x";
}

std::string GenerateUniqueGid(const sacm::AssuranceCasePackage& package, const std::string& id) {
    const auto existing = CollectGids(package);
    const std::string base = "gid-" + id;
    if (existing.find(base) == existing.end())
        return base;
    for (int index = 2; index < 100000; ++index) {
        std::string candidate = base + "-" + std::to_string(index);
        if (existing.find(candidate) == existing.end())
            return candidate;
    }
    return base + "-x";
}

std::vector<std::string> NormalizeCategoryRefs(const std::vector<std::string>& refs) {
    std::vector<std::string> normalized;
    for (std::string ref : refs) {
        ref = TrimWhitespace(ref);
        if (!ref.empty() && ref.front() == '#')
            ref.erase(ref.begin());
        if (ref.empty() || std::find(normalized.begin(), normalized.end(), ref) != normalized.end())
            continue;
        normalized.push_back(std::move(ref));
    }
    return normalized;
}

void ApplyTermDraft(sacm::Term& term, const TerminologyTermDraft& draft) {
    term.value = TrimWhitespace(draft.value);
    term.name = TrimWhitespace(draft.name);
    term.name_ml.set("en", term.name);
    term.description = TrimWhitespace(draft.description);
    term.description_ml.texts.erase("en");
    if (!term.description.empty())
        term.description_ml.set("en", term.description);
    term.category_refs = NormalizeCategoryRefs(draft.category_refs);
    term.externalReference = TrimWhitespace(draft.externalReference);
    term.origin = TrimWhitespace(draft.origin);
}

void ApplyCategoryDraft(sacm::Category& category, const TerminologyCategoryDraft& draft) {
    category.name = TrimWhitespace(draft.name);
    category.name_ml.set("en", category.name);
    category.description = TrimWhitespace(draft.description);
    category.description_ml.texts.erase("en");
    if (!category.description.empty())
        category.description_ml.set("en", category.description);
}

bool MatchesCategoryRefString(const sacm::Category& category, const std::string& raw_ref) {
    const std::string ref = NormalizeRef(raw_ref);
    if (ref.empty())
        return false;
    return (!category.id.empty() && category.id == ref) || (!category.gid.empty() && category.gid == ref);
}

bool MatchesRawRef(const std::string& raw_ref, const std::string& id, const std::string& gid) {
    const std::string ref = NormalizeRef(raw_ref);
    if (ref.empty())
        return false;
    return (!id.empty() && ref == id) || (!gid.empty() && ref == gid);
}

} // namespace core::detail
