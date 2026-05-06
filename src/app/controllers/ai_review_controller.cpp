#include "app/controllers/ai_review_controller.h"

#include "app/guideline_catalog.h"
#include "parser/guidelines_parser.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
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
    if (value.size() <= limit)
        return value;
    return value.substr(0, limit) + "...";
}

std::string NowUtcString() {
    using clock = std::chrono::system_clock;
    std::time_t now = clock::to_time_t(clock::now());
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
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

std::vector<std::string> GuidelineIds(const std::vector<const parser::Guideline*>& guidelines) {
    std::vector<std::string> ids;
    for (const parser::Guideline* guideline : guidelines) {
        if (guideline && !guideline->id.empty())
            ids.push_back(guideline->id);
    }
    return ids;
}

const core::TreeNode* FindTreeNode(const core::AssuranceTree& tree, const std::string& element_id) {
    for (const auto& node : tree.nodes) {
        if (node && node->id == element_id)
            return node.get();
    }
    return nullptr;
}

} // namespace

AiReviewGuidelineSelection SelectReviewProfileGuidelines(const GuidelineCatalog& guideline_catalog,
                                                         const std::string& review_profile_id) {
    AiReviewGuidelineSelection selection;
    selection.review_profile = guideline_catalog.document.FindReviewProfileById(review_profile_id);
    if (!selection.review_profile) {
        selection.error_message = "SCCG review profile was not found: " + review_profile_id;
        return selection;
    }

    selection.guidelines = guideline_catalog.document.FindGuidelinesByReviewProfile(review_profile_id);
    if (selection.guidelines.empty()) {
        selection.error_message = "SCCG review profile '" + review_profile_id + "' references no valid guidelines.";
    }
    return selection;
}

AiReviewGuidelineSelection SelectClaimReviewGuidelines(const GuidelineCatalog& guideline_catalog) {
    AiReviewGuidelineSelection selection;
    selection.review_profile = guideline_catalog.document.FindReviewProfileById(kDefaultClaimReviewProfileId);
    selection.guidelines = selection.review_profile
                               ? guideline_catalog.document.FindGuidelinesByReviewProfile(kDefaultClaimReviewProfileId)
                               : guideline_catalog.document.FindGuidelinesByCategory("CL");

    if (selection.review_profile && selection.guidelines.empty()) {
        selection.error_message = std::string("SCCG catalog contains review profile '") + kDefaultClaimReviewProfileId +
                                  "' but it references no valid guidelines.";
    } else if (selection.guidelines.empty()) {
        selection.error_message = "No SCCG guidelines were found for AI review.";
    }
    return selection;
}

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
    BeginReviewForSelection(assurance_case, current_tree, selected_element_id, kDefaultClaimReviewProfileId);
}

void AiReviewController::BeginReviewForSelection(const parser::AssuranceCase* assurance_case,
                                                 const core::AssuranceTree& current_tree,
                                                 const std::string& selected_element_id,
                                                 const std::string& review_profile_id) {
    if (review_task_ && review_task_->IsRunning()) {
        events_.Emit(StatusMessageEvent{"AI review is already running."});
        return;
    }

    if (selected_element_id.empty()) {
        problems_manager_.AddOrUpdateProblem(MakeAiReviewProblem("ai-review:no-selection",
                                                                 core::ProblemSeverity::Info,
                                                                 {},
                                                                 "AI Review",
                                                                 "No GSN element is selected for AI review."));
        events_.Emit(StatusMessageEvent{"No GSN element is selected for AI review."});
        return;
    }

    if (!assurance_case) {
        problems_manager_.AddOrUpdateProblem(MakeAiReviewProblem("ai-review:" + selected_element_id + ":no-loaded-case",
                                                                 core::ProblemSeverity::Error,
                                                                 selected_element_id,
                                                                 "AI Review",
                                                                 "No assurance case is loaded for AI review."));
        events_.Emit(StatusMessageEvent{"No assurance case is loaded for AI review."});
        return;
    }

    const parser::SacmElement* selected_element = ai::FindSacmElement(*assurance_case, selected_element_id);
    if (!selected_element) {
        problems_manager_.AddOrUpdateProblem(
            MakeAiReviewProblem("ai-review:" + selected_element_id + ":missing-element",
                                core::ProblemSeverity::Error,
                                selected_element_id,
                                "AI Review",
                                "Selected element was not found."));
        events_.Emit(StatusMessageEvent{"Selected element was not found."});
        return;
    }

    if (!ai::IsSupportedAiReviewElement(*selected_element)) {
        problems_manager_.AddOrUpdateProblem(
            MakeAiReviewProblem("ai-review:" + selected_element_id + ":unsupported-type",
                                core::ProblemSeverity::Info,
                                selected_element_id,
                                ai::AiReviewElementType(*selected_element),
                                "AI Review does not support the selected element type."));
        events_.Emit(StatusMessageEvent{"AI Review does not support the selected element type."});
        return;
    }

    ai::AiReviewPayload payload;
    std::string payload_error;
    if (!ai::BuildAiReviewPayload(*assurance_case, current_tree, selected_element_id, payload, payload_error)) {
        problems_manager_.AddOrUpdateProblem(
            MakeAiReviewProblem("ai-review:" + selected_element_id + ":payload-error",
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
        problems_manager_.AddOrUpdateProblem(
            MakeAiReviewProblem("ai-review:" + selected_element_id + ":guidelines-missing",
                                core::ProblemSeverity::Error,
                                selected_element_id,
                                payload.selected.type,
                                "SCCG guidelines could not be loaded for AI review: " + guideline_error));
        events_.Emit(StatusMessageEvent{"SCCG guidelines could not be loaded for AI review."});
        return;
    }

    AiReviewGuidelineSelection guideline_selection =
        review_profile_id == kDefaultClaimReviewProfileId ? SelectClaimReviewGuidelines(guideline_catalog)
                                                          : SelectReviewProfileGuidelines(guideline_catalog, review_profile_id);

    if (!guideline_selection.error_message.empty()) {
        problems_manager_.AddOrUpdateProblem(
            MakeAiReviewProblem("ai-review:" + selected_element_id + ":guidelines-empty",
                                core::ProblemSeverity::Error,
                                selected_element_id,
                                payload.selected.type,
                                guideline_selection.error_message));
        events_.Emit(StatusMessageEvent{guideline_selection.error_message});
        return;
    }

    if (guideline_selection.review_profile &&
        !ai::IsReviewProfileCompatibleWithElement(
            *guideline_selection.review_profile, *selected_element, FindTreeNode(current_tree, selected_element_id))) {
        const std::string message = "SCCG review profile '" + guideline_selection.review_profile->display_name +
                                    "' does not apply to the selected element type.";
        problems_manager_.AddOrUpdateProblem(
            MakeAiReviewProblem("ai-review:" + selected_element_id + ":profile-incompatible",
                                core::ProblemSeverity::Info,
                                selected_element_id,
                                payload.selected.type,
                                message));
        events_.Emit(StatusMessageEvent{message});
        return;
    }

    ai::AiReviewDataPackageBundle data_packages;
    std::string data_package_error;
    if (!ai::CollectAiReviewDataPackages(*assurance_case,
                                         current_tree,
                                         selected_element_id,
                                         guideline_selection.review_profile,
                                         data_packages,
                                         data_package_error)) {
        problems_manager_.AddOrUpdateProblem(
            MakeAiReviewProblem("ai-review:" + selected_element_id + ":data-package-error",
                                core::ProblemSeverity::Error,
                                selected_element_id,
                                payload.selected.type,
                                data_package_error.empty() ? "AI review data packages could not be collected."
                                                           : data_package_error));
        events_.Emit(StatusMessageEvent{"AI review data packages could not be collected."});
        return;
    }

    pending_review_ = ai::BuildAiReviewRequestArtifacts(
        payload, guideline_selection.guidelines, guideline_selection.review_profile, &data_packages);
    pending_review_element_id_ = payload.selected.id;
    pending_review_element_type_ = payload.selected.type;
    pending_review_profile_id_ = guideline_selection.review_profile ? guideline_selection.review_profile->id : review_profile_id;
    pending_guideline_ids_ = GuidelineIds(guideline_selection.guidelines);
    last_raw_response_.clear();
    last_parse_error_.clear();
    show_debug_modal_ = false;
    events_.Emit(StatusMessageEvent{"AI review request is ready in the AI Debug panel."});
}

void AiReviewController::StartPendingRequest() {
    if (pending_review_.prompt.empty())
        return;
    if (review_task_ && review_task_->IsRunning())
        return;

    ai::AiRequest request;
    request.systemInstruction = pending_review_.systemInstruction;
    request.userPrompt = pending_review_.prompt;

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
        problems_manager_.AddOrUpdateProblem(
            MakeAiReviewProblem("ai-review:" + pending_review_element_id_ + ":request-error",
                                core::ProblemSeverity::Error,
                                pending_review_element_id_,
                                pending_review_element_type_,
                                "AI review request failed: " + message));
        events_.Emit(StatusMessageEvent{"AI review request failed."});
        return;
    }

    last_raw_response_ = response.text.empty() ? response.rawJson : response.text;
    ai::AiReviewParseResult parse_result =
        ai::ParseAiReviewResponse(last_raw_response_, pending_review_element_id_, pending_guideline_ids_);
    if (!parse_result.success) {
        last_parse_error_ = parse_result.errorMessage;
        std::string message = "AI response could not be parsed as the expected JSON format.";
        if (!parse_result.errorMessage.empty())
            message += " " + parse_result.errorMessage;
        if (!last_raw_response_.empty()) {
            message += " Raw response: " + TruncateForProblemMessage(last_raw_response_);
        }
        problems_manager_.AddOrUpdateProblem(
            MakeAiReviewProblem("ai-review:" + pending_review_element_id_ + ":parse-error",
                                core::ProblemSeverity::Error,
                                pending_review_element_id_,
                                pending_review_element_type_,
                                message));
        events_.Emit(StatusMessageEvent{"AI review response could not be parsed."});
        return;
    }

    if (!parse_result.reviewedElementId.empty() && parse_result.reviewedElementId != pending_review_element_id_) {
        last_parse_error_ = "AI response reviewed_element_id did not match the requested element.";
        std::string message = last_parse_error_;
        if (!last_raw_response_.empty()) {
            message += " Raw response: " + TruncateForProblemMessage(last_raw_response_);
        }
        problems_manager_.AddOrUpdateProblem(
            MakeAiReviewProblem("ai-review:" + pending_review_element_id_ + ":parse-error",
                                core::ProblemSeverity::Error,
                                pending_review_element_id_,
                                pending_review_element_type_,
                                message));
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
    for (const core::ProblemItem& problem : parse_result.problems) {
        problems_manager_.AddOrUpdateProblem(problem);

        core::reviews::ReviewItem review_item;
        review_item.id = review_prefix + std::to_string(++review_comment_count);
        review_item.element_id = pending_review_element_id_;
        review_item.title = ReviewTitleForProblem(problem);
        review_item.message = problem.message;
        review_item.severity = SeverityString(problem.severity);
        review_item.reviewer_name = "AI Review";
        if (!problem.guideline_id.empty())
            review_item.guideline_ids = {problem.guideline_id};
        review_item.source = core::reviews::ReviewItemSource::AIReview;
        review_item.status = core::reviews::ReviewItemStatus::Open;
        review_item.created_utc = timestamp;
        review_item.updated_utc = timestamp;
        review_controller_.AddOrUpdateItem(std::move(review_item));
    }

    events_.Emit(StatusMessageEvent{parse_result.problems.empty()
                                        ? "AI review completed with no findings."
                                        : "AI review completed with " + std::to_string(parse_result.problems.size()) +
                                              " finding(s) added as review comment(s)."});
}

void AiReviewController::CancelPendingRequest() {
    show_debug_modal_ = false;
    pending_review_ = {};
    pending_review_element_id_.clear();
    pending_review_element_type_.clear();
    pending_review_profile_id_.clear();
    pending_guideline_ids_.clear();
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