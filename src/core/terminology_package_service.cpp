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

    add_base(package);
    for (const auto& terminology_package : package.terminologyPackages) {
        add_base(terminology_package);
        for (const auto& term : terminology_package.terms)
            add_base(term);
        for (const auto& expression : terminology_package.expressions)
            add_base(expression);
    }
    for (const auto& artifact_package : package.artifactPackages) {
        add_base(artifact_package);
        for (const auto& artifact : artifact_package.artifacts)
            add_base(artifact);
    }
    for (const auto& argument_package : package.argumentPackages) {
        add_base(argument_package);
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

    add_base(package);
    for (const auto& terminology_package : package.terminologyPackages) {
        add_base(terminology_package);
        for (const auto& term : terminology_package.terms)
            add_base(term);
        for (const auto& expression : terminology_package.expressions)
            add_base(expression);
    }
    for (const auto& artifact_package : package.artifactPackages) {
        add_base(artifact_package);
        for (const auto& artifact : artifact_package.artifacts)
            add_base(artifact);
    }
    for (const auto& argument_package : package.argumentPackages) {
        add_base(argument_package);
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

TerminologyTermRef RefFor(const sacm::Term& term) {
    return TerminologyTermRef{term.id, term.gid};
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
    return nullptr;
}

const sacm::TerminologyPackage* FindTerminologyPackage(const sacm::AssuranceCasePackage& package,
                                                       const TerminologyPackageRef& package_ref) {
    for (const auto& terminology_package : package.terminologyPackages) {
        if (MatchesRef(terminology_package, package_ref))
            return &terminology_package;
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
            issues.push_back({ref, TerminologyTermIssueSeverity::Warning,
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

std::vector<TerminologyTermUsageSummary> BuildTerminologyTermUsageSummaries(
    const sacm::AssuranceCasePackage& package, const sacm::TerminologyPackage& terminology_package) {
    std::vector<TerminologyTermUsageSummary> summaries;
    for (const auto& term : terminology_package.terms) {
        summaries.push_back({RefFor(term), CountTerminologyTermUsage(package, term)});
    }
    return summaries;
}

} // namespace core