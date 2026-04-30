#include "app/app_runtime.h"
#include "app/app_runtime_state.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "ai/ai_claim_review.h"
#include "ai/ai_service.h"
#include "imgui.h"
#include "parser/guidelines_parser.h"
#include "ui/ui_state.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace app {
namespace {

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

std::filesystem::path ExecutableDirectory() {
#ifdef _WIN32
    char path[MAX_PATH] = {};
    DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        return std::filesystem::path(path).parent_path();
    }
#endif
    return std::filesystem::current_path();
}

std::filesystem::path FindGuidelinesFile() {
    const std::filesystem::path executable_dir = ExecutableDirectory();
    const std::filesystem::path current_dir = std::filesystem::current_path();
    const std::vector<std::filesystem::path> candidates = {
        executable_dir / "data" / "guidelines.yaml",
        current_dir / "data" / "guidelines.yaml",
        current_dir / "external" / "safety-case-core-guidelines" / "data" / "guidelines.yaml",
        current_dir.parent_path() / "external" / "safety-case-core-guidelines" / "data" / "guidelines.yaml",
    };

    for (const std::filesystem::path& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::exists(candidate, error)) return candidate;
    }
    return {};
}

}  // namespace

void AppRuntime::BeginAiReviewForSelection() {
    if (impl_->ai_review_task && impl_->ai_review_task->IsRunning()) {
        SetStatus("AI review is already running.");
        return;
    }

    const std::string selected_element_id = ui::GetUiState().selected_element_id;
    if (selected_element_id.empty()) {
        impl_->problems_manager.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:no-selection",
            core::ProblemSeverity::Info,
            {},
            "AI Review",
            "No GSN element is selected for AI review."));
        SetStatus("No GSN element is selected for AI review.");
        return;
    }

    const parser::AssuranceCase* assurance_case = GetLoadedCase();
    if (!assurance_case) {
        impl_->problems_manager.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:" + selected_element_id + ":no-loaded-case",
            core::ProblemSeverity::Error,
            selected_element_id,
            "AI Review",
            "No assurance case is loaded for AI review."));
        SetStatus("No assurance case is loaded for AI review.");
        return;
    }

    const parser::SacmElement* selected_element = ai::FindSacmElement(*assurance_case, selected_element_id);
    if (!selected_element) {
        impl_->problems_manager.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:" + selected_element_id + ":missing-element",
            core::ProblemSeverity::Error,
            selected_element_id,
            "AI Review",
            "Selected element was not found."));
        SetStatus("Selected element was not found.");
        return;
    }

    if (!ai::IsSupportedAiReviewElement(*selected_element)) {
        impl_->problems_manager.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:" + selected_element_id + ":unsupported-type",
            core::ProblemSeverity::Info,
            selected_element_id,
            ai::AiReviewElementType(*selected_element),
            "AI Review currently supports GSN Goal / SACM Claim elements only."));
        SetStatus("AI Review currently supports GSN Goal / SACM Claim elements only.");
        return;
    }

    ai::AiReviewPayload payload;
    std::string payload_error;
    if (!ai::BuildAiReviewPayload(*assurance_case, impl_->current_tree, selected_element_id, payload, payload_error)) {
        impl_->problems_manager.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:" + selected_element_id + ":payload-error",
            core::ProblemSeverity::Error,
            selected_element_id,
            ai::AiReviewElementType(*selected_element),
            payload_error.empty() ? "AI review payload could not be created." : payload_error));
        SetStatus("AI review payload could not be created.");
        return;
    }

    const std::filesystem::path guidelines_path = FindGuidelinesFile();
    if (guidelines_path.empty()) {
        impl_->problems_manager.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:" + selected_element_id + ":guidelines-missing",
            core::ProblemSeverity::Error,
            selected_element_id,
            payload.selected.type,
            "SCCG guidelines.yaml could not be found for AI review."));
        SetStatus("SCCG guidelines.yaml could not be found for AI review.");
        return;
    }

    parser::GuidelinesParseResult guidelines_result = parser::GuidelinesParser::ParseFile(guidelines_path.string());
    if (!guidelines_result.success) {
        impl_->problems_manager.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:" + selected_element_id + ":guidelines-parse-error",
            core::ProblemSeverity::Error,
            selected_element_id,
            payload.selected.type,
            "SCCG guidelines could not be parsed for AI review: " + guidelines_result.error_message));
        SetStatus("SCCG guidelines could not be parsed for AI review.");
        return;
    }

    std::vector<const parser::Guideline*> claim_guidelines = guidelines_result.document.FindGuidelinesByCategory("CL");
    if (claim_guidelines.empty()) {
        impl_->problems_manager.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:" + selected_element_id + ":guidelines-empty",
            core::ProblemSeverity::Error,
            selected_element_id,
            payload.selected.type,
            "No SCCG CL guidelines were found for AI review."));
        SetStatus("No SCCG CL guidelines were found for AI review.");
        return;
    }

    impl_->pending_ai_review = ai::BuildAiReviewRequestArtifacts(payload, claim_guidelines);
    impl_->pending_ai_review_element_id = payload.selected.id;
    impl_->pending_ai_review_element_type = payload.selected.type;
    impl_->last_ai_review_raw_response.clear();
    impl_->last_ai_review_parse_error.clear();
    impl_->show_ai_review_debug_modal = true;
    SetStatus("AI review request is ready for inspection.");
}

void AppRuntime::StartPendingAiReviewRequest() {
    if (impl_->pending_ai_review.prompt.empty()) return;
    if (impl_->ai_review_task && impl_->ai_review_task->IsRunning()) return;

    ai::AiRequest request;
    request.systemInstruction = impl_->pending_ai_review.systemInstruction;
    request.userPrompt = impl_->pending_ai_review.prompt;

    std::shared_ptr<ai::AiService> service = impl_->ai_service;
    impl_->ai_review_task = impl_->ai_task_runner.RunGenerate([service, request]() {
        if (service) return service->Generate(request);
        ai::AiResponse response;
        response.success = false;
        response.errorCode = ai::AiErrorCode::Unknown;
        response.errorMessage = "AI service is unavailable.";
        return response;
    });
    SetStatus("AI review request sent.");
}

void AppRuntime::PollAiReviewTask() {
    if (!impl_->ai_review_task) return;

    ai::AiTaskSnapshot snapshot = impl_->ai_review_task->Snapshot();
    if (snapshot.state == ai::AiTaskState::Running) return;

    impl_->ai_review_task.reset();
    ai::AiResponse response = std::move(snapshot.response);
    if (!response.success) {
        std::string message = response.errorMessage.empty() ? ai::ToString(response.errorCode) : response.errorMessage;
        impl_->last_ai_review_raw_response = response.rawJson;
        impl_->problems_manager.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:" + impl_->pending_ai_review_element_id + ":request-error",
            core::ProblemSeverity::Error,
            impl_->pending_ai_review_element_id,
            impl_->pending_ai_review_element_type,
            "AI review request failed: " + message));
        SetStatus("AI review request failed.");
        return;
    }

    impl_->last_ai_review_raw_response = response.text.empty() ? response.rawJson : response.text;
    ai::AiReviewParseResult parse_result = ai::ParseAiReviewResponse(response.text, impl_->pending_ai_review_element_id);
    if (!parse_result.success) {
        impl_->last_ai_review_parse_error = parse_result.errorMessage;
        std::string message = "AI response could not be parsed as the expected JSON format.";
        if (!parse_result.errorMessage.empty()) message += " " + parse_result.errorMessage;
        if (!impl_->last_ai_review_raw_response.empty()) {
            message += " Raw response: " + TruncateForProblemMessage(impl_->last_ai_review_raw_response);
        }
        impl_->problems_manager.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:" + impl_->pending_ai_review_element_id + ":parse-error",
            core::ProblemSeverity::Error,
            impl_->pending_ai_review_element_id,
            impl_->pending_ai_review_element_type,
            message));
        SetStatus("AI review response could not be parsed.");
        return;
    }

    if (!parse_result.reviewedElementId.empty() &&
        parse_result.reviewedElementId != impl_->pending_ai_review_element_id) {
        impl_->last_ai_review_parse_error =
            "AI response reviewed_element_id did not match the requested element.";
        std::string message = impl_->last_ai_review_parse_error;
        if (!impl_->last_ai_review_raw_response.empty()) {
            message += " Raw response: " + TruncateForProblemMessage(impl_->last_ai_review_raw_response);
        }
        impl_->problems_manager.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:" + impl_->pending_ai_review_element_id + ":parse-error",
            core::ProblemSeverity::Error,
            impl_->pending_ai_review_element_id,
            impl_->pending_ai_review_element_type,
            message));
        SetStatus("AI review response could not be validated.");
        return;
    }

    if (parse_result.reviewedElementType.empty()) parse_result.reviewedElementType = impl_->pending_ai_review_element_type;
    for (core::ProblemItem& problem : parse_result.problems) {
        if (problem.type.empty()) problem.type = parse_result.reviewedElementType;
    }

    impl_->problems_manager.ClearProblemsForElementAndSource(
        impl_->pending_ai_review_element_id, core::ProblemSource::AIReview);
    for (const core::ProblemItem& problem : parse_result.problems) {
        impl_->problems_manager.AddOrUpdateProblem(problem);
    }

    SetStatus(parse_result.problems.empty()
        ? "AI review completed with no findings."
        : "AI review completed with " + std::to_string(parse_result.problems.size()) + " finding(s).");
}

void AppRuntime::RenderAiReviewDebugModal() {
    if (!impl_->show_ai_review_debug_modal) return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(920.0f, 700.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("AI Review Debug Request", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextWrapped("Inspect the exact AI review request data before sending.");
        ImGui::Spacing();

        const float child_height = std::max(240.0f, ImGui::GetContentRegionAvail().y - 58.0f);
        ImGui::BeginChild("##ai_review_debug_text", ImVec2(0.0f, child_height), true, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(impl_->pending_ai_review.debugText.c_str());
        ImGui::EndChild();
        ImGui::Spacing();

        const float button_width = 110.0f;
        if (ImGui::Button("OK", ImVec2(button_width, 0.0f))) {
            impl_->show_ai_review_debug_modal = false;
            ImGui::CloseCurrentPopup();
            StartPendingAiReviewRequest();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(button_width, 0.0f))) {
            impl_->show_ai_review_debug_modal = false;
            impl_->pending_ai_review = {};
            impl_->pending_ai_review_element_id.clear();
            impl_->pending_ai_review_element_type.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    } else if (impl_->show_ai_review_debug_modal) {
        ImGui::OpenPopup("AI Review Debug Request");
    }
}

}  // namespace app
