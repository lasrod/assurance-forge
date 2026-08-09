#include "core/terminology_package_service.h"

#include "core/string_utils.h"
#include "core/terminology_internal.h"
#include "core/terminology_scope_service.h"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace core {
namespace {

using detail::GenerateUniqueGid;
using detail::GenerateUniqueId;
using detail::MatchesRawRef;

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
                                                           const sacm::Term& term,
                                                           const std::string& forced_id,
                                                           const std::string& forced_gid) {
    sacm::ArtifactReference artifact_reference;
    artifact_reference.id = forced_id.empty() ? GenerateUniqueId(package, "TC") : forced_id;
    artifact_reference.gid = forced_gid.empty() ? GenerateUniqueGid(package, artifact_reference.id) : forced_gid;
    artifact_reference.name = TermContextLabel(term);
    artifact_reference.name_ml.set("en", artifact_reference.name);
    artifact_reference.referencedArtifact = !term.id.empty() ? term.id : term.gid;
    return artifact_reference;
}

sacm::AssertedContext CreateVisibleTerminologyContext(const sacm::AssuranceCasePackage& package,
                                                      const sacm::Term& term,
                                                      const std::string& source_ref,
                                                      const std::string& target_ref,
                                                      const std::string& forced_id,
                                                      const std::string& forced_gid) {
    sacm::AssertedContext context;
    context.id = forced_id.empty() ? GenerateUniqueId(package, "AC") : forced_id;
    context.gid = forced_gid.empty() ? GenerateUniqueGid(package, context.id) : forced_gid;
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

namespace {

// Shared by the two planners: the AssertedContext half is identical, only the
// artifact-reference prefix differs ("AR" when associating, "TC" when adding a
// visible context).
TerminologyContextForcedIds PlanContextIdentities(const sacm::AssuranceCasePackage& package,
                                                  const std::string& reference_prefix) {
    TerminologyContextForcedIds planned;
    planned.artifact_reference_id = GenerateUniqueId(package, reference_prefix);
    planned.artifact_reference_gid = GenerateUniqueGid(package, planned.artifact_reference_id);
    planned.asserted_context_id = GenerateUniqueId(package, "AC");
    planned.asserted_context_gid = GenerateUniqueGid(package, planned.asserted_context_id);
    return planned;
}

} // namespace

TerminologyContextForcedIds PlanTerminologyAssociationIdentities(const sacm::AssuranceCasePackage& package) {
    return PlanContextIdentities(package, "AR");
}

TerminologyContextForcedIds PlanVisibleTerminologyContextIdentities(const sacm::AssuranceCasePackage& package) {
    return PlanContextIdentities(package, "TC");
}

TerminologyContextAssociationResult
AssociateTerminologyTermWithElementWithIds(sacm::AssuranceCasePackage& package,
                                           const std::string& target_element_id,
                                           const TerminologyPackageRef& package_ref,
                                           const TerminologyTermRef& term_ref,
                                           const TerminologyContextForcedIds& forced) {
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
                result.artifact_reference_gid = artifact_reference.gid;
                result.asserted_context_id = context.id;
                result.asserted_context_gid = context.gid;
                return result;
            }
        }
    }

    if (!reusable_reference) {
        sacm::ArtifactReference artifact_reference;
        artifact_reference.id =
            forced.artifact_reference_id.empty() ? GenerateUniqueId(package, "AR") : forced.artifact_reference_id;
        artifact_reference.gid = forced.artifact_reference_gid.empty()
                                     ? GenerateUniqueGid(package, artifact_reference.id)
                                     : forced.artifact_reference_gid;
        artifact_reference.name = TermContextLabel(*term);
        artifact_reference.name_ml.set("en", artifact_reference.name);
        artifact_reference.referencedArtifact = !term->id.empty() ? term->id : term->gid;
        argument_package->artifactReferences.push_back(std::move(artifact_reference));
        reusable_reference = &argument_package->artifactReferences.back();
        result.created_artifact_reference = true;
    }

    sacm::AssertedContext context;
    context.id = forced.asserted_context_id.empty() ? GenerateUniqueId(package, "AC") : forced.asserted_context_id;
    context.gid =
        forced.asserted_context_gid.empty() ? GenerateUniqueGid(package, context.id) : forced.asserted_context_gid;
    context.name = "Context: " + TermContextLabel(*term);
    context.name_ml.set("en", context.name);
    context.sources.push_back(reusable_reference->id);
    context.targets.push_back(NormalizeRef(target_element_id));
    argument_package->assertedContexts.push_back(std::move(context));

    result.success = true;
    result.created_asserted_context = true;
    result.artifact_reference_id = reusable_reference->id;
    result.artifact_reference_gid = reusable_reference->gid;
    result.asserted_context_id = argument_package->assertedContexts.back().id;
    result.asserted_context_gid = argument_package->assertedContexts.back().gid;
    return result;
}

TerminologyContextAssociationResult AssociateTerminologyTermWithElement(sacm::AssuranceCasePackage& package,
                                                                        const std::string& target_element_id,
                                                                        const TerminologyPackageRef& package_ref,
                                                                        const TerminologyTermRef& term_ref) {
    return AssociateTerminologyTermWithElementWithIds(package, target_element_id, package_ref, term_ref, {});
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
            resolution.term_ref = detail::RefFor(term);
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

TerminologyContextAssociationResult
AddTerminologyTermAsVisibleContextWithIds(sacm::AssuranceCasePackage& package,
                                          const std::string& target_element_id,
                                          const TerminologyPackageRef& package_ref,
                                          const TerminologyTermRef& term_ref,
                                          const TerminologyContextForcedIds& forced) {
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
        result.artifact_reference_gid = candidates.existing_visible_reference->gid;
        result.asserted_context_id = candidates.existing_visible_context->id;
        result.asserted_context_gid = candidates.existing_visible_context->gid;
        return result;
    }

    if (candidates.promotable_reference && candidates.promotable_context) {
        candidates.promotable_context->description = kVisibleTerminologyContextMarker;
        candidates.promotable_context->description_ml.texts.clear();
        result.success = true;
        result.artifact_reference_id = candidates.promotable_reference->id;
        result.artifact_reference_gid = candidates.promotable_reference->gid;
        result.asserted_context_id = candidates.promotable_context->id;
        result.asserted_context_gid = candidates.promotable_context->gid;
        return result;
    }

    argument_package->artifactReferences.push_back(CreateTerminologyArtifactReference(
        package, *term, forced.artifact_reference_id, forced.artifact_reference_gid));
    sacm::ArtifactReference& visible_reference = argument_package->artifactReferences.back();
    const std::string visible_reference_id = visible_reference.id;
    const std::string visible_reference_gid = visible_reference.gid;

    argument_package->assertedContexts.push_back(CreateVisibleTerminologyContext(
        package, *term, visible_reference.id, target_ref, forced.asserted_context_id, forced.asserted_context_gid));

    RemoveContextsById(*argument_package, candidates.hidden_contexts_to_remove);
    RemoveUnreferencedTerminologyArtifacts(*argument_package, *term, visible_reference_id);

    result.success = true;
    result.created_artifact_reference = true;
    result.created_asserted_context = true;
    result.artifact_reference_id = visible_reference_id;
    result.artifact_reference_gid = visible_reference_gid;
    result.asserted_context_id = argument_package->assertedContexts.back().id;
    result.asserted_context_gid = argument_package->assertedContexts.back().gid;
    return result;
}

TerminologyContextAssociationResult AddTerminologyTermAsVisibleContext(sacm::AssuranceCasePackage& package,
                                                                       const std::string& target_element_id,
                                                                       const TerminologyPackageRef& package_ref,
                                                                       const TerminologyTermRef& term_ref) {
    return AddTerminologyTermAsVisibleContextWithIds(package, target_element_id, package_ref, term_ref, {});
}

} // namespace core
