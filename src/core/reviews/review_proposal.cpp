#include "core/reviews/review_proposal.h"

#include "core/reviews/review_proposal_patch_service.h"
#include "core/sha256.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace core::reviews {

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

bool ElementExists(const parser::AssuranceCase& model, const std::string& id) {
    return std::any_of(model.elements.begin(), model.elements.end(), [&](const parser::SacmElement& element) {
        return element.id == id;
    });
}

bool RefNamesExistingElement(const std::optional<ElementRef>& ref) {
    return ref.has_value() && ref->existing_id.has_value() && !ref->existing_id->empty();
}

// Whether the proposal reads or modifies anything already in the case, as
// opposed to being purely additive.
//
// This is what decides if an empty `anchor_element_id` is acceptable. The anchor
// exists so a proposal can be shown against the element it concerns and can go
// stale when that element changes underneath it. A proposal that creates the
// case's first goal has neither property: there is no element to anchor to, and
// nothing it could be stale against. Any proposal that does touch an existing
// element still requires an anchor, so relaxing this costs no staleness
// detection.
bool TouchesExistingElement(const ReviewProposal& proposal) {
    if (!proposal.affected_existing_element_ids.empty()) {
        return true;
    }
    for (const PatchOperation& operation : proposal.operations) {
        if (RefNamesExistingElement(operation.element) || RefNamesExistingElement(operation.source) ||
            RefNamesExistingElement(operation.target)) {
            return true;
        }
    }
    return false;
}

const parser::SacmElement* FindElement(const parser::AssuranceCase& model, const std::string& id) {
    for (const parser::SacmElement& element : model.elements) {
        if (element.id == id)
            return &element;
    }
    return nullptr;
}

std::string WithHashPrefix(const std::string& digest) {
    return "sha256:" + digest;
}

nlohmann::json RefToJson(const ElementRef& ref, const char* id_key, const char* ref_key) {
    nlohmann::json object;
    if (ref.existing_id.has_value())
        object[id_key] = ref.existing_id.value();
    if (ref.create_ref.has_value())
        object[ref_key] = ref.create_ref.value();
    return object;
}

void WriteRefFields(nlohmann::json& object, const ElementRef& ref, const char* id_key, const char* ref_key) {
    nlohmann::json ref_json = RefToJson(ref, id_key, ref_key);
    for (auto it = ref_json.begin(); it != ref_json.end(); ++it) {
        object[it.key()] = it.value();
    }
}

std::optional<ElementRef> ReadRefFields(const nlohmann::json& object, const char* id_key, const char* ref_key) {
    ElementRef ref;
    if (object.contains(id_key) && object[id_key].is_string())
        ref.existing_id = object[id_key].get<std::string>();
    if (object.contains(ref_key) && object[ref_key].is_string())
        ref.create_ref = object[ref_key].get<std::string>();
    if (!ref.existing_id.has_value() && !ref.create_ref.has_value())
        return std::nullopt;
    return ref;
}

bool ValidateElementRef(const ElementRef& ref,
                        const parser::AssuranceCase& model,
                        const std::unordered_set<std::string>& known_create_refs,
                        std::string& reason) {
    const bool has_existing = ref.existing_id.has_value() && !ref.existing_id->empty();
    const bool has_create = ref.create_ref.has_value() && !ref.create_ref->empty();
    if (has_existing == has_create) {
        reason = "Element references must contain exactly one existing id or create_ref.";
        return false;
    }
    if (has_existing && !ElementExists(model, ref.existing_id.value())) {
        reason = "The proposal refers to " + ref.existing_id.value() + ", but that element no longer exists.";
        return false;
    }
    if (has_create && known_create_refs.count(ref.create_ref.value()) == 0) {
        reason = "The proposal refers to unknown patch-local handle " + ref.create_ref.value() + ".";
        return false;
    }
    return true;
}

} // namespace

const char* PatchOperationTypeToString(PatchOperationType type) {
    switch (type) {
    case PatchOperationType::CreateClaim:
        return "CreateClaim";
    case PatchOperationType::CreateStrategy:
        return "CreateStrategy";
    case PatchOperationType::CreateSolution:
        return "CreateSolution";
    case PatchOperationType::CreateContext:
        return "CreateContext";
    case PatchOperationType::CreateAssumption:
        return "CreateAssumption";
    case PatchOperationType::CreateJustification:
        return "CreateJustification";
    case PatchOperationType::UpdateElementText:
        return "UpdateElementText";
    case PatchOperationType::UpdateElementName:
        return "UpdateElementName";
    case PatchOperationType::SetUndeveloped:
        return "SetUndeveloped";
    case PatchOperationType::ClearUndeveloped:
        return "ClearUndeveloped";
    case PatchOperationType::AddSupportedBy:
        return "AddSupportedBy";
    case PatchOperationType::RemoveSupportedBy:
        return "RemoveSupportedBy";
    case PatchOperationType::AddInContextOf:
        return "AddInContextOf";
    case PatchOperationType::RemoveInContextOf:
        return "RemoveInContextOf";
    case PatchOperationType::RemoveElement:
        return "RemoveElement";
    }
    return "UpdateElementText";
}

bool PatchOperationTypeFromString(const std::string& value, PatchOperationType& type) {
    static const std::unordered_map<std::string, PatchOperationType> kTypes = {
        {"CreateClaim", PatchOperationType::CreateClaim},
        {"CreateStrategy", PatchOperationType::CreateStrategy},
        {"CreateSolution", PatchOperationType::CreateSolution},
        {"CreateContext", PatchOperationType::CreateContext},
        {"CreateAssumption", PatchOperationType::CreateAssumption},
        {"CreateJustification", PatchOperationType::CreateJustification},
        {"UpdateElementText", PatchOperationType::UpdateElementText},
        {"UpdateElementName", PatchOperationType::UpdateElementName},
        {"SetUndeveloped", PatchOperationType::SetUndeveloped},
        {"ClearUndeveloped", PatchOperationType::ClearUndeveloped},
        {"AddSupportedBy", PatchOperationType::AddSupportedBy},
        {"RemoveSupportedBy", PatchOperationType::RemoveSupportedBy},
        {"AddInContextOf", PatchOperationType::AddInContextOf},
        {"RemoveInContextOf", PatchOperationType::RemoveInContextOf},
        {"RemoveElement", PatchOperationType::RemoveElement},
    };
    auto it = kTypes.find(value);
    if (it == kTypes.end())
        return false;
    type = it->second;
    return true;
}

std::string SerializeReviewProposal(const ReviewProposal& proposal) {
    nlohmann::json root;
    root["schema"] = proposal.schema;
    root["id"] = proposal.id;
    root["review_item_id"] = proposal.review_item_id;
    root["title"] = proposal.title;
    root["summary"] = proposal.summary;
    root["author_name"] = proposal.author_name;
    root["created_utc"] = proposal.created_utc;
    root["anchor_element_id"] = proposal.anchor_element_id;
    root["affected_existing_element_ids"] = proposal.affected_existing_element_ids;
    root["base_model_hash"] = proposal.base_model_hash;
    root["base_element_hashes"] = proposal.base_element_hashes;
    root["operations"] = nlohmann::json::array();

    for (const PatchOperation& operation : proposal.operations) {
        nlohmann::json object;
        object["type"] = PatchOperationTypeToString(operation.type);
        if (operation.create_ref.has_value())
            object["create_ref"] = operation.create_ref.value();
        if (operation.element.has_value())
            WriteRefFields(object, operation.element.value(), "element_id", "element_ref");
        if (operation.source.has_value())
            WriteRefFields(object, operation.source.value(), "source_id", "source_ref");
        if (operation.target.has_value())
            WriteRefFields(object, operation.target.value(), "target_id", "target_ref");
        if (!operation.field.empty())
            object["field"] = operation.field;
        if (!operation.old_value.empty())
            object["old_value"] = operation.old_value;
        if (!operation.new_value.empty())
            object["new_value"] = operation.new_value;
        if (!operation.text.empty())
            object["text"] = operation.text;
        root["operations"].push_back(std::move(object));
    }

    return root.dump(2) + "\n";
}

bool DeserializeReviewProposal(const std::string& content, ReviewProposal& proposal, std::string& error) {
    proposal = ReviewProposal{};
    error.clear();
    try {
        nlohmann::json root = nlohmann::json::parse(content);
        if (!root.is_object()) {
            error = "Proposal file root is not an object.";
            return false;
        }
        proposal.schema = root.value("schema", "");
        proposal.id = root.value("id", "");
        proposal.review_item_id = root.value("review_item_id", "");
        proposal.title = root.value("title", "");
        proposal.summary = root.value("summary", "");
        proposal.author_name = root.value("author_name", "");
        proposal.created_utc = root.value("created_utc", "");
        proposal.anchor_element_id = root.value("anchor_element_id", "");
        proposal.base_model_hash = root.value("base_model_hash", "");

        for (const auto& value : root.value("affected_existing_element_ids", nlohmann::json::array())) {
            if (value.is_string())
                proposal.affected_existing_element_ids.push_back(value.get<std::string>());
        }
        if (root.contains("base_element_hashes") && root["base_element_hashes"].is_object()) {
            for (auto it = root["base_element_hashes"].begin(); it != root["base_element_hashes"].end(); ++it) {
                if (it.value().is_string())
                    proposal.base_element_hashes[it.key()] = it.value().get<std::string>();
            }
        }

        const nlohmann::json operations = root.value("operations", nlohmann::json::array());
        if (!operations.is_array()) {
            error = "Proposal operations field is not an array.";
            return false;
        }
        for (const auto& operation_json : operations) {
            if (!operation_json.is_object()) {
                error = "Proposal operation is not an object.";
                return false;
            }
            PatchOperation operation;
            if (!PatchOperationTypeFromString(operation_json.value("type", ""), operation.type)) {
                error = "Proposal operation has an unknown type.";
                return false;
            }
            if (operation_json.contains("create_ref") && operation_json["create_ref"].is_string()) {
                operation.create_ref = operation_json["create_ref"].get<std::string>();
            }
            operation.element = ReadRefFields(operation_json, "element_id", "element_ref");
            operation.source = ReadRefFields(operation_json, "source_id", "source_ref");
            operation.target = ReadRefFields(operation_json, "target_id", "target_ref");
            operation.field = operation_json.value("field", "");
            operation.old_value = operation_json.value("old_value", "");
            operation.new_value = operation_json.value("new_value", "");
            operation.text = operation_json.value("text", "");
            proposal.operations.push_back(std::move(operation));
        }
    } catch (const nlohmann::json::exception& e) {
        error = std::string("Proposal JSON parse failed: ") + e.what();
        return false;
    }
    return true;
}

std::string ComputeElementSemanticHash(const parser::SacmElement& element) {
    std::ostringstream normalized;
    normalized << element.id << '\n'
               << element.type << '\n'
               << element.name << '\n'
               << element.content << '\n'
               << element.description << '\n'
               << (element.undeveloped ? "undeveloped" : "developed") << '\n'
               << element.assertion_declaration << '\n'
               << element.reasoning_ref << '\n';
    for (const std::string& source : element.source_refs)
        normalized << "source:" << source << '\n';
    for (const std::string& target : element.target_refs)
        normalized << "target:" << target << '\n';
    return WithHashPrefix(Sha256::HexDigest(normalized.str()));
}

std::string ComputeModelSemanticHash(const parser::AssuranceCase& model) {
    std::vector<std::string> lines;
    lines.reserve(model.elements.size());
    for (const parser::SacmElement& element : model.elements) {
        lines.push_back(ComputeElementSemanticHash(element));
    }
    std::sort(lines.begin(), lines.end());
    std::ostringstream normalized;
    normalized << model.id << '\n' << model.name << '\n' << model.description << '\n';
    for (const std::string& line : lines)
        normalized << line << '\n';
    return WithHashPrefix(Sha256::HexDigest(normalized.str()));
}

ProposalValidityResult EvaluateReviewProposalValidity(const ReviewProposal& proposal,
                                                      const parser::AssuranceCase& current_model) {
    ProposalValidityResult result;
    result.validity = ProposalValidity::Broken;

    if (proposal.schema != kReviewProposalSchema) {
        result.reason = "The proposal uses an unsupported schema.";
        return result;
    }
    if (proposal.id.empty()) {
        result.reason = "The proposal is missing an id.";
        return result;
    }
    if (proposal.anchor_element_id.empty()) {
        // A purely additive proposal -- creating the case's first goal, say --
        // has nothing to anchor to. Anything that touches an existing element
        // still needs one.
        if (TouchesExistingElement(proposal)) {
            result.reason = "The proposal changes existing elements but names no anchor element.";
            return result;
        }
    } else if (!ElementExists(current_model, proposal.anchor_element_id)) {
        result.reason = "The anchor element no longer exists.";
        return result;
    }

    std::set<std::string> affected_ids(proposal.affected_existing_element_ids.begin(),
                                       proposal.affected_existing_element_ids.end());
    if (!proposal.anchor_element_id.empty()) {
        affected_ids.insert(proposal.anchor_element_id);
    }
    for (const std::string& id : affected_ids) {
        const parser::SacmElement* element = FindElement(current_model, id);
        if (!element) {
            result.reason = "The proposal refers to " + id + ", but that element no longer exists.";
            return result;
        }
        auto expected_hash = proposal.base_element_hashes.find(id);
        if (expected_hash != proposal.base_element_hashes.end() &&
            expected_hash->second != ComputeElementSemanticHash(*element)) {
            result.reason = "The affected element " + id + " changed since the proposal was created.";
            return result;
        }
    }

    std::unordered_set<std::string> create_refs;
    for (const PatchOperation& operation : proposal.operations) {
        if (!IsCreateOperation(operation.type))
            continue;
        if (!operation.create_ref.has_value() || operation.create_ref->empty()) {
            result.reason = "Create operations must use a patch-local create_ref.";
            return result;
        }
        if (operation.create_ref->front() != '$') {
            result.reason = "Patch-local create_ref values must start with '$'.";
            return result;
        }
        if (!create_refs.insert(operation.create_ref.value()).second) {
            result.reason = "Duplicate patch-local create_ref: " + operation.create_ref.value();
            return result;
        }
    }

    // Feasibility first, and against what the patch actually produces.
    //
    // The per-operation checks below exist for their messages, not for their
    // verdict: this is the authority on whether the patch applies, because it
    // applies it, in order, to a scratch copy. Running it first also gives the
    // reference checks a model that holds what earlier operations create --
    // without which an operation naming an element this same change set made,
    // by the id `stage_operations` reported for it, is rejected for referring to
    // something that "no longer exists". It never existed in the committed
    // model; that is what creating it means. Staging accepted such a change set,
    // the canvas drew it and the Review panel counted it, and only acceptance
    // refused -- so the gate was stricter than the thing it guards, and an
    // eighty-operation argument built over two calls could not be accepted at
    // all.
    ReviewProposalPatchService patch_service;
    ProposalPreviewResult preview = patch_service.BuildPreviewModel(proposal, current_model);
    if (!preview.success) {
        result.reason = preview.error;
        return result;
    }
    const parser::AssuranceCase& reachable = preview.preview_model;

    for (const PatchOperation& operation : proposal.operations) {
        std::string reason;
        if (IsCreateOperation(operation.type))
            continue;
        switch (operation.type) {
        case PatchOperationType::UpdateElementText:
        case PatchOperationType::UpdateElementName:
        case PatchOperationType::SetUndeveloped:
        case PatchOperationType::ClearUndeveloped:
        case PatchOperationType::RemoveElement:
            if (!operation.element.has_value()) {
                result.reason =
                    "Operation " + std::string(PatchOperationTypeToString(operation.type)) + " is missing element_id.";
                return result;
            }
            if (!ValidateElementRef(operation.element.value(), reachable, create_refs, reason)) {
                result.reason = reason;
                return result;
            }
            break;
        case PatchOperationType::AddSupportedBy:
        case PatchOperationType::RemoveSupportedBy:
        case PatchOperationType::AddInContextOf:
        case PatchOperationType::RemoveInContextOf:
            if (!operation.source.has_value() || !operation.target.has_value()) {
                result.reason = "Relationship operations require source and target references.";
                return result;
            }
            if (!ValidateElementRef(operation.source.value(), reachable, create_refs, reason) ||
                !ValidateElementRef(operation.target.value(), reachable, create_refs, reason)) {
                result.reason = reason;
                return result;
            }
            break;
        default:
            break;
        }

        if (operation.type == PatchOperationType::UpdateElementText && operation.field.empty()) {
            result.reason = "UpdateElementText operations must specify a field.";
            return result;
        }
    }

    result.validity = ProposalValidity::Valid;
    result.reason.clear();
    return result;
}

} // namespace core::reviews