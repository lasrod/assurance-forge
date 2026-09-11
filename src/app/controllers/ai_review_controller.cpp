#include "app/controllers/ai_review_controller.h"

#include "core/reviews/review_proposal.h"
#include "core/reviews/review_text_utils.h"
#include "core/time_utils.h"
#include "parser/guidelines_parser.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
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

bool AnyTaskRunning(const std::vector<std::shared_ptr<ai::AiTaskHandle>>& tasks) {
    return std::any_of(tasks.begin(), tasks.end(), [](const std::shared_ptr<ai::AiTaskHandle>& task) {
        return task && task->IsRunning();
    });
}

bool SegmentsAreThePrompt(const review::AiReviewRequestArtifacts& request) {
    if (request.promptSegments.empty())
        return false;
    std::string joined;
    for (const std::string& segment : request.promptSegments)
        joined += segment;
    return joined == request.prompt;
}

ai::AiResponse GenerateWith(const std::shared_ptr<ai::AiService>& service, const ai::AiRequest& request) {
    if (service)
        return service->Generate(request);
    ai::AiResponse response;
    response.success = false;
    response.errorCode = ai::AiErrorCode::Unknown;
    response.errorMessage = "AI service is unavailable.";
    return response;
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
    if (AnyTaskRunning(review_tasks_)) {
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
    pending_passes_ = preparation.passes;
    RebuildCombinedPrompt();
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
    if (pending_review_.prompt.empty() || pending_passes_.empty())
        return;
    if (AnyTaskRunning(review_tasks_))
        return;

    pending_review_run_id_ = GenerateAiReviewRunId();
    review_tasks_.clear();
    // Concurrent, not sequential: the passes are independent requests over the
    // same data, and four smaller requests in parallel finish sooner than one
    // large one -- which is part of what makes splitting affordable.
    std::shared_ptr<ai::AiService> service = ai_service_;
    for (const review::SccgReviewPassRequest& pass : pending_passes_) {
        ai::AiRequest request;
        request.systemInstruction = pass.request.systemInstruction;
        request.userPrompt = pass.request.prompt;
        // Cache the segments every review of this profile and pass shares --
        // the instructions, the profile and its rules -- and not the element's
        // own data: a cache write costs more than an uncached read, and a review
        // of one element is rarely repeated before the cache expires.
        //
        // Only while the segments are still the prompt: an edit in the debug
        // panel rewrites the prompt and not its segments, and sending the
        // segments then would send what the user replaced. An edited prompt is
        // sent whole, uncached.
        if (SegmentsAreThePrompt(pass.request)) {
            for (std::size_t index = 0; index < pass.request.promptSegments.size(); ++index) {
                const bool shared = index + 1 < pass.request.promptSegments.size();
                request.promptSegments.push_back({pass.request.promptSegments[index], shared});
            }
            request.promptCacheKey = pass.request.promptCacheKey;
        }
        review_tasks_.push_back(
            task_runner_.RunGenerate([service, request]() { return GenerateWith(service, request); }));
    }
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
    events_.Emit(StatusMessageEvent{
        pending_passes_.size() > 1 ? "AI review sent as " + std::to_string(pending_passes_.size()) + " review passes."
                                   : std::string("AI review request sent.")});
}

void AiReviewController::PollTask() {
    if (review_tasks_.empty() || AnyTaskRunning(review_tasks_))
        return;

    std::vector<ai::AiResponse> responses;
    responses.reserve(review_tasks_.size());
    for (const std::shared_ptr<ai::AiTaskHandle>& task : review_tasks_)
        responses.push_back(task ? task->Snapshot().response : ai::AiResponse{});
    review_tasks_.clear();

    if (responses.size() == 1 && pending_passes_.size() <= 1)
        CompleteSingleRequest(responses.front());
    else
        CompletePassRequests(std::move(responses));
}

void AiReviewController::CompleteSingleRequest(const ai::AiResponse& response) {
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

    ApplyReviewFindings(std::move(parse_result), {});
}

void AiReviewController::ApplyReviewFindings(review::AiReviewParseResult parse_result,
                                             const std::string& incomplete_reason) {
    const bool incomplete = !incomplete_reason.empty();
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

    if (incomplete) {
        // Recorded as its own review item so the gap is visible next to the
        // findings, not only in a status line that scrolls away.
        const std::string review_item_id = review_prefix + "incomplete";
        review_controller_.AddOrUpdateItem(MakeAiReviewItem(review_item_id,
                                                            pending_review_element_id_,
                                                            "AI review incomplete",
                                                            incomplete_reason,
                                                            core::ProblemSeverity::Error,
                                                            timestamp));
        EmitReviewVisualEvent(events_,
                              ElementReviewVisualEventKind::AiFailed,
                              pending_review_element_id_,
                              pending_review_profile_id_,
                              pending_review_profile_name_,
                              "AI review incomplete.");
        review_controller_.SetAiReviewOutcome(pending_review_element_id_,
                                              false,
                                              true,
                                              pending_review_profile_id_,
                                              pending_review_profile_name_,
                                              "AI review incomplete.",
                                              NowUtcString());
        events_.Emit(StatusMessageEvent{"AI review incomplete: " + incomplete_reason});
    } else {
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

        events_.Emit(StatusMessageEvent{
            parse_result.problems.empty() ? "AI review completed with no findings."
                                          : "AI review completed with " + std::to_string(parse_result.problems.size()) +
                                                " finding(s) added as review comment(s)."});
    }
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

void AiReviewController::CompletePassRequests(std::vector<ai::AiResponse> responses) {
    std::vector<review::ReviewPassOutcome> outcomes;
    std::string combined_raw;
    for (std::size_t index = 0; index < pending_passes_.size(); ++index) {
        const review::SccgReviewPassRequest& pass = pending_passes_[index];
        review::ReviewPassOutcome outcome;
        outcome.pass_id = pass.pass_id;
        if (index >= responses.size()) {
            outcome.error = "no response was received.";
        } else if (!responses[index].success) {
            const ai::AiResponse& response = responses[index];
            outcome.error =
                "the request failed: " +
                (response.errorMessage.empty() ? std::string(ai::ToString(response.errorCode)) : response.errorMessage);
            outcome.raw_response = response.rawJson;
        } else {
            const ai::AiResponse& response = responses[index];
            outcome.raw_response = response.text.empty() ? response.rawJson : response.text;
            // Each pass may cite only its own guidelines; one citing another
            // pass's guideline is placed by the merge, not by the parser.
            outcome.result =
                review::ParseAiReviewResponse(outcome.raw_response, pending_review_element_id_, pass.guideline_ids);
        }
        combined_raw += (combined_raw.empty() ? "" : "\n\n") + std::string("===== ") + pass.pass_id + " =====\n" +
                        outcome.raw_response;
        outcomes.push_back(std::move(outcome));
    }
    last_raw_response_ = combined_raw;

    review::MergedReviewPasses merged =
        review::MergeReviewPasses(outcomes, pending_passes_, pending_review_element_id_);
    last_parse_error_.clear();
    for (const std::string& error : merged.pass_errors)
        last_parse_error_ += (last_parse_error_.empty() ? "" : " ") + error;
    for (const std::string& discarded : merged.discarded_findings)
        events_.Emit(StatusMessageEvent{"AI review: " + discarded});

    if (!merged.any_succeeded()) {
        // Reported the way a single request failing the same way would be: a
        // review whose every pass came back unparseable is a parse failure, and
        // calling it a request failure would send the reader to check a
        // connection that worked.
        const bool every_pass_unparseable =
            std::all_of(outcomes.begin(), outcomes.end(), [](const review::ReviewPassOutcome& outcome) {
                return outcome.error.empty() && !outcome.result.errorMessage.empty();
            });
        const std::string suffix = every_pass_unparseable ? "parse-error" : "request-error";
        const std::string title =
            every_pass_unparseable ? "AI review response could not be parsed" : "AI review request failed";
        const std::string message =
            every_pass_unparseable
                ? "AI response could not be parsed as the expected JSON format. " + merged.merged.errorMessage
                : "AI review request failed: " + merged.merged.errorMessage;
        const std::string outcome_text =
            every_pass_unparseable ? "AI review response could not be parsed." : "AI review request failed.";
        ReplaceAiReviewWithSingleItem(review_controller_,
                                      pending_review_element_id_,
                                      ReviewCommentPrefix(pending_review_element_id_, pending_review_profile_id_),
                                      suffix,
                                      title,
                                      message,
                                      core::ProblemSeverity::Error);
        EmitReviewVisualEvent(events_,
                              ElementReviewVisualEventKind::AiFailed,
                              pending_review_element_id_,
                              pending_review_profile_id_,
                              pending_review_profile_name_,
                              outcome_text);
        review_controller_.SetAiReviewOutcome(pending_review_element_id_,
                                              false,
                                              true,
                                              pending_review_profile_id_,
                                              pending_review_profile_name_,
                                              outcome_text,
                                              NowUtcString());
        events_.Emit(StatusMessageEvent{outcome_text});
        return;
    }

    std::string incomplete_reason;
    if (!merged.complete()) {
        incomplete_reason = std::to_string(merged.failed_pass_ids.size()) + " of " +
                            std::to_string(merged.passes_total) +
                            " review passes did not complete, so guidelines in those passes were not reviewed.";
        for (const std::string& error : merged.pass_errors)
            incomplete_reason += " " + error;
    }
    ApplyReviewFindings(std::move(merged.merged), incomplete_reason);
}

void AiReviewController::CancelPendingRequest() {
    show_debug_modal_ = false;
    pending_review_ = {};
    pending_passes_.clear();
    pending_combined_prompt_.clear();
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
    return AnyTaskRunning(review_tasks_) && !pending_review_.prompt.empty();
}

bool AiReviewController::WaitForCompletion(std::chrono::milliseconds timeout) const {
    // One deadline for all passes, not one each: a caller waiting "up to a
    // second" must not wait four.
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (const std::shared_ptr<ai::AiTaskHandle>& task : review_tasks_) {
        if (!task)
            continue;
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (!task->WaitUntilComplete(std::max(remaining, std::chrono::milliseconds(0))))
            return false;
    }
    return true;
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
    return pending_passes_.size() > 1 ? pending_combined_prompt_ : pending_review_.prompt;
}

// An edit in the AI Debug panel. For a multi-pass review the panel shows every
// pass under a separator; an edit that keeps the separators goes back to the
// passes it was made in. One that removes them cannot be attributed to a pass,
// so it is sent the way it now reads -- as one request over the whole profile
// -- and the user is told, rather than having part of their edit dropped.
void AiReviewController::SetPendingPrompt(std::string prompt) {
    if (pending_passes_.size() <= 1) {
        pending_review_.prompt = prompt;
        if (!pending_passes_.empty())
            pending_passes_.front().request.prompt = std::move(prompt);
        return;
    }
    if (review::SplitPassPrompts(prompt, pending_passes_)) {
        pending_combined_prompt_ = std::move(prompt);
        return;
    }
    review::SccgReviewPassRequest whole;
    whole.guideline_ids = pending_guideline_ids_;
    whole.request = pending_review_;
    whole.request.prompt = prompt;
    pending_review_.prompt = std::move(prompt);
    pending_passes_ = {std::move(whole)};
    pending_combined_prompt_.clear();
    events_.Emit(StatusMessageEvent{
        "The edited prompt no longer separates into its review passes; it will be sent as one request."});
}

void AiReviewController::RebuildCombinedPrompt() {
    pending_combined_prompt_ = pending_passes_.size() > 1 ? review::CombinePassPrompts(pending_passes_) : std::string{};
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
