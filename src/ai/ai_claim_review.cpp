#include "ai/ai_claim_review.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace ai {
namespace {

const char* kAiReviewSystemInstruction = "You are reviewing a safety case claim for Assurance Forge. Return JSON only.";

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string Trim(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) ++start;

    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;

    return value.substr(start, end - start);
}

nlohmann::json ReviewElementToJson(const AiReviewElement& element) {
    return {
        {"role", element.role},
        {"id", element.id},
        {"type", element.type},
        {"name", element.name},
        {"content", element.content},
        {"description", element.description},
    };
}

AiReviewElement MakeReviewElement(const parser::SacmElement& element, const std::string& role) {
    AiReviewElement review_element;
    review_element.role = role;
    review_element.id = element.id;
    review_element.type = AiReviewElementType(element);
    review_element.name = element.name;
    review_element.content = element.content;
    review_element.description = element.description;
    return review_element;
}

const core::TreeNode* FindTreeNode(const core::AssuranceTree& tree, const std::string& element_id) {
    for (const auto& node : tree.nodes) {
        if (node && node->id == element_id) return node.get();
    }
    return nullptr;
}

void AddChildIfPresent(const parser::AssuranceCase& assurance_case,
                       const core::TreeNode* child_node,
                       std::vector<AiReviewElement>& children) {
    if (!child_node) return;
    const parser::SacmElement* child = FindSacmElement(assurance_case, child_node->id);
    if (!child) return;
    children.push_back(MakeReviewElement(*child, "child"));
}

nlohmann::json GuidelinesToJson(const std::vector<const parser::Guideline*>& claim_guidelines) {
    nlohmann::json guidelines = nlohmann::json::array();
    for (const parser::Guideline* guideline : claim_guidelines) {
        if (!guideline) continue;

        nlohmann::json review_prompts = nlohmann::json::array();
        for (const std::string& prompt : guideline->review_prompts) {
            review_prompts.push_back(prompt);
        }

        nlohmann::json suggested_checks = nlohmann::json::array();
        for (const parser::SuggestedCheck& check : guideline->tool_guidance.suggested_checks) {
            suggested_checks.push_back({
                {"id", check.id},
                {"description", check.description},
            });
        }

        guidelines.push_back({
            {"id", guideline->id},
            {"category", guideline->category},
            {"title", guideline->title},
            {"guideline", guideline->guideline},
            {"why", guideline->why},
            {"review_prompts", review_prompts},
            {"example", {
                {"bad", guideline->example.bad},
                {"problem", guideline->example.problem},
                {"good", guideline->example.good},
            }},
            {"suggested_checks", suggested_checks},
        });
    }
    return guidelines;
}

core::ProblemSeverity SeverityFromString(const std::string& value) {
    std::string severity = ToLower(value);
    if (severity == "info") return core::ProblemSeverity::Info;
    if (severity == "error") return core::ProblemSeverity::Error;
    return core::ProblemSeverity::Warning;
}

std::string JsonStringValue(const nlohmann::json& object, const char* key) {
    if (!object.is_object()) return {};
    auto value = object.find(key);
    if (value == object.end() || !value->is_string()) return {};
    return value->get<std::string>();
}

std::string BuildProblemMessage(const nlohmann::json& finding) {
    std::string message = JsonStringValue(finding, "message");
    std::string why = JsonStringValue(finding, "why_it_matters");
    std::string suggested_fix = JsonStringValue(finding, "suggested_fix");
    std::string suggested_wording = JsonStringValue(finding, "suggested_claim_wording");
    std::string confidence = JsonStringValue(finding, "confidence");

    if (!why.empty()) message += " Why it matters: " + why;
    if (!suggested_fix.empty()) message += " Suggested fix: " + suggested_fix;
    if (!suggested_wording.empty()) message += " Suggested claim wording: " + suggested_wording;
    if (!confidence.empty()) message += " Confidence: " + confidence;
    return message;
}

}  // namespace

const parser::SacmElement* FindSacmElement(const parser::AssuranceCase& assurance_case,
                                           const std::string& element_id) {
    for (const parser::SacmElement& element : assurance_case.elements) {
        if (element.id == element_id) return &element;
    }
    return nullptr;
}

bool IsSupportedAiReviewElement(const parser::SacmElement& element) {
    if (element.type != "claim") return false;
    if (element.assertion_declaration == "assumed") return false;
    if (element.assertion_declaration == "justification") return false;
    return true;
}

std::string AiReviewElementType(const parser::SacmElement& element) {
    if (element.type == "claim") {
        if (element.assertion_declaration == "assumed") return "GSN Assumption / SACM Claim";
        if (element.assertion_declaration == "justification") return "GSN Justification / SACM Claim";
        return "GSN Goal / SACM Claim";
    }
    if (element.type == "argumentreasoning") return "GSN Strategy / SACM ArgumentReasoning";
    if (element.type == "artifact" || element.type == "artifactreference") return "GSN Solution / SACM Artifact";
    return element.type.empty() ? "SACM Element" : "SACM " + element.type;
}

bool BuildAiReviewPayload(const parser::AssuranceCase& assurance_case,
                          const core::AssuranceTree& tree,
                          const std::string& selected_element_id,
                          AiReviewPayload& out_payload,
                          std::string& out_error) {
    const parser::SacmElement* selected = FindSacmElement(assurance_case, selected_element_id);
    if (!selected) {
        out_error = "Selected element was not found.";
        return false;
    }
    if (!IsSupportedAiReviewElement(*selected)) {
        out_error = "AI Review currently supports GSN Goal / SACM Claim elements only.";
        return false;
    }

    AiReviewPayload payload;
    payload.selected = MakeReviewElement(*selected, "selected");

    const core::TreeNode* selected_node = FindTreeNode(tree, selected_element_id);
    if (selected_node && selected_node->parent) {
        const parser::SacmElement* parent = FindSacmElement(assurance_case, selected_node->parent->id);
        if (parent) payload.parent = MakeReviewElement(*parent, "parent");
    }

    if (selected_node) {
        for (const core::TreeNode* child_node : selected_node->group1_children) {
            AddChildIfPresent(assurance_case, child_node, payload.children);
        }
        for (const core::TreeNode* child_node : selected_node->group2_attachments) {
            AddChildIfPresent(assurance_case, child_node, payload.children);
        }
    }

    out_payload = std::move(payload);
    out_error.clear();
    return true;
}

AiReviewRequestArtifacts BuildAiReviewRequestArtifacts(
    const AiReviewPayload& payload,
    const std::vector<const parser::Guideline*>& claim_guidelines) {
    nlohmann::json selected = ReviewElementToJson(payload.selected);
    nlohmann::json parent = payload.parent.has_value()
        ? ReviewElementToJson(payload.parent.value())
        : nlohmann::json(nullptr);
    nlohmann::json children = nlohmann::json::array();
    for (const AiReviewElement& child : payload.children) {
        children.push_back(ReviewElementToJson(child));
    }
    nlohmann::json guidelines = GuidelinesToJson(claim_guidelines);

    AiReviewRequestArtifacts artifacts;
    artifacts.systemInstruction = kAiReviewSystemInstruction;
    artifacts.selectedElementJson = selected.dump(2);
    artifacts.parentElementJson = parent.dump(2);
    artifacts.childElementsJson = children.dump(2);
    artifacts.guidelinesJson = guidelines.dump(2);
    artifacts.responseSchemaJson = BuildExpectedAiReviewResponseSchemaText();
    artifacts.expectedResponseSchema = artifacts.responseSchemaJson;

    std::ostringstream prompt;
    prompt << "You are reviewing a safety case claim for Assurance Forge.\n\n"
           << "Assurance Forge is an assurance case tool using SACM as the domain model and GSN as one graphical view. In this review, treat GSN Goals as claims.\n\n"
           << "Your task is to review the selected claim against the SCCG CL claim rules provided below.\n\n"
           << "Review only the selected claim. Use the parent and child/sub-element information only as context for understanding the claim and its decomposition.\n\n"
           << "Do not invent missing project information.\n"
           << "Do not claim that a rule is violated unless the provided data supports that finding.\n"
           << "If there is no clear violation, return an empty findings array.\n"
           << "Return JSON only. Do not include Markdown. Do not include explanations outside the JSON object.\n\n"
           << "## SCCG CL rules\n\n"
           << artifacts.guidelinesJson << "\n\n"
           << "## Selected element\n\n"
           << artifacts.selectedElementJson << "\n\n"
           << "## Parent element\n\n"
           << artifacts.parentElementJson << "\n\n"
           << "## Direct child/sub-elements\n\n"
           << artifacts.childElementsJson << "\n\n"
           << "## Required JSON response\n\n"
           << artifacts.responseSchemaJson << "\n";
    artifacts.prompt = prompt.str();
    std::ostringstream debug;
    debug << "Selected element data\n"
          << artifacts.selectedElementJson << "\n\n"
          << "Parent element data\n"
          << artifacts.parentElementJson << "\n\n"
          << "Child/sub-element data\n"
          << artifacts.childElementsJson << "\n\n"
          << "SCCG CL guideline data\n"
          << artifacts.guidelinesJson << "\n\n"
          << "Final AI prompt text\n"
          << artifacts.prompt << "\n\n"
          << "Expected JSON response schema\n"
          << artifacts.responseSchemaJson << "\n";
    artifacts.debugText = debug.str();
    return artifacts;
}

AiReviewPromptParts BuildAiReviewPrompt(
    const AiReviewPayload& payload,
    const std::vector<const parser::Guideline*>& claim_guidelines) {
    return BuildAiReviewRequestArtifacts(payload, claim_guidelines);
}

std::string BuildExpectedAiReviewResponseSchemaText() {
    return R"json(Return exactly one JSON object using this schema:

{
  "reviewed_element_id": "string",
  "reviewed_element_type": "string",
  "findings": [
    {
      "source": "SCCG",
      "guideline_id": "string",
      "guideline_title": "string",
      "severity": "info | warning | error",
      "confidence": "low | medium | high",
      "message": "string",
      "why_it_matters": "string",
      "suggested_fix": "string",
      "suggested_claim_wording": "string",
      "related_element_ids": ["string"]
    }
  ]
}

Field rules:

- source must be "SCCG".
- guideline_id must match one of the provided SCCG CL rule IDs.
- guideline_title must match the title of the referenced guideline.
- severity should normally be "warning" for claim quality issues.
- message should describe what is violated.
- why_it_matters should explain the review concern briefly.
- suggested_fix should describe how the user can improve the claim.
- suggested_claim_wording should provide a better wording for the selected claim when appropriate.
- related_element_ids should include the selected element ID and any parent/child IDs relevant to the finding.
- If there are no findings, return "findings": [].

Return JSON only.)json";
}

std::string StripJsonCodeFence(const std::string& response_text) {
    std::string trimmed = Trim(response_text);
    if (trimmed.rfind("```", 0) != 0) return trimmed;

    size_t first_line_end = trimmed.find('\n');
    if (first_line_end == std::string::npos) return trimmed;

    size_t fence_start = trimmed.rfind("```");
    if (fence_start == 0 || fence_start == std::string::npos) return trimmed;

    return Trim(trimmed.substr(first_line_end + 1, fence_start - first_line_end - 1));
}

AiReviewParseResult ParseAiReviewResponse(const std::string& response_text,
                                          const std::string& selected_element_id) {
    AiReviewParseResult result;
    result.sanitizedJson = StripJsonCodeFence(response_text);

    try {
        nlohmann::json root = nlohmann::json::parse(result.sanitizedJson);
        if (!root.is_object()) {
            result.errorMessage = "AI response root is not a JSON object.";
            return result;
        }
        if (!root.contains("findings") || !root["findings"].is_array()) {
            result.errorMessage = "AI response is missing a findings array.";
            return result;
        }

        result.reviewedElementId = JsonStringValue(root, "reviewed_element_id");
        if (result.reviewedElementId.empty()) result.reviewedElementId = selected_element_id;
        result.reviewedElementType = JsonStringValue(root, "reviewed_element_type");

        for (const nlohmann::json& finding : root["findings"]) {
            if (!finding.is_object()) continue;

            std::string guideline_id = JsonStringValue(finding, "guideline_id");
            if (guideline_id.empty()) guideline_id = "unknown";

            core::ProblemItem problem;
            problem.id = "ai-review:" + result.reviewedElementId + ":" + guideline_id;
            problem.severity = SeverityFromString(JsonStringValue(finding, "severity"));
            problem.source = core::ProblemSource::AIReview;
            problem.element_id = result.reviewedElementId;
            problem.type = result.reviewedElementType;
            problem.message = BuildProblemMessage(finding);
            problem.guideline_id = guideline_id == "unknown" ? std::string{} : guideline_id;
            result.problems.push_back(std::move(problem));
        }

        result.success = true;
        return result;
    } catch (const nlohmann::json::exception& exception) {
        result.errorMessage = exception.what();
        return result;
    } catch (const std::exception& exception) {
        result.errorMessage = exception.what();
        return result;
    }
}

ParsedAiReviewResponse ParseAiReviewResponse(const std::string& response_text,
                                             const std::string& selected_element_id,
                                             const std::string& fallback_element_type) {
    ParsedAiReviewResponse result = ParseAiReviewResponse(response_text, selected_element_id);
    if (result.reviewedElementType.empty()) result.reviewedElementType = fallback_element_type;
    for (core::ProblemItem& problem : result.problems) {
        if (problem.type.empty()) problem.type = result.reviewedElementType;
    }
    return result;
}

}  // namespace ai