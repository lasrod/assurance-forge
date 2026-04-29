#pragma once

#include "core/assurance_tree.h"
#include "core/problems/problem_item.h"
#include "parser/guidelines_parser.h"
#include "parser/xml_parser.h"

#include <optional>
#include <string>
#include <vector>

namespace ai {

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

struct AiReviewRequestArtifacts {
    std::string systemInstruction;
    std::string selectedElementJson;
    std::string parentElementJson;
    std::string childElementsJson;
    std::string guidelinesJson;
    std::string responseSchemaJson;
    std::string expectedResponseSchema;
    std::string prompt;
    std::string debugText;
};

using AiReviewPromptParts = AiReviewRequestArtifacts;

struct AiReviewParseResult {
    bool success = false;
    std::string errorMessage;
    std::string sanitizedJson;
    std::string reviewedElementId;
    std::string reviewedElementType;
    std::vector<core::ProblemItem> problems;
};

using ParsedAiReviewResponse = AiReviewParseResult;

const parser::SacmElement* FindSacmElement(const parser::AssuranceCase& assurance_case,
                                           const std::string& element_id);
bool IsSupportedAiReviewElement(const parser::SacmElement& element);
std::string AiReviewElementType(const parser::SacmElement& element);

bool BuildAiReviewPayload(const parser::AssuranceCase& assurance_case,
                          const core::AssuranceTree& tree,
                          const std::string& selected_element_id,
                          AiReviewPayload& out_payload,
                          std::string& out_error);

AiReviewRequestArtifacts BuildAiReviewRequestArtifacts(
    const AiReviewPayload& payload,
    const std::vector<const parser::Guideline*>& claim_guidelines);
AiReviewPromptParts BuildAiReviewPrompt(
    const AiReviewPayload& payload,
    const std::vector<const parser::Guideline*>& claim_guidelines);

std::string BuildExpectedAiReviewResponseSchemaText();
std::string StripJsonCodeFence(const std::string& response_text);
AiReviewParseResult ParseAiReviewResponse(const std::string& response_text,
                                          const std::string& selected_element_id);
ParsedAiReviewResponse ParseAiReviewResponse(const std::string& response_text,
                                             const std::string& selected_element_id,
                                             const std::string& fallback_element_type);

}  // namespace ai