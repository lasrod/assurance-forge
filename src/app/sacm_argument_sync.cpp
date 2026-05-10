#include "app/sacm_argument_sync.h"

#include "core/string_utils.h"
#include "core/terminology_package_service.h"
#include "parser/model_utils.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"

#include <algorithm>
#include <map>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace app {
namespace {

void CopyCommonSacmFields(sacm::SacmElement& target, const parser::SacmElement& source) {
    target.id = source.id;
    target.gid = source.gid;
    target.name = source.name;
    target.description = source.description;
    target.name_ml.texts = source.name_langs;
    target.description_ml.texts = source.description_langs;
    if (target.name_ml.texts.empty() && !source.name.empty())
        target.name_ml.set("en", source.name);
    if (target.description_ml.texts.empty() && !source.description.empty())
        target.description_ml.set("en", source.description);
}

void CollectTermRefs(const sacm::TerminologyPackage& terminology_package, std::unordered_set<std::string>& refs) {
    for (const sacm::Term& term : terminology_package.terms) {
        if (!term.id.empty())
            refs.insert(term.id);
        if (!term.gid.empty())
            refs.insert(term.gid);
    }
}

std::unordered_set<std::string> CollectTermRefs(const sacm::AssuranceCasePackage& package) {
    std::unordered_set<std::string> refs;
    for (const sacm::TerminologyPackage& terminology_package : package.terminologyPackages)
        CollectTermRefs(terminology_package, refs);
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        for (const sacm::TerminologyPackage& terminology_package : argument_package.terminologyPackages)
            CollectTermRefs(terminology_package, refs);
    }
    return refs;
}

bool ReferencesAny(const std::vector<std::string>& refs, const std::unordered_set<std::string>& candidates) {
    return std::any_of(refs.begin(), refs.end(), [&](const std::string& ref) {
        return candidates.find(core::StripLeadingHash(ref)) != candidates.end();
    });
}

bool ArtifactReferenceTargetsTerm(const sacm::ArtifactReference& artifact_reference,
                                  const std::unordered_set<std::string>& term_refs) {
    return term_refs.find(core::StripLeadingHash(artifact_reference.referencedArtifact)) != term_refs.end();
}

void AddElementRefs(const parser::AssuranceCase& model, std::unordered_set<std::string>& refs) {
    for (const parser::SacmElement& element : model.elements) {
        if (parser::IsRelationshipElement(element))
            continue;
        if (!element.id.empty())
            refs.insert(element.id);
        if (!element.gid.empty())
            refs.insert(element.gid);
    }
}

bool ContainsSacmElementRef(const std::vector<std::string>& refs, const std::unordered_set<std::string>& candidates) {
    return ReferencesAny(refs, candidates);
}

bool HasArtifactReference(const sacm::ArgumentPackage& argument_package, const sacm::ArtifactReference& candidate) {
    return std::any_of(argument_package.artifactReferences.begin(),
                       argument_package.artifactReferences.end(),
                       [&](const auto& existing) {
                           return (!candidate.id.empty() && existing.id == candidate.id) ||
                                  (!candidate.gid.empty() && existing.gid == candidate.gid);
                       });
}

bool HasAssertedContext(const sacm::ArgumentPackage& argument_package, const sacm::AssertedContext& candidate) {
    return std::any_of(
        argument_package.assertedContexts.begin(), argument_package.assertedContexts.end(), [&](const auto& existing) {
            return (!candidate.id.empty() && existing.id == candidate.id) ||
                   (!candidate.gid.empty() && existing.gid == candidate.gid) ||
                   (existing.sources == candidate.sources && existing.targets == candidate.targets);
        });
}

} // namespace

void RebuildSacmArgumentPackageFromParser(const parser::AssuranceCase& model, sacm::AssuranceCasePackage& package) {
    if (package.argumentPackages.empty())
        package.argumentPackages.emplace_back();
    std::map<std::string, std::string> artifact_reference_targets;
    const std::unordered_set<std::string> term_refs = CollectTermRefs(package);
    std::vector<sacm::ArtifactReference> preserved_term_references;
    std::vector<sacm::AssertedContext> preserved_term_contexts;
    for (sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        std::unordered_set<std::string> term_artifact_refs;
        for (const sacm::ArtifactReference& artifact_reference : argument_package.artifactReferences) {
            if (artifact_reference.referencedArtifact.empty())
                continue;
            if (!artifact_reference.id.empty())
                artifact_reference_targets[artifact_reference.id] = artifact_reference.referencedArtifact;
            if (!artifact_reference.gid.empty())
                artifact_reference_targets[artifact_reference.gid] = artifact_reference.referencedArtifact;
            if (ArtifactReferenceTargetsTerm(artifact_reference, term_refs) &&
                !core::IsVisibleTerminologyArtifactReference(package, argument_package, artifact_reference)) {
                preserved_term_references.push_back(artifact_reference);
                if (!artifact_reference.id.empty())
                    term_artifact_refs.insert(artifact_reference.id);
                if (!artifact_reference.gid.empty())
                    term_artifact_refs.insert(artifact_reference.gid);
            }
        }
        for (const sacm::AssertedContext& context : argument_package.assertedContexts) {
            if (ReferencesAny(context.sources, term_artifact_refs))
                preserved_term_contexts.push_back(context);
        }
        argument_package.claims.clear();
        argument_package.argumentReasonings.clear();
        argument_package.artifactReferences.clear();
        argument_package.assertedInferences.clear();
        argument_package.assertedContexts.clear();
        argument_package.assertedEvidences.clear();
    }

    sacm::ArgumentPackage& argument_package = package.argumentPackages.front();
    for (const parser::SacmElement& element : model.elements) {
        if (element.type == "claim") {
            sacm::Claim claim;
            CopyCommonSacmFields(claim, element);
            claim.content = element.content;
            claim.content_ml.texts = element.content_langs;
            if (claim.content_ml.texts.empty() && !element.content.empty())
                claim.content_ml.set("en", element.content);
            claim.assertionDeclaration = element.assertion_declaration;
            claim.undeveloped = element.undeveloped;
            argument_package.claims.push_back(std::move(claim));
        } else if (element.type == "argumentreasoning") {
            sacm::ArgumentReasoning reasoning;
            CopyCommonSacmFields(reasoning, element);
            reasoning.content = element.content;
            reasoning.content_ml.texts = element.content_langs;
            if (reasoning.content_ml.texts.empty() && !element.content.empty())
                reasoning.content_ml.set("en", element.content);
            reasoning.undeveloped = element.undeveloped;
            argument_package.argumentReasonings.push_back(std::move(reasoning));
        } else if (element.type == "artifactreference" || element.type == "artifact") {
            sacm::ArtifactReference artifact_reference;
            CopyCommonSacmFields(artifact_reference, element);
            auto target = artifact_reference_targets.find(element.id);
            if (target == artifact_reference_targets.end())
                target = artifact_reference_targets.find(element.gid);
            if (target != artifact_reference_targets.end())
                artifact_reference.referencedArtifact = target->second;
            argument_package.artifactReferences.push_back(std::move(artifact_reference));
        } else if (element.type == "assertedinference") {
            sacm::AssertedInference inference;
            CopyCommonSacmFields(inference, element);
            inference.sources = element.source_refs;
            inference.targets = element.target_refs;
            inference.reasoning = element.reasoning_ref;
            inference.assertionDeclaration = element.assertion_declaration;
            argument_package.assertedInferences.push_back(std::move(inference));
        } else if (element.type == "assertedcontext") {
            sacm::AssertedContext context;
            CopyCommonSacmFields(context, element);
            context.sources = element.source_refs;
            context.targets = element.target_refs;
            context.assertionDeclaration = element.assertion_declaration;
            argument_package.assertedContexts.push_back(std::move(context));
        } else if (element.type == "assertedevidence") {
            sacm::AssertedEvidence evidence;
            CopyCommonSacmFields(evidence, element);
            evidence.sources = element.source_refs;
            evidence.targets = element.target_refs;
            evidence.assertionDeclaration = element.assertion_declaration;
            argument_package.assertedEvidences.push_back(std::move(evidence));
        }
    }

    std::unordered_set<std::string> model_element_refs;
    AddElementRefs(model, model_element_refs);
    std::unordered_set<std::string> kept_artifact_refs;
    for (const sacm::ArtifactReference& artifact_reference : preserved_term_references) {
        if (HasArtifactReference(argument_package, artifact_reference))
            continue;
        argument_package.artifactReferences.push_back(artifact_reference);
        if (!artifact_reference.id.empty())
            kept_artifact_refs.insert(artifact_reference.id);
        if (!artifact_reference.gid.empty())
            kept_artifact_refs.insert(artifact_reference.gid);
    }
    for (const sacm::AssertedContext& context : preserved_term_contexts) {
        if (!ReferencesAny(context.sources, kept_artifact_refs) ||
            !ContainsSacmElementRef(context.targets, model_element_refs)) {
            continue;
        }
        if (!HasAssertedContext(argument_package, context))
            argument_package.assertedContexts.push_back(context);
    }
    for (sacm::ArtifactReference& artifact_reference : argument_package.artifactReferences) {
        if (!core::IsVisibleTerminologyArtifactReference(package, argument_package, artifact_reference))
            continue;
        artifact_reference.description.clear();
        artifact_reference.description_ml.texts.clear();
    }
}

} // namespace app
