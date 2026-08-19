#include "review/sccg/sccg_review.h"

#include "core/string_utils.h"
#include "review/sccg/suggestion_mapping.h"

#include <algorithm>
#include <set>
#include <format>
#include <nlohmann/json.hpp>
#include <string>

namespace review {
namespace {

const char* kAiReviewSystemInstruction =
    "You are reviewing an assurance case element for Assurance Forge. Return JSON only.";

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

AiReviewElement
MakeReviewElement(const parser::SacmElement& element, const std::string& role, const core::TreeNode* node = nullptr) {
    AiReviewElement review_element;
    review_element.role = role;
    review_element.id = element.id;
    review_element.type = AiReviewElementType(element, node);
    review_element.name = element.name;
    review_element.content = element.content;
    review_element.description = element.description;
    return review_element;
}

void AddChildIfPresent(const parser::AssuranceCase& assurance_case,
                       const core::TreeNode* child_node,
                       std::vector<AiReviewElement>& children) {
    if (!child_node)
        return;
    const parser::SacmElement* child = FindSacmElement(assurance_case, child_node->id);
    if (!child)
        return;
    children.push_back(MakeReviewElement(*child, "child", child_node));
}

nlohmann::json StringVectorToJson(const std::vector<std::string>& values) {
    nlohmann::json array = nlohmann::json::array();
    for (const std::string& value : values) {
        array.push_back(value);
    }
    return array;
}

nlohmann::json ReviewDataPackagesToJson(const AiReviewDataPackageBundle* data_packages) {
    nlohmann::json packages = nlohmann::json::array();
    if (!data_packages)
        return packages;
    for (const AiReviewDataPackage& data_package : data_packages->available) {
        try {
            packages.push_back({{"id", data_package.id}, {"data", nlohmann::json::parse(data_package.json)}});
        } catch (const nlohmann::json::exception&) {
            packages.push_back({{"id", data_package.id}, {"data", data_package.json}});
        }
    }
    return packages;
}

nlohmann::json UnavailableDataPackagesToJson(const AiReviewDataPackageBundle* data_packages) {
    nlohmann::json packages = nlohmann::json::array();
    if (!data_packages)
        return packages;
    for (const AiReviewUnavailableDataPackage& data_package : data_packages->unavailable) {
        packages.push_back({
            {"id", data_package.id},
            {"required", data_package.required},
            {"reason", data_package.reason},
        });
    }
    return packages;
}

nlohmann::json PrecheckResultsToJson(const std::vector<review::sccg::PrecheckResult>* precheck_results) {
    nlohmann::json results = nlohmann::json::array();
    if (!precheck_results)
        return results;
    for (const review::sccg::PrecheckResult& result : *precheck_results) {
        nlohmann::json entry{
            {"precheck_id", result.precheck_id},
            {"display_name", result.display_name},
            {"related_guideline_ids", StringVectorToJson(result.guideline_ids)},
            {"result_type", result.result_type},
            {"interpretation", result.interpretation},
            {"status", result.unavailable ? "not_run" : (result.candidate ? "candidate" : "clear")},
        };
        if (!result.detail.empty())
            entry["detail"] = result.detail;
        results.push_back(std::move(entry));
    }
    return results;
}

nlohmann::json ReviewProfileToJson(const parser::ReviewProfile* review_profile) {
    if (!review_profile)
        return nlohmann::json(nullptr);
    return {
        {"id", review_profile->id},
        {"display_name", review_profile->display_name},
        {"description", review_profile->description},
        {"applies_to", StringVectorToJson(review_profile->applies_to)},
        {"guideline_ids", StringVectorToJson(review_profile->guideline_ids)},
        {"required_data", StringVectorToJson(review_profile->required_data)},
        {"optional_data", StringVectorToJson(review_profile->optional_data)},
    };
}

nlohmann::json GuidelinesToJson(const std::vector<const parser::Guideline*>& guidelines_to_review) {
    nlohmann::json guidelines = nlohmann::json::array();
    for (const parser::Guideline* guideline : guidelines_to_review) {
        if (!guideline)
            continue;

        nlohmann::json suggested_checks = nlohmann::json::array();
        for (const parser::SuggestedCheck& check : guideline->tool.suggested_checks) {
            suggested_checks.push_back({
                {"id", check.id},
                {"description", check.description},
            });
        }

        guidelines.push_back({
            {"id", guideline->id},
            {"rule_id", guideline->rule_id.empty() ? guideline->id : guideline->rule_id},
            {"category", guideline->category},
            {"category_id", guideline->category_id.empty() ? guideline->category : guideline->category_id},
            {"title", guideline->title},
            {"statement", guideline->statement},
            {"rationale", guideline->rationale},
            {"review_prompts", StringVectorToJson(guideline->review_prompts)},
            {"reference_source_ids", StringVectorToJson(guideline->reference_source_ids)},
            {"review_profile_ids", StringVectorToJson(guideline->review_profile_ids)},
            {"data_package_ids", StringVectorToJson(guideline->data_package_ids)},
            {"schema_version", guideline->schema_version},
            {"sccg_version", guideline->sccg_version},
            {"examples",
             {
                 {"bad", guideline->examples.bad},
                 {"problem", guideline->examples.problem},
                 {"good", guideline->examples.good},
             }},
            {"tool",
             {
                 {"applicable_elements", StringVectorToJson(guideline->tool.applicable_elements)},
                 {"detection_hints", StringVectorToJson(guideline->tool.detection_hints)},
                 {"suggested_checks", suggested_checks},
             }},
        });
    }
    return guidelines;
}

bool IsAllowedGuidelineId(const std::string& guideline_id, const std::vector<std::string>& allowed_guideline_ids) {
    return std::find(allowed_guideline_ids.begin(), allowed_guideline_ids.end(), guideline_id) !=
           allowed_guideline_ids.end();
}

void AddMappedName(std::vector<std::string>& names, const std::string& name) {
    if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end())
        names.push_back(name);
}

std::string NodeRoleName(core::NodeRole role) {
    switch (role) {
    case core::NodeRole::Claim:
        return "claim";
    case core::NodeRole::Strategy:
        return "strategy";
    case core::NodeRole::Solution:
        return "solution";
    case core::NodeRole::Context:
        return "context";
    case core::NodeRole::Assumption:
        return "assumption";
    case core::NodeRole::Justification:
        return "justification";
    case core::NodeRole::Other:
        return "other";
    }
    return "other";
}

// The text the model is shown for an element. Delegates so that the field a
// suggestion is staged into cannot drift from the text the suggestion answered;
// they were two rules once, and they disagreed.
std::string ElementText(const parser::SacmElement& element) {
    return TextTargetFor(element).current_text;
}

nlohmann::json
ElementDataToJson(const parser::SacmElement& element, const std::string& role, const core::TreeNode* node = nullptr) {
    return {
        {"role", role},
        {"element_id", element.id},
        {"element_type", AiReviewElementType(element, node)},
        {"sccg_applies_to", StringVectorToJson(SccgAppliesToNamesForElement(element, node))},
        {"raw_type", element.type},
        {"name", element.name},
        {"text", ElementText(element)},
        {"description", element.description},
        {"content", element.content},
        {"undeveloped", element.undeveloped},
        {"tree_role", node ? NodeRoleName(node->role) : std::string{}},
    };
}

const parser::SacmElement* ElementForNode(const parser::AssuranceCase& assurance_case, const core::TreeNode* node) {
    return node ? FindSacmElement(assurance_case, node->id) : nullptr;
}

void AddPackage(AiReviewDataPackageBundle& packages, const std::string& id, const nlohmann::json& data) {
    packages.available.push_back(AiReviewDataPackage{id, data.dump(2)});
}

void AddUnavailable(AiReviewDataPackageBundle& packages,
                    const std::string& id,
                    const std::string& reason,
                    bool required) {
    packages.unavailable.push_back(AiReviewUnavailableDataPackage{id, reason, required});
}

bool HasPackage(const AiReviewDataPackageBundle& packages, const std::string& id) {
    return std::find_if(packages.available.begin(), packages.available.end(), [&](const AiReviewDataPackage& package) {
               return package.id == id;
           }) != packages.available.end();
}

core::ProblemSeverity SeverityFromString(const std::string& value) {
    std::string severity = core::ToLower(value);
    if (severity == "info")
        return core::ProblemSeverity::Info;
    if (severity == "error")
        return core::ProblemSeverity::Error;
    return core::ProblemSeverity::Warning;
}

std::string JsonStringValue(const nlohmann::json& object, const char* key) {
    if (!object.is_object())
        return {};
    auto value = object.find(key);
    if (value == object.end() || !value->is_string())
        return {};
    return value->get<std::string>();
}

std::string BuildProblemMessage(const nlohmann::json& finding) {
    std::string message = JsonStringValue(finding, "message");
    std::string why = JsonStringValue(finding, "why_it_matters");
    std::string suggested_fix = JsonStringValue(finding, "suggested_fix");
    std::string suggested_element_text = JsonStringValue(finding, "suggested_element_text");
    std::string suggested_claim_wording = JsonStringValue(finding, "suggested_claim_wording");
    std::string confidence = JsonStringValue(finding, "confidence");

    std::vector<std::string> sections;
    if (!message.empty())
        sections.push_back(message);
    if (!why.empty())
        sections.push_back("Why it matters:\n" + why);
    if (!suggested_fix.empty())
        sections.push_back("Suggested fix:\n" + suggested_fix);
    if (!suggested_element_text.empty())
        sections.push_back("Suggested element text:\n" + suggested_element_text);
    else if (!suggested_claim_wording.empty())
        sections.push_back("Suggested claim wording:\n" + suggested_claim_wording);
    if (!confidence.empty())
        sections.push_back("Confidence:\n" + confidence);

    std::string formatted_message;
    for (size_t index = 0; index < sections.size(); ++index) {
        if (index > 0)
            formatted_message += "\n\n";
        formatted_message += sections[index];
    }
    return formatted_message;
}

} // namespace

const parser::SacmElement* FindSacmElement(const parser::AssuranceCase& assurance_case, const std::string& element_id) {
    for (const parser::SacmElement& element : assurance_case.elements) {
        if (element.id == element_id)
            return &element;
    }
    return nullptr;
}

bool IsSupportedAiReviewElement(const parser::SacmElement& element) {
    return element.type == "claim" || element.type == "argumentreasoning" || element.type == "artifact" ||
           element.type == "artifactreference" || element.type == "expression";
}

std::string AiReviewElementType(const parser::SacmElement& element, const core::TreeNode* node) {
    if (node && node->is_counter_source)
        return "GSN Counter Claim / SACM " + (element.type.empty() ? std::string("Element") : element.type);
    if (node) {
        switch (node->role) {
        case core::NodeRole::Claim:
            return "GSN Goal / SACM Claim";
        case core::NodeRole::Strategy:
            return "GSN Strategy / SACM ArgumentReasoning";
        case core::NodeRole::Solution:
            return "GSN Solution / SACM ArtifactReference";
        case core::NodeRole::Context:
            return "GSN Context / SACM ArtifactReference";
        case core::NodeRole::Assumption:
            return "GSN Assumption / SACM Claim";
        case core::NodeRole::Justification:
            return "GSN Justification / SACM Claim";
        case core::NodeRole::Other:
            break;
        }
    }
    if (element.type == "claim") {
        if (element.assertion_declaration == "assumed")
            return "GSN Assumption / SACM Claim";
        if (element.assertion_declaration == "justification")
            return "GSN Justification / SACM Claim";
        return "GSN Goal / SACM Claim";
    }
    if (element.type == "argumentreasoning")
        return "GSN Strategy / SACM ArgumentReasoning";
    if (element.type == "artifact" || element.type == "artifactreference" || element.type == "expression")
        return "GSN Solution / SACM ArtifactReference";
    return element.type.empty() ? "SACM Element" : "SACM " + element.type;
}

std::vector<std::string> SccgAppliesToNamesForElement(const parser::SacmElement& element, const core::TreeNode* node) {
    std::vector<std::string> names;
    if (node && node->is_counter_source) {
        AddMappedName(names, "GSN Counter Claim");
        AddMappedName(names, "CAE Defeater");
        return names;
    }
    if (node) {
        switch (node->role) {
        case core::NodeRole::Claim:
            AddMappedName(names, "GSN Goal");
            AddMappedName(names, "SACM Claim");
            AddMappedName(names, "CAE Claim");
            return names;
        case core::NodeRole::Strategy:
            AddMappedName(names, "GSN Strategy");
            AddMappedName(names, "SACM ArgumentReasoning");
            AddMappedName(names, "CAE Argument");
            return names;
        case core::NodeRole::Solution:
            AddMappedName(names, "GSN Solution");
            AddMappedName(names, "SACM ArtifactReference");
            AddMappedName(names, "CAE Evidence");
            return names;
        case core::NodeRole::Context:
            AddMappedName(names, "GSN Context");
            AddMappedName(names, "SACM ArtifactReference");
            AddMappedName(names, "CAE Context");
            return names;
        case core::NodeRole::Assumption:
            AddMappedName(names, "GSN Assumption");
            AddMappedName(names, "SACM Claim");
            AddMappedName(names, "CAE Assumption");
            return names;
        case core::NodeRole::Justification:
            AddMappedName(names, "GSN Justification");
            AddMappedName(names, "SACM Claim");
            AddMappedName(names, "CAE Warrant");
            return names;
        case core::NodeRole::Other:
            break;
        }
    }
    if (element.type == "claim") {
        if (element.assertion_declaration == "assumed" || (node && node->role == core::NodeRole::Assumption)) {
            AddMappedName(names, "GSN Assumption");
            AddMappedName(names, "SACM Claim");
            AddMappedName(names, "CAE Assumption");
            return names;
        }
        if (element.assertion_declaration == "justification" || (node && node->role == core::NodeRole::Justification)) {
            AddMappedName(names, "GSN Justification");
            AddMappedName(names, "SACM Claim");
            AddMappedName(names, "CAE Warrant");
            return names;
        }
        AddMappedName(names, "GSN Goal");
        AddMappedName(names, "SACM Claim");
        AddMappedName(names, "CAE Claim");
        return names;
    }
    if (element.type == "argumentreasoning") {
        AddMappedName(names, "GSN Strategy");
        AddMappedName(names, "SACM ArgumentReasoning");
        AddMappedName(names, "CAE Argument");
        return names;
    }
    if (element.type == "artifact" || element.type == "artifactreference" || element.type == "expression" ||
        (node && node->role == core::NodeRole::Solution)) {
        AddMappedName(names, "GSN Solution");
        AddMappedName(names, "SACM ArtifactReference");
        AddMappedName(names, "CAE Evidence");
        return names;
    }
    return names;
}

bool IsReviewProfileCompatibleWithElement(const parser::ReviewProfile& review_profile,
                                          const parser::SacmElement& element,
                                          const core::TreeNode* node) {
    const std::vector<std::string> names = SccgAppliesToNamesForElement(element, node);
    for (const std::string& name : names) {
        if (std::find(review_profile.applies_to.begin(), review_profile.applies_to.end(), name) !=
            review_profile.applies_to.end()) {
            return true;
        }
    }
    return false;
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
        out_error = "AI Review does not support the selected element type.";
        return false;
    }

    const core::TreeNode* selected_node = core::FindTreeNode(tree, selected_element_id);
    AiReviewPayload payload;
    payload.selected = MakeReviewElement(*selected, "selected", selected_node);

    if (selected_node && selected_node->parent) {
        const parser::SacmElement* parent = FindSacmElement(assurance_case, selected_node->parent->id);
        if (parent)
            payload.parent = MakeReviewElement(*parent, "parent", selected_node->parent);
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

bool CollectAiReviewDataPackages(const parser::AssuranceCase& assurance_case,
                                 const core::AssuranceTree& tree,
                                 const std::string& selected_element_id,
                                 const parser::ReviewProfile* review_profile,
                                 AiReviewDataPackageBundle& out_packages,
                                 std::string& out_error) {
    out_packages = {};
    const parser::SacmElement* selected = FindSacmElement(assurance_case, selected_element_id);
    if (!selected) {
        out_error = "Selected element was not found.";
        return false;
    }

    const core::TreeNode* selected_node = core::FindTreeNode(tree, selected_element_id);
    AddPackage(out_packages, "SEL", ElementDataToJson(*selected, "selected", selected_node));

    if (selected_node && selected_node->parent) {
        if (const parser::SacmElement* parent = ElementForNode(assurance_case, selected_node->parent))
            AddPackage(out_packages, "PARENT", ElementDataToJson(*parent, "parent", selected_node->parent));
    }

    if (selected_node && !selected_node->group1_children.empty()) {
        nlohmann::json children = nlohmann::json::array();
        for (const core::TreeNode* child_node : selected_node->group1_children) {
            if (const parser::SacmElement* child = ElementForNode(assurance_case, child_node))
                children.push_back(ElementDataToJson(*child, "child", child_node));
        }
        AddPackage(out_packages, "CHILDREN", {{"child_elements", children}});
    }

    if (selected_node && !selected_node->group2_attachments.empty()) {
        nlohmann::json context = nlohmann::json::array();
        nlohmann::json assumptions = nlohmann::json::array();
        nlohmann::json justifications = nlohmann::json::array();
        for (const core::TreeNode* attachment_node : selected_node->group2_attachments) {
            const parser::SacmElement* attachment = ElementForNode(assurance_case, attachment_node);
            if (!attachment)
                continue;
            nlohmann::json element_json = ElementDataToJson(*attachment, "direct_context", attachment_node);
            if (attachment_node->role == core::NodeRole::Assumption)
                assumptions.push_back(element_json);
            else if (attachment_node->role == core::NodeRole::Justification)
                justifications.push_back(element_json);
            else
                context.push_back(element_json);
        }
        AddPackage(out_packages,
                   "DIRECT_CONTEXT",
                   {{"context_elements", context}, {"assumptions", assumptions}, {"justifications", justifications}});
    }

    if (selected_node) {
        nlohmann::json ancestor_context = nlohmann::json::array();
        nlohmann::json ancestor_assumptions = nlohmann::json::array();
        for (const core::TreeNode* ancestor = selected_node->parent; ancestor; ancestor = ancestor->parent) {
            for (const core::TreeNode* attachment_node : ancestor->group2_attachments) {
                const parser::SacmElement* attachment = ElementForNode(assurance_case, attachment_node);
                if (!attachment)
                    continue;
                nlohmann::json element_json = ElementDataToJson(*attachment, "inherited_context", attachment_node);
                element_json["ancestor_id"] = ancestor->id;
                if (attachment_node->role == core::NodeRole::Assumption)
                    ancestor_assumptions.push_back(element_json);
                else
                    ancestor_context.push_back(element_json);
            }
        }
        if (!ancestor_context.empty() || !ancestor_assumptions.empty()) {
            AddPackage(out_packages,
                       "INHERITED_CONTEXT",
                       {{"ancestor_context", ancestor_context}, {"ancestor_assumptions", ancestor_assumptions}});
        }
    }

    const core::TreeNode* strategy_node = nullptr;
    if (selected_node && selected_node->role == core::NodeRole::Strategy)
        strategy_node = selected_node;
    else if (selected_node && selected_node->parent && selected_node->parent->role == core::NodeRole::Strategy)
        strategy_node = selected_node->parent;
    else if (selected_node) {
        for (const core::TreeNode* child_node : selected_node->group1_children) {
            if (child_node && child_node->role == core::NodeRole::Strategy) {
                strategy_node = child_node;
                break;
            }
        }
    }
    if (const parser::SacmElement* strategy = ElementForNode(assurance_case, strategy_node))
        AddPackage(out_packages, "STRATEGY", ElementDataToJson(*strategy, "strategy", strategy_node));

    if (selected_node && selected_node->role == core::NodeRole::Solution)
        AddPackage(out_packages, "EVIDENCE_ITEM", ElementDataToJson(*selected, "evidence_item", selected_node));

    if (selected_node) {
        nlohmann::json path_elements = nlohmann::json::array();
        nlohmann::json evidence_items = nlohmann::json::array();
        std::vector<const core::TreeNode*> stack(selected_node->group1_children.begin(),
                                                 selected_node->group1_children.end());
        while (!stack.empty()) {
            const core::TreeNode* node = stack.back();
            stack.pop_back();
            const parser::SacmElement* element = ElementForNode(assurance_case, node);
            if (!element)
                continue;
            path_elements.push_back(ElementDataToJson(*element, "evidence_path", node));
            if (node->role == core::NodeRole::Solution)
                evidence_items.push_back(ElementDataToJson(*element, "evidence_item", node));
            for (const core::TreeNode* child_node : node->group1_children)
                stack.push_back(child_node);
        }
        if (!evidence_items.empty())
            AddPackage(
                out_packages, "EVIDENCE_PATH", {{"path_elements", path_elements}, {"evidence_items", evidence_items}});
    }

    if (review_profile) {
        auto mark_missing = [&](const std::vector<std::string>& package_ids, bool required) {
            for (const std::string& package_id : package_ids) {
                if (HasPackage(out_packages, package_id))
                    continue;
                AddUnavailable(out_packages,
                               package_id,
                               "Assurance Forge does not have this data package available yet.",
                               required);
            }
        };
        mark_missing(review_profile->required_data, true);
        mark_missing(review_profile->optional_data, false);
    }

    out_error.clear();
    return true;
}

AiReviewRequestArtifacts
BuildAiReviewRequestArtifacts(const AiReviewPayload& payload,
                              const std::vector<const parser::Guideline*>& guidelines_to_review,
                              const parser::ReviewProfile* review_profile,
                              const AiReviewDataPackageBundle* data_packages,
                              const std::vector<review::sccg::PrecheckResult>* precheck_results) {
    nlohmann::json selected = ReviewElementToJson(payload.selected);
    nlohmann::json parent = payload.parent.transform([](const AiReviewElement& p) { return ReviewElementToJson(p); })
                                .value_or(nlohmann::json(nullptr));
    nlohmann::json children = nlohmann::json::array();
    for (const AiReviewElement& child : payload.children) {
        children.push_back(ReviewElementToJson(child));
    }
    nlohmann::json review_profile_json = ReviewProfileToJson(review_profile);
    nlohmann::json guidelines = GuidelinesToJson(guidelines_to_review);
    nlohmann::json available_data_packages = ReviewDataPackagesToJson(data_packages);
    nlohmann::json unavailable_data_packages = UnavailableDataPackagesToJson(data_packages);

    AiReviewRequestArtifacts artifacts;
    artifacts.systemInstruction = kAiReviewSystemInstruction;
    artifacts.precheckResultsJson = PrecheckResultsToJson(precheck_results).dump(2);
    artifacts.selectedElementJson = selected.dump(2);
    artifacts.parentElementJson = parent.dump(2);
    artifacts.childElementsJson = children.dump(2);
    artifacts.availableDataPackagesJson = available_data_packages.dump(2);
    artifacts.unavailableDataPackagesJson = unavailable_data_packages.dump(2);
    artifacts.reviewProfileJson = review_profile_json.dump(2);
    artifacts.guidelinesJson = guidelines.dump(2);
    artifacts.responseSchemaJson = BuildExpectedAiReviewResponseSchemaText();
    artifacts.expectedResponseSchema = artifacts.responseSchemaJson;

    const std::string review_profile_heading =
        review_profile ? ": " + review_profile->id : std::string(": CL category fallback");
    artifacts.prompt = std::format(
        "You are reviewing the selected assurance case element using the SCCG review profile below.\n\n"
        "Assurance Forge is an assurance case tool using SACM as the domain model and GSN as one graphical view. "
        "Use SCCG applies_to values and the selected element data to interpret the element.\n\n"
        "Use only the SCCG rules provided in this request. Return findings that reference the relevant SCCG rule "
        "IDs.\n\n"
        "Review only the selected element. Use related elements and data packages only as context.\n\n"
        "Do not invent missing project information.\n"
        "Pre-check results are candidate signals a tool decided mechanically, not findings. Observe each "
        "one's stated interpretation: a candidate still needs your judgement, and a check reported not_run "
        "was never performed, which is not the same as passing.\n"
        "Treat unavailable data packages as unavailable; do not assume their contents.\n"
        "Do not claim that a rule is violated unless the provided data supports that finding.\n"
        "If there is no clear violation, return an empty findings array.\n"
        "Return JSON only. Do not include Markdown. Do not include explanations outside the JSON object.\n\n"
        "## SCCG review profile{}\n\n"
        "{}\n\n"
        "## SCCG rules\n\n"
        "{}\n\n"
        "## Deterministic pre-check results\n\n"
        "{}\n\n"
        "## Available data packages\n\n"
        "{}\n\n"
        "## Unavailable data packages\n\n"
        "{}\n\n"
        "## Selected element\n\n"
        "{}\n\n"
        "## Parent element\n\n"
        "{}\n\n"
        "## Direct child/sub-elements\n\n"
        "{}\n\n"
        "## Required JSON response\n\n"
        "{}\n",
        review_profile_heading,
        artifacts.reviewProfileJson,
        artifacts.guidelinesJson,
        artifacts.precheckResultsJson,
        artifacts.availableDataPackagesJson,
        artifacts.unavailableDataPackagesJson,
        artifacts.selectedElementJson,
        artifacts.parentElementJson,
        artifacts.childElementsJson,
        artifacts.responseSchemaJson);

    artifacts.debugText = std::format("Selected element data\n{}\n\n"
                                      "Parent element data\n{}\n\n"
                                      "Child/sub-element data\n{}\n\n"
                                      "Available data packages\n{}\n\n"
                                      "Unavailable data packages\n{}\n\n"
                                      "SCCG review profile data\n{}\n\n"
                                      "SCCG guideline data\n{}\n\n"
                                      "Final AI prompt text\n{}\n\n"
                                      "Expected JSON response schema\n{}\n",
                                      artifacts.selectedElementJson,
                                      artifacts.parentElementJson,
                                      artifacts.childElementsJson,
                                      artifacts.availableDataPackagesJson,
                                      artifacts.unavailableDataPackagesJson,
                                      artifacts.reviewProfileJson,
                                      artifacts.guidelinesJson,
                                      artifacts.prompt,
                                      artifacts.responseSchemaJson);
    return artifacts;
}

AiReviewPromptParts BuildAiReviewPrompt(const AiReviewPayload& payload,
                                        const std::vector<const parser::Guideline*>& guidelines,
                                        const parser::ReviewProfile* review_profile,
                                        const AiReviewDataPackageBundle* data_packages) {
    return BuildAiReviewRequestArtifacts(payload, guidelines, review_profile, data_packages);
}

namespace {

void CollectElementIdsFromJson(const nlohmann::json& value, std::set<std::string>& element_ids) {
    if (value.is_object()) {
        for (const auto& item : value.items()) {
            if ((item.key() == "element_id" || item.key() == "ancestor_id") && item.value().is_string()) {
                element_ids.insert(item.value().get<std::string>());
            }
            CollectElementIdsFromJson(item.value(), element_ids);
        }
        return;
    }
    if (value.is_array()) {
        for (const nlohmann::json& child : value) {
            CollectElementIdsFromJson(child, element_ids);
        }
    }
}

} // namespace

std::vector<std::string> ReviewedElementIds(const AiReviewPayload& payload,
                                            const AiReviewDataPackageBundle& data_packages) {
    std::set<std::string> element_ids;
    if (!payload.selected.id.empty())
        element_ids.insert(payload.selected.id);
    if (payload.parent.has_value() && !payload.parent->id.empty())
        element_ids.insert(payload.parent->id);
    for (const AiReviewElement& child : payload.children) {
        if (!child.id.empty())
            element_ids.insert(child.id);
    }
    for (const AiReviewDataPackage& data_package : data_packages.available) {
        const nlohmann::json parsed = nlohmann::json::parse(data_package.json, nullptr, false);
        if (!parsed.is_discarded())
            CollectElementIdsFromJson(parsed, element_ids);
    }
    return std::vector<std::string>(element_ids.begin(), element_ids.end());
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
            "suggested_element_text": "string",
      "suggested_claim_wording": "string",
      "proposed_operations": [
        {
          "type": "CreateStrategy | CreateClaim | CreateSolution | CreateContext | CreateAssumption | CreateJustification | CreateTerm | UpdateElementText | SetUndeveloped | ClearUndeveloped | AddSupportedBy | RemoveSupportedBy | AddInContextOf | RemoveInContextOf",
          "create_ref": "string",
          "element": {"id": "existing element id"},
          "source": {"id": "existing element id"} | {"ref": "$a_create_ref"},
          "target": {"id": "existing element id"} | {"ref": "$a_create_ref"},
          "text": "string",
          "field": "string",
          "old_value": "string",
          "new_value": "string"
        }
      ],
      "related_element_ids": ["string"]
    }
  ]
}

Field rules:

- source must be "SCCG".
- guideline_id must match one of the provided SCCG guideline IDs.
- guideline_title must match the title of the referenced guideline.
- severity should normally be "warning" for claim quality issues.
- message should describe what is violated.
- why_it_matters should explain the review concern briefly.
- suggested_fix should describe how the user can improve the selected element or its immediate review context.
- suggested_element_text should provide replacement text for the selected element when a concise text edit would fix the finding. Use this for selected strategies, reasoning steps, claims, or other reviewed elements when appropriate.
- suggested_claim_wording is a legacy alias for claim wording suggestions. Prefer suggested_element_text; leave suggested_claim_wording empty unless the selected element is a claim and you need claim-specific wording.
- proposed_operations is how a finding asks for a repair SCCG describes as adding or restructuring, rather than rewording. Leave it empty when a text edit is the whole fix.
  - A "Create..." operation names the new element with "create_ref" (any label you choose, e.g. "$strategy") and its text in "text". Attach it with a second operation whose "source" or "target" carries {"ref": "$strategy"}.
  - AddSupportedBy attaches "source" beneath "target"; AddInContextOf attaches context, assumption or justification to "target".
  - Reference an existing element as {"id": "G1"} and a new one as {"ref": "$strategy"}.
  - Every existing element you touch must be one shown to you in the data packages above. Operations reaching outside them are refused.
- What SCCG asks for, when the repair is structural:
  - AR.2, a decomposition with no stated reasoning: CreateStrategy, then AddSupportedBy it under the parent and re-attach the sub-claims beneath it.
  - EV.1, a claim with no evidence path: CreateSolution and AddSupportedBy, or SetUndeveloped when the work is genuinely outstanding.
  - CL.3, AR.3, AR.6, AR.7, RD.1, RD.6, scope or a dependency hidden in the claim text: CreateContext or CreateAssumption, then AddInContextOf.
  - SU.2, SU.9, an assumption that is really an unsupported claim: CreateClaim and attach it, so it needs support.
  - CL.5, an unbounded qualifier: CreateTerm, defining the term once, rather than restating the bound in every claim.
- Do not propose an operation you cannot justify from a provided guideline. A finding with no repair is better than an invented one.
- related_element_ids should include the selected element ID and any parent/child IDs relevant to the finding.
- If there are no findings, return "findings": [].

Return JSON only.)json";
}

std::string StripJsonCodeFence(const std::string& response_text) {
    std::string trimmed = core::TrimWhitespace(response_text);
    if (trimmed.rfind("```", 0) != 0)
        return trimmed;

    size_t first_line_end = trimmed.find('\n');
    if (first_line_end == std::string::npos)
        return trimmed;

    size_t fence_start = trimmed.rfind("```");
    if (fence_start == 0 || fence_start == std::string::npos)
        return trimmed;

    return core::TrimWhitespace(trimmed.substr(first_line_end + 1, fence_start - first_line_end - 1));
}

AiReviewParseResult ParseAiReviewResponse(const std::string& response_text, const std::string& selected_element_id) {
    return ParseAiReviewResponse(response_text, selected_element_id, std::vector<std::string>{});
}

AiReviewParseResult ParseAiReviewResponse(const std::string& response_text,
                                          const std::string& selected_element_id,
                                          const std::vector<std::string>& allowed_guideline_ids) {
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
        if (result.reviewedElementId.empty())
            result.reviewedElementId = selected_element_id;
        result.reviewedElementType = JsonStringValue(root, "reviewed_element_type");

        size_t finding_index = 0;
        for (const nlohmann::json& finding : root["findings"]) {
            ++finding_index;
            if (!finding.is_object())
                continue;

            std::string guideline_id = JsonStringValue(finding, "guideline_id");
            if (guideline_id.empty())
                guideline_id = JsonStringValue(finding, "rule_id");
            const std::string original_guideline_id = guideline_id;
            const bool unknown_guideline_id =
                !allowed_guideline_ids.empty() &&
                (guideline_id.empty() || !IsAllowedGuidelineId(guideline_id, allowed_guideline_ids));
            if (guideline_id.empty())
                guideline_id = "unknown";

            core::ProblemItem problem;
            problem.id =
                "ai-review:" + result.reviewedElementId + ":" + guideline_id + ":" + std::to_string(finding_index);
            problem.severity = SeverityFromString(JsonStringValue(finding, "severity"));
            problem.source = core::ProblemSource::AIReview;
            problem.element_id = result.reviewedElementId;
            problem.type = result.reviewedElementType;
            problem.message = BuildProblemMessage(finding);
            if (unknown_guideline_id) {
                const std::string shown_id =
                    original_guideline_id.empty() ? std::string("<empty>") : original_guideline_id;
                problem.message += " Unknown SCCG rule reference: " + shown_id + ".";
            }
            problem.guideline_id = unknown_guideline_id || guideline_id == "unknown" ? std::string{} : guideline_id;
            result.problems.push_back(std::move(problem));
            std::string suggested_element_text = JsonStringValue(finding, "suggested_element_text");
            if (suggested_element_text.empty())
                suggested_element_text = JsonStringValue(finding, "suggested_claim_wording");
            result.suggestedElementTexts.push_back(std::move(suggested_element_text));

            // Parsed here, judged later: whether an operation is one this review
            // is allowed to make depends on the model and the reviewed scope,
            // neither of which parsing has. A malformed operation is dropped
            // with its reason so the finding still reaches the reviewer.
            std::vector<core::reviews::PatchOperation> operations;
            const nlohmann::json::const_iterator proposed = finding.find("proposed_operations");
            if (proposed != finding.end() && proposed->is_array()) {
                size_t operation_index = 0;
                for (const nlohmann::json& operation_json : *proposed) {
                    ++operation_index;
                    core::reviews::PatchOperation operation;
                    std::string operation_error;
                    if (!core::reviews::ParsePatchOperationJson(operation_json, operation, operation_error)) {
                        result.rejectedOperationReasons.push_back("Finding " + std::to_string(finding_index) +
                                                                  ", operation " + std::to_string(operation_index) +
                                                                  ": " + operation_error);
                        continue;
                    }
                    operations.push_back(std::move(operation));
                }
            }
            result.proposedOperations.push_back(std::move(operations));
        }

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
    if (result.reviewedElementType.empty())
        result.reviewedElementType = fallback_element_type;
    for (core::ProblemItem& problem : result.problems) {
        if (problem.type.empty())
            problem.type = result.reviewedElementType;
    }
    return result;
}

} // namespace review
