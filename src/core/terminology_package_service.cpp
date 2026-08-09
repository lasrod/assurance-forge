#include "core/terminology_package_service.h"

#include "core/string_utils.h"
#include "core/terminology_scope_service.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <map>
#include <optional>
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

TerminologyTermRef RefFor(const sacm::Term& term) {
    return TerminologyTermRef{term.id, term.gid};
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

TerminologyPackageRef PlanTerminologyPackageIdentity(const sacm::AssuranceCasePackage& package) {
    TerminologyPackageRef planned;
    planned.id = GenerateUniqueId(package, "TP");
    planned.gid = GenerateUniqueGid(package, planned.id);
    return planned;
}

TerminologyTermRef PlanTerminologyTermIdentity(const sacm::AssuranceCasePackage& package) {
    TerminologyTermRef planned;
    planned.id = GenerateUniqueId(package, "T");
    planned.gid = GenerateUniqueGid(package, planned.id);
    return planned;
}

TerminologyPackageCreateResult CreateTerminologyPackageWithIds(sacm::AssuranceCasePackage& package,
                                                               const std::string& name,
                                                               const std::string& description,
                                                               const std::string& forced_id,
                                                               const std::string& forced_gid) {
    TerminologyPackageCreateResult result;
    if (name.empty()) {
        result.error = "Terminology package name is required.";
        return result;
    }

    sacm::TerminologyPackage terminology_package;
    terminology_package.id = forced_id.empty() ? GenerateUniqueId(package, "TP") : forced_id;
    terminology_package.gid = forced_gid.empty() ? GenerateUniqueGid(package, terminology_package.id) : forced_gid;
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

TerminologyPackageCreateResult
CreateTerminologyPackage(sacm::AssuranceCasePackage& package, const std::string& name, const std::string& description) {
    return CreateTerminologyPackageWithIds(package, name, description, {}, {});
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

TerminologyTermCreateResult CreateTerminologyTermWithIds(sacm::AssuranceCasePackage& package,
                                                         const TerminologyPackageRef& package_ref,
                                                         const TerminologyTermDraft& draft,
                                                         const std::string& forced_id,
                                                         const std::string& forced_gid) {
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
    term.id = forced_id.empty() ? GenerateUniqueId(package, "T") : forced_id;
    term.gid = forced_gid.empty() ? GenerateUniqueGid(package, term.id) : forced_gid;
    ApplyTermDraft(term, draft);
    result.term_ref = RefFor(term);
    terminology_package->terms.push_back(std::move(term));
    result.success = true;
    return result;
}

TerminologyTermCreateResult CreateTerminologyTerm(sacm::AssuranceCasePackage& package,
                                                  const TerminologyPackageRef& package_ref,
                                                  const TerminologyTermDraft& draft) {
    return CreateTerminologyTermWithIds(package, package_ref, draft, {}, {});
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

} // namespace core
