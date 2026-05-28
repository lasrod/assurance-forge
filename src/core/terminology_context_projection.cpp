#include "core/terminology_context_projection.h"

#include "core/terminology_text_utils.h"
#include "parser/model_utils.h"

namespace core {

namespace {

const sacm::ArtifactReference* FindArtifactReferenceById(const sacm::AssuranceCasePackage& package,
                                                         const std::string& artifact_reference_id) {
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        for (const sacm::ArtifactReference& artifact_reference : argument_package.artifactReferences) {
            if (artifact_reference.id == artifact_reference_id || artifact_reference.gid == artifact_reference_id)
                return &artifact_reference;
        }
    }
    return nullptr;
}

const sacm::AssertedContext* FindAssertedContextById(const sacm::AssuranceCasePackage& package,
                                                     const std::string& asserted_context_id) {
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        for (const sacm::AssertedContext& context : argument_package.assertedContexts) {
            if (context.id == asserted_context_id || context.gid == asserted_context_id)
                return &context;
        }
    }
    return nullptr;
}

} // namespace

bool RefreshVisibleTerminologyContextProjection(parser::AssuranceCase&            model,
                                                const sacm::AssuranceCasePackage& package) {
    bool changed = false;
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        for (const sacm::ArtifactReference& artifact_reference : argument_package.artifactReferences) {
            if (!IsVisibleTerminologyArtifactReference(package, argument_package, artifact_reference))
                continue;
            parser::SacmElement* element =
                parser::FindElementByIdOrGid(model, artifact_reference.id, artifact_reference.gid);
            if (!element)
                continue;
            const TerminologyTermReferenceResolution resolution =
                ResolveTerminologyTermReference(package, artifact_reference.referencedArtifact);
            const std::string previous_name        = element->name;
            const std::string previous_description = element->description;
            if (!resolution.resolved || !resolution.term) {
                element->description.clear();
                element->description_langs.clear();
            } else {
                element->name      = TermContextDisplayLabel(*resolution.term);
                element->name_langs = resolution.term->name_ml.texts;
                if (element->name_langs.empty() && !element->name.empty())
                    element->name_langs["en"] = element->name;
                element->description      = resolution.term->description;
                element->description_langs = resolution.term->description_ml.texts;
                if (element->description_langs.empty() && !element->description.empty())
                    element->description_langs["en"] = element->description;
            }
            changed = changed || element->name != previous_name || element->description != previous_description;
        }
    }
    return changed;
}

bool SyncVisibleTerminologyContextToParser(parser::AssuranceCase&                     model,
                                           const sacm::AssuranceCasePackage&          package,
                                           const TerminologyContextAssociationResult& result) {
    const sacm::ArtifactReference* artifact_reference =
        FindArtifactReferenceById(package, result.artifact_reference_id);
    const sacm::AssertedContext* context = FindAssertedContextById(package, result.asserted_context_id);
    if (!artifact_reference || !context || !IsVisibleTerminologyContext(*context))
        return false;

    bool changed = false;
    if (!parser::FindElementByIdOrGid(model, artifact_reference->id, artifact_reference->gid)) {
        parser::SacmElement element;
        element.id                = artifact_reference->id;
        element.gid               = artifact_reference->gid;
        element.name              = artifact_reference->name;
        element.type              = "artifactreference";
        element.description       = artifact_reference->description;
        element.name_langs        = artifact_reference->name_ml.texts;
        element.description_langs = artifact_reference->description_ml.texts;
        model.elements.push_back(std::move(element));
        changed = true;
    }
    if (!parser::FindElementByIdOrGid(model, context->id, context->gid)) {
        parser::SacmElement element;
        element.id                    = context->id;
        element.gid                   = context->gid;
        element.name                  = context->name;
        element.type                  = "assertedcontext";
        element.description           = context->description;
        element.name_langs            = context->name_ml.texts;
        element.description_langs     = context->description_ml.texts;
        element.source_refs           = context->sources;
        element.target_refs           = context->targets;
        element.assertion_declaration = context->assertionDeclaration;
        model.elements.push_back(std::move(element));
        changed = true;
    }
    return RefreshVisibleTerminologyContextProjection(model, package) || changed;
}

} // namespace core
