#pragma once

#include "parser/xml_parser.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace core::reviews {

constexpr const char* kReviewProposalSchema = "assurance-forge.review-proposal.v1";
constexpr const char* kReviewProposalRemoveModeNodeOnly = "node_only";
constexpr const char* kReviewProposalRemoveModeNodeAndDescendants = "node_and_descendants";

enum class PatchOperationType {
    CreateClaim,
    CreateStrategy,
    CreateSolution,
    CreateContext,
    CreateAssumption,
    CreateJustification,
    UpdateElementText,
    UpdateElementName,
    SetUndeveloped,
    ClearUndeveloped,
    AddSupportedBy,
    RemoveSupportedBy,
    AddInContextOf,
    RemoveInContextOf,
    RemoveElement,
};

struct ElementRef {
    std::optional<std::string> existing_id;
    std::optional<std::string> create_ref;
};

struct PatchOperation {
    PatchOperationType type = PatchOperationType::UpdateElementText;
    std::optional<ElementRef> element;
    std::optional<ElementRef> source;
    std::optional<ElementRef> target;
    std::optional<std::string> create_ref;
    std::string field;
    std::string old_value;
    std::string new_value;
    std::string text;
};

struct ReviewProposal {
    std::string schema = kReviewProposalSchema;
    std::string id;
    std::string review_item_id;
    std::string title;
    std::string summary;
    std::string author_name;
    std::string created_utc;
    std::string anchor_element_id;
    std::vector<std::string> affected_existing_element_ids;
    std::string base_model_hash;
    std::map<std::string, std::string> base_element_hashes;
    std::vector<PatchOperation> operations;
};

enum class ProposalValidity {
    Valid,
    Broken,
};

struct ProposalValidityResult {
    ProposalValidity validity = ProposalValidity::Broken;
    std::string reason;
};

struct ReviewProposalSummary {
    std::string id;
    std::string title;
    std::string summary;
    std::string review_item_id;
    std::string anchor_element_id;
    std::filesystem::path relative_path;
    ProposalValidityResult validity;
};

const char* PatchOperationTypeToString(PatchOperationType type);
bool PatchOperationTypeFromString(const std::string& value, PatchOperationType& type);

std::string SerializeReviewProposal(const ReviewProposal& proposal);
bool DeserializeReviewProposal(const std::string& content, ReviewProposal& proposal, std::string& error);

std::string ComputeModelSemanticHash(const parser::AssuranceCase& model);
std::string ComputeElementSemanticHash(const parser::SacmElement& element);
ProposalValidityResult EvaluateReviewProposalValidity(const ReviewProposal& proposal,
                                                      const parser::AssuranceCase& current_model);

} // namespace core::reviews