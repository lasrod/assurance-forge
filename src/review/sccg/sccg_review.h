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
//
// These are SCCG's own `availability_states` minus `available`, which is the
// present case and needs no absence. `DataPackageAbsenceToString` returns the
// published ids, and a test holds all three against the loaded catalog so this
// enum cannot drift into naming states SCCG does not define.
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
    // SCCG's declared instruction for reviewing without this package, from the
    // profile's `when_absent`. Carried so a degraded review is degraded the way
    // the catalog says rather than the way the model guesses: for
    // `EVIDENCE_BASIS` it is what stops the missing basis being reported as a
    // finding against the argument.
    std::string when_absent_statement;
    // The guidelines that statement says cannot be assessed without the
    // package.
    std::vector<std::string> unassessable_guideline_ids;
};

// What the tool knows beyond the argument itself. Supplied by the caller rather
// than reached for: review items and the user's own words live in application
// state, and a review method that went looking for them would only be usable
// from where that state happens to be.
struct AiReviewCaseContext {
    std::vector<core::reviews::ReviewItem> review_items;
    // What the reviewer said they were worried about. **No surface supplies
    // this yet** -- there is no field on the review action for it -- so in a
    // real run `USER_REVIEW_INTENT` is reported empty rather than carried. The
    // contract holds it because the package is published and a caller that has
    // an intent should be able to pass one without a schema change.
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

// This model mapped onto SCCG's notation-neutral element role -- claim,
// strategy, evidence, context, assumption, justification, challenge -- or empty
// where no role applies.
//
// This is the one mapping point for a tool whose model is neither GSN nor CAE,
// and everything downstream hangs off it: which review profile applies, which
// selected-element data package carries the element, and which element a
// published repair asks for. It replaces a table of GSN, SACM and CAE element
// names, of which the SACM ones matched nothing SCCG published.
std::string SccgElementRoleForElement(const parser::SacmElement& element, const core::TreeNode* node = nullptr);

// Whether the profile reviews this element, decided on the role its
// selected-element package carries. Role rather than `applies_to` name so the
// profile chosen and the package the element is sent in cannot disagree: both
// answer to the same key.
bool IsReviewProfileCompatibleWithElement(const parser::GuidelinesDocument& catalog,
                                          const parser::ReviewProfile& review_profile,
                                          const parser::SacmElement& element,
                                          const core::TreeNode* node = nullptr);

bool BuildAiReviewPayload(const parser::AssuranceCase& assurance_case,
                          const core::AssuranceTree& tree,
                          const std::string& selected_element_id,
                          AiReviewPayload& out_payload,
                          std::string& out_error);
// `catalog` names the packages. The selected element goes in whichever
// package the profile requires with role `selected_element` -- one per element
// role since SCCG 0.7.0, where a single generic `SEL` used to serve them all.
bool CollectAiReviewDataPackages(const parser::AssuranceCase& assurance_case,
                                 const core::AssuranceTree& tree,
                                 const std::string& selected_element_id,
                                 const parser::GuidelinesDocument& catalog,
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
