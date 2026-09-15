#include "eval/baseline_review.h"

#include "review/sccg/sccg_review.h"
#include "review/sccg/suggestion_mapping.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <utility>

namespace eval {
namespace {

using nlohmann::json;

// The system instruction an SCCG review sends (review/sccg/sccg_review.cpp), so
// the two setups differ in the user prompt alone.
constexpr const char* kSystemInstruction =
    "You are reviewing an assurance case element for Assurance Forge. Return JSON only.";

// Version `baseline-generic-v1`. The kinds of weakness it lists are what a team
// writing a review prompt without SCCG would plausibly name: broad, with no
// guideline, no defect taxonomy and no example. Fixed before any baseline run.
constexpr const char* kInstruction = R"PROMPT(Review the selected element of a safety case argument.

Act as an experienced safety case reviewer. Read the selected element in the context of the surrounding argument supplied below, and report the weaknesses a reviewer should raise about it: for example unclear or ambiguous wording, claims that are not supported or not fully supported, gaps or flaws in the reasoning, problems with the evidence, and unstated assumptions or limitations. Report only real problems with the selected element. If you find none, return an empty findings array. Do not invent project information that is not supplied.

Return JSON only, in this shape:
{"reviewed_element_id": "<id of the selected element>", "findings": [{"message": "<the problem>", "why_it_matters": "<why a safety reviewer should care>", "suggested_fix": "<how to fix it>", "confidence": "low | medium | high"}]}

)PROMPT";

// Version `baseline-element-only-v1`: `baseline-generic-v1` less its reference
// to a surrounding argument, since none is sent. Added after the pilot of the
// generic baseline, to separate what the argument contributes from what the
// prompt does.
constexpr const char* kElementOnlyInstruction = R"PROMPT(Review the selected element of a safety case argument.

Act as an experienced safety case reviewer. Report the weaknesses a reviewer should raise about the selected element below: for example unclear or ambiguous wording, claims that are not supported or not fully supported, gaps or flaws in the reasoning, problems with the evidence, and unstated assumptions or limitations. Report only real problems with the selected element. If you find none, return an empty findings array. Do not invent project information that is not supplied.

Return JSON only, in this shape:
{"reviewed_element_id": "<id of the selected element>", "findings": [{"message": "<the problem>", "why_it_matters": "<why a safety reviewer should care>", "suggested_fix": "<how to fix it>", "confidence": "low | medium | high"}]}

)PROMPT";

const parser::SacmElement* ElementForNode(const parser::AssuranceCase& assurance_case, const core::TreeNode* node) {
    return node ? review::FindSacmElement(assurance_case, node->id) : nullptr;
}

// What an SCCG data package says about an element, less what is SCCG's or the
// tool's own: no SCCG element role, no raw SACM type, no layout role.
json ElementJson(const parser::SacmElement& element, const core::TreeNode* node) {
    return {
        {"element_id", element.id},
        {"element_type", review::AiReviewElementType(element, node)},
        {"name", element.name},
        {"text", review::TextTargetFor(element).current_text},
        {"description", element.description},
        {"content", element.content},
        {"undeveloped", element.undeveloped},
    };
}

// The surrounding argument, gathered as review::CollectAiReviewDataPackages
// gathers PARENT, CHILDREN, DIRECT_CONTEXT, INHERITED_CONTEXT, STRATEGY,
// EVIDENCE_PATH and PROJECT_GLOSSARY, under plain names. Kept a copy of that
// walk rather than a refactor of it, so the SCCG requests the paper already
// reports cannot change; the tests hold the two to the same elements.
json SurroundingArgument(const parser::AssuranceCase& assurance_case, const core::TreeNode* node) {
    json argument{
        {"parent", nullptr},
        {"children", json::array()},
        {"direct_context",
         {{"context", json::array()}, {"assumptions", json::array()}, {"justifications", json::array()}}},
        {"inherited_context", {{"context", json::array()}, {"assumptions", json::array()}}},
        {"strategy", nullptr},
        {"evidence_path", nullptr},
        {"glossary", json::array()},
    };
    if (node != nullptr) {
        if (const parser::SacmElement* parent = ElementForNode(assurance_case, node->parent))
            argument["parent"] = ElementJson(*parent, node->parent);

        for (const core::TreeNode* child_node : node->group1_children) {
            if (const parser::SacmElement* child = ElementForNode(assurance_case, child_node))
                argument["children"].push_back(ElementJson(*child, child_node));
        }

        for (const core::TreeNode* attachment_node : node->group2_attachments) {
            const parser::SacmElement* attachment = ElementForNode(assurance_case, attachment_node);
            if (!attachment)
                continue;
            const char* bucket = attachment_node->role == core::NodeRole::Assumption      ? "assumptions"
                                 : attachment_node->role == core::NodeRole::Justification ? "justifications"
                                                                                          : "context";
            argument["direct_context"][bucket].push_back(ElementJson(*attachment, attachment_node));
        }

        for (const core::TreeNode* ancestor = node->parent; ancestor; ancestor = ancestor->parent) {
            for (const core::TreeNode* attachment_node : ancestor->group2_attachments) {
                const parser::SacmElement* attachment = ElementForNode(assurance_case, attachment_node);
                if (!attachment)
                    continue;
                json element_json = ElementJson(*attachment, attachment_node);
                element_json["ancestor_id"] = ancestor->id;
                const char* bucket = attachment_node->role == core::NodeRole::Assumption ? "assumptions" : "context";
                argument["inherited_context"][bucket].push_back(std::move(element_json));
            }
        }

        const core::TreeNode* strategy_node = nullptr;
        if (node->role == core::NodeRole::Strategy)
            strategy_node = node;
        else if (node->parent && node->parent->role == core::NodeRole::Strategy)
            strategy_node = node->parent;
        else {
            for (const core::TreeNode* child_node : node->group1_children) {
                if (child_node && child_node->role == core::NodeRole::Strategy) {
                    strategy_node = child_node;
                    break;
                }
            }
        }
        if (const parser::SacmElement* strategy = ElementForNode(assurance_case, strategy_node))
            argument["strategy"] = ElementJson(*strategy, strategy_node);

        json path_elements = json::array();
        json evidence_items = json::array();
        std::vector<const core::TreeNode*> stack(node->group1_children.begin(), node->group1_children.end());
        while (!stack.empty()) {
            const core::TreeNode* path_node = stack.back();
            stack.pop_back();
            const parser::SacmElement* element = ElementForNode(assurance_case, path_node);
            if (!element)
                continue;
            path_elements.push_back(ElementJson(*element, path_node));
            if (path_node->role == core::NodeRole::Solution)
                evidence_items.push_back(ElementJson(*element, path_node));
            for (const core::TreeNode* child_node : path_node->group1_children)
                stack.push_back(child_node);
        }
        if (!evidence_items.empty())
            argument["evidence_path"] = {{"path_elements", path_elements}, {"evidence_items", evidence_items}};
    }

    for (const parser::SacmElement& element : assurance_case.elements) {
        if (element.type != "term")
            continue;
        argument["glossary"].push_back({{"element_id", element.id},
                                        {"term", element.content},
                                        {"definition", element.description},
                                        {"name", element.name}});
    }
    return argument;
}

void CollectElementIds(const json& value, std::vector<std::string>& ids) {
    if (value.is_object()) {
        for (const auto& [key, item] : value.items()) {
            if ((key == "element_id" || key == "ancestor_id") && item.is_string()) {
                const std::string id = item.get<std::string>();
                if (std::find(ids.begin(), ids.end(), id) == ids.end())
                    ids.push_back(id);
            } else {
                CollectElementIds(item, ids);
            }
        }
    } else if (value.is_array()) {
        for (const json& item : value)
            CollectElementIds(item, ids);
    }
}

std::string StringField(const json& object, const char* key) {
    const auto value = object.find(key);
    return value != object.end() && value->is_string() ? value->get<std::string>() : std::string{};
}

} // namespace

const char* BaselinePromptVersion(BaselineContext context) {
    return context == BaselineContext::ElementOnly ? "baseline-element-only-v1" : "baseline-generic-v1";
}

bool BuildBaselineReviewRequest(const parser::AssuranceCase& assurance_case,
                                const core::AssuranceTree& tree,
                                const std::string& element_id,
                                BaselineReviewRequest& out_request,
                                std::string& out_error,
                                BaselineContext context) {
    const parser::SacmElement* selected = review::FindSacmElement(assurance_case, element_id);
    if (!selected) {
        out_error = "Selected element was not found.";
        return false;
    }
    if (!review::IsSupportedAiReviewElement(*selected)) {
        out_error = "AI Review does not support the selected element type.";
        return false;
    }

    const core::TreeNode* node = core::FindTreeNode(tree, element_id);
    const json selected_json = ElementJson(*selected, node);

    BaselineReviewRequest request;
    request.system_instruction = kSystemInstruction;
    request.reviewed_element_ids = {selected->id};
    const char* instruction = kInstruction;
    std::string element_data = "## Selected element\n\n" + selected_json.dump(2) + "\n";
    if (context == BaselineContext::ElementOnly) {
        instruction = kElementOnlyInstruction;
    } else {
        const json argument = SurroundingArgument(assurance_case, node);
        element_data += "\n## Surrounding argument\n\n" + argument.dump(2) + "\n";
        CollectElementIds(argument, request.reviewed_element_ids);
    }
    request.prompt_segments = {instruction, element_data};
    request.prompt = std::string(instruction) + element_data;

    out_request = std::move(request);
    out_error.clear();
    return true;
}

BaselineReviewParseResult ParseBaselineReviewResponse(const std::string& response_text) {
    BaselineReviewParseResult result;
    const json parsed = json::parse(review::StripJsonCodeFence(response_text), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        result.error_message = "The response is not a JSON object.";
        return result;
    }
    const auto findings = parsed.find("findings");
    if (findings == parsed.end() || !findings->is_array()) {
        result.error_message = "The response has no findings array.";
        return result;
    }
    result.reviewed_element_id = StringField(parsed, "reviewed_element_id");
    for (const json& finding : *findings) {
        if (!finding.is_object())
            continue;
        BaselineFinding out;
        out.confidence = StringField(finding, "confidence");
        std::vector<std::string> sections;
        if (std::string message = StringField(finding, "message"); !message.empty())
            sections.push_back(std::move(message));
        if (std::string why = StringField(finding, "why_it_matters"); !why.empty())
            sections.push_back("Why it matters:\n" + why);
        if (std::string fix = StringField(finding, "suggested_fix"); !fix.empty())
            sections.push_back("Suggested fix:\n" + fix);
        if (!out.confidence.empty())
            sections.push_back("Confidence:\n" + out.confidence);
        if (sections.empty())
            continue;
        for (std::size_t index = 0; index < sections.size(); ++index)
            out.message += (index > 0 ? "\n\n" : "") + sections[index];
        result.findings.push_back(std::move(out));
    }
    return result;
}

} // namespace eval
