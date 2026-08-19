#pragma once

#include "core/assurance_tree.h"
#include "core/problems/problem_item.h"
#include "parser/guidelines_parser.h"
#include "parser/xml_parser.h"

#include <optional>
#include <string>
#include <vector>

namespace review {

struct AiReviewElement {
    std::string role;
    std::string id;
    std::string type;
    std::string name;
    std::string content;
    std::string description;
};

struct AiReviewPayload {
    AiReviewElement selected;
    std::optional<AiReviewElement> parent;
    std::vector<AiReviewElement> children;
};

struct AiReviewDataPackage {
    std::string id;
    std::string json;
};

struct AiReviewUnavailableDataPackage {
    std::string id;
    std::string reason;
    bool required = false;
};

struct AiReviewDataPackageBundle {
    std::vector<AiReviewDataPackage> available;
    std::vector<AiReviewUnavailableDataPackage> unavailable;
};

struct AiReviewRequestArtifacts {
    std::string systemInstruction;
    std::string selectedElementJson;
    std::string parentElementJson;
    std::string childElementsJson;
    std::string availableDataPackagesJson;
    std::string unavailableDataPackagesJson;
    std::string reviewProfileJson;
    std::string guidelinesJson;
    std::string responseSchemaJson;
    std::string expectedResponseSchema;
    std::string prompt;
    std::string debugText;
};

using AiReviewPromptParts = AiReviewRequestArtifacts;

// `errorMessage` is the success/failure signal: empty means success.
struct AiReviewParseResult {
    std::string errorMessage;
    std::string sanitizedJson;
    std::string reviewedElementId;
    std::string reviewedElementType;
    std::vector<core::ProblemItem> problems;
    std::vector<std::string> suggestedElementTexts;
};

using ParsedAiReviewResponse = AiReviewParseResult;

const parser::SacmElement* FindSacmElement(const parser::AssuranceCase& assurance_case, const std::string& element_id);
bool IsSupportedAiReviewElement(const parser::SacmElement& element);
std::string AiReviewElementType(const parser::SacmElement& element, const core::TreeNode* node = nullptr);
std::vector<std::string> SccgAppliesToNamesForElement(const parser::SacmElement& element,
                                                      const core::TreeNode* node = nullptr);
bool IsReviewProfileCompatibleWithElement(const parser::ReviewProfile& review_profile,
                                          const parser::SacmElement& element,
                                          const core::TreeNode* node = nullptr);

bool BuildAiReviewPayload(const parser::AssuranceCase& assurance_case,
                          const core::AssuranceTree& tree,
                          const std::string& selected_element_id,
                          AiReviewPayload& out_payload,
                          std::string& out_error);
bool CollectAiReviewDataPackages(const parser::AssuranceCase& assurance_case,
                                 const core::AssuranceTree& tree,
                                 const std::string& selected_element_id,
                                 const parser::ReviewProfile* review_profile,
                                 AiReviewDataPackageBundle& out_packages,
                                 std::string& out_error);

AiReviewRequestArtifacts BuildAiReviewRequestArtifacts(const AiReviewPayload& payload,
                                                       const std::vector<const parser::Guideline*>& guidelines,
                                                       const parser::ReviewProfile* review_profile = nullptr,
                                                       const AiReviewDataPackageBundle* data_packages = nullptr);
AiReviewPromptParts BuildAiReviewPrompt(const AiReviewPayload& payload,
                                        const std::vector<const parser::Guideline*>& guidelines,
                                        const parser::ReviewProfile* review_profile = nullptr,
                                        const AiReviewDataPackageBundle* data_packages = nullptr);

// Every element the review actually read: the payload's own elements plus every
// element id carried by the data packages. Derived from what was sent rather
// than tracked beside it, so a package that starts including another element
// cannot quietly fall outside the scope.
//
// This is the set a review is judged stale against, and -- once findings may
// propose structural changes -- the set they are allowed to touch.
std::vector<std::string> ReviewedElementIds(const AiReviewPayload& payload,
                                            const AiReviewDataPackageBundle& data_packages);

std::string BuildExpectedAiReviewResponseSchemaText();
std::string StripJsonCodeFence(const std::string& response_text);
AiReviewParseResult ParseAiReviewResponse(const std::string& response_text, const std::string& selected_element_id);
AiReviewParseResult ParseAiReviewResponse(const std::string& response_text,
                                          const std::string& selected_element_id,
                                          const std::vector<std::string>& allowed_guideline_ids);
ParsedAiReviewResponse ParseAiReviewResponse(const std::string& response_text,
                                             const std::string& selected_element_id,
                                             const std::string& fallback_element_type);

} // namespace review
