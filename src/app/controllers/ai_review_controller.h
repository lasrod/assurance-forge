#pragma once

#include "ai/ai_service.h"
#include "ai/ai_task_runner.h"
#include "app/app_events.h"
#include "core/guideline_catalog.h"
#include "app/controllers/review_controller.h"
#include "core/assurance_tree.h"
#include "core/problems/problems_manager.h"
#include "parser/xml_parser.h"
#include "review/sccg/sccg_profile_selector.h"
#include "review/sccg/sccg_review.h"
#include "review/sccg/sccg_review_preparation.h"

#include <chrono>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace app::controllers {

class AiReviewController {
public:
    AiReviewController(AppEvents& events,
                       core::ProblemsManager& problems_manager,
                       ReviewController& review_controller,
                       ai::AiTaskRunner& task_runner,
                       std::shared_ptr<ai::AiService> ai_service);

    void BeginReviewForSelection(const parser::AssuranceCase* assurance_case,
                                 const core::AssuranceTree& current_tree,
                                 const std::string& selected_element_id);
    void BeginReviewForSelection(const parser::AssuranceCase* assurance_case,
                                 const core::AssuranceTree& current_tree,
                                 const std::string& selected_element_id,
                                 const std::string& review_profile_id);
    void StartPendingRequest();
    void MarkPendingRequestIncludesWorkingDraft();
    void PollTask();
    void CancelPendingRequest();
    bool WaitForCompletion(std::chrono::milliseconds timeout) const;

    bool IsReviewRunning() const;
    bool ShouldShowDebugModal() const;
    void SetDebugModalVisible(bool visible);

    bool HasPendingRequest() const;
    const std::string& PendingPrompt() const;
    void SetPendingPrompt(std::string prompt);
    const std::string& PendingDebugText() const;
    const std::string& LastRawResponse() const;
    const std::string& LastParseError() const;

private:
    void ReportPreparationFailure(const review::SccgReviewPreparation& preparation,
                                  const std::string& selected_element_id,
                                  const std::string& requested_review_profile_id);
    // The single-request path, unchanged from before review passes existed.
    void CompleteSingleRequest(const ai::AiResponse& response);
    // A review sent as several passes, merged when every pass has answered.
    void CompletePassRequests(std::vector<ai::AiResponse> responses);
    // Records findings as review items and reports the outcome. A non-empty
    // `incomplete_reason` means some passes failed: the findings of the ones
    // that ran are still recorded, and the review is reported failed so an
    // incomplete review can never earn the green no-findings badge.
    void ApplyReviewFindings(review::AiReviewParseResult parse_result, const std::string& incomplete_reason);
    void RebuildCombinedPrompt();

    AppEvents& events_;
    core::ProblemsManager& problems_manager_;
    ReviewController& review_controller_;
    ai::AiTaskRunner& task_runner_;
    std::shared_ptr<ai::AiService> ai_service_;

    // One task per pass, in pass order. A single-request review has one.
    std::vector<std::shared_ptr<ai::AiTaskHandle>> review_tasks_;
    review::AiReviewRequestArtifacts pending_review_;
    // The requests to send. More than one when the profile publishes review
    // passes (SCCG 0.8.0); exactly one, covering the whole profile, otherwise.
    std::vector<review::SccgReviewPassRequest> pending_passes_;
    // What PendingPrompt shows for a multi-pass review: every pass's prompt
    // under a separator naming it. Rebuilt when the passes change.
    std::string pending_combined_prompt_;
    std::string pending_review_element_id_;
    std::string pending_review_element_type_;
    std::string pending_review_profile_id_;
    std::string pending_review_profile_name_;
    std::unordered_set<std::string> pending_review_scope_element_ids_;
    std::vector<std::string> pending_guideline_ids_;
    std::string pending_review_run_id_;
    // The elements the pending review read, and their hash. A result is stale
    // when its own scope moved -- not when the model moved somewhere else.
    std::vector<std::string> pending_review_scope_element_id_list_;
    std::string pending_review_scope_hash_;
    std::string last_raw_response_;
    std::string last_parse_error_;
    bool show_debug_modal_ = false;
};

} // namespace app::controllers
