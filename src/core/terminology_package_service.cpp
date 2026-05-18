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

TerminologyPackageRef RefFor(const sacm::TerminologyPackage& package) {
    return TerminologyPackageRef{package.id, package.gid};
}

TerminologyCategoryRef RefFor(const sacm::Category& category) {
    return TerminologyCategoryRef{category.id, category.gid};
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

bool ArtifactReferenceTargetsTerm(const sacm::ArtifactReference& artifact_reference, const sacm::Term& term) {
    return MatchesRawRef(artifact_reference.referencedArtifact, term.id, term.gid);
}

bool ArtifactReferenceTargetsAnyTerm(const sacm::AssuranceCasePackage& package,
                                     const sacm::ArtifactReference& artifact_reference) {
    return ResolveTerminologyTermReference(package, artifact_reference.referencedArtifact).resolved;
}

bool RelationshipReferencesElement(const std::vector<std::string>& refs,
                                   const std::string& id,
                                   const std::string& gid = {}) {
    return std::any_of(refs.begin(), refs.end(), [&](const std::string& ref) { return MatchesRawRef(ref, id, gid); });
}

bool ArgumentPackageContainsTargetElement(const sacm::ArgumentPackage& argument_package,
                                          const std::string& target_element_id) {
    for (const auto& claim : argument_package.claims) {
        if (MatchesRawRef(target_element_id, claim.id, claim.gid))
            return true;
    }
    for (const auto& reasoning : argument_package.argumentReasonings) {
        if (MatchesRawRef(target_element_id, reasoning.id, reasoning.gid))
            return true;
    }
    for (const auto& artifact_reference : argument_package.artifactReferences) {
        if (MatchesRawRef(target_element_id, artifact_reference.id, artifact_reference.gid))
            return true;
    }
    return false;
}

bool ArgumentPackageContainsVisibleContextTarget(const sacm::ArgumentPackage& argument_package,
                                                 const std::string& target_element_id) {
    for (const auto& claim : argument_package.claims) {
        if (MatchesRawRef(target_element_id, claim.id, claim.gid))
            return true;
    }
    for (const auto& reasoning : argument_package.argumentReasonings) {
        if (MatchesRawRef(target_element_id, reasoning.id, reasoning.gid))
            return true;
    }
    return false;
}

sacm::ArgumentPackage* FindOwningArgumentPackageForContext(sacm::AssuranceCasePackage& package,
                                                           const std::string& target_element_id) {
    for (auto& argument_package : package.argumentPackages) {
        if (ArgumentPackageContainsTargetElement(argument_package, target_element_id))
            return &argument_package;
    }
    return nullptr;
}

sacm::ArgumentPackage* FindOwningArgumentPackageForVisibleContext(sacm::AssuranceCasePackage& package,
                                                                  const std::string& target_element_id) {
    for (auto& argument_package : package.argumentPackages) {
        if (ArgumentPackageContainsVisibleContextTarget(argument_package, target_element_id))
            return &argument_package;
    }
    return nullptr;
}

bool ContextReferencesArtifact(const sacm::AssertedContext& context,
                               const sacm::ArtifactReference& artifact_reference) {
    return RelationshipReferencesElement(context.sources, artifact_reference.id, artifact_reference.gid);
}

bool ContextTargetsElement(const sacm::AssertedContext& context, const std::string& target_element_id) {
    return RelationshipReferencesElement(context.targets, target_element_id);
}

const sacm::ArtifactReference* FindArtifactReferenceByRef(const sacm::ArgumentPackage& argument_package,
                                                          const std::string& raw_ref) {
    for (const sacm::ArtifactReference& artifact_reference : argument_package.artifactReferences) {
        if (MatchesRawRef(raw_ref, artifact_reference.id, artifact_reference.gid))
            return &artifact_reference;
    }
    return nullptr;
}

bool IsClaimOrReasoningTarget(const sacm::ArgumentPackage& argument_package, const std::string& raw_ref) {
    for (const sacm::Claim& claim : argument_package.claims) {
        if (MatchesRawRef(raw_ref, claim.id, claim.gid))
            return true;
    }
    for (const sacm::ArgumentReasoning& reasoning : argument_package.argumentReasonings) {
        if (MatchesRawRef(raw_ref, reasoning.id, reasoning.gid))
            return true;
    }
    return false;
}

std::string CanonicalTargetRef(const sacm::ArgumentPackage& argument_package, const std::string& raw_ref) {
    for (const sacm::Claim& claim : argument_package.claims) {
        if (MatchesRawRef(raw_ref, claim.id, claim.gid))
            return claim.id.empty() ? claim.gid : claim.id;
    }
    for (const sacm::ArgumentReasoning& reasoning : argument_package.argumentReasonings) {
        if (MatchesRawRef(raw_ref, reasoning.id, reasoning.gid))
            return reasoning.id.empty() ? reasoning.gid : reasoning.id;
    }
    return NormalizeRef(raw_ref);
}

std::string CanonicalTermRef(const TerminologyTermReferenceResolution& resolution) {
    if (!resolution.term_ref.id.empty())
        return resolution.term_ref.id;
    return resolution.term_ref.gid;
}

std::string DisplayRef(const std::string& ref, const char* fallback) {
    const std::string normalized = NormalizeRef(ref);
    return normalized.empty() ? fallback : normalized;
}

TerminologyContextReferenceIssue MakeContextIssue(TerminologyContextReferenceIssueKind kind,
                                                  const sacm::ArgumentPackage& argument_package,
                                                  const sacm::AssertedContext& context,
                                                  const sacm::ArtifactReference* artifact_reference,
                                                  const std::string& source_ref,
                                                  const std::string& target_ref,
                                                  const std::string& message) {
    TerminologyContextReferenceIssue issue;
    issue.kind = kind;
    issue.severity = kind == TerminologyContextReferenceIssueKind::DuplicateContext
                         ? TerminologyTermIssueSeverity::Warning
                         : TerminologyTermIssueSeverity::Error;
    issue.argument_package_id = argument_package.id;
    issue.argument_package_gid = argument_package.gid;
    issue.asserted_context_id = context.id;
    issue.artifact_reference_id = artifact_reference ? artifact_reference->id : NormalizeRef(source_ref);
    issue.target_ref = NormalizeRef(target_ref);
    issue.referenced_artifact =
        artifact_reference ? NormalizeRef(artifact_reference->referencedArtifact) : NormalizeRef(source_ref);
    issue.message = message;
    return issue;
}

bool ArtifactReferenceUsedByOtherContexts(const sacm::ArgumentPackage& argument_package,
                                          const sacm::ArtifactReference& artifact_reference,
                                          const std::string& target_element_id,
                                          const std::string& context_id_to_ignore = {}) {
    return std::any_of(argument_package.assertedContexts.begin(),
                       argument_package.assertedContexts.end(),
                       [&](const sacm::AssertedContext& context) {
                           if (!context_id_to_ignore.empty() && context.id == context_id_to_ignore)
                               return false;
                           if (!ContextReferencesArtifact(context, artifact_reference))
                               return false;
                           return !ContextTargetsElement(context, target_element_id);
                       });
}

std::string TermContextLabel(const sacm::Term& term) {
    if (term.value.empty())
        return term.name.empty() ? term.id : term.name;
    if (term.name.empty() || term.name == term.value)
        return term.value;
    return term.value + ": " + term.name;
}

struct VisibleContextSearchResult {
    sacm::ArtifactReference* existing_visible_reference = nullptr;
    sacm::AssertedContext* existing_visible_context = nullptr;
    sacm::ArtifactReference* promotable_reference = nullptr;
    sacm::AssertedContext* promotable_context = nullptr;
    std::vector<std::string> hidden_contexts_to_remove;
};

VisibleContextSearchResult FindVisibleContextCandidates(sacm::ArgumentPackage& argument_package,
                                                        const sacm::Term& term,
                                                        const std::string& target_ref) {
    VisibleContextSearchResult result;
    for (auto& artifact_reference : argument_package.artifactReferences) {
        if (!ArtifactReferenceTargetsTerm(artifact_reference, term))
            continue;
        for (auto& context : argument_package.assertedContexts) {
            if (!ContextReferencesArtifact(context, artifact_reference) || !ContextTargetsElement(context, target_ref))
                continue;
            if (IsVisibleTerminologyContext(context)) {
                result.existing_visible_reference = &artifact_reference;
                result.existing_visible_context = &context;
                return result;
            }
            if (!ArtifactReferenceUsedByOtherContexts(argument_package, artifact_reference, target_ref, context.id) &&
                !result.promotable_reference && !result.promotable_context) {
                result.promotable_reference = &artifact_reference;
                result.promotable_context = &context;
            } else if (!context.id.empty()) {
                result.hidden_contexts_to_remove.push_back(context.id);
            }
        }
    }
    return result;
}

sacm::ArtifactReference CreateTerminologyArtifactReference(const sacm::AssuranceCasePackage& package,
                                                           const sacm::Term& term) {
    sacm::ArtifactReference artifact_reference;
    artifact_reference.id = GenerateUniqueId(package, "TC");
    artifact_reference.gid = GenerateUniqueGid(package, artifact_reference.id);
    artifact_reference.name = TermContextLabel(term);
    artifact_reference.name_ml.set("en", artifact_reference.name);
    artifact_reference.referencedArtifact = !term.id.empty() ? term.id : term.gid;
    return artifact_reference;
}

sacm::AssertedContext CreateVisibleTerminologyContext(const sacm::AssuranceCasePackage& package,
                                                      const sacm::Term& term,
                                                      const std::string& source_ref,
                                                      const std::string& target_ref) {
    sacm::AssertedContext context;
    context.id = GenerateUniqueId(package, "AC");
    context.gid = GenerateUniqueGid(package, context.id);
    context.name = "Context: " + TermContextLabel(term);
    context.name_ml.set("en", context.name);
    context.description = kVisibleTerminologyContextMarker;
    context.sources.push_back(source_ref);
    context.targets.push_back(target_ref);
    return context;
}

void RemoveContextsById(sacm::ArgumentPackage& argument_package, const std::vector<std::string>& context_ids) {
    for (const std::string& hidden_context_id : context_ids) {
        argument_package.assertedContexts.erase(
            std::remove_if(argument_package.assertedContexts.begin(),
                           argument_package.assertedContexts.end(),
                           [&](const sacm::AssertedContext& existing) { return existing.id == hidden_context_id; }),
            argument_package.assertedContexts.end());
    }
}

void RemoveUnreferencedTerminologyArtifacts(sacm::ArgumentPackage& argument_package,
                                            const sacm::Term& term,
                                            const std::string& artifact_reference_id_to_keep) {
    argument_package.artifactReferences.erase(
        std::remove_if(argument_package.artifactReferences.begin(),
                       argument_package.artifactReferences.end(),
                       [&](const sacm::ArtifactReference& existing) {
                           if (!ArtifactReferenceTargetsTerm(existing, term))
                               return false;
                           if (existing.id == artifact_reference_id_to_keep)
                               return false;
                           return !std::any_of(argument_package.assertedContexts.begin(),
                                               argument_package.assertedContexts.end(),
                                               [&](const sacm::AssertedContext& existing_context) {
                                                   return ContextReferencesArtifact(existing_context, existing);
                                               });
                       }),
        argument_package.artifactReferences.end());
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

TerminologyContextAssociationResult AssociateTerminologyTermWithElement(sacm::AssuranceCasePackage& package,
                                                                        const std::string& target_element_id,
                                                                        const TerminologyPackageRef& package_ref,
                                                                        const TerminologyTermRef& term_ref) {
    TerminologyContextAssociationResult result;
    if (NormalizeRef(target_element_id).empty()) {
        result.error = "Target element is required.";
        return result;
    }

    const sacm::Term* term = FindTerminologyTerm(package, package_ref, term_ref);
    if (!term) {
        result.error = "Term not found.";
        return result;
    }

    sacm::ArgumentPackage* argument_package = FindOwningArgumentPackageForContext(package, target_element_id);
    if (!argument_package) {
        result.error = "Selected element is not a claim, strategy, or solution in an argument package.";
        return result;
    }

    sacm::ArtifactReference* reusable_reference = nullptr;
    for (auto& artifact_reference : argument_package->artifactReferences) {
        if (!ArtifactReferenceTargetsTerm(artifact_reference, *term))
            continue;
        if (!reusable_reference)
            reusable_reference = &artifact_reference;
        for (const auto& context : argument_package->assertedContexts) {
            if (RelationshipReferencesElement(context.sources, artifact_reference.id, artifact_reference.gid) &&
                RelationshipReferencesElement(context.targets, target_element_id)) {
                result.success = true;
                result.already_associated = true;
                result.artifact_reference_id = artifact_reference.id;
                result.asserted_context_id = context.id;
                return result;
            }
        }
    }

    if (!reusable_reference) {
        sacm::ArtifactReference artifact_reference;
        artifact_reference.id = GenerateUniqueId(package, "AR");
        artifact_reference.gid = GenerateUniqueGid(package, artifact_reference.id);
        artifact_reference.name = TermContextLabel(*term);
        artifact_reference.name_ml.set("en", artifact_reference.name);
        artifact_reference.referencedArtifact = !term->id.empty() ? term->id : term->gid;
        argument_package->artifactReferences.push_back(std::move(artifact_reference));
        reusable_reference = &argument_package->artifactReferences.back();
        result.created_artifact_reference = true;
    }

    sacm::AssertedContext context;
    context.id = GenerateUniqueId(package, "AC");
    context.gid = GenerateUniqueGid(package, context.id);
    context.name = "Context: " + TermContextLabel(*term);
    context.name_ml.set("en", context.name);
    context.sources.push_back(reusable_reference->id);
    context.targets.push_back(NormalizeRef(target_element_id));
    argument_package->assertedContexts.push_back(std::move(context));

    result.success = true;
    result.created_asserted_context = true;
    result.artifact_reference_id = reusable_reference->id;
    result.asserted_context_id = argument_package->assertedContexts.back().id;
    return result;
}

bool IsVisibleTerminologyContext(const sacm::AssertedContext& context) {
    return TrimWhitespace(context.description) == kVisibleTerminologyContextMarker;
}

TerminologyTermReferenceResolution ResolveTerminologyTermReference(const sacm::AssuranceCasePackage& package,
                                                                   const std::string& raw_ref) {
    TerminologyTermReferenceResolution resolution;
    const std::string ref = NormalizeRef(raw_ref);
    if (ref.empty())
        return resolution;

    auto resolve_from_package = [&](const sacm::TerminologyPackage& terminology_package) {
        for (const sacm::Term& term : terminology_package.terms) {
            if (!MatchesRawRef(ref, term.id, term.gid))
                continue;
            resolution.resolved = true;
            resolution.package_ref = TerminologyPackageRef{terminology_package.id, terminology_package.gid};
            resolution.term_ref = RefFor(term);
            resolution.package = &terminology_package;
            resolution.term = &term;
            return true;
        }
        return false;
    };

    for (const sacm::TerminologyPackage& terminology_package : package.terminologyPackages) {
        if (resolve_from_package(terminology_package))
            return resolution;
    }
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        for (const sacm::TerminologyPackage& terminology_package : argument_package.terminologyPackages) {
            if (resolve_from_package(terminology_package))
                return resolution;
        }
    }
    return resolution;
}

bool IsTerminologyArtifactReference(const sacm::AssuranceCasePackage& package,
                                    const sacm::ArtifactReference& artifact_reference) {
    return ArtifactReferenceTargetsAnyTerm(package, artifact_reference);
}

bool IsVisibleTerminologyArtifactReference(const sacm::AssuranceCasePackage& package,
                                           const sacm::ArgumentPackage& argument_package,
                                           const sacm::ArtifactReference& artifact_reference) {
    (void)package;
    return std::any_of(argument_package.assertedContexts.begin(),
                       argument_package.assertedContexts.end(),
                       [&](const sacm::AssertedContext& context) {
                           return IsVisibleTerminologyContext(context) &&
                                  ContextReferencesArtifact(context, artifact_reference);
                       });
}

std::vector<TerminologyContextReferenceIssue>
ValidateTerminologyContextReferences(const sacm::AssuranceCasePackage& package) {
    std::vector<TerminologyContextReferenceIssue> issues;
    std::map<std::string, const sacm::AssertedContext*> seen_visible_contexts;

    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        for (const sacm::AssertedContext& context : argument_package.assertedContexts) {
            if (!IsVisibleTerminologyContext(context))
                continue;

            for (const std::string& target_ref : context.targets) {
                if (!IsClaimOrReasoningTarget(argument_package, target_ref)) {
                    issues.push_back(MakeContextIssue(TerminologyContextReferenceIssueKind::InvalidTarget,
                                                      argument_package,
                                                      context,
                                                      nullptr,
                                                      {},
                                                      target_ref,
                                                      "Visible terminology context targets missing claim or strategy " +
                                                          DisplayRef(target_ref, "<empty>") + "."));
                }
            }

            for (const std::string& source_ref : context.sources) {
                const sacm::ArtifactReference* artifact_reference =
                    FindArtifactReferenceByRef(argument_package, source_ref);
                if (!artifact_reference) {
                    issues.push_back(
                        MakeContextIssue(TerminologyContextReferenceIssueKind::MissingArtifactReference,
                                         argument_package,
                                         context,
                                         nullptr,
                                         source_ref,
                                         context.targets.empty() ? std::string{} : context.targets.front(),
                                         "Visible terminology context references missing artifact source " +
                                             DisplayRef(source_ref, "<empty>") + "."));
                    continue;
                }

                const TerminologyTermReferenceResolution resolution =
                    ResolveTerminologyTermReference(package, artifact_reference->referencedArtifact);
                if (!resolution.resolved) {
                    issues.push_back(
                        MakeContextIssue(TerminologyContextReferenceIssueKind::MissingTerm,
                                         argument_package,
                                         context,
                                         artifact_reference,
                                         source_ref,
                                         context.targets.empty() ? std::string{} : context.targets.front(),
                                         "Visible terminology context references missing term " +
                                             DisplayRef(artifact_reference->referencedArtifact, "<empty>") + "."));
                    continue;
                }

                for (const std::string& target_ref : context.targets) {
                    if (!IsClaimOrReasoningTarget(argument_package, target_ref))
                        continue;
                    const std::string argument_package_ref =
                        argument_package.id.empty() ? argument_package.gid : argument_package.id;
                    const std::string duplicate_key = argument_package_ref + "\n" +
                                                      CanonicalTargetRef(argument_package, target_ref) + "\n" +
                                                      CanonicalTermRef(resolution);
                    const auto [it, inserted] = seen_visible_contexts.emplace(duplicate_key, &context);
                    if (inserted)
                        continue;
                    const std::string existing_id = it->second && !it->second->id.empty() ? it->second->id : "another";
                    issues.push_back(MakeContextIssue(TerminologyContextReferenceIssueKind::DuplicateContext,
                                                      argument_package,
                                                      context,
                                                      artifact_reference,
                                                      source_ref,
                                                      target_ref,
                                                      "Visible terminology context duplicates " + existing_id +
                                                          " for target " + DisplayRef(target_ref, "<empty>") +
                                                          " and term " + CanonicalTermRef(resolution) + "."));
                }
            }
        }
    }

    return issues;
}

TerminologyContextAssociationResult AddTerminologyTermAsVisibleContext(sacm::AssuranceCasePackage& package,
                                                                       const std::string& target_element_id,
                                                                       const TerminologyPackageRef& package_ref,
                                                                       const TerminologyTermRef& term_ref) {
    TerminologyContextAssociationResult result;
    const std::string target_ref = NormalizeRef(target_element_id);
    if (target_ref.empty()) {
        result.error = "Target element is required.";
        return result;
    }

    const sacm::Term* term = FindTerminologyTerm(package, package_ref, term_ref);
    if (!term) {
        result.error = "Term not found.";
        return result;
    }

    sacm::ArgumentPackage* argument_package = FindOwningArgumentPackageForVisibleContext(package, target_ref);
    if (!argument_package) {
        result.error = "Selected element is not a claim or strategy in an argument package.";
        return result;
    }

    const VisibleContextSearchResult candidates = FindVisibleContextCandidates(*argument_package, *term, target_ref);
    if (candidates.existing_visible_reference && candidates.existing_visible_context) {
        result.success = true;
        result.already_associated = true;
        result.artifact_reference_id = candidates.existing_visible_reference->id;
        result.asserted_context_id = candidates.existing_visible_context->id;
        return result;
    }

    if (candidates.promotable_reference && candidates.promotable_context) {
        candidates.promotable_context->description = kVisibleTerminologyContextMarker;
        candidates.promotable_context->description_ml.texts.clear();
        result.success = true;
        result.artifact_reference_id = candidates.promotable_reference->id;
        result.asserted_context_id = candidates.promotable_context->id;
        return result;
    }

    argument_package->artifactReferences.push_back(CreateTerminologyArtifactReference(package, *term));
    sacm::ArtifactReference& visible_reference = argument_package->artifactReferences.back();
    const std::string visible_reference_id = visible_reference.id;

    argument_package->assertedContexts.push_back(
        CreateVisibleTerminologyContext(package, *term, visible_reference.id, target_ref));

    RemoveContextsById(*argument_package, candidates.hidden_contexts_to_remove);
    RemoveUnreferencedTerminologyArtifacts(*argument_package, *term, visible_reference_id);

    result.success = true;
    result.created_artifact_reference = true;
    result.created_asserted_context = true;
    result.artifact_reference_id = visible_reference_id;
    result.asserted_context_id = argument_package->assertedContexts.back().id;
    return result;
}

} // namespace core
