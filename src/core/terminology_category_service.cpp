#include "core/string_utils.h"
#include "core/terminology_internal.h"
#include "core/terminology_package_service.h"

#include <algorithm>

namespace core {

using detail::ApplyCategoryDraft;
using detail::GenerateUniqueGid;
using detail::GenerateUniqueId;
using detail::MatchesCategoryRefString;
using detail::MatchesRef;
using detail::RefFor;

sacm::Category* FindTerminologyCategory(sacm::TerminologyPackage& package, const TerminologyCategoryRef& category_ref) {
    for (auto& category : package.categories) {
        if (MatchesRef(category, category_ref))
            return &category;
    }
    return nullptr;
}

const sacm::Category* FindTerminologyCategory(const sacm::TerminologyPackage& package,
                                              const TerminologyCategoryRef& category_ref) {
    for (const auto& category : package.categories) {
        if (MatchesRef(category, category_ref))
            return &category;
    }
    return nullptr;
}

sacm::Category* FindTerminologyCategory(sacm::AssuranceCasePackage& package,
                                        const TerminologyPackageRef& package_ref,
                                        const TerminologyCategoryRef& category_ref) {
    sacm::TerminologyPackage* terminology_package = FindTerminologyPackage(package, package_ref);
    return terminology_package ? FindTerminologyCategory(*terminology_package, category_ref) : nullptr;
}

const sacm::Category* FindTerminologyCategory(const sacm::AssuranceCasePackage& package,
                                              const TerminologyPackageRef& package_ref,
                                              const TerminologyCategoryRef& category_ref) {
    const sacm::TerminologyPackage* terminology_package = FindTerminologyPackage(package, package_ref);
    return terminology_package ? FindTerminologyCategory(*terminology_package, category_ref) : nullptr;
}

TerminologyCategoryCreateResult CreateTerminologyCategoryWithIds(sacm::AssuranceCasePackage& package,
                                                                 const TerminologyPackageRef& package_ref,
                                                                 const TerminologyCategoryDraft& draft,
                                                                 const std::string& forced_id,
                                                                 const std::string& forced_gid) {
    TerminologyCategoryCreateResult result;
    if (TrimWhitespace(draft.name).empty()) {
        result.error = "Category name is required.";
        return result;
    }

    sacm::TerminologyPackage* terminology_package = FindTerminologyPackage(package, package_ref);
    if (!terminology_package) {
        result.error = "Terminology package not found.";
        return result;
    }

    sacm::Category category;
    category.id = forced_id.empty() ? GenerateUniqueId(package, "CAT") : forced_id;
    category.gid = forced_gid.empty() ? GenerateUniqueGid(package, category.id) : forced_gid;
    ApplyCategoryDraft(category, draft);
    result.category_ref = RefFor(category);
    terminology_package->categories.push_back(std::move(category));
    result.success = true;
    return result;
}

TerminologyCategoryCreateResult CreateTerminologyCategory(sacm::AssuranceCasePackage& package,
                                                          const TerminologyPackageRef& package_ref,
                                                          const TerminologyCategoryDraft& draft) {
    return CreateTerminologyCategoryWithIds(package, package_ref, draft, {}, {});
}

bool UpdateTerminologyCategory(sacm::AssuranceCasePackage& package,
                               const TerminologyPackageRef& package_ref,
                               const TerminologyCategoryRef& category_ref,
                               const TerminologyCategoryDraft& draft,
                               std::string& out_error) {
    out_error.clear();
    if (TrimWhitespace(draft.name).empty()) {
        out_error = "Category name is required.";
        return false;
    }

    sacm::Category* category = FindTerminologyCategory(package, package_ref, category_ref);
    if (!category) {
        out_error = "Category not found.";
        return false;
    }

    ApplyCategoryDraft(*category, draft);
    return true;
}

int CountTermsUsingCategory(const sacm::TerminologyPackage& package, const TerminologyCategoryRef& category_ref) {
    const sacm::Category* category = FindTerminologyCategory(package, category_ref);
    if (!category)
        return 0;

    int count = 0;
    for (const auto& term : package.terms) {
        const bool assigned =
            std::any_of(term.category_refs.begin(), term.category_refs.end(), [&](const std::string& ref) {
                return MatchesCategoryRefString(*category, ref);
            });
        if (assigned)
            ++count;
    }
    return count;
}

bool DeleteTerminologyCategory(sacm::AssuranceCasePackage& package,
                               const TerminologyPackageRef& package_ref,
                               const TerminologyCategoryRef& category_ref,
                               std::string& out_error) {
    out_error.clear();
    sacm::TerminologyPackage* terminology_package = FindTerminologyPackage(package, package_ref);
    if (!terminology_package) {
        out_error = "Terminology package not found.";
        return false;
    }

    if (CountTermsUsingCategory(*terminology_package, category_ref) > 0) {
        out_error = "Category is assigned to one or more terms.";
        return false;
    }

    auto it = std::find_if(terminology_package->categories.begin(),
                           terminology_package->categories.end(),
                           [&](const sacm::Category& category) { return MatchesRef(category, category_ref); });
    if (it == terminology_package->categories.end()) {
        out_error = "Category not found.";
        return false;
    }

    terminology_package->categories.erase(it);
    return true;
}

std::vector<TerminologyCategoryUsageSummary>
BuildTerminologyCategoryUsageSummaries(const sacm::TerminologyPackage& package) {
    std::vector<TerminologyCategoryUsageSummary> summaries;
    for (const auto& category : package.categories) {
        const TerminologyCategoryRef ref = RefFor(category);
        summaries.push_back({ref, CountTermsUsingCategory(package, ref)});
    }
    return summaries;
}

std::string CategoryDisplayName(const sacm::TerminologyPackage& package, const std::string& category_ref) {
    const std::string ref = NormalizeRef(category_ref);
    for (const auto& category : package.categories) {
        if (MatchesCategoryRefString(category, ref)) {
            if (!category.name.empty())
                return category.name;
            if (!category.id.empty())
                return category.id;
            return category.gid;
        }
    }
    return ref;
}

} // namespace core
