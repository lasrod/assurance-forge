#include "core/reviews/review_proposal_patch_service.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <unordered_set>

namespace core {
namespace {

bool IsCreateOperation(PatchOperationType type) {
    switch (type) {
        case PatchOperationType::CreateClaim:
        case PatchOperationType::CreateStrategy:
        case PatchOperationType::CreateSolution:
        case PatchOperationType::CreateContext:
        case PatchOperationType::CreateAssumption:
        case PatchOperationType::CreateJustification:
            return true;
        default:
            return false;
    }
}

bool IsRelationshipType(const std::string& type) {
    return type == "assertedinference" || type == "assertedcontext" || type == "assertedevidence";
}

const char* PrefixFor(PatchOperationType type) {
    switch (type) {
        case PatchOperationType::CreateClaim: return "G";
        case PatchOperationType::CreateStrategy: return "S";
        case PatchOperationType::CreateSolution: return "Sn";
        case PatchOperationType::CreateContext: return "C";
        case PatchOperationType::CreateAssumption: return "A";
        case PatchOperationType::CreateJustification: return "J";
        default: return "N";
    }
}

const char* DefaultNameFor(PatchOperationType type) {
    switch (type) {
        case PatchOperationType::CreateClaim: return "New Goal";
        case PatchOperationType::CreateStrategy: return "New Strategy";
        case PatchOperationType::CreateSolution: return "New Solution";
        case PatchOperationType::CreateContext: return "New Context";
        case PatchOperationType::CreateAssumption: return "New Assumption";
        case PatchOperationType::CreateJustification: return "New Justification";
        default: return "New Element";
    }
}

std::string ElementTypeFor(PatchOperationType type) {
    switch (type) {
        case PatchOperationType::CreateClaim:
        case PatchOperationType::CreateContext:
        case PatchOperationType::CreateAssumption:
        case PatchOperationType::CreateJustification:
            return "claim";
        case PatchOperationType::CreateStrategy:
            return "argumentreasoning";
        case PatchOperationType::CreateSolution:
            return "artifactreference";
        default:
            return {};
    }
}

std::string AssertionDeclarationFor(PatchOperationType type) {
    switch (type) {
        case PatchOperationType::CreateAssumption: return "assumed";
        case PatchOperationType::CreateJustification: return "justification";
        default: return {};
    }
}

parser::SacmElement* FindElement(parser::AssuranceCase& model, const std::string& id) {
    for (parser::SacmElement& element : model.elements) {
        if (element.id == id) return &element;
    }
    return nullptr;
}

const parser::SacmElement* FindElement(const parser::AssuranceCase& model, const std::string& id) {
    for (const parser::SacmElement& element : model.elements) {
        if (element.id == id) return &element;
    }
    return nullptr;
}

std::unordered_set<std::string> CollectIds(const parser::AssuranceCase& model) {
    std::unordered_set<std::string> ids;
    ids.reserve(model.elements.size() * 2);
    for (const parser::SacmElement& element : model.elements) {
        if (!element.id.empty()) ids.insert(element.id);
    }
    return ids;
}

std::string GenerateUniqueId(std::unordered_set<std::string>& ids, const std::string& prefix) {
    for (int i = 1; i < 100000; ++i) {
        std::string candidate = prefix + std::to_string(i);
        if (ids.insert(candidate).second) return candidate;
    }
    std::string fallback = prefix + "x" + std::to_string(ids.size() + 1);
    ids.insert(fallback);
    return fallback;
}

std::string GenerateRelationshipId(std::unordered_set<std::string>& ids) {
    return GenerateUniqueId(ids, "R");
}

bool ValidateCreateRefName(const std::string& create_ref, std::string& error) {
    if (create_ref.empty()) {
        error = "Create operation is missing create_ref.";
        return false;
    }
    if (create_ref.front() != '$') {
        error = "Patch-local create_ref values must start with '$'.";
        return false;
    }
    return true;
}

bool CollectGeneratedIds(const ReviewProposal& proposal,
                         const parser::AssuranceCase& current_model,
                         std::map<std::string, std::string>& generated_ids,
                         std::string& error) {
    generated_ids.clear();
    std::unordered_set<std::string> ids = CollectIds(current_model);
    for (const PatchOperation& operation : proposal.operations) {
        if (!IsCreateOperation(operation.type)) continue;
        if (!operation.create_ref.has_value() || !ValidateCreateRefName(operation.create_ref.value(), error)) {
            return false;
        }
        if (generated_ids.count(operation.create_ref.value()) > 0) {
            error = "Duplicate patch-local create_ref: " + operation.create_ref.value();
            return false;
        }
        generated_ids[operation.create_ref.value()] = GenerateUniqueId(ids, PrefixFor(operation.type));
    }
    return true;
}

bool ResolveRef(const ElementRef& ref,
                const parser::AssuranceCase& model,
                const std::map<std::string, std::string>& generated_ids,
                std::string& resolved_id,
                std::string& error) {
    resolved_id.clear();
    const bool has_existing = ref.existing_id.has_value() && !ref.existing_id->empty();
    const bool has_create = ref.create_ref.has_value() && !ref.create_ref->empty();
    if (has_existing == has_create) {
        error = "Element references must contain exactly one existing id or create_ref.";
        return false;
    }
    if (has_existing) {
        if (!FindElement(model, ref.existing_id.value())) {
            error = "Operation references missing existing element " + ref.existing_id.value() + ".";
            return false;
        }
        resolved_id = ref.existing_id.value();
        return true;
    }

    auto found = generated_ids.find(ref.create_ref.value());
    if (found == generated_ids.end()) {
        error = "Operation references unknown created element " + ref.create_ref.value() + ".";
        return false;
    }
    resolved_id = found->second;
    return true;
}

bool ResolveOptionalElementRef(const std::optional<ElementRef>& ref,
                               const parser::AssuranceCase& model,
                               const std::map<std::string, std::string>& generated_ids,
                               const std::string& role,
                               std::string& resolved_id,
                               std::string& error) {
    if (!ref.has_value()) {
        error = "Operation is missing " + role + " reference.";
        return false;
    }
    return ResolveRef(ref.value(), model, generated_ids, resolved_id, error);
}

void SetElementText(parser::SacmElement& element, const std::string& value) {
    if (element.type == "claim" || element.type == "argumentreasoning") {
        element.content = value;
        element.content_langs["en"] = value;
    } else {
        element.description = value;
        element.description_langs["en"] = value;
    }
}

bool ApplyCreateOperation(const PatchOperation& operation,
                          parser::AssuranceCase& model,
                          const std::map<std::string, std::string>& generated_ids,
                          std::string& error) {
    if (!operation.create_ref.has_value()) {
        error = "Create operation is missing create_ref.";
        return false;
    }
    auto id_it = generated_ids.find(operation.create_ref.value());
    if (id_it == generated_ids.end()) {
        error = "Create operation references unknown create_ref " + operation.create_ref.value() + ".";
        return false;
    }
    if (FindElement(model, id_it->second)) {
        error = "Generated element id already exists: " + id_it->second;
        return false;
    }

    parser::SacmElement element;
    element.id = id_it->second;
    element.type = ElementTypeFor(operation.type);
    element.name = DefaultNameFor(operation.type);
    element.name_langs["en"] = element.name;
    element.assertion_declaration = AssertionDeclarationFor(operation.type);
    if (!operation.text.empty()) {
        SetElementText(element, operation.text);
    }
    model.elements.push_back(std::move(element));
    return true;
}

bool AddUnique(std::vector<std::string>& values, const std::string& value) {
    if (std::find(values.begin(), values.end(), value) != values.end()) return false;
    values.push_back(value);
    return true;
}

bool RemoveValue(std::vector<std::string>& values, const std::string& value) {
    const auto old_size = values.size();
    values.erase(std::remove(values.begin(), values.end(), value), values.end());
    return values.size() != old_size;
}

bool IsEvidenceLikeElement(const parser::SacmElement& element) {
    return element.type == "artifact" || element.type == "artifactreference" || element.type == "expression";
}

bool ApplyUpdateOperation(const PatchOperation& operation,
                          parser::AssuranceCase& model,
                          const std::map<std::string, std::string>& generated_ids,
                          std::string& error) {
    std::string element_id;
    if (!ResolveOptionalElementRef(operation.element, model, generated_ids, "element", element_id, error)) return false;
    parser::SacmElement* element = FindElement(model, element_id);
    if (!element) {
        error = "Operation references missing element " + element_id + ".";
        return false;
    }

    switch (operation.type) {
        case PatchOperationType::UpdateElementName:
            element->name = operation.new_value;
            element->name_langs["en"] = operation.new_value;
            return true;
        case PatchOperationType::UpdateElementText:
            if (operation.field == "name") {
                element->name = operation.new_value;
                element->name_langs["en"] = operation.new_value;
            } else if (operation.field == "content" || operation.field.empty()) {
                element->content = operation.new_value;
                element->content_langs["en"] = operation.new_value;
            } else if (operation.field == "description") {
                element->description = operation.new_value;
                element->description_langs["en"] = operation.new_value;
            } else {
                error = "Unsupported UpdateElementText field: " + operation.field;
                return false;
            }
            return true;
        case PatchOperationType::SetUndeveloped:
            element->undeveloped = true;
            return true;
        case PatchOperationType::ClearUndeveloped:
            element->undeveloped = false;
            return true;
        default:
            error = "Unsupported update operation.";
            return false;
    }
}

bool ApplyAddRelationshipOperation(const PatchOperation& operation,
                                   parser::AssuranceCase& model,
                                   const std::map<std::string, std::string>& generated_ids,
                                   std::unordered_set<std::string>& ids,
                                   std::string& error) {
    std::string source_id;
    std::string target_id;
    if (!ResolveOptionalElementRef(operation.source, model, generated_ids, "source", source_id, error) ||
        !ResolveOptionalElementRef(operation.target, model, generated_ids, "target", target_id, error)) {
        return false;
    }

    const parser::SacmElement* source = FindElement(model, source_id);
    const parser::SacmElement* target = FindElement(model, target_id);
    if (!source || !target) {
        error = "Relationship operation references an element that does not exist.";
        return false;
    }

    parser::SacmElement relationship;
    relationship.id = GenerateRelationshipId(ids);
    relationship.target_refs.push_back(target_id);

    if (operation.type == PatchOperationType::AddInContextOf) {
        relationship.type = "assertedcontext";
        relationship.source_refs.push_back(source_id);
    } else if (IsEvidenceLikeElement(*source)) {
        relationship.type = "assertedevidence";
        relationship.source_refs.push_back(source_id);
    } else {
        relationship.type = "assertedinference";
        if (source->type == "argumentreasoning") {
            relationship.reasoning_ref = source_id;
        } else {
            relationship.source_refs.push_back(source_id);
        }
    }

    model.elements.push_back(std::move(relationship));
    return true;
}

bool RelationshipMatches(const parser::SacmElement& relationship,
                         PatchOperationType operation_type,
                         const std::string& source_id,
                         const std::string& target_id) {
    const bool target_matches = std::find(relationship.target_refs.begin(), relationship.target_refs.end(), target_id) != relationship.target_refs.end();
    if (!target_matches) return false;

    if (operation_type == PatchOperationType::RemoveInContextOf) {
        return relationship.type == "assertedcontext" &&
               std::find(relationship.source_refs.begin(), relationship.source_refs.end(), source_id) != relationship.source_refs.end();
    }

    if (relationship.type == "assertedevidence") {
        return std::find(relationship.source_refs.begin(), relationship.source_refs.end(), source_id) != relationship.source_refs.end();
    }

    if (relationship.type != "assertedinference") return false;
    if (relationship.reasoning_ref == source_id) return true;
    return std::find(relationship.source_refs.begin(), relationship.source_refs.end(), source_id) != relationship.source_refs.end();
}

bool IsDanglingRelationship(const parser::SacmElement& relationship) {
    if (!IsRelationshipType(relationship.type)) return false;
    if (relationship.target_refs.empty()) return true;
    if (relationship.type == "assertedinference") {
        return relationship.source_refs.empty() && relationship.reasoning_ref.empty();
    }
    return relationship.source_refs.empty();
}

bool ApplyRemoveRelationshipOperation(const PatchOperation& operation,
                                      parser::AssuranceCase& model,
                                      const std::map<std::string, std::string>& generated_ids,
                                      std::string& error) {
    std::string source_id;
    std::string target_id;
    if (!ResolveOptionalElementRef(operation.source, model, generated_ids, "source", source_id, error) ||
        !ResolveOptionalElementRef(operation.target, model, generated_ids, "target", target_id, error)) {
        return false;
    }

    bool removed = false;
    for (parser::SacmElement& relationship : model.elements) {
        if (!RelationshipMatches(relationship, operation.type, source_id, target_id)) continue;
        if (relationship.type == "assertedinference" && relationship.reasoning_ref == source_id) {
            relationship.reasoning_ref.clear();
            removed = true;
        }
        removed = RemoveValue(relationship.source_refs, source_id) || removed;
    }

    model.elements.erase(std::remove_if(model.elements.begin(), model.elements.end(), [](const parser::SacmElement& element) {
        return IsDanglingRelationship(element);
    }), model.elements.end());

    if (!removed) {
        error = "No matching relationship was found to remove.";
        return false;
    }
    return true;
}

bool ApplyRemoveElementOperation(const PatchOperation& operation,
                                 parser::AssuranceCase& model,
                                 const std::map<std::string, std::string>& generated_ids,
                                 std::string& error) {
    std::string element_id;
    if (!ResolveOptionalElementRef(operation.element, model, generated_ids, "element", element_id, error)) return false;
    if (!FindElement(model, element_id)) {
        error = "RemoveElement references missing element " + element_id + ".";
        return false;
    }

    model.elements.erase(std::remove_if(model.elements.begin(), model.elements.end(), [&](const parser::SacmElement& element) {
        if (element.id == element_id) return true;
        if (!IsRelationshipType(element.type)) return false;
        return std::find(element.source_refs.begin(), element.source_refs.end(), element_id) != element.source_refs.end() ||
               std::find(element.target_refs.begin(), element.target_refs.end(), element_id) != element.target_refs.end() ||
               element.reasoning_ref == element_id;
    }), model.elements.end());
    return true;
}

bool ApplyOperation(const PatchOperation& operation,
                    parser::AssuranceCase& model,
                    const std::map<std::string, std::string>& generated_ids,
                    std::unordered_set<std::string>& ids,
                    std::string& error) {
    if (IsCreateOperation(operation.type)) {
        return ApplyCreateOperation(operation, model, generated_ids, error);
    }

    switch (operation.type) {
        case PatchOperationType::UpdateElementText:
        case PatchOperationType::UpdateElementName:
        case PatchOperationType::SetUndeveloped:
        case PatchOperationType::ClearUndeveloped:
            return ApplyUpdateOperation(operation, model, generated_ids, error);
        case PatchOperationType::AddSupportedBy:
        case PatchOperationType::AddInContextOf:
            return ApplyAddRelationshipOperation(operation, model, generated_ids, ids, error);
        case PatchOperationType::RemoveSupportedBy:
        case PatchOperationType::RemoveInContextOf:
            return ApplyRemoveRelationshipOperation(operation, model, generated_ids, error);
        case PatchOperationType::RemoveElement:
            return ApplyRemoveElementOperation(operation, model, generated_ids, error);
        default:
            error = "Unsupported proposal operation.";
            return false;
    }
}

}  // namespace

ProposalPreviewResult ReviewProposalPatchService::BuildPreviewModel(const ReviewProposal& proposal,
                                                                    const parser::AssuranceCase& current_model) const {
    ProposalPreviewResult result;
    result.preview_model = current_model;

    if (!CollectGeneratedIds(proposal, current_model, result.generated_ids, result.error)) {
        return result;
    }

    std::unordered_set<std::string> ids = CollectIds(result.preview_model);
    for (size_t i = 0; i < proposal.operations.size(); ++i) {
        std::string error;
        if (!ApplyOperation(proposal.operations[i], result.preview_model, result.generated_ids, ids, error)) {
            std::ostringstream message;
            message << "Operation " << (i + 1) << " failed: " << error;
            result.error = message.str();
            return result;
        }
    }

    result.success = true;
    result.error.clear();
    return result;
}

ApplyProposalResult ReviewProposalPatchService::ApplyProposal(const ReviewProposal& proposal,
                                                             parser::AssuranceCase& current_model) const {
    ProposalPreviewResult preview = BuildPreviewModel(proposal, current_model);
    ApplyProposalResult result;
    result.generated_ids = preview.generated_ids;
    if (!preview.success) {
        result.error = preview.error;
        return result;
    }

    current_model = std::move(preview.preview_model);
    result.success = true;
    result.error.clear();
    return result;
}

}  // namespace core
