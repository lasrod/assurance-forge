#include "core/terminology_package_service.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace core {
namespace {

std::unordered_set<std::string> CollectElementIds(const sacm::AssuranceCasePackage& package) {
    std::unordered_set<std::string> ids;
    auto add_base = [&](const sacm::SacmElement& element) {
        if (!element.id.empty())
            ids.insert(element.id);
    };
    auto add_terminology_package = [&](const sacm::TerminologyPackage& terminology_package) {
        add_base(terminology_package);
        for (const auto& category : terminology_package.categories)
            add_base(category);
        for (const auto& term : terminology_package.terms)
            add_base(term);
        for (const auto& expression : terminology_package.expressions)
            add_base(expression);
    };

    add_base(package);
    for (const auto& terminology_package : package.terminologyPackages) {
        add_terminology_package(terminology_package);
    }
    for (const auto& artifact_package : package.artifactPackages) {
        add_base(artifact_package);
        for (const auto& artifact : artifact_package.artifacts)
            add_base(artifact);
    }
    for (const auto& argument_package : package.argumentPackages) {
        add_base(argument_package);
        for (const auto& terminology_package : argument_package.terminologyPackages)
            add_terminology_package(terminology_package);
        for (const auto& claim : argument_package.claims)
            add_base(claim);
        for (const auto& reasoning : argument_package.argumentReasonings)
            add_base(reasoning);
        for (const auto& artifact_reference : argument_package.artifactReferences)
            add_base(artifact_reference);
        for (const auto& relationship : argument_package.assertedInferences)
            add_base(relationship);
        for (const auto& relationship : argument_package.assertedContexts)
            add_base(relationship);
        for (const auto& relationship : argument_package.assertedEvidences)
            add_base(relationship);
    }
    return ids;
}

std::unordered_set<std::string> CollectGids(const sacm::AssuranceCasePackage& package) {
    std::unordered_set<std::string> gids;
    auto add_base = [&](const sacm::SacmElement& element) {
        if (!element.gid.empty())
            gids.insert(element.gid);
    };
    auto add_terminology_package = [&](const sacm::TerminologyPackage& terminology_package) {
        add_base(terminology_package);
        for (const auto& category : terminology_package.categories)
            add_base(category);
        for (const auto& term : terminology_package.terms)
            add_base(term);
        for (const auto& expression : terminology_package.expressions)
            add_base(expression);
    };

    add_base(package);
    for (const auto& terminology_package : package.terminologyPackages) {
        add_terminology_package(terminology_package);
    }
    for (const auto& artifact_package : package.artifactPackages) {
        add_base(artifact_package);
        for (const auto& artifact : artifact_package.artifacts)
            add_base(artifact);
    }
    for (const auto& argument_package : package.argumentPackages) {
        add_base(argument_package);
        for (const auto& terminology_package : argument_package.terminologyPackages)
            add_terminology_package(terminology_package);
        for (const auto& claim : argument_package.claims)
            add_base(claim);
        for (const auto& reasoning : argument_package.argumentReasonings)
            add_base(reasoning);
        for (const auto& artifact_reference : argument_package.artifactReferences)
            add_base(artifact_reference);
        for (const auto& relationship : argument_package.assertedInferences)
            add_base(relationship);
        for (const auto& relationship : argument_package.assertedContexts)
            add_base(relationship);
        for (const auto& relationship : argument_package.assertedEvidences)
            add_base(relationship);
    }
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

bool MatchesRef(const sacm::TerminologyPackage& package, const TerminologyPackageRef& package_ref) {
    if (!package_ref.id.empty() && package.id == package_ref.id)
        return true;
    if (!package_ref.gid.empty() && package.gid == package_ref.gid)
        return true;
    return false;
}

bool MatchesRef(const sacm::Term& term, const TerminologyTermRef& term_ref) {
    if (!term_ref.id.empty() && term.id == term_ref.id)
        return true;
    if (!term_ref.gid.empty() && term.gid == term_ref.gid)
        return true;
    return false;
}

bool MatchesRef(const sacm::Category& category, const TerminologyCategoryRef& category_ref) {
    if (!category_ref.id.empty() && category.id == category_ref.id)
        return true;
    if (!category_ref.gid.empty() && category.gid == category_ref.gid)
        return true;
    return false;
}

TerminologyTermRef RefFor(const sacm::Term& term) {
    return TerminologyTermRef{term.id, term.gid};
}

TerminologyCategoryRef RefFor(const sacm::Category& category) {
    return TerminologyCategoryRef{category.id, category.gid};
}

std::string TrimWhitespace(const std::string& value) {
    auto begin = value.begin();
    while (begin != value.end() && std::isspace(static_cast<unsigned char>(*begin)))
        ++begin;
    auto end = value.end();
    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1))))
        --end;
    return std::string(begin, end);
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

std::string NormalizeRef(std::string ref) {
    ref = TrimWhitespace(ref);
    if (!ref.empty() && ref.front() == '#')
        ref.erase(ref.begin());
    return ref;
}

bool MatchesCategoryRefString(const sacm::Category& category, const std::string& raw_ref) {
    const std::string ref = NormalizeRef(raw_ref);
    if (ref.empty())
        return false;
    return (!category.id.empty() && category.id == ref) || (!category.gid.empty() && category.gid == ref);
}

bool IsWordChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
    return value;
}

int CountWholeWordOccurrences(const std::string& text, const std::string& needle) {
    if (text.empty() || needle.empty())
        return 0;
    const std::string searchable_text = ToLower(text);
    const std::string searchable_needle = ToLower(needle);
    int count = 0;
    std::size_t pos = searchable_text.find(searchable_needle);
    while (pos != std::string::npos) {
        const bool left_ok = pos == 0 || !IsWordChar(searchable_text[pos - 1]);
        const std::size_t end = pos + searchable_needle.size();
        const bool right_ok = end >= searchable_text.size() || !IsWordChar(searchable_text[end]);
        if (left_ok && right_ok)
            ++count;
        pos = searchable_text.find(searchable_needle, pos + 1);
    }
    return count;
}

void AddText(std::vector<std::string>& texts, const std::string& text) {
    if (!text.empty())
        texts.push_back(text);
}

std::vector<std::string> CollectUsageTexts(const sacm::AssuranceCasePackage& package) {
    std::vector<std::string> texts;
    AddText(texts, package.name);
    AddText(texts, package.description);
    for (const auto& artifact_package : package.artifactPackages) {
        AddText(texts, artifact_package.name);
        AddText(texts, artifact_package.description);
        for (const auto& artifact : artifact_package.artifacts) {
            AddText(texts, artifact.name);
            AddText(texts, artifact.description);
        }
    }
    for (const auto& argument_package : package.argumentPackages) {
        AddText(texts, argument_package.name);
        AddText(texts, argument_package.description);
        for (const auto& claim : argument_package.claims) {
            AddText(texts, claim.name);
            AddText(texts, claim.description);
            AddText(texts, claim.content);
        }
        for (const auto& reasoning : argument_package.argumentReasonings) {
            AddText(texts, reasoning.name);
            AddText(texts, reasoning.description);
            AddText(texts, reasoning.content);
        }
        for (const auto& artifact_reference : argument_package.artifactReferences) {
            AddText(texts, artifact_reference.name);
            AddText(texts, artifact_reference.description);
        }
    }
    return texts;
}

} // namespace

sacm::TerminologyPackage* FindTerminologyPackage(sacm::AssuranceCasePackage& package,
                                                 const TerminologyPackageRef& package_ref) {
    for (auto& terminology_package : package.terminologyPackages) {
        if (MatchesRef(terminology_package, package_ref))
            return &terminology_package;
    }
    for (auto& argument_package : package.argumentPackages) {
        for (auto& terminology_package : argument_package.terminologyPackages) {
            if (MatchesRef(terminology_package, package_ref))
                return &terminology_package;
        }
    }
    return nullptr;
}

const sacm::TerminologyPackage* FindTerminologyPackage(const sacm::AssuranceCasePackage& package,
                                                       const TerminologyPackageRef& package_ref) {
    for (const auto& terminology_package : package.terminologyPackages) {
        if (MatchesRef(terminology_package, package_ref))
            return &terminology_package;
    }
    for (const auto& argument_package : package.argumentPackages) {
        for (const auto& terminology_package : argument_package.terminologyPackages) {
            if (MatchesRef(terminology_package, package_ref))
                return &terminology_package;
        }
    }
    return nullptr;
}

TerminologyPackageCreateResult
CreateTerminologyPackage(sacm::AssuranceCasePackage& package, const std::string& name, const std::string& description) {
    TerminologyPackageCreateResult result;
    if (name.empty()) {
        result.error = "Terminology package name is required.";
        return result;
    }

    sacm::TerminologyPackage terminology_package;
    terminology_package.id = GenerateUniqueId(package, "TP");
    terminology_package.gid = GenerateUniqueGid(package, terminology_package.id);
    terminology_package.name = name;
    terminology_package.name_ml.set("en", name);
    terminology_package.description = description;
    if (!description.empty())
        terminology_package.description_ml.set("en", description);

    result.package_ref.id = terminology_package.id;
    result.package_ref.gid = terminology_package.gid;
    package.terminologyPackages.push_back(std::move(terminology_package));
    result.success = true;
    return result;
}

bool UpdateTerminologyPackage(sacm::AssuranceCasePackage& package,
                              const TerminologyPackageRef& package_ref,
                              const std::string& name,
                              const std::string& description,
                              std::string& out_error) {
    out_error.clear();
    if (name.empty()) {
        out_error = "Terminology package name is required.";
        return false;
    }

    sacm::TerminologyPackage* terminology_package = FindTerminologyPackage(package, package_ref);
    if (!terminology_package) {
        out_error = "Terminology package not found.";
        return false;
    }

    terminology_package->name = name;
    terminology_package->name_ml.set("en", name);
    terminology_package->description = description;
    terminology_package->description_ml.texts.erase("en");
    if (!description.empty())
        terminology_package->description_ml.set("en", description);
    return true;
}

bool CanDeleteTerminologyPackage(const sacm::TerminologyPackage& package, std::string& out_reason) {
    out_reason.clear();
    if (!package.categories.empty()) {
        out_reason = "Terminology package contains categories.";
        return false;
    }
    if (!package.terms.empty()) {
        out_reason = "Terminology package contains terms.";
        return false;
    }
    if (!package.expressions.empty()) {
        out_reason = "Terminology package contains terminology entries.";
        return false;
    }
    return true;
}

bool DeleteTerminologyPackage(sacm::AssuranceCasePackage& package,
                              const TerminologyPackageRef& package_ref,
                              std::string& out_error) {
    out_error.clear();
    auto it = std::find_if(package.terminologyPackages.begin(),
                           package.terminologyPackages.end(),
                           [&](const sacm::TerminologyPackage& terminology_package) {
                               return MatchesRef(terminology_package, package_ref);
                           });
    if (it == package.terminologyPackages.end()) {
        out_error = "Terminology package not found.";
        return false;
    }

    if (!CanDeleteTerminologyPackage(*it, out_error))
        return false;

    package.terminologyPackages.erase(it);
    return true;
}

sacm::Term* FindTerminologyTerm(sacm::TerminologyPackage& package, const TerminologyTermRef& term_ref) {
    for (auto& term : package.terms) {
        if (MatchesRef(term, term_ref))
            return &term;
    }
    return nullptr;
}

const sacm::Term* FindTerminologyTerm(const sacm::TerminologyPackage& package, const TerminologyTermRef& term_ref) {
    for (const auto& term : package.terms) {
        if (MatchesRef(term, term_ref))
            return &term;
    }
    return nullptr;
}

sacm::Term* FindTerminologyTerm(sacm::AssuranceCasePackage& package,
                                const TerminologyPackageRef& package_ref,
                                const TerminologyTermRef& term_ref) {
    sacm::TerminologyPackage* terminology_package = FindTerminologyPackage(package, package_ref);
    return terminology_package ? FindTerminologyTerm(*terminology_package, term_ref) : nullptr;
}

const sacm::Term* FindTerminologyTerm(const sacm::AssuranceCasePackage& package,
                                      const TerminologyPackageRef& package_ref,
                                      const TerminologyTermRef& term_ref) {
    const sacm::TerminologyPackage* terminology_package = FindTerminologyPackage(package, package_ref);
    return terminology_package ? FindTerminologyTerm(*terminology_package, term_ref) : nullptr;
}

TerminologyTermCreateResult CreateTerminologyTerm(sacm::AssuranceCasePackage& package,
                                                  const TerminologyPackageRef& package_ref,
                                                  const TerminologyTermDraft& draft) {
    TerminologyTermCreateResult result;
    if (TrimWhitespace(draft.value).empty()) {
        result.error = "Term value is required.";
        return result;
    }

    sacm::TerminologyPackage* terminology_package = FindTerminologyPackage(package, package_ref);
    if (!terminology_package) {
        result.error = "Terminology package not found.";
        return result;
    }

    sacm::Term term;
    term.id = GenerateUniqueId(package, "T");
    term.gid = GenerateUniqueGid(package, term.id);
    ApplyTermDraft(term, draft);
    result.term_ref = RefFor(term);
    terminology_package->terms.push_back(std::move(term));
    result.success = true;
    return result;
}

bool UpdateTerminologyTerm(sacm::AssuranceCasePackage& package,
                           const TerminologyPackageRef& package_ref,
                           const TerminologyTermRef& term_ref,
                           const TerminologyTermDraft& draft,
                           std::string& out_error) {
    out_error.clear();
    if (TrimWhitespace(draft.value).empty()) {
        out_error = "Term value is required.";
        return false;
    }

    sacm::Term* term = FindTerminologyTerm(package, package_ref, term_ref);
    if (!term) {
        out_error = "Term not found.";
        return false;
    }

    ApplyTermDraft(*term, draft);
    return true;
}

bool DeleteTerminologyTerm(sacm::AssuranceCasePackage& package,
                           const TerminologyPackageRef& package_ref,
                           const TerminologyTermRef& term_ref,
                           std::string& out_error) {
    out_error.clear();
    sacm::TerminologyPackage* terminology_package = FindTerminologyPackage(package, package_ref);
    if (!terminology_package) {
        out_error = "Terminology package not found.";
        return false;
    }

    auto it = std::find_if(terminology_package->terms.begin(),
                           terminology_package->terms.end(),
                           [&](const sacm::Term& term) { return MatchesRef(term, term_ref); });
    if (it == terminology_package->terms.end()) {
        out_error = "Term not found.";
        return false;
    }

    terminology_package->terms.erase(it);
    return true;
}

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

TerminologyCategoryCreateResult CreateTerminologyCategory(sacm::AssuranceCasePackage& package,
                                                          const TerminologyPackageRef& package_ref,
                                                          const TerminologyCategoryDraft& draft) {
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
    category.id = GenerateUniqueId(package, "CAT");
    category.gid = GenerateUniqueGid(package, category.id);
    ApplyCategoryDraft(category, draft);
    result.category_ref = RefFor(category);
    terminology_package->categories.push_back(std::move(category));
    result.success = true;
    return result;
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

std::vector<TerminologyTermIssue> ValidateTerminologyTerms(const sacm::TerminologyPackage& package) {
    std::vector<TerminologyTermIssue> issues;
    std::map<std::string, int> value_counts;
    for (const auto& term : package.terms) {
        const std::string value = TrimWhitespace(term.value);
        if (!value.empty())
            ++value_counts[value];
    }

    for (const auto& term : package.terms) {
        const TerminologyTermRef ref = RefFor(term);
        const std::string value = TrimWhitespace(term.value);
        if (value.empty()) {
            issues.push_back({ref, TerminologyTermIssueSeverity::Error, "Term has no value."});
        } else if (value_counts[value] > 1) {
            issues.push_back({ref,
                              TerminologyTermIssueSeverity::Warning,
                              "Duplicate term value exists in this terminology package."});
        }
        if (TrimWhitespace(term.description).empty()) {
            issues.push_back({ref, TerminologyTermIssueSeverity::Warning, "Concrete term has no description."});
        }
        if (term.category_refs.empty()) {
            issues.push_back({ref, TerminologyTermIssueSeverity::Info, "Term has no category."});
        }
    }
    return issues;
}

int CountTerminologyTermUsage(const sacm::AssuranceCasePackage& package, const sacm::Term& term) {
    int count = 0;
    const std::vector<std::string> texts = CollectUsageTexts(package);
    for (const auto& text : texts) {
        count += CountWholeWordOccurrences(text, term.value);
    }
    return count;
}

std::vector<TerminologyTermUsageSummary>
BuildTerminologyTermUsageSummaries(const sacm::AssuranceCasePackage& package,
                                   const sacm::TerminologyPackage& terminology_package) {
    std::vector<TerminologyTermUsageSummary> summaries;
    for (const auto& term : terminology_package.terms) {
        summaries.push_back({RefFor(term), CountTerminologyTermUsage(package, term)});
    }
    return summaries;
}

} // namespace core