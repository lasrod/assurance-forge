#include "app/app_runtime.h"

#include "app/actions/ai_review_actions.h"
#include "app/app_runtime_state.h"
#include "core/guideline_catalog.h"
#include "ui/i18n/localization.h"
#include "ui/ui_state.h"

#include "imgui.h"

#include <utility>

namespace app {
namespace {

void EnsureAiGuidelineCatalogLoaded(AppRuntimeState& state) {
    if (state.guideline_catalog_load_attempted)
        return;

    core::GuidelineCatalog catalog;
    std::string error;
    if (core::LoadGuidelineCatalog(catalog, error)) {
        state.guideline_catalog = std::move(catalog);
        state.guideline_catalog_error.clear();
    } else {
        state.guideline_catalog.reset();
        state.guideline_catalog_error = error;
    }
    state.guideline_catalog_load_attempted = true;
}

void DrawTooltipIfHovered(const std::string& text) {
    if (!text.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", text.c_str());
}

} // namespace

void AppRuntime::BeginAiReviewForSelection() {
    actions::AiReviewActions(*impl_).BeginForSelection();
}

void AppRuntime::RunAiReviewForSelection() {
    actions::AiReviewActions(*impl_).RunForSelection();
}

void AppRuntime::RunAiReviewForSelection(const std::string& review_profile_id) {
    actions::AiReviewActions(*impl_).RunForSelection(review_profile_id);
}

void AppRuntime::RenderAiReviewContextMenuForSelected() {
    EnsureAiGuidelineCatalogLoaded(*impl_);

    const ui::UiState& ui_state = ui::GetUiState();
    const parser::AssuranceCase* loaded_case = GetLoadedCase();
    const parser::SacmElement* selected_element = loaded_case && !ui_state.selected_element_id.empty()
                                                      ? ai::FindSacmElement(*loaded_case, ui_state.selected_element_id)
                                                      : nullptr;
    const core::TreeNode* selected_node = core::FindTreeNode(impl_->current_tree, ui_state.selected_element_id);
    const bool review_running = impl_->ai.review_controller->IsReviewRunning();

    controllers::AiReviewGuidelineSelection profile_selection;
    if (impl_->guideline_catalog.has_value() && selected_element) {
        profile_selection =
            controllers::SelectReviewProfileForElement(*impl_->guideline_catalog, *selected_element, selected_node);
    }

    const bool ai_review_enabled =
        !review_running && loaded_case && selected_element && impl_->guideline_catalog.has_value() &&
        profile_selection.review_profile != nullptr && profile_selection.error_message.empty();
    std::string ai_review_tooltip;
    if (review_running)
        ai_review_tooltip = AF_TR("AI review is already running.");
    else if (!loaded_case)
        ai_review_tooltip = AF_TR("Open an assurance case before running SCCG profile reviews.");
    else if (!selected_element)
        ai_review_tooltip = AF_TR("Select a GSN/SACM element before running SCCG profile reviews.");
    else if (!impl_->guideline_catalog.has_value())
        ai_review_tooltip = impl_->guideline_catalog_error.empty() ? AF_TR("SCCG profiles unavailable.")
                                                                   : impl_->guideline_catalog_error;
    else if (!profile_selection.error_message.empty())
        ai_review_tooltip = profile_selection.error_message;
    else if (profile_selection.review_profile)
        ai_review_tooltip =
            profile_selection.review_profile->display_name + "\n\n" + profile_selection.review_profile->description;

    if (ImGui::MenuItem(AF_TR("AI Review").c_str(), nullptr, false, ai_review_enabled))
        RunAiReviewForSelection();
    DrawTooltipIfHovered(ai_review_tooltip);

    const bool has_project = impl_->app_state.current_project.has_value();
    const bool manual_ok_enabled = !review_running && has_project && loaded_case && selected_element;
    std::string manual_ok_tooltip;
    if (review_running)
        manual_ok_tooltip = AF_TR("AI review is already running.");
    else if (!has_project)
        manual_ok_tooltip = AF_TR("Open or create a project before marking review status.");
    else if (!loaded_case)
        manual_ok_tooltip = AF_TR("Open an assurance case before marking review status.");
    else if (!selected_element)
        manual_ok_tooltip = AF_TR("Select a GSN/SACM element before marking review status.");
    else
        manual_ok_tooltip = AF_TR("Mark this element as manually reviewed OK.");

    if (ImGui::MenuItem(AF_TR("Mark review OK manually").c_str(), nullptr, false, manual_ok_enabled)) {
        SetManualReviewOk(ui_state.selected_element_id, true);
    }
    DrawTooltipIfHovered(manual_ok_tooltip);
}

void AppRuntime::StartPendingAiReviewRequest() {
    actions::AiReviewActions(*impl_).StartPendingRequest();
}

void AppRuntime::PollAiReviewTask() {
    actions::AiReviewActions(*impl_).PollTask();
}

} // namespace app
