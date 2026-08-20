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
    AppEvents& events_;
    core::ProblemsManager& problems_manager_;
    ReviewController& review_controller_;
    ai::AiTaskRunner& task_runner_;
    std::shared_ptr<ai::AiService> ai_service_;

    std::shared_ptr<ai::AiTaskHandle> review_task_;
    review::AiReviewRequestArtifacts pending_review_;
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
