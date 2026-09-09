#include "app/controllers/ai_review_controller.h"

#include "core/reviews/review_proposal.h"
#include "core/reviews/review_text_utils.h"
#include "core/time_utils.h"
#include "parser/guidelines_parser.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <string>
#include <unordered_set>
#include <vector>

namespace app::controllers {
namespace {

using core::NowUtcString;
using core::reviews::TruncateForProblemMessage;

std::string GenerateAiReviewRunId() {
    static std::atomic_uint64_t counter = 0;
    return "ai-review-run-" + NowUtcString() + "-" + std::to_string(++counter);
}

core::ProblemItem MakeAiReviewProblem(const std::string& id,
                                      core::ProblemSeverity severity,
                                      const std::string& element_id,
                                      const std::string& type,
                                      const std::string& message,
                                      const std::string& guideline_id = {}) {
    core::ProblemItem problem;
    problem.id = id;
    problem.severity = severity;
    problem.source = core::ProblemSource::AIReview;
    problem.element_id = element_id;
    problem.type = type;
    problem.message = message;
    problem.guideline_id = guideline_id;
    return problem;
}

std::string ReviewCommentPrefix(const std::string& element_id, const std::string& profile_id) {
    return "ai-review-comment:" + element_id + ":" + profile_id + ":";
}

std::string SeverityString(core::ProblemSeverity severity) {
    switch (severity) {
    case core::ProblemSeverity::Info:
        return "info";
    case core::ProblemSeverity::Warning:
        return "warning";
    case core::ProblemSeverity::Error:
        return "error";
    }
    return "warning";
}

std::string ReviewTitleForProblem(const core::ProblemItem& problem) {
    if (!problem.guideline_id.empty())
        return "AI review finding: " + problem.guideline_id;
    return "AI review finding";
}

void EmitReviewVisualEvent(AppEvents& events,
                           ElementReviewVisualEventKind kind,
                           const std::string& element_id,
                           const std::string& review_profile_id = {},
                           const std::string& review_profile_name = {},
                           const std::string& message = {},
                           std::unordered_set<std::string> review_scope_element_ids = {}) {
    events.Emit(ElementReviewVisualEvent{
        kind, element_id, review_profile_id, review_profile_name, message, std::move(review_scope_element_ids)});
}

core::reviews::ReviewItem MakeAiReviewItem(const std::string& id,
                                           const std::string& element_id,
                                           const std::string& title,
                                           const std::string& message,
                                           core::ProblemSeverity severity,
                                           const std::string& timestamp,
                                           const std::string& guideline_id = {}) {
    core::reviews::ReviewItem review_item;
    review_item.id = id;
    review_item.element_id = element_id;
    review_item.title = title;
    review_item.message = message;
    review_item.severity = SeverityString(severity);
    review_item.reviewer_name = "AI Review";
    if (!guideline_id.empty())
        review_item.guideline_ids = {guideline_id};
    review_item.source = core::reviews::ReviewItemSource::AIReview;
    review_item.status = core::reviews::ReviewItemStatus::Open;
    review_item.created_utc = timestamp;
    review_item.updated_utc = timestamp;
    return review_item;
}

void ReplaceAiReviewWithSingleItem(ReviewController& review_controller,
                                   const std::string& element_id,
                                   const std::string& review_prefix,
                                   const std::string& suffix,
                                   const std::string& title,
                                   const std::string& message,
                                   core::ProblemSeverity severity) {
    review_controller.ClearAiReviewItemsForElementAndPrefix(element_id, review_prefix);
    review_controller.AddOrUpdateItem(
        MakeAiReviewItem(review_prefix + suffix, element_id, title, message, severity, NowUtcString()));
}

} // namespace

AiReviewController::AiReviewController(AppEvents& events,
                                       core::ProblemsManager& problems_manager,
                                       ReviewController& review_controller,
                                       ai::AiTaskRunner& task_runner,
                                       std::shared_ptr<ai::AiService> ai_service)
    : events_(events),
      problems_manager_(problems_manager),
      review_controller_(review_controller),
      task_runner_(task_runner),
      ai_service_(std::move(ai_service)) {}

void AiReviewController::BeginReviewForSelection(const parser::AssuranceCase* assurance_case,
                                                 const core::AssuranceTree& current_tree,
                                                 const std::string& selected_element_id) {
    BeginReviewForSelection(assurance_case, current_tree, selected_element_id, {});
}

void AiReviewController::BeginReviewForSelection(const parser::AssuranceCase* assurance_case,
                                                 const core::AssuranceTree& current_tree,
                                                 const std::string& selected_element_id,
                                                 const std::string& review_profile_id) {
    if (review_task_ && review_task_->IsRunning()) {
        events_.Emit(StatusMessageEvent{"AI review is already running."});
        return;
    }

    // What the tool knows beyond the argument: prior findings, which is what
    // SU.4, SU.5 and SU.11 turn on. Every item rather than the selected
    // element's, because the collector scopes them to what the data packages
    // actually carry -- a challenge standing against the parent claim is part of
    // this review's history, and filtering here would hide it.
    review::AiReviewCaseContext case_context;
    case_context.review_items = review_controller_.Items();

    // Steps 1-5 of the SCCG workflow, which `review` owns. Everything below is
    // the part only the application can do with the outcome.
    const review::SccgReviewPreparation preparation =
        review::PrepareSccgReview(assurance_case, current_tree, selected_element_id, review_profile_id, case_context);

    if (!preparation.ok()) {
        ReportPreparationFailure(preparation, selected_element_id, review_profile_id);
        return;
    }

    pending_review_ = preparation.request;
    pending_review_element_id_ = preparation.payload.selected.id;
    pending_review_element_type_ = preparation.payload.selected.type;
    pending_review_profile_id_ = preparation.review_profile_id;
    pending_review_profile_name_ = preparation.review_profile_name;
    pending_review_scope_element_ids_ = std::unordered_set<std::string>(preparation.reviewed_element_ids.begin(),
                                                                        preparation.reviewed_element_ids.end());
    pending_review_scope_element_id_list_ = preparation.reviewed_element_ids;
    pending_guideline_ids_ = preparation.guideline_ids;
    pending_review_scope_hash_ =
        core::reviews::ComputeScopeSemanticHash(*assurance_case, preparation.reviewed_element_ids);
    pending_review_run_id_.clear();
    last_raw_response_.clear();
    last_parse_error_.clear();
    show_debug_modal_ = false;
    events_.Emit(StatusMessageEvent{"AI review request is ready in the AI Debug panel."});
}

// The two failure styles are not interchangeable. A setup problem the user can
// see and fix from the selection alone (nothing selected, an element type SCCG
// does not review) is a Problems entry; a failure of the review itself is
// recorded against the element as a review item, drawn on the canvas, and
// stored as that element's AI review outcome, because a review that was
// attempted and failed must not leave the element looking unreviewed.
void AiReviewController::ReportPreparationFailure(const review::SccgReviewPreparation& preparation,
                                                  const std::string& selected_element_id,
                                                  const std::string& requested_review_profile_id) {
    const std::string& message = preparation.error_message;

    const auto report_as_problem = [&](const std::string& problem_suffix,
                                       core::ProblemSeverity severity,
                                       const std::string& element_id,
                                       const std::string& type) {
        const std::string problem_id =
            element_id.empty() ? "ai-review:" + problem_suffix : "ai-review:" + element_id + ":" + problem_suffix;
        problems_manager_.AddOrUpdateProblem(MakeAiReviewProblem(problem_id, severity, element_id, type, message));
        events_.Emit(StatusMessageEvent{message});
    };

    const auto report_as_failed_review = [&](const std::string& suffix, const std::string& status_message) {
        const std::string profile_id =
            preparation.review_profile_id.empty() ? requested_review_profile_id : preparation.review_profile_id;
        ReplaceAiReviewWithSingleItem(review_controller_,
                                      selected_element_id,
                                      ReviewCommentPrefix(selected_element_id, profile_id),
                                      suffix,
                                      "AI review setup failed",
                                      message,
                                      core::ProblemSeverity::Error);
        EmitReviewVisualEvent(events_,
                              ElementReviewVisualEventKind::AiFailed,
                              selected_element_id,
                              profile_id,
                              preparation.review_profile_name,
                              status_message);
        review_controller_.SetAiReviewOutcome(selected_element_id,
                                              false,
                                              true,
                                              profile_id,
                                              preparation.review_profile_name,
                                              status_message,
                                              NowUtcString());
        events_.Emit(StatusMessageEvent{status_message});
    };

    switch (preparation.failure) {
    case review::SccgReviewPreparationFailure::None:
        return;
    case review::SccgReviewPreparationFailure::NoSelection:
        report_as_problem("no-selection", core::ProblemSeverity::Info, {}, "AI Review");
        return;
    case review::SccgReviewPreparationFailure::NoCase:
        report_as_problem("no-loaded-case", core::ProblemSeverity::Error, selected_element_id, "AI Review");
        return;
    case review::SccgReviewPreparationFailure::ElementNotFound:
        report_as_problem("missing-element", core::ProblemSeverity::Error, selected_element_id, "AI Review");
        return;
    case review::SccgReviewPreparationFailure::UnsupportedElementType:
        report_as_problem(
            "unsupported-type", core::ProblemSeverity::Info, selected_element_id, preparation.element_type);
        return;
    case review::SccgReviewPreparationFailure::ProfileIncompatible:
        report_as_problem(
            "profile-incompatible", core::ProblemSeverity::Info, selected_element_id, preparation.element_type);
        return;
    case review::SccgReviewPreparationFailure::PayloadFailed:
        report_as_failed_review("payload-error", "AI review payload could not be created.");
        return;
    case review::SccgReviewPreparationFailure::CatalogUnavailable:
        report_as_failed_review("guidelines-missing", "SCCG guidelines could not be loaded for AI review.");
        return;
    case review::SccgReviewPreparationFailure::ProfileSelectionFailed:
        report_as_failed_review("guidelines-empty", message);
        return;
    case review::SccgReviewPreparationFailure::DataPackagesFailed:
        report_as_failed_review("data-package-error", "AI review data packages could not be collected.");
        return;
    }
}

void AiReviewController::MarkPendingRequestIncludesWorkingDraft() {
    if (pending_review_.prompt.empty())
        return;
    pending_review_.debugText =
        "NOTICE: This AI review includes unaccepted working-draft content.\n\n" + pending_review_.debugText;
}

void AiReviewController::StartPendingRequest() {
    if (pending_review_.prompt.empty())
        return;
    if (review_task_ && review_task_->IsRunning())
        return;

    ai::AiRequest request;
    request.systemInstruction = pending_review_.systemInstruction;
    request.userPrompt = pending_review_.prompt;
    pending_review_run_id_ = GenerateAiReviewRunId();

    std::shared_ptr<ai::AiService> service = ai_service_;
    review_task_ = task_runner_.RunGenerate([service, request]() {
        if (service)
            return service->Generate(request);
        ai::AiResponse response;
        response.success = false;
        response.errorCode = ai::AiErrorCode::Unknown;
        response.errorMessage = "AI service is unavailable.";
        return response;
    });
    EmitReviewVisualEvent(events_,
                          ElementReviewVisualEventKind::AiStarted,
                          pending_review_element_id_,
                          pending_review_profile_id_,
                          pending_review_profile_name_,
                          "AI review in progress.",
                          pending_review_scope_element_ids_);
    review_controller_.SetAiReviewOutcome(pending_review_element_id_,
                                          false,
                                          false,
                                          pending_review_profile_id_,
                                          pending_review_profile_name_,
                                          "AI review in progress.",
                                          NowUtcString());
    events_.Emit(StatusMessageEvent{"AI review request sent."});
}

void AiReviewController::PollTask() {
    if (!review_task_)
        return;

    ai::AiTaskSnapshot snapshot = review_task_->Snapshot();
    if (snapshot.state == ai::AiTaskState::Running)
        return;

    review_task_.reset();
    ai::AiResponse response = std::move(snapshot.response);
    if (!response.success) {
        std::string message = response.errorMessage.empty() ? ai::ToString(response.errorCode) : response.errorMessage;
        last_raw_response_ = response.rawJson;
        ReplaceAiReviewWithSingleItem(review_controller_,
                                      pending_review_element_id_,
                                      ReviewCommentPrefix(pending_review_element_id_, pending_review_profile_id_),
                                      "request-error",
                                      "AI review request failed",
                                      "AI review request failed: " + message,
                                      core::ProblemSeverity::Error);
        EmitReviewVisualEvent(events_,
                              ElementReviewVisualEventKind::AiFailed,
                              pending_review_element_id_,
                              pending_review_profile_id_,
                              pending_review_profile_name_,
                              "AI review request failed.");
        review_controller_.SetAiReviewOutcome(pending_review_element_id_,
                                              false,
                                              true,
                                              pending_review_profile_id_,
                                              pending_review_profile_name_,
                                              "AI review request failed.",
                                              NowUtcString());
        events_.Emit(StatusMessageEvent{"AI review request failed."});
        return;
    }

    last_raw_response_ = response.text.empty() ? response.rawJson : response.text;
    review::AiReviewParseResult parse_result =
        review::ParseAiReviewResponse(last_raw_response_, pending_review_element_id_, pending_guideline_ids_);
    if (!parse_result.errorMessage.empty()) {
        last_parse_error_ = parse_result.errorMessage;
        std::string message = "AI response could not be parsed as the expected JSON format.";
        if (!parse_result.errorMessage.empty())
            message += " " + parse_result.errorMessage;
        if (!last_raw_response_.empty()) {
            message += " Raw response: " + TruncateForProblemMessage(last_raw_response_);
        }
        ReplaceAiReviewWithSingleItem(review_controller_,
                                      pending_review_element_id_,
                                      ReviewCommentPrefix(pending_review_element_id_, pending_review_profile_id_),
                                      "parse-error",
                                      "AI review response could not be parsed",
                                      message,
                                      core::ProblemSeverity::Error);
        EmitReviewVisualEvent(events_,
                              ElementReviewVisualEventKind::AiFailed,
                              pending_review_element_id_,
                              pending_review_profile_id_,
                              pending_review_profile_name_,
                              "AI review response could not be parsed.");
        review_controller_.SetAiReviewOutcome(pending_review_element_id_,
                                              false,
                                              true,
                                              pending_review_profile_id_,
                                              pending_review_profile_name_,
                                              "AI review response could not be parsed.",
                                              NowUtcString());
        events_.Emit(StatusMessageEvent{"AI review response could not be parsed."});
        return;
    }

    if (!parse_result.reviewedElementId.empty() && parse_result.reviewedElementId != pending_review_element_id_) {
        last_parse_error_ = "AI response reviewed_element_id did not match the requested element.";
        std::string message = last_parse_error_;
        if (!last_raw_response_.empty()) {
            message += " Raw response: " + TruncateForProblemMessage(last_raw_response_);
        }
        ReplaceAiReviewWithSingleItem(review_controller_,
                                      pending_review_element_id_,
                                      ReviewCommentPrefix(pending_review_element_id_, pending_review_profile_id_),
                                      "validation-error",
                                      "AI review response could not be validated",
                                      message,
                                      core::ProblemSeverity::Error);
        EmitReviewVisualEvent(events_,
                              ElementReviewVisualEventKind::AiFailed,
                              pending_review_element_id_,
                              pending_review_profile_id_,
                              pending_review_profile_name_,
                              "AI review response could not be validated.");
        review_controller_.SetAiReviewOutcome(pending_review_element_id_,
                                              false,
                                              true,
                                              pending_review_profile_id_,
                                              pending_review_profile_name_,
                                              "AI review response could not be validated.",
                                              NowUtcString());
        events_.Emit(StatusMessageEvent{"AI review response could not be validated."});
        return;
    }

    if (parse_result.reviewedElementType.empty())
        parse_result.reviewedElementType = pending_review_element_type_;
    for (core::ProblemItem& problem : parse_result.problems) {
        if (problem.type.empty())
            problem.type = parse_result.reviewedElementType;
    }

    problems_manager_.ClearProblemsForElementAndSource(pending_review_element_id_, core::ProblemSource::AIReview);
    const std::string review_prefix = ReviewCommentPrefix(pending_review_element_id_, pending_review_profile_id_);
    review_controller_.ClearAiReviewItemsForElementAndPrefix(pending_review_element_id_, review_prefix);

    const std::string timestamp = NowUtcString();
    size_t review_comment_count = 0;
    std::vector<AiReviewProposalSuggestion> proposal_suggestions;
    for (size_t problem_index = 0; problem_index < parse_result.problems.size(); ++problem_index) {
        const core::ProblemItem& problem = parse_result.problems[problem_index];
        const std::string review_item_id = review_prefix + std::to_string(++review_comment_count);
        review_controller_.AddOrUpdateItem(MakeAiReviewItem(review_item_id,
                                                            pending_review_element_id_,
                                                            ReviewTitleForProblem(problem),
                                                            problem.message,
                                                            problem.severity,
                                                            timestamp,
                                                            problem.guideline_id));
        const std::string suggested_text = problem_index < parse_result.suggestedElementTexts.size()
                                               ? parse_result.suggestedElementTexts[problem_index]
                                               : std::string{};
        const std::vector<core::reviews::PatchOperation> operations =
            problem_index < parse_result.proposedOperations.size() ? parse_result.proposedOperations[problem_index]
                                                                   : std::vector<core::reviews::PatchOperation>{};
        if (!suggested_text.empty() || !operations.empty()) {
            proposal_suggestions.push_back(
                AiReviewProposalSuggestion{review_item_id, pending_review_element_id_, suggested_text, operations});
        }
    }

    // A repair the parser could not read must not look like a finding that had
    // none. The reviewer is deciding whether to trust this review; an operation
    // it asked for and we dropped is part of that picture.
    for (const std::string& rejected : parse_result.rejectedOperationReasons) {
        events_.Emit(StatusMessageEvent{"AI review proposed a change that could not be read: " + rejected});
    }

    EmitReviewVisualEvent(events_,
                          parse_result.problems.empty() ? ElementReviewVisualEventKind::AiNoFindings
                                                        : ElementReviewVisualEventKind::AiFindings,
                          pending_review_element_id_,
                          pending_review_profile_id_,
                          pending_review_profile_name_,
                          parse_result.problems.empty() ? "AI review completed with no findings."
                                                        : "AI review completed with findings.");
    review_controller_.SetAiReviewOutcome(pending_review_element_id_,
                                          parse_result.problems.empty(),
                                          false,
                                          pending_review_profile_id_,
                                          pending_review_profile_name_,
                                          parse_result.problems.empty() ? "AI review completed with no findings."
                                                                        : "AI review completed with findings.",
                                          NowUtcString());

    events_.Emit(StatusMessageEvent{parse_result.problems.empty()
                                        ? "AI review completed with no findings."
                                        : "AI review completed with " + std::to_string(parse_result.problems.size()) +
                                              " finding(s) added as review comment(s)."});
    if (!proposal_suggestions.empty()) {
        AiReviewProposalSuggestionsEvent event;
        event.suggestions = std::move(proposal_suggestions);
        event.review_profile_id = pending_review_profile_id_;
        event.review_profile_name = pending_review_profile_name_;
        event.review_run_id = pending_review_run_id_;
        event.reviewed_scope_hash = pending_review_scope_hash_;
        event.reviewed_element_ids = pending_review_scope_element_id_list_;
        events_.Emit(std::move(event));
    }
}

void AiReviewController::CancelPendingRequest() {
    show_debug_modal_ = false;
    pending_review_ = {};
    pending_review_element_id_.clear();
    pending_review_element_type_.clear();
    pending_review_profile_id_.clear();
    pending_review_profile_name_.clear();
    pending_review_scope_element_ids_.clear();
    pending_guideline_ids_.clear();
    pending_review_run_id_.clear();
    pending_review_scope_hash_.clear();
    pending_review_scope_element_id_list_.clear();
}

bool AiReviewController::IsReviewRunning() const {
    return review_task_ && review_task_->IsRunning() && !pending_review_.prompt.empty();
}

bool AiReviewController::WaitForCompletion(std::chrono::milliseconds timeout) const {
    if (!review_task_)
        return true;
    return review_task_->WaitUntilComplete(timeout);
}

bool AiReviewController::ShouldShowDebugModal() const {
    return show_debug_modal_;
}

void AiReviewController::SetDebugModalVisible(bool visible) {
    show_debug_modal_ = visible;
}

bool AiReviewController::HasPendingRequest() const {
    return !pending_review_.prompt.empty();
}

const std::string& AiReviewController::PendingPrompt() const {
    return pending_review_.prompt;
}

void AiReviewController::SetPendingPrompt(std::string prompt) {
    pending_review_.prompt = std::move(prompt);
}

const std::string& AiReviewController::PendingDebugText() const {
    return pending_review_.debugText;
}

const std::string& AiReviewController::LastRawResponse() const {
    return last_raw_response_;
}

const std::string& AiReviewController::LastParseError() const {
    return last_parse_error_;
}

} // namespace app::controllers
