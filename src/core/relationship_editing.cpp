#include "core/relationship_editing.h"

#include "core/problems/gsn_wellformedness.h"
#include "core/string_utils.h"

#include <algorithm>
#include <string>
#include <vector>

namespace core {
namespace {

parser::SacmElement* FindRelationship(parser::AssuranceCase& ac, const std::string& id) {
    for (parser::SacmElement& element : ac.elements) {
        if (element.id != id)
            continue;
        return GsnKindOf(element) == GsnElementKind::Relationship ? &element : nullptr;
    }
    return nullptr;
}

// SACM multiplicity, in the terms the parser model uses. An AssertedInference
// needs a target plus either a source or a reasoning (clause 11.13 read together
// with the GSN strategy encoding); everything else needs a source and a target.
// Matches `IsParserRelationshipDangling` in element_factory, which is what the
// node-removal path drops on.
bool IsStructurallyEmpty(const parser::SacmElement& relationship) {
    if (relationship.target_refs.empty())
        return true;
    if (relationship.type == "assertedinference")
        return relationship.source_refs.empty() && relationship.reasoning_ref.empty();
    return relationship.source_refs.empty();
}

bool EraseReference(std::vector<std::string>& refs, const std::string& reference) {
    const size_t before = refs.size();
    std::erase_if(refs, [&](const std::string& candidate) { return NormalizeRef(candidate) == reference; });
    return refs.size() != before;
}

// Drops the relationship from every argument package that holds it. The id is
// unique across the case, so at most one erase does anything.
void ErasePackageRelationship(sacm::AssuranceCasePackage* pkg, const std::string& relationship_id) {
    if (!pkg)
        return;
    for (sacm::ArgumentPackage& argument_package : pkg->argumentPackages) {
        std::erase_if(argument_package.assertedInferences,
                      [&](const sacm::AssertedInference& r) { return r.id == relationship_id; });
        std::erase_if(argument_package.assertedContexts,
                      [&](const sacm::AssertedContext& r) { return r.id == relationship_id; });
        std::erase_if(argument_package.assertedEvidences,
                      [&](const sacm::AssertedEvidence& r) { return r.id == relationship_id; });
    }
}

// Applies the same reference drop to the package copy of the relationship, so
// the render model and the model that gets saved stay in step.
void DropPackageReference(sacm::AssuranceCasePackage* pkg,
                          const std::string& relationship_id,
                          const std::string& reference) {
    if (!pkg)
        return;
    for (sacm::ArgumentPackage& argument_package : pkg->argumentPackages) {
        for (sacm::AssertedInference& r : argument_package.assertedInferences) {
            if (r.id != relationship_id)
                continue;
            EraseReference(r.sources, reference);
            EraseReference(r.targets, reference);
            if (NormalizeRef(r.reasoning) == reference)
                r.reasoning.clear();
        }
        for (sacm::AssertedContext& r : argument_package.assertedContexts) {
            if (r.id != relationship_id)
                continue;
            EraseReference(r.sources, reference);
            EraseReference(r.targets, reference);
        }
        for (sacm::AssertedEvidence& r : argument_package.assertedEvidences) {
            if (r.id != relationship_id)
                continue;
            EraseReference(r.sources, reference);
            EraseReference(r.targets, reference);
        }
    }
}

} // namespace

bool RemoveRelationship(parser::AssuranceCase& ac,
                        sacm::AssuranceCasePackage* pkg,
                        const std::string& relationship_id,
                        std::string& out_error) {
    out_error.clear();
    if (relationship_id.empty()) {
        out_error = "No relationship id supplied.";
        return false;
    }
    if (!FindRelationship(ac, relationship_id)) {
        out_error = "Relationship not found in model: " + relationship_id + ".";
        return false;
    }

    std::erase_if(ac.elements, [&](const parser::SacmElement& element) { return element.id == relationship_id; });
    ErasePackageRelationship(pkg, relationship_id);
    return true;
}

bool DropRelationshipReference(parser::AssuranceCase& ac,
                               sacm::AssuranceCasePackage* pkg,
                               const std::string& relationship_id,
                               const std::string& reference,
                               bool& out_removed_relationship,
                               std::string& out_error) {
    out_error.clear();
    out_removed_relationship = false;

    const std::string normalized = NormalizeRef(reference);
    if (normalized.empty()) {
        out_error = "No reference supplied.";
        return false;
    }
    parser::SacmElement* relationship = FindRelationship(ac, relationship_id);
    if (!relationship) {
        out_error = "Relationship not found in model: " + relationship_id + ".";
        return false;
    }

    bool dropped = EraseReference(relationship->source_refs, normalized);
    dropped = EraseReference(relationship->target_refs, normalized) || dropped;
    if (NormalizeRef(relationship->reasoning_ref) == normalized) {
        relationship->reasoning_ref.clear();
        dropped = true;
    }
    if (!dropped) {
        out_error = "Relationship " + relationship_id + " does not reference " + normalized + ".";
        return false;
    }

    DropPackageReference(pkg, relationship_id, normalized);

    // Re-find rather than reuse the pointer: nothing has invalidated it here, but
    // the removal below erases from the same vector and the ordering matters.
    if (IsStructurallyEmpty(*FindRelationship(ac, relationship_id))) {
        out_removed_relationship = true;
        std::string removal_error;
        if (!RemoveRelationship(ac, pkg, relationship_id, removal_error)) {
            out_error = removal_error;
            return false;
        }
    }
    return true;
}

bool MoveStrategyToReasoning(parser::AssuranceCase& ac,
                             sacm::AssuranceCasePackage* pkg,
                             const std::string& relationship_id,
                             const std::string& strategy_id,
                             std::string& out_error) {
    out_error.clear();

    parser::SacmElement* relationship = FindRelationship(ac, relationship_id);
    if (!relationship) {
        out_error = "Relationship not found in model: " + relationship_id + ".";
        return false;
    }
    if (relationship->type != "assertedinference") {
        out_error = "Only an inference carries a reasoning; " + relationship_id + " is a " + relationship->type + ".";
        return false;
    }
    if (!relationship->reasoning_ref.empty()) {
        out_error = "Inference " + relationship_id + " already has the reasoning " + relationship->reasoning_ref + ".";
        return false;
    }

    const std::string normalized_strategy = NormalizeRef(strategy_id);
    const auto is_strategy_source = [&](const std::string& candidate) {
        return NormalizeRef(candidate) == normalized_strategy;
    };
    if (std::none_of(relationship->source_refs.begin(), relationship->source_refs.end(), is_strategy_source)) {
        out_error = "Inference " + relationship_id + " does not have " + strategy_id + " as a source.";
        return false;
    }

    std::erase_if(relationship->source_refs, is_strategy_source);
    relationship->reasoning_ref = normalized_strategy;

    if (pkg) {
        for (sacm::ArgumentPackage& argument_package : pkg->argumentPackages) {
            for (sacm::AssertedInference& inference : argument_package.assertedInferences) {
                if (inference.id != relationship_id)
                    continue;
                EraseReference(inference.sources, normalized_strategy);
                inference.reasoning = normalized_strategy;
            }
        }
    }
    return true;
}

} // namespace core
