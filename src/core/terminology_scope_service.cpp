#include "core/terminology_scope_service.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_set>

namespace core {
namespace {

std::string TrimWhitespace(const std::string& value) {
    auto begin = value.begin();
    while (begin != value.end() && std::isspace(static_cast<unsigned char>(*begin)))
        ++begin;
    auto end = value.end();
    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1))))
        --end;
    return std::string(begin, end);
}

std::string NormalizeRef(std::string ref) {
    ref = TrimWhitespace(ref);
    if (!ref.empty() && ref.front() == '#')
        ref.erase(ref.begin());
    return ref;
}

bool RefMatches(const std::string& raw_ref, const std::string& id, const std::string& gid) {
    const std::string ref = NormalizeRef(raw_ref);
    if (ref.empty())
        return false;
    return (!id.empty() && ref == id) || (!gid.empty() && ref == gid);
}

bool MatchesRef(const sacm::TerminologyPackage& package, const TerminologyPackageRef& package_ref) {
    return (!package_ref.id.empty() && package.id == package_ref.id) ||
           (!package_ref.gid.empty() && package.gid == package_ref.gid);
}

bool MatchesRef(const sacm::ArgumentPackage& package, const TerminologyArgumentPackageRef& package_ref) {
    return (!package_ref.id.empty() && package.id == package_ref.id) ||
           (!package_ref.gid.empty() && package.gid == package_ref.gid);
}

bool MatchesRef(const sacm::Term& term, const TerminologyTermRef& term_ref) {
    return (!term_ref.id.empty() && term.id == term_ref.id) || (!term_ref.gid.empty() && term.gid == term_ref.gid);
}

bool HasRef(const TerminologyTermRef& ref) {
    return !ref.id.empty() || !ref.gid.empty();
}

bool HasRef(const TerminologyPackageRef& ref) {
    return !ref.id.empty() || !ref.gid.empty();
}

bool HasRef(const TerminologyArgumentPackageRef& ref) {
    return !ref.id.empty() || !ref.gid.empty();
}

TerminologyPackageRef RefFor(const sacm::TerminologyPackage& package) {
    return TerminologyPackageRef{package.id, package.gid};
}

TerminologyTermRef RefFor(const sacm::Term& term) {
    return TerminologyTermRef{term.id, term.gid};
}

TerminologyArgumentPackageRef RefFor(const sacm::ArgumentPackage& package) {
    return TerminologyArgumentPackageRef{package.id, package.gid};
}

std::string TermKey(const sacm::Term& term) {
    if (!term.id.empty())
        return "id:" + term.id;
    if (!term.gid.empty())
        return "gid:" + term.gid;
    return "ptr:" + std::to_string(reinterpret_cast<std::uintptr_t>(&term));
}

void AddTerm(std::vector<TerminologyScopedTermRef>& terms,
             std::unordered_set<std::string>& seen,
             const sacm::TerminologyPackage& package,
             const sacm::Term& term,
             TerminologyLookupLayer layer,
             int package_order,
             int term_order) {
    const std::string key = TermKey(term);
    if (!seen.insert(key).second)
        return;
    terms.push_back({RefFor(term), RefFor(package), layer, package_order, term_order, &term});
}

void AddPackageTerms(std::vector<TerminologyScopedTermRef>& terms,
                     std::unordered_set<std::string>& seen,
                     const sacm::TerminologyPackage& package,
                     TerminologyLookupLayer layer,
                     int package_order) {
    for (std::size_t index = 0; index < package.terms.size(); ++index) {
        AddTerm(terms, seen, package, package.terms[index], layer, package_order, static_cast<int>(index));
    }
}

const sacm::ArgumentPackage* FindArgumentPackage(const sacm::AssuranceCasePackage& package,
                                                 const TerminologyArgumentPackageRef& package_ref) {
    if (!HasRef(package_ref))
        return nullptr;
    for (const auto& argument_package : package.argumentPackages) {
        if (MatchesRef(argument_package, package_ref))
            return &argument_package;
    }
    return nullptr;
}

const sacm::ArgumentPackage* FindContainingArgumentPackage(const sacm::AssuranceCasePackage& package,
                                                           const std::string& element_id,
                                                           const std::string& element_gid) {
    for (const auto& argument_package : package.argumentPackages) {
        for (const auto& claim : argument_package.claims) {
            if (RefMatches(element_id, claim.id, claim.gid) || RefMatches(element_gid, claim.id, claim.gid))
                return &argument_package;
        }
        for (const auto& reasoning : argument_package.argumentReasonings) {
            if (RefMatches(element_id, reasoning.id, reasoning.gid) || RefMatches(element_gid, reasoning.id, reasoning.gid))
                return &argument_package;
        }
        for (const auto& artifact_reference : argument_package.artifactReferences) {
            if (RefMatches(element_id, artifact_reference.id, artifact_reference.gid) ||
                RefMatches(element_gid, artifact_reference.id, artifact_reference.gid))
                return &argument_package;
        }
    }
    return nullptr;
}

const sacm::ArgumentPackage* ResolveArgumentPackage(const sacm::AssuranceCasePackage& package,
                                                    const TerminologyScopeContext& scope) {
    if (const sacm::ArgumentPackage* argument_package = FindArgumentPackage(package, scope.argument_package_ref))
        return argument_package;
    return FindContainingArgumentPackage(package, scope.element_id, scope.element_gid);
}

const sacm::ArtifactReference* FindArtifactReference(const sacm::ArgumentPackage& package, const std::string& raw_ref) {
    for (const auto& artifact_reference : package.artifactReferences) {
        if (RefMatches(raw_ref, artifact_reference.id, artifact_reference.gid))
            return &artifact_reference;
    }
    return nullptr;
}

bool RelationshipTargetsElement(const sacm::AssertedContext& context, const TerminologyScopeContext& scope) {
    for (const auto& target : context.targets) {
        if (RefMatches(target, scope.element_id, scope.element_gid))
            return true;
    }
    return false;
}

bool FindTermByRefInPackage(const sacm::TerminologyPackage& package,
                            const std::string& raw_ref,
                            const sacm::Term*& out_term,
                            int& out_term_order) {
    for (std::size_t index = 0; index < package.terms.size(); ++index) {
        const sacm::Term& term = package.terms[index];
        if (RefMatches(raw_ref, term.id, term.gid)) {
            out_term = &term;
            out_term_order = static_cast<int>(index);
            return true;
        }
    }
    return false;
}

bool FindTermByRef(const sacm::AssuranceCasePackage& package,
                   const TerminologyTermRef& term_ref,
                   const sacm::TerminologyPackage*& out_package,
                   const sacm::Term*& out_term,
                   int& out_package_order,
                   int& out_term_order) {
    const std::string ref = !term_ref.id.empty() ? term_ref.id : term_ref.gid;
    if (ref.empty())
        return false;

    for (std::size_t package_index = 0; package_index < package.terminologyPackages.size(); ++package_index) {
        const sacm::TerminologyPackage& terminology_package = package.terminologyPackages[package_index];
        if (FindTermByRefInPackage(terminology_package, ref, out_term, out_term_order)) {
            out_package = &terminology_package;
            out_package_order = static_cast<int>(package_index);
            return true;
        }
    }

    int package_order = static_cast<int>(package.terminologyPackages.size());
    for (const auto& argument_package : package.argumentPackages) {
        for (const auto& terminology_package : argument_package.terminologyPackages) {
            if (FindTermByRefInPackage(terminology_package, ref, out_term, out_term_order)) {
                out_package = &terminology_package;
                out_package_order = package_order;
                return true;
            }
            ++package_order;
        }
    }
    return false;
}

bool FindTermByRawRef(const sacm::AssuranceCasePackage& package,
                      const std::string& raw_ref,
                      const sacm::TerminologyPackage*& out_package,
                      const sacm::Term*& out_term,
                      int& out_package_order,
                      int& out_term_order) {
    TerminologyTermRef ref;
    ref.id = NormalizeRef(raw_ref);
    return FindTermByRef(package, ref, out_package, out_term, out_package_order, out_term_order);
}

void AddExplicitTerm(std::vector<TerminologyScopedTermRef>& terms,
                     std::unordered_set<std::string>& seen,
                     const sacm::AssuranceCasePackage& package,
                     const TerminologyTermRef& term_ref,
                     TerminologyLookupLayer layer) {
    const sacm::TerminologyPackage* terminology_package = nullptr;
    const sacm::Term* term = nullptr;
    int package_order = 0;
    int term_order = 0;
    if (!FindTermByRef(package, term_ref, terminology_package, term, package_order, term_order) || !terminology_package ||
        !term)
        return;
    AddTerm(terms, seen, *terminology_package, *term, layer, package_order, term_order);
}

void AddExplicitContextTerms(std::vector<TerminologyScopedTermRef>& terms,
                             std::unordered_set<std::string>& seen,
                             const sacm::AssuranceCasePackage& package,
                             const sacm::ArgumentPackage& argument_package,
                             const TerminologyScopeContext& scope) {
    for (const auto& context : argument_package.assertedContexts) {
        if (!RelationshipTargetsElement(context, scope))
            continue;
        for (const auto& source : context.sources) {
            const sacm::ArtifactReference* artifact_reference = FindArtifactReference(argument_package, source);
            if (!artifact_reference || artifact_reference->referencedArtifact.empty())
                continue;

            const sacm::TerminologyPackage* terminology_package = nullptr;
            const sacm::Term* term = nullptr;
            int package_order = 0;
            int term_order = 0;
            if (FindTermByRawRef(package,
                                 artifact_reference->referencedArtifact,
                                 terminology_package,
                                 term,
                                 package_order,
                                 term_order) &&
                terminology_package && term) {
                AddTerm(terms,
                        seen,
                        *terminology_package,
                        *term,
                        TerminologyLookupLayer::ExplicitElementContext,
                        package_order,
                        term_order);
            }
        }
    }
}

} // namespace

TerminologyService::TerminologyService(const sacm::AssuranceCasePackage& package) : package_(package) {}

TerminologyScopeContext TerminologyService::BuildScopeContextForElement(const std::string& element_ref) const {
    TerminologyScopeContext scope;
    scope.element_id = NormalizeRef(element_ref);
    if (const sacm::ArgumentPackage* argument_package =
            FindContainingArgumentPackage(package_, scope.element_id, scope.element_gid)) {
        scope.argument_package_ref = RefFor(*argument_package);
    }
    return scope;
}

std::vector<TerminologyScopedTermRef> TerminologyService::GetActiveTermsForElement(
    const std::string& element_gid) const {
    return GetActiveTermsForElement(BuildScopeContextForElement(element_gid));
}

std::vector<TerminologyScopedTermRef> TerminologyService::GetActiveTermsForElement(
    const TerminologyScopeContext& scope) const {
    std::vector<TerminologyScopedTermRef> terms;
    std::unordered_set<std::string> seen;

    if (scope.has_explicit_term_ref)
        AddExplicitTerm(terms, seen, package_, scope.explicit_term_ref, TerminologyLookupLayer::ExplicitOccurrenceBinding);

    const sacm::ArgumentPackage* argument_package = ResolveArgumentPackage(package_, scope);
    if (argument_package) {
        AddExplicitContextTerms(terms, seen, package_, *argument_package, scope);
        for (std::size_t index = 0; index < argument_package->terminologyPackages.size(); ++index) {
            AddPackageTerms(terms,
                            seen,
                            argument_package->terminologyPackages[index],
                            TerminologyLookupLayer::ArgumentPackageTerminology,
                            static_cast<int>(index));
        }
    }

    for (std::size_t index = 0; index < package_.terminologyPackages.size(); ++index) {
        const sacm::TerminologyPackage& terminology_package = package_.terminologyPackages[index];
        if (HasRef(scope.project_glossary_ref) && MatchesRef(terminology_package, scope.project_glossary_ref))
            continue;
        AddPackageTerms(terms,
                        seen,
                        terminology_package,
                        TerminologyLookupLayer::AssuranceCaseTerminology,
                        static_cast<int>(index));
    }

    if (HasRef(scope.project_glossary_ref)) {
        for (std::size_t index = 0; index < package_.terminologyPackages.size(); ++index) {
            const sacm::TerminologyPackage& terminology_package = package_.terminologyPackages[index];
            if (!MatchesRef(terminology_package, scope.project_glossary_ref))
                continue;
            AddPackageTerms(terms,
                            seen,
                            terminology_package,
                            TerminologyLookupLayer::ProjectGlossary,
                            static_cast<int>(index));
            break;
        }
    }

    return terms;
}

std::vector<TerminologyScopedTermRef> TerminologyService::FindTermsByValue(
    const std::string& text, const TerminologyScopeContext& scope) const {
    std::vector<TerminologyScopedTermRef> matches;
    const std::string value = TrimWhitespace(text);
    if (value.empty())
        return matches;

    for (const auto& candidate : GetActiveTermsForElement(scope)) {
        if (candidate.term && candidate.term->value == value)
            matches.push_back(candidate);
    }
    return matches;
}

TermResolution TerminologyService::ResolveOccurrence(const TextOccurrence& occurrence,
                                                     const TerminologyScopeContext& scope) const {
    TermResolution resolution;
    if (scope.ignored || occurrence.ignored) {
        resolution.status = TermResolutionStatus::Ignored;
        return resolution;
    }

    TerminologyTermRef explicit_ref;
    bool has_explicit_ref = false;
    if (occurrence.has_explicit_term_ref) {
        explicit_ref = occurrence.explicit_term_ref;
        has_explicit_ref = true;
    } else if (scope.has_explicit_term_ref) {
        explicit_ref = scope.explicit_term_ref;
        has_explicit_ref = true;
    }

    if (has_explicit_ref && HasRef(explicit_ref)) {
        TerminologyScopeContext explicit_scope = scope;
        explicit_scope.has_explicit_term_ref = true;
        explicit_scope.explicit_term_ref = explicit_ref;
        std::vector<TerminologyScopedTermRef> active_terms = GetActiveTermsForElement(explicit_scope);
        auto selected = std::find_if(active_terms.begin(), active_terms.end(), [&](const TerminologyScopedTermRef& term) {
            return (!explicit_ref.id.empty() && term.term_ref.id == explicit_ref.id) ||
                   (!explicit_ref.gid.empty() && term.term_ref.gid == explicit_ref.gid);
        });
        if (selected != active_terms.end()) {
            resolution.status = TermResolutionStatus::Explicit;
            resolution.selected = *selected;
            resolution.candidates.push_back(*selected);
            return resolution;
        }
    }

    resolution.candidates = FindTermsByValue(occurrence.text, scope);
    if (resolution.candidates.size() == 1) {
        resolution.status = TermResolutionStatus::Unique;
        resolution.selected = resolution.candidates.front();
    } else if (resolution.candidates.size() > 1) {
        resolution.status = TermResolutionStatus::Ambiguous;
    } else {
        resolution.status = TermResolutionStatus::None;
        resolution.important_undefined = LooksImportantUndefinedTerm(occurrence.text);
    }
    return resolution;
}

bool LooksImportantUndefinedTerm(const std::string& text) {
    const std::string value = TrimWhitespace(text);
    if (value.size() < 2)
        return false;

    bool has_letter = false;
    for (char c : value) {
        const unsigned char ch = static_cast<unsigned char>(c);
        if (std::isalpha(ch)) {
            has_letter = true;
            if (!std::isupper(ch))
                return false;
        } else if (!std::isdigit(ch) && c != '_' && c != '-') {
            return false;
        }
    }
    return has_letter;
}

} // namespace core
