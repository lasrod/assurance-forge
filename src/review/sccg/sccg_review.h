#pragma once

#include "core/assurance_tree.h"
#include "core/problems/problem_item.h"
#include "core/reviews/review_item.h"
#include "core/reviews/review_proposal.h"
#include "parser/guidelines_parser.h"
#include "review/sccg/sccg_prechecks.h"
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

// Why a package is not in the request. Three states rather than two, because a
// review told nothing assumes the data does not exist and may report a
// sufficiency finding that is purely an artifact of what it was not shown.
enum class DataPackageAbsence {
    // The tool has no source for this package at all.
    NotImplemented,
    // The source exists and this case has nothing in it.
    Empty,
    // It exists and was deliberately not shared. Reserved for the per-item
    // consent decision that arrives with linked evidence; nothing sets it yet,
    // and the contract carries it now so adding that later is not a schema
    // break.
    Withheld,
};

const char* DataPackageAbsenceToString(DataPackageAbsence absence);

struct AiReviewUnavailableDataPackage {
    std::string id;
    std::string reason;
    bool required = false;
    DataPackageAbsence absence = DataPackageAbsence::NotImplemented;
};

// What the tool knows beyond the argument itself. Supplied by the caller rather
// than reached for: review items and the user's own words live in application
// state, and a review method that went looking for them would only be usable
// from where that state happens to be.
struct AiReviewCaseContext {
    std::vector<core::reviews::ReviewItem> review_items;
    std::string user_review_intent;
};

struct AiReviewDataPackageBundle {
    std::vector<AiReviewDataPackage> available;
    std::vector<AiReviewUnavailableDataPackage> unavailable;
};

struct AiReviewRequestArtifacts {
    std::string systemInstruction;
    std::string precheckResultsJson;
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
    // Per finding, in the same order as `problems`: the structural repair it
    // asks for, when SCCG's answer is to add or re-attach an element rather
    // than to reword one. Empty for a finding a text edit fixes.
    std::vector<std::vector<core::reviews::PatchOperation>> proposedOperations;
    // Operations the model asked for that were refused, worded for a reader.
    // Reported rather than dropped: a finding whose repair silently vanished
    // reads as a finding with no repair.
    std::vector<std::string> rejectedOperationReasons;
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
                                 std::string& out_error,
                                 const AiReviewCaseContext* case_context = nullptr);

AiReviewRequestArtifacts
BuildAiReviewRequestArtifacts(const AiReviewPayload& payload,
                              const std::vector<const parser::Guideline*>& guidelines,
                              const parser::ReviewProfile* review_profile = nullptr,
                              const AiReviewDataPackageBundle* data_packages = nullptr,
                              const std::vector<review::sccg::PrecheckResult>* precheck_results = nullptr);
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
