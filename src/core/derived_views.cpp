#include "core/derived_views.h"

#include "core/string_utils.h"
#include "core/terminology_package_service.h"
#include "core/terminology_text_utils.h"
#include "sacm_adapter/gsn_role_tag.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace core {

namespace {

bool ReferencesAny(const std::vector<std::string>& refs, const std::unordered_set<std::string>& candidates) {
    return std::any_of(refs.begin(), refs.end(), [&](const std::string& ref) {
        return candidates.find(StripLeadingHash(ref)) != candidates.end();
    });
}

void AddRef(std::unordered_set<std::string>& refs, const std::string& ref) {
    if (!ref.empty())
        refs.insert(ref);
}

struct HiddenTerminologyRefs {
    std::unordered_set<std::string> artifact_reference_refs;
    std::unordered_set<std::string> asserted_context_refs;
};

bool ContextSourcesArtifactReference(const sacm::AssertedContext& context,
                                     const sacm::ArtifactReference& artifact_reference) {
    std::unordered_set<std::string> artifact_refs;
    AddRef(artifact_refs, artifact_reference.id);
    AddRef(artifact_refs, artifact_reference.gid);
    return ReferencesAny(context.sources, artifact_refs);
}

HiddenTerminologyRefs CollectHiddenTerminologyRefs(const sacm::AssuranceCasePackage& package) {
    HiddenTerminologyRefs refs;
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        for (const sacm::ArtifactReference& artifact_reference : argument_package.artifactReferences) {
            if (!IsTerminologyArtifactReference(package, artifact_reference))
                continue;
            if (!IsVisibleTerminologyArtifactReference(package, argument_package, artifact_reference)) {
                AddRef(refs.artifact_reference_refs, artifact_reference.id);
                AddRef(refs.artifact_reference_refs, artifact_reference.gid);
            }
            for (const sacm::AssertedContext& context : argument_package.assertedContexts) {
                if (!ContextSourcesArtifactReference(context, artifact_reference) ||
                    IsVisibleTerminologyContext(context))
                    continue;
                AddRef(refs.asserted_context_refs, context.id);
                AddRef(refs.asserted_context_refs, context.gid);
            }
        }
    }
    return refs;
}

} // namespace

void HideTerminologyArtifactReferences(parser::AssuranceCase& model, const sacm::AssuranceCasePackage& package) {
    const HiddenTerminologyRefs hidden_refs = CollectHiddenTerminologyRefs(package);
    if (hidden_refs.artifact_reference_refs.empty() && hidden_refs.asserted_context_refs.empty())
        return;

    model.elements.erase(
        std::remove_if(model.elements.begin(),
                       model.elements.end(),
                       [&](const parser::SacmElement& element) {
                           if (element.type == "artifactreference") {
                               return hidden_refs.artifact_reference_refs.find(element.id) !=
                                          hidden_refs.artifact_reference_refs.end() ||
                                      hidden_refs.artifact_reference_refs.find(element.gid) !=
                                          hidden_refs.artifact_reference_refs.end();
                           }
                           return element.type == "assertedcontext" &&
                                  (hidden_refs.asserted_context_refs.find(element.id) !=
                                       hidden_refs.asserted_context_refs.end() ||
                                   hidden_refs.asserted_context_refs.find(element.gid) !=
                                       hidden_refs.asserted_context_refs.end() ||
                                   ReferencesAny(element.source_refs, hidden_refs.artifact_reference_refs));
                       }),
        model.elements.end());
}

void RefreshVisibleTerminologyContextDisplay(parser::AssuranceCase& model, const sacm::AssuranceCasePackage& package) {
    // Index elements by id and gid once so each artifact reference is an O(1) lookup instead of a
    // linear scan of every element (the loop below is otherwise O(references * elements)). The
    // index records the first occurrence per key and a lookup prefers the lowest matching index,
    // reproducing parser::FindElementByIdOrGid's "first element matching id or gid" behaviour.
    const std::size_t none = model.elements.size();
    std::unordered_map<std::string, std::size_t> first_index_by_id;
    std::unordered_map<std::string, std::size_t> first_index_by_gid;
    first_index_by_id.reserve(model.elements.size());
    first_index_by_gid.reserve(model.elements.size());
    for (std::size_t i = 0; i < model.elements.size(); ++i) {
        const parser::SacmElement& element = model.elements[i];
        if (!element.id.empty())
            first_index_by_id.emplace(element.id, i);
        if (!element.gid.empty())
            first_index_by_gid.emplace(element.gid, i);
    }
    const auto find_element = [&](const std::string& id, const std::string& gid) -> parser::SacmElement* {
        std::size_t best = none;
        if (!id.empty()) {
            const auto it = first_index_by_id.find(id);
            if (it != first_index_by_id.end())
                best = it->second;
        }
        if (!gid.empty()) {
            const auto it = first_index_by_gid.find(gid);
            if (it != first_index_by_gid.end())
                best = std::min(best, it->second);
        }
        return best == none ? nullptr : &model.elements[best];
    };

    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        for (const sacm::ArtifactReference& artifact_reference : argument_package.artifactReferences) {
            if (!IsVisibleTerminologyArtifactReference(package, argument_package, artifact_reference))
                continue;
            parser::SacmElement* element = find_element(artifact_reference.id, artifact_reference.gid);
            if (!element)
                continue;
            const TerminologyTermReferenceResolution resolution =
                ResolveTerminologyTermReference(package, artifact_reference.referencedArtifact);
            if (!resolution.resolved || !resolution.term) {
                element->description.clear();
                element->description_langs.clear();
                continue;
            }

            element->name = TermContextDisplayLabel(*resolution.term);
            element->name_langs = resolution.term->name_ml.texts;
            if (element->name_langs.empty() && !element->name.empty())
                element->name_langs["en"] = element->name;
            element->description = resolution.term->description;
            element->description_langs = resolution.term->description_ml.texts;
            if (element->description_langs.empty() && !element->description.empty())
                element->description_langs["en"] = element->description;
        }
    }
}

void SynthesizeBareStrategyPlacements(parser::AssuranceCase& model,
                                      const sacm::AssuranceCasePackage& package) {
    // Strategy ids that already have a real inference in the render model.
    std::unordered_set<std::string> materialized;
    for (const parser::SacmElement& element : model.elements) {
        if (element.type == "assertedinference" && !element.reasoning_ref.empty())
            materialized.insert(element.reasoning_ref);
    }

    std::vector<parser::SacmElement> placeholders;
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        for (const sacm::ArgumentReasoning& reasoning : argument_package.argumentReasonings) {
            if (materialized.find(reasoning.id) != materialized.end())
                continue;  // a real inference already places this strategy
            std::string target;
            for (const sacm::TaggedValue& tag : reasoning.taggedValues) {
                if (tag.key == sacm_adapter::kGsnStrategyTargetTagKey) {
                    target = tag.value;
                    break;
                }
            }
            if (target.empty())
                continue;  // not a bare strategy
            parser::SacmElement placeholder;
            placeholder.id = reasoning.id + "__pending_inference";
            placeholder.type = "assertedinference";
            placeholder.reasoning_ref = reasoning.id;
            placeholder.target_refs.push_back(target);
            placeholders.push_back(std::move(placeholder));
        }
    }
    for (parser::SacmElement& placeholder : placeholders)
        model.elements.push_back(std::move(placeholder));
}

} // namespace core
