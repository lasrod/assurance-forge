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

} // namespace

const char* DataPackageAbsenceToString(DataPackageAbsence absence) {
    switch (absence) {
    case DataPackageAbsence::NotImplemented:
        return "not_implemented";
    case DataPackageAbsence::Empty:
        return "empty";
    case DataPackageAbsence::Withheld:
        return "withheld";
    }
    return "not_implemented";
}

namespace {

nlohmann::json UnavailableDataPackagesToJson(const AiReviewDataPackageBundle* data_packages) {
    nlohmann::json packages = nlohmann::json::array();
    if (!data_packages)
        return packages;
    for (const AiReviewUnavailableDataPackage& data_package : data_packages->unavailable) {
        nlohmann::json entry{
            {"id", data_package.id},
            {"required", data_package.required},
            {"reason", data_package.reason},
            {"absence", DataPackageAbsenceToString(data_package.absence)},
        };
        // Emitted separately: a catalog that names the guidelines it cannot
        // assess without saying what to do instead still has to reach the
        // model with that list, and one that gives the instruction without a
        // list is equally legitimate.
        if (!data_package.when_absent_statement.empty()) {
            entry["when_absent"] = data_package.when_absent_statement;
        }
        if (!data_package.unassessable_guideline_ids.empty()) {
            entry["unassessable_guideline_ids"] = StringVectorToJson(data_package.unassessable_guideline_ids);
        }
        packages.push_back(std::move(entry));
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

nlohmann::json ReviewProfileToJson(const parser::ReviewProfile* review_profile, const parser::ReviewPass* review_pass) {
    if (!review_profile)
        return nlohmann::json(nullptr);
    nlohmann::json profile{
        {"id", review_profile->id},
        {"display_name", review_profile->display_name},
        {"description", review_profile->description},
        {"applies_to", StringVectorToJson(review_profile->applies_to)},
        {"guideline_ids", StringVectorToJson(review_profile->guideline_ids)},
        {"required_data", StringVectorToJson(review_profile->required_data)},
        {"optional_data", StringVectorToJson(review_profile->optional_data)},
    };
    if (!review_profile->review_passes.empty()) {
        nlohmann::json passes = nlohmann::json::array();
        for (const parser::ReviewPass& pass : review_profile->review_passes) {
            passes.push_back({{"id", pass.id},
                              {"display_name", pass.display_name},
                              {"question", pass.question},
                              {"guideline_ids", StringVectorToJson(pass.guideline_ids)}});
        }
        profile["review_passes"] = std::move(passes);
    }
    if (review_pass) {
        profile["this_request_is_pass"] = {{"id", review_pass->id},
                                           {"display_name", review_pass->display_name},
                                           {"question", review_pass->question},
                                           {"guideline_ids", StringVectorToJson(review_pass->guideline_ids)}};
    }
    return profile;
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

        // SCCG 0.8.0's disambiguation, verbatim. Published for the pairs it saw
        // confused: without it a review files a correctly-detected defect under a
        // neighbouring guideline, which is what 11 of 48 measured probes did.
        nlohmann::json distinctions = nlohmann::json::array();
        for (const parser::GuidelineDistinction& distinction : guideline->distinguish_from)
            distinctions.push_back({{"id", distinction.id}, {"note", distinction.note}});

        // The repair SCCG prescribes, every guideline having one since 0.7.0.
        // The response contract translates this vocabulary into operations, so the
        // model proposes the catalogue's repair rather than a list this tool kept
        // by hand -- a list that named three guidelines SCCG has since retired.
        nlohmann::json repairs = nlohmann::json::array();
        for (const parser::GuidelineRepair& repair : guideline->tool.repair) {
            repairs.push_back({{"action", repair.action},
                               {"element_role", repair.element_role},
                               {"attach_to", repair.attach_to},
                               {"statement", repair.statement}});
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
            {"distinguish_from", distinctions},
            {"tool",
             {
                 {"applicable_elements", StringVectorToJson(guideline->tool.applicable_elements)},
                 {"detection_hints", StringVectorToJson(guideline->tool.detection_hints)},
                 {"suggested_checks", suggested_checks},
                 {"repair", repairs},
             }},
        });
    }
    return guidelines;
}

bool IsAllowedGuidelineId(const std::string& guideline_id, const std::vector<std::string>& allowed_guideline_ids) {
    return std::find(allowed_guideline_ids.begin(), allowed_guideline_ids.end(), guideline_id) !=
           allowed_guideline_ids.end();
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
        {"sccg_element_role", SccgElementRoleForElement(element, node)},
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
                    bool required,
                    DataPackageAbsence absence) {
    packages.unavailable.push_back(AiReviewUnavailableDataPackage{id, reason, required, absence});
}

bool HasUnavailable(const AiReviewDataPackageBundle& packages, const std::string& id) {
    return std::find_if(packages.unavailable.begin(),
                        packages.unavailable.end(),
                        [&](const AiReviewUnavailableDataPackage& package) { return package.id == id; }) !=
           packages.unavailable.end();
}

// The packages this tool builds. One of these missing from a request means the
// case has nothing to put in it, which is a fact about the argument; a package
// NOT listed here is one Assurance Forge has no source for, which is a fact
// about the tool. SCCG publishes separate availability states for the two and a
// review is entitled to read them differently -- absent context the tool could
// not supply is a gap in the review, absent context the case does not contain
// may be a gap in the argument.
//
// EVIDENCE_BASIS and STANDARD_LINKS are deliberately absent from this list:
// they are reported as not-implemented above, with their own reasons.
struct EmptyPackageReason {
    const char* id;
    const char* reason;
};

const EmptyPackageReason* FindEmptyPackageReason(const std::string& package_id) {
    static constexpr EmptyPackageReason kReasons[] = {
        {"PARENT", "This element has no parent in the argument."},
        {"CHILDREN", "This element has no children in the argument."},
        {"DIRECT_CONTEXT", "No context, assumption or justification is attached to this element."},
        {"INHERITED_CONTEXT", "No context or assumption is attached to any ancestor of this element."},
        {"STRATEGY", "No strategy element stands between this element and its support."},
        {"EVIDENCE_PATH", "No evidence is reachable below this element."},
        {"EVIDENCE_ITEM", "This element is not an evidence item."},
        {"SELECTED_CLAIM", "The selected element is not a claim."},
        {"SELECTED_STRATEGY", "The selected element is not a strategy."},
        {"SELECTED_EVIDENCE", "The selected element is not an evidence item."},
        {"SELECTED_CONTEXT", "The selected element is not a context element."},
        {"SELECTED_ASSUMPTION", "The selected element is not an assumption."},
        {"SELECTED_JUSTIFICATION", "The selected element is not a justification."},
        {"SELECTED_CHALLENGE", "The selected element is not a challenge."},
    };
    for (const EmptyPackageReason& reason : kReasons) {
        if (package_id == reason.id)
            return &reason;
    }
    return nullptr;
}

bool HasPackage(const AiReviewDataPackageBundle& packages, const std::string& id) {
    return std::find_if(packages.available.begin(), packages.available.end(), [&](const AiReviewDataPackage& package) {
               return package.id == id;
           }) != packages.available.end();
}

// Confidence, as the model reported it, normalized. Unlike the severity this
// replaced, it is a judgement the model is actually positioned to make: how far
// the data it was given supports the finding. The contract no longer asks for a
// severity at all -- SCCG defines none, so the field was the tool's own
// invention, and the prompt line that said it "should normally be warning" got
// exactly what it asked for: 149 findings out of 149 marked warning, a field
// carrying no information a reviewer could rank or filter by.
std::string ConfidenceFromString(const std::string& value) {
    std::string confidence = core::ToLower(value);
    if (confidence == "low" || confidence == "medium" || confidence == "high")
        return confidence;
    return {};
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

std::string SccgElementRoleForElement(const parser::SacmElement& element, const core::TreeNode* node) {
    // A counter source is a challenge whatever it is built from, so it is
    // decided before the node role: the same SACM claim is a goal when it
    // supports and a defeater when it challenges.
    if (node && node->is_counter_source)
        return "challenge";
    if (node) {
        switch (node->role) {
        case core::NodeRole::Claim:
            return "claim";
        case core::NodeRole::Strategy:
            return "strategy";
        case core::NodeRole::Solution:
            return "evidence";
        case core::NodeRole::Context:
            return "context";
        case core::NodeRole::Assumption:
            return "assumption";
        case core::NodeRole::Justification:
            return "justification";
        case core::NodeRole::Other:
            break;
        }
    }
    // No tree node, or a node the tree could not place: fall back to what the
    // element itself declares. A review can be asked for from a register or a
    // search result, where there is no canvas node to hand.
    if (element.type == "claim") {
        if (element.assertion_declaration == "assumed")
            return "assumption";
        if (element.assertion_declaration == "justification")
            return "justification";
        return "claim";
    }
    if (element.type == "argumentreasoning")
        return "strategy";
    if (element.type == "artifact" || element.type == "artifactreference" || element.type == "expression")
        return "evidence";
    return {};
}

bool IsReviewProfileCompatibleWithElement(const parser::GuidelinesDocument& catalog,
                                          const parser::ReviewProfile& review_profile,
                                          const parser::SacmElement& element,
                                          const core::TreeNode* node) {
    const std::string element_role = SccgElementRoleForElement(element, node);
    if (element_role.empty())
        return false;
    const parser::DataPackage* selected_package = catalog.FindSelectedElementPackage(review_profile);
    return selected_package != nullptr && selected_package->element_role == element_role;
}

namespace {

// The package the selected element belongs in. From the profile when there is
// one -- exactly one of its required packages has role `selected_element` --
// and otherwise from the element's own role, which is the same answer by a
// longer route and keeps a profile-less collection working.
std::string SelectedElementPackageId(const parser::GuidelinesDocument& catalog,
                                     const parser::ReviewProfile* review_profile,
                                     const parser::SacmElement& element,
                                     const core::TreeNode* node) {
    if (review_profile != nullptr) {
        if (const parser::DataPackage* package = catalog.FindSelectedElementPackage(*review_profile))
            return package->id;
    }
    const std::string element_role = SccgElementRoleForElement(element, node);
    if (element_role.empty())
        return {};
    for (const parser::DataPackage& package : catalog.data_packages) {
        if (package.role == "selected_element" && package.element_role == element_role)
            return package.id;
    }
    return {};
}

} // namespace

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

namespace {

void CollectElementIdsFrom(const nlohmann::json& value, std::vector<std::string>& out) {
    if (value.is_object()) {
        for (const auto& item : value.items()) {
            if ((item.key() == "element_id" || item.key() == "ancestor_id") && item.value().is_string()) {
                const std::string id = item.value().get<std::string>();
                if (std::find(out.begin(), out.end(), id) == out.end())
                    out.push_back(id);
            }
            CollectElementIdsFrom(item.value(), out);
        }
        return;
    }
    if (value.is_array()) {
        for (const nlohmann::json& child : value)
            CollectElementIdsFrom(child, out);
    }
}

// PROJECT_GLOSSARY. The terms a case defines are exactly what CL.4 and AR.6 ask
// about -- whether a broad word is bounded somewhere -- and the case has been
// able to hold them since terminology landed. Reviewing without them asks the
// model to judge ambiguity against definitions the project already wrote.
nlohmann::json ProjectGlossaryJson(const parser::AssuranceCase& assurance_case) {
    nlohmann::json terms = nlohmann::json::array();
    for (const parser::SacmElement& element : assurance_case.elements) {
        if (element.type != "term")
            continue;
        terms.push_back({
            {"element_id", element.id},
            {"term", element.content},
            {"definition", element.description},
            {"name", element.name},
        });
    }
    return terms;
}

// CHANGE_HISTORY. Prior findings on the elements under review: whether this
// claim has been challenged before, and whether the challenge was resolved or
// dismissed. SU.4, SU.5 and SU.11 all turn on it.
//
// Filed under SCCG's published field names -- `prior_findings` for what an AI
// review raised, `review_comments` for what a person wrote -- rather than under
// a key of this tool's own. That stopped being cosmetic in 0.8.0, which judges
// the availability of a package with no required fields by whether its
// published fields are populated: history sent under any other key is, by the
// catalogue's definition, an empty package.
struct ChangeHistory {
    nlohmann::json prior_findings = nlohmann::json::array();
    nlohmann::json review_comments = nlohmann::json::array();

    bool empty() const {
        return prior_findings.empty() && review_comments.empty();
    }
};

ChangeHistory ChangeHistoryJson(const std::vector<core::reviews::ReviewItem>& review_items,
                                const std::vector<std::string>& scope_element_ids,
                                const parser::GuidelinesDocument& catalog) {
    ChangeHistory history;
    for (const core::reviews::ReviewItem& item : review_items) {
        if (std::find(scope_element_ids.begin(), scope_element_ids.end(), item.element_id) == scope_element_ids.end())
            continue;
        nlohmann::json entry{
            {"element_id", item.element_id},
            {"title", item.title},
            {"message", item.message},
            {"reviewer", item.reviewer_name},
            {"status", core::reviews::ReviewItemStatusToString(item.status)},
            {"guideline_ids", StringVectorToJson(item.guideline_ids)},
            {"created_utc", item.created_utc},
        };
        // A finding recorded against a guideline SCCG has since retired still
        // happened, so its id is kept as recorded -- and the redirect is carried
        // beside it, or the review reads a prior AR.3 finding as a citation of a
        // rule it was never given.
        nlohmann::json retired = nlohmann::json::array();
        for (const std::string& guideline_id : item.guideline_ids) {
            if (const parser::RetiredGuideline* entry_retired = catalog.FindRetiredGuidelineById(guideline_id)) {
                retired.push_back({{"id", entry_retired->id},
                                   {"retired_in", entry_retired->retired_in},
                                   {"replaced_by", StringVectorToJson(entry_retired->replaced_by)},
                                   {"note", entry_retired->note}});
            }
        }
        if (!retired.empty())
            entry["retired_guidelines"] = std::move(retired);

        if (item.source == core::reviews::ReviewItemSource::AIReview)
            history.prior_findings.push_back(std::move(entry));
        else
            history.review_comments.push_back(std::move(entry));
    }
    return history;
}

bool IsPopulated(const nlohmann::json& value) {
    if (value.is_null())
        return false;
    if (value.is_string())
        return !value.get_ref<const std::string&>().empty();
    if (value.is_array() || value.is_object())
        return !value.empty();
    return true;
}

} // namespace

// SCCG 0.9.0: a package is available when "supplied with its required fields
// present, and at least one of its fields is populated", where populated means
// anything but null, an empty string, an empty list or an empty object -- "the
// same rule applies to every package, whether or not it has required fields."
// (0.8.0 stated it only for packages with no required fields, which left
// `CHILDREN` sent as `{"child_elements": []}` undefined; raised as
// safety-case-core-guidelines#17.) Applied to every package the collector
// built, generically and from the registry.
//
// Empty is not the absence of information: SCCG's when_unavailable lets the
// review rely on an empty package as a fact about the case -- a claim with no
// children has no path to evidence -- so demoting a package to empty is itself
// something the review is told.
//
// Judged on the PUBLISHED field names only. A package carrying its data under a
// key SCCG does not name has, by the catalogue's own definition, none of its
// fields populated -- which is exactly the drift this is meant to surface.
//
// Both halves of the rule apply. A package with something populated but a
// required field left out is not available; it is not empty either -- the case
// may well hold what was left out -- so it is reported as a package the tool did
// not supply, naming the fields it lacks.
void ApplyContentDefinedAvailability(AiReviewDataPackageBundle& packages,
                                     const parser::GuidelinesDocument& catalog,
                                     const parser::ReviewProfile* review_profile) {
    for (auto package = packages.available.begin(); package != packages.available.end();) {
        const parser::DataPackage* definition = catalog.FindDataPackageById(package->id);
        if (definition == nullptr) {
            ++package;
            continue;
        }
        const nlohmann::json parsed = nlohmann::json::parse(package->json, nullptr, false);
        std::string missing_required;
        for (const std::string& field : definition->required_fields) {
            if (parsed.is_object() && parsed.contains(field))
                continue;
            if (!missing_required.empty())
                missing_required += ", ";
            missing_required += field;
        }
        bool populated = false;
        if (parsed.is_object()) {
            for (const std::vector<std::string>* fields :
                 {&definition->required_fields, &definition->optional_fields}) {
                for (const std::string& field : *fields) {
                    const auto value = parsed.find(field);
                    if (value != parsed.end() && IsPopulated(*value)) {
                        populated = true;
                        break;
                    }
                }
                if (populated)
                    break;
            }
        }
        if (populated && missing_required.empty()) {
            ++package;
            continue;
        }
        const bool required =
            review_profile != nullptr &&
            std::find(review_profile->required_data.begin(), review_profile->required_data.end(), package->id) !=
                review_profile->required_data.end();
        if (populated) {
            std::string reason = "Supplied without its required field(s) ";
            reason += missing_required;
            reason += ", so SCCG does not count it as available.";
            packages.unavailable.push_back(AiReviewUnavailableDataPackage{
                package->id, std::move(reason), required, DataPackageAbsence::NotImplemented});
        } else {
            packages.unavailable.push_back(
                AiReviewUnavailableDataPackage{package->id,
                                               "Supplied with none of its published fields populated, which SCCG "
                                               "counts as empty.",
                                               required,
                                               DataPackageAbsence::Empty});
        }
        package = packages.available.erase(package);
    }
}

bool CollectAiReviewDataPackages(const parser::AssuranceCase& assurance_case,
                                 const core::AssuranceTree& tree,
                                 const std::string& selected_element_id,
                                 const parser::GuidelinesDocument& catalog,
                                 const parser::ReviewProfile* review_profile,
                                 AiReviewDataPackageBundle& out_packages,
                                 std::string& out_error,
                                 const AiReviewCaseContext* case_context) {
    out_packages = {};
    out_packages.when_unavailable = catalog.when_unavailable;
    out_packages.availability_states = catalog.availability_states;
    const parser::SacmElement* selected = FindSacmElement(assurance_case, selected_element_id);
    if (!selected) {
        out_error = "Selected element was not found.";
        return false;
    }

    const core::TreeNode* selected_node = core::FindTreeNode(tree, selected_element_id);
    // Which package the selected element travels in is the profile's decision,
    // read from the catalog rather than named here. SCCG 0.7.0 replaced the one
    // generic `SEL` with one package per element role, and a tool that kept
    // sending `SEL` would send a package no profile asks for while reporting
    // the profile's own required package as unavailable -- a degraded review
    // every time, with nothing failing to say so.
    const std::string selected_package_id = SelectedElementPackageId(catalog, review_profile, *selected, selected_node);
    if (selected_package_id.empty()) {
        out_error = "The SCCG catalog names no selected-element data package for this element.";
        return false;
    }
    AddPackage(out_packages, selected_package_id, ElementDataToJson(*selected, "selected", selected_node));

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

    const nlohmann::json glossary = ProjectGlossaryJson(assurance_case);
    if (!glossary.empty()) {
        AddPackage(out_packages, "PROJECT_GLOSSARY", {{"terms", glossary}});
    } else {
        AddUnavailable(
            out_packages, "PROJECT_GLOSSARY", "This case defines no terms yet.", false, DataPackageAbsence::Empty);
    }

    if (case_context != nullptr) {
        // Scoped to the elements the packages carry, so a review is shown the
        // history of what it is reading and not of the whole case.
        std::vector<std::string> scope_ids{selected_element_id};
        for (const AiReviewDataPackage& package : out_packages.available) {
            const nlohmann::json parsed = nlohmann::json::parse(package.json, nullptr, false);
            if (!parsed.is_discarded())
                CollectElementIdsFrom(parsed, scope_ids);
        }
        const ChangeHistory history = ChangeHistoryJson(case_context->review_items, scope_ids, catalog);
        if (!history.empty()) {
            nlohmann::json package = nlohmann::json::object();
            if (!history.prior_findings.empty())
                package["prior_findings"] = history.prior_findings;
            if (!history.review_comments.empty())
                package["review_comments"] = history.review_comments;
            AddPackage(out_packages, "CHANGE_HISTORY", package);
        } else {
            AddUnavailable(out_packages,
                           "CHANGE_HISTORY",
                           "Nothing under review has been reviewed before.",
                           false,
                           DataPackageAbsence::Empty);
        }

        if (!case_context->user_review_intent.empty()) {
            // `review_intent` is the package's one published field; `intent`, the
            // key this used to be sent under, is not in the catalogue at all.
            AddPackage(out_packages, "USER_REVIEW_INTENT", {{"review_intent", case_context->user_review_intent}});
        }
    }

    if (!HasPackage(out_packages, "USER_REVIEW_INTENT")) {
        AddUnavailable(out_packages,
                       "USER_REVIEW_INTENT",
                       "The reviewer did not state a particular concern for this run.",
                       false,
                       DataPackageAbsence::Empty);
    }

    // EVIDENCE_BASIS: this tool has no field for any of the six things the
    // package carries -- acceptance criteria, coverage, thresholds, scenario
    // set, configuration, limitations -- so it has no source for the package.
    //
    // Under SCCG 0.7.0 that absence silenced eight guidelines: evidence_review's
    // `when_absent` named EV.5, EV.6, SU.3, SU.6, SU.7, SU.8, LF.5 and LF.7
    // unassessable, and measured over three runs per element EV.5 was cited 0 of
    // 3 times against evidence that stated no sufficiency basis at all. This
    // tool worked round it by sending the package available with every field
    // empty. SCCG 0.8.0 fixed the cause instead (safety-case-core-guidelines#13):
    // the package is optional in evidence_review, the `when_absent` entry is
    // gone, and the registry-wide `when_unavailable` rule says an unavailable
    // package never silences a guideline. It also defines a package with no
    // required fields and nothing in it as EMPTY, which makes the workaround
    // non-conforming. So the package is reported for what it is.
    AddUnavailable(out_packages,
                   "EVIDENCE_BASIS",
                   "Assurance Forge has no field in which a project records the acceptance criteria, coverage, "
                   "thresholds, scenario set, configuration or limitations behind an evidence item. The route is "
                   "the evidence register, once it links the artifact itself.",
                   false,
                   DataPackageAbsence::NotImplemented);

    // No source in the tool at all, named so the absence is a stated limitation
    // rather than a silent one.
    AddUnavailable(out_packages,
                   "STANDARD_LINKS",
                   "Assurance Forge does not model links to external standard requirements.",
                   false,
                   DataPackageAbsence::NotImplemented);

    ApplyContentDefinedAvailability(out_packages, catalog, review_profile);

    if (review_profile) {
        // EVIDENCE_BASIS was added above as not required; say whether this
        // profile requires it, now that the profile is known.
        for (AiReviewUnavailableDataPackage& package : out_packages.unavailable) {
            if (std::find(review_profile->required_data.begin(), review_profile->required_data.end(), package.id) !=
                review_profile->required_data.end())
                package.required = true;
        }
        auto mark_missing = [&](const std::vector<std::string>& package_ids, bool required) {
            for (const std::string& package_id : package_ids) {
                if (HasPackage(out_packages, package_id))
                    continue;
                if (HasUnavailable(out_packages, package_id))
                    continue;
                // A package this tool builds, that this case has nothing for,
                // is EMPTY -- not "not implemented". A root goal has no parent
                // and a leaf claim has no children; saying the tool cannot
                // produce those invites a review to discount structure the case
                // genuinely does not have, which is the opposite of what the
                // three availability states are for.
                const EmptyPackageReason* empty_reason = FindEmptyPackageReason(package_id);
                AddUnavailable(out_packages,
                               package_id,
                               empty_reason ? empty_reason->reason
                                            : "Assurance Forge does not have this data package available yet.",
                               required,
                               empty_reason ? DataPackageAbsence::Empty : DataPackageAbsence::NotImplemented);
            }
        };
        mark_missing(review_profile->required_data, true);
        mark_missing(review_profile->optional_data, false);

        // What the profile says a review missing this package should do
        // instead. Attached to the absence rather than left to the model:
        // `evidence_review` requires `EVIDENCE_BASIS`, this tool has no source
        // for it, and without the statement the honest degradation -- report
        // citation and control, say sufficiency was not assessed, do not report
        // the absent basis as a finding -- is the model's guess to make.
        for (const parser::DataPackageAbsenceStatement& statement : review_profile->when_absent) {
            for (AiReviewUnavailableDataPackage& package : out_packages.unavailable) {
                if (package.id != statement.id)
                    continue;
                package.when_absent_statement = statement.statement;
                package.unassessable_guideline_ids = statement.unassessable_guideline_ids;
            }
        }
    }

    out_error.clear();
    return true;
}

AiReviewRequestArtifacts
BuildAiReviewRequestArtifacts(const AiReviewPayload& payload,
                              const std::vector<const parser::Guideline*>& guidelines_to_review,
                              const parser::ReviewProfile* review_profile,
                              const AiReviewDataPackageBundle* data_packages,
                              const std::vector<review::sccg::PrecheckResult>* precheck_results,
                              const parser::ReviewPass* review_pass,
                              const std::string& review_pass_instruction) {
    nlohmann::json selected = ReviewElementToJson(payload.selected);
    nlohmann::json parent = payload.parent.transform([](const AiReviewElement& p) { return ReviewElementToJson(p); })
                                .value_or(nlohmann::json(nullptr));
    nlohmann::json children = nlohmann::json::array();
    for (const AiReviewElement& child : payload.children) {
        children.push_back(ReviewElementToJson(child));
    }
    nlohmann::json review_profile_json = ReviewProfileToJson(review_profile, review_pass);
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
        (review_profile ? ": " + review_profile->id : std::string(": CL category fallback")) +
        (review_pass ? ", pass " + review_pass->id : std::string{});

    // The pass this request is, stated where the model reads its instructions.
    // A pass request carries only that pass's guidelines, and the review must
    // not answer the other passes' questions from memory: each is asked in its
    // own request, and a finding cited outside its pass is discarded on merge.
    // SCCG publishes the sentence (0.9.0), with `{question}` its one
    // placeholder, and every tool sending it verbatim is what makes two tools
    // frame a pass the same way. The sentence below is only for a catalogue
    // that predates it.
    std::string pass_instruction;
    if (review_pass && !review_pass_instruction.empty()) {
        pass_instruction = review_pass_instruction;
        const std::string placeholder = "{question}";
        const std::size_t at = pass_instruction.find(placeholder);
        if (at != std::string::npos)
            pass_instruction.replace(at, placeholder.size(), review_pass->question);
        pass_instruction += "\n\n";
    } else if (review_pass) {
        pass_instruction = std::format(
            "This request is one review pass of the profile: \"{}\". It asks: {} The profile's other passes are "
            "sent as separate requests, so review only against the rules in this request and do not report a "
            "finding under any other guideline, even one the profile carries.\n\n",
            review_pass->display_name,
            review_pass->question);
    }

    // SCCG's own rule for unavailable packages, and its own meaning for each
    // availability state, from the catalogue the packages were collected
    // against. The paraphrase that stood here predates the published rule and
    // is kept only for a catalogue that has none.
    std::string unavailable_instruction;
    if (data_packages != nullptr && !data_packages->when_unavailable.empty()) {
        unavailable_instruction =
            "Unavailable data packages -- SCCG's rule, follow it exactly: " + data_packages->when_unavailable + "\n";
        for (const parser::AvailabilityState& state : data_packages->availability_states) {
            unavailable_instruction += "  - " + state.id + ": " + state.meaning + "\n";
        }
    } else {
        unavailable_instruction =
            "Treat unavailable data packages as unavailable; do not assume their contents.\n"
            "An unavailable package says why: not_implemented means this tool has no source for it, empty means "
            "the case holds none, and withheld means it exists and was deliberately not shared. Withheld is not "
            "absent -- say so when a judgement is bounded by what you were not shown, rather than concluding the "
            "data does not exist.\n";
    }

    // Three segments, ordered from the most shared to the least, so a provider
    // can cache each prefix and a later request pays for only what differs:
    //   1. what every review sends: the instructions and the response contract;
    //   2. what every review of this profile and pass sends: the pass sentence,
    //      the profile and its rules -- most of the request;
    //   3. what only this element sends: pre-checks, packages, the element.
    // The text is the same as when it was one string; only the order changed,
    // with the response contract moved ahead of the element data. Nothing that
    // depends on the element may move into the first two segments, or no two
    // reviews would share them.
    const std::string shared_segment = std::format(
        "You are reviewing the selected assurance case element using the SCCG review profile below.\n\n"
        "Assurance Forge is an assurance case tool using SACM as the domain model and GSN as one graphical view. "
        "Each element carries the SCCG element_role it maps onto -- claim, strategy, evidence, context, "
        "assumption, justification, challenge -- which is the vocabulary the profile and the data packages "
        "use. Interpret the element through its role and its data.\n\n"
        "Use only the SCCG rules provided in this request. Return findings that reference the relevant SCCG rule "
        "IDs.\n"
        "Where a rule lists distinguish_from, its notes are SCCG's instruction for choosing between that rule and "
        "a neighbouring one. When a defect could be read as either, cite the one the note says fits.\n\n"
        "Review only the selected element. Use related elements and data packages only as context.\n\n"
        "Do not invent missing project information.\n"
        "Pre-check results are candidate signals a tool decided mechanically, not findings. Observe each "
        "one's stated interpretation: a candidate still needs your judgement, and a check reported not_run "
        "was never performed, which is not the same as passing.\n"
        "{}"
        "Where an unavailable package carries a when_absent statement, it is the one exception SCCG makes to that "
        "rule: follow it exactly, and do not report the guidelines it names as unassessable against the "
        "argument.\n"
        "Do not claim that a rule is violated unless the provided data supports that finding.\n"
        "If there is no clear violation, return an empty findings array.\n"
        "Return JSON only. Do not include Markdown. Do not include explanations outside the JSON object.\n\n"
        "## Required JSON response\n\n"
        "{}\n\n",
        unavailable_instruction,
        artifacts.responseSchemaJson);
    const std::string profile_segment = std::format("{}"
                                                    "## SCCG review profile{}\n\n"
                                                    "{}\n\n"
                                                    "## SCCG rules\n\n"
                                                    "{}\n\n",
                                                    pass_instruction,
                                                    review_profile_heading,
                                                    artifacts.reviewProfileJson,
                                                    artifacts.guidelinesJson);
    const std::string element_segment = std::format("## Deterministic pre-check results\n\n"
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
                                                    "{}\n",
                                                    artifacts.precheckResultsJson,
                                                    artifacts.availableDataPackagesJson,
                                                    artifacts.unavailableDataPackagesJson,
                                                    artifacts.selectedElementJson,
                                                    artifacts.parentElementJson,
                                                    artifacts.childElementsJson);
    artifacts.promptSegments = {shared_segment, profile_segment, element_segment};
    artifacts.prompt = shared_segment + profile_segment + element_segment;
    // Requests that share the first two segments share this key, so the
    // provider routes them to the cache that already holds those segments.
    artifacts.promptCacheKey = "sccg-" + (review_profile ? review_profile->sccg_version : std::string("none")) + "-" +
                               (review_profile ? review_profile->id : std::string("fallback")) +
                               (review_pass ? "-" + review_pass->id : std::string{});

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

std::vector<std::string> CorroboratingPrecheckIds(const std::string& guideline_id,
                                                  const std::vector<sccg::PrecheckResult>& precheck_results) {
    std::vector<std::string> ids;
    if (guideline_id.empty())
        return ids;
    for (const sccg::PrecheckResult& precheck : precheck_results) {
        if (!precheck.candidate)
            continue;
        if (std::find(precheck.guideline_ids.begin(), precheck.guideline_ids.end(), guideline_id) !=
            precheck.guideline_ids.end())
            ids.push_back(precheck.precheck_id);
    }
    return ids;
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
- confidence states how far the supplied data supports the finding: "high" when the
  provided element text and data packages show the violation directly, "medium" when
  the finding depends on a reading the data permits but does not settle, "low" when it
  rests on something you were not shown. Do NOT report a low-confidence finding whose
  basis is an unavailable data package -- report the missing context instead.
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
- Each rule's tool.repair states the repair SCCG prescribes for it. Propose that repair, translated as follows, and nothing the rule does not prescribe:
  - add_element, element_role strategy, attach_to between_selected_and_children: CreateStrategy; AddSupportedBy it under the selected element; move the selected element's children beneath it (RemoveSupportedBy, then AddSupportedBy under the strategy).
  - add_element, element_role claim, attach_to children: CreateClaim; AddSupportedBy it under the selected element, so the new claim itself needs support.
  - add_element, element_role evidence, attach_to children: CreateSolution; AddSupportedBy it under the selected element.
  - add_element, element_role context, assumption or justification, attach_to selected: CreateContext, CreateAssumption or CreateJustification; AddInContextOf the selected element.
  - add_element, element_role challenge: there is no operation for adding a challenge. Describe it in suggested_fix and propose no operation.
  - move_text with an element_role of context or assumption: CreateContext or CreateAssumption carrying the moved text; AddInContextOf the selected element; and give the selected element's remaining text in suggested_element_text.
  - move_text with no element_role: give the remaining text in suggested_element_text and say in suggested_fix where the moved text belongs.
  - define_term: CreateTerm, defining the term once, rather than restating the bound in every claim.
  - mark_undeveloped: SetUndeveloped on the selected element, when the support is genuinely outstanding.
  - reword_element: no operation; put the new wording in suggested_element_text.
  - split_element: CreateClaim for each part split off, attached where the selected element is attached; the part the selected element keeps goes in suggested_element_text.
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
            // Assigned by the tool, not asked of the model. Every SCCG finding is
            // an argument-quality observation for a human to judge; ranking them
            // is what `confidence` and pre-check corroboration are for.
            problem.severity = core::ProblemSeverity::Warning;
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
            result.findingConfidences.push_back(ConfidenceFromString(JsonStringValue(finding, "confidence")));
            result.citedGuidelineIds.push_back(original_guideline_id);
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
