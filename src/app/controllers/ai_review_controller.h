#pragma once

#include "ai/ai_claim_review.h"
#include "ai/ai_service.h"
#include "ai/ai_task_runner.h"
#include "app/app_events.h"
#include "core/assurance_tree.h"
#include "core/problems/problems_manager.h"
#include "parser/xml_parser.h"

#include <memory>
#include <string>

namespace app::controllers {

class AiReviewController {
public:
    AiReviewController(AppEvents& events,
                       core::ProblemsManager& problems_manager,
                       ai::AiTaskRunner& task_runner,
                       std::shared_ptr<ai::AiService> ai_service);

    void BeginReviewForSelection(const parser::AssuranceCase* assurance_case,
                                 const core::AssuranceTree& current_tree,
                                 const std::string& selected_element_id);
    void StartPendingRequest();
    void PollTask();
    void CancelPendingRequest();

    bool IsReviewRunning() const;
    bool ShouldShowDebugModal() const;
    void SetDebugModalVisible(bool visible);

    const std::string& PendingDebugText() const;
    const std::string& LastRawResponse() const;
    const std::string& LastParseError() const;

private:
    AppEvents& events_;
    core::ProblemsManager& problems_manager_;
    ai::AiTaskRunner& task_runner_;
    std::shared_ptr<ai::AiService> ai_service_;

    std::shared_ptr<ai::AiTaskHandle> review_task_;
    ai::AiReviewRequestArtifacts pending_review_;
    std::string pending_review_element_id_;
    std::string pending_review_element_type_;
    std::string last_raw_response_;
    std::string last_parse_error_;
    bool show_debug_modal_ = false;
};

}  // namespace app::controllers