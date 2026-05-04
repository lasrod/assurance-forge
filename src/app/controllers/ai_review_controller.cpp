#include "app/controllers/ai_review_controller.h"

#include "app/guideline_catalog.h"
#include "parser/guidelines_parser.h"

#include <string>
#include <vector>

namespace app::controllers {
namespace {

constexpr const char* kDefaultClaimReviewProfileId = "claim_wording_review";

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

std::string TruncateForProblemMessage(const std::string& value, size_t limit = 400) {
    if (value.size() <= limit) return value;
    return value.substr(0, limit) + "...";
}

std::vector<std::string> GuidelineIds(const std::vector<const parser::Guideline*>& guidelines) {
    std::vector<std::string> ids;
    for (const parser::Guideline* guideline : guidelines) {
        if (guideline && !guideline->id.empty()) ids.push_back(guideline->id);
    }
    return ids;
}

}  // namespace

AiReviewGuidelineSelection SelectClaimReviewGuidelines(const GuidelineCatalog& guideline_catalog) {
    AiReviewGuidelineSelection selection;
    selection.review_profile = guideline_catalog.document.FindReviewProfileById(kDefaultClaimReviewProfileId);
    selection.guidelines = selection.review_profile
        ? guideline_catalog.document.FindGuidelinesByReviewProfile(kDefaultClaimReviewProfileId)
        : guideline_catalog.document.FindGuidelinesByCategory("CL");

    if (selection.review_profile && selection.guidelines.empty()) {
        selection.error_message =
            "SCCG catalog contains review profile 'claim_wording_review' but it references no valid guidelines.";
    } else if (selection.guidelines.empty()) {
        selection.error_message = "No SCCG guidelines were found for AI review.";
    }
    return selection;
}

AiReviewController::AiReviewController(AppEvents& events,
                                       core::ProblemsManager& problems_manager,
                                       ai::AiTaskRunner& task_runner,
                                       std::shared_ptr<ai::AiService> ai_service)
    : events_(events),
      problems_manager_(problems_manager),
      task_runner_(task_runner),
      ai_service_(std::move(ai_service)) {}

void AiReviewController::BeginReviewForSelection(const parser::AssuranceCase* assurance_case,
                                                 const core::AssuranceTree& current_tree,
                                                 const std::string& selected_element_id) {
    if (review_task_ && review_task_->IsRunning()) {
        events_.Emit(StatusMessageEvent{"AI review is already running."});
        return;
    }

    if (selected_element_id.empty()) {
        problems_manager_.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:no-selection",
            core::ProblemSeverity::Info,
            {},
            "AI Review",
            "No GSN element is selected for AI review."));
        events_.Emit(StatusMessageEvent{"No GSN element is selected for AI review."});
        return;
    }

    if (!assurance_case) {
        problems_manager_.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:" + selected_element_id + ":no-loaded-case",
            core::ProblemSeverity::Error,
            selected_element_id,
            "AI Review",
            "No assurance case is loaded for AI review."));
        events_.Emit(StatusMessageEvent{"No assurance case is loaded for AI review."});
        return;
    }

    const parser::SacmElement* selected_element = ai::FindSacmElement(*assurance_case, selected_element_id);
    if (!selected_element) {
        problems_manager_.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:" + selected_element_id + ":missing-element",
            core::ProblemSeverity::Error,
            selected_element_id,
            "AI Review",
            "Selected element was not found."));
        events_.Emit(StatusMessageEvent{"Selected element was not found."});
        return;
    }

    if (!ai::IsSupportedAiReviewElement(*selected_element)) {
        problems_manager_.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:" + selected_element_id + ":unsupported-type",
            core::ProblemSeverity::Info,
            selected_element_id,
            ai::AiReviewElementType(*selected_element),
            "AI Review currently supports GSN Goal / SACM Claim elements only."));
        events_.Emit(StatusMessageEvent{"AI Review currently supports GSN Goal / SACM Claim elements only."});
        return;
    }

    ai::AiReviewPayload payload;
    std::string payload_error;
    if (!ai::BuildAiReviewPayload(*assurance_case, current_tree, selected_element_id, payload, payload_error)) {
        problems_manager_.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:" + selected_element_id + ":payload-error",
            core::ProblemSeverity::Error,
            selected_element_id,
            ai::AiReviewElementType(*selected_element),
            payload_error.empty() ? "AI review payload could not be created." : payload_error));
        events_.Emit(StatusMessageEvent{"AI review payload could not be created."});
        return;
    }

    GuidelineCatalog guideline_catalog;
    std::string guideline_error;
    if (!LoadGuidelineCatalog(guideline_catalog, guideline_error)) {
        problems_manager_.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:" + selected_element_id + ":guidelines-missing",
            core::ProblemSeverity::Error,
            selected_element_id,
            payload.selected.type,
            "SCCG guidelines could not be loaded for AI review: " + guideline_error));
        events_.Emit(StatusMessageEvent{"SCCG guidelines could not be loaded for AI review."});
        return;
    }

    AiReviewGuidelineSelection guideline_selection = SelectClaimReviewGuidelines(guideline_catalog);

    if (!guideline_selection.error_message.empty()) {
        problems_manager_.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:" + selected_element_id + ":guidelines-empty",
            core::ProblemSeverity::Error,
            selected_element_id,
            payload.selected.type,
            guideline_selection.error_message));
        events_.Emit(StatusMessageEvent{guideline_selection.error_message});
        return;
    }

    pending_review_ = ai::BuildAiReviewRequestArtifacts(
        payload,
        guideline_selection.guidelines,
        guideline_selection.review_profile);
    pending_review_element_id_ = payload.selected.id;
    pending_review_element_type_ = payload.selected.type;
    pending_guideline_ids_ = GuidelineIds(guideline_selection.guidelines);
    last_raw_response_.clear();
    last_parse_error_.clear();
    show_debug_modal_ = true;
    events_.Emit(StatusMessageEvent{"AI review request is ready for inspection."});
}

void AiReviewController::StartPendingRequest() {
    if (pending_review_.prompt.empty()) return;
    if (review_task_ && review_task_->IsRunning()) return;

    ai::AiRequest request;
    request.systemInstruction = pending_review_.systemInstruction;
    request.userPrompt = pending_review_.prompt;

    std::shared_ptr<ai::AiService> service = ai_service_;
    review_task_ = task_runner_.RunGenerate([service, request]() {
        if (service) return service->Generate(request);
        ai::AiResponse response;
        response.success = false;
        response.errorCode = ai::AiErrorCode::Unknown;
        response.errorMessage = "AI service is unavailable.";
        return response;
    });
    events_.Emit(StatusMessageEvent{"AI review request sent."});
}

void AiReviewController::PollTask() {
    if (!review_task_) return;

    ai::AiTaskSnapshot snapshot = review_task_->Snapshot();
    if (snapshot.state == ai::AiTaskState::Running) return;

    review_task_.reset();
    ai::AiResponse response = std::move(snapshot.response);
    if (!response.success) {
        std::string message = response.errorMessage.empty() ? ai::ToString(response.errorCode) : response.errorMessage;
        last_raw_response_ = response.rawJson;
        problems_manager_.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:" + pending_review_element_id_ + ":request-error",
            core::ProblemSeverity::Error,
            pending_review_element_id_,
            pending_review_element_type_,
            "AI review request failed: " + message));
        events_.Emit(StatusMessageEvent{"AI review request failed."});
        return;
    }

    last_raw_response_ = response.text.empty() ? response.rawJson : response.text;
    ai::AiReviewParseResult parse_result =
        ai::ParseAiReviewResponse(response.text, pending_review_element_id_, pending_guideline_ids_);
    if (!parse_result.success) {
        last_parse_error_ = parse_result.errorMessage;
        std::string message = "AI response could not be parsed as the expected JSON format.";
        if (!parse_result.errorMessage.empty()) message += " " + parse_result.errorMessage;
        if (!last_raw_response_.empty()) {
            message += " Raw response: " + TruncateForProblemMessage(last_raw_response_);
        }
        problems_manager_.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:" + pending_review_element_id_ + ":parse-error",
            core::ProblemSeverity::Error,
            pending_review_element_id_,
            pending_review_element_type_,
            message));
        events_.Emit(StatusMessageEvent{"AI review response could not be parsed."});
        return;
    }

    if (!parse_result.reviewedElementId.empty() &&
        parse_result.reviewedElementId != pending_review_element_id_) {
        last_parse_error_ = "AI response reviewed_element_id did not match the requested element.";
        std::string message = last_parse_error_;
        if (!last_raw_response_.empty()) {
            message += " Raw response: " + TruncateForProblemMessage(last_raw_response_);
        }
        problems_manager_.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:" + pending_review_element_id_ + ":parse-error",
            core::ProblemSeverity::Error,
            pending_review_element_id_,
            pending_review_element_type_,
            message));
        events_.Emit(StatusMessageEvent{"AI review response could not be validated."});
        return;
    }

    if (parse_result.reviewedElementType.empty()) parse_result.reviewedElementType = pending_review_element_type_;
    for (core::ProblemItem& problem : parse_result.problems) {
        if (problem.type.empty()) problem.type = parse_result.reviewedElementType;
    }

    problems_manager_.ClearProblemsForElementAndSource(pending_review_element_id_, core::ProblemSource::AIReview);
    for (const core::ProblemItem& problem : parse_result.problems) {
        problems_manager_.AddOrUpdateProblem(problem);
    }

    events_.Emit(StatusMessageEvent{parse_result.problems.empty()
        ? "AI review completed with no findings."
        : "AI review completed with " + std::to_string(parse_result.problems.size()) + " finding(s)."});
}

void AiReviewController::CancelPendingRequest() {
    show_debug_modal_ = false;
    pending_review_ = {};
    pending_review_element_id_.clear();
    pending_review_element_type_.clear();
    pending_guideline_ids_.clear();
}

bool AiReviewController::IsReviewRunning() const {
    return review_task_ && review_task_->IsRunning() && !pending_review_.prompt.empty();
}

bool AiReviewController::ShouldShowDebugModal() const {
    return show_debug_modal_;
}

void AiReviewController::SetDebugModalVisible(bool visible) {
    show_debug_modal_ = visible;
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

}  // namespace app::controllers