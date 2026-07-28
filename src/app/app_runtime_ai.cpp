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

    if (!ImGui::BeginMenu(AF_TR("AI Review").c_str()))
        return;

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
    ImGui::Separator();

    if (!impl_->guideline_catalog.has_value()) {
        ImGui::TextDisabled("%s", AF_TR("SCCG profiles unavailable.").c_str());
        if (!impl_->guideline_catalog_error.empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", impl_->guideline_catalog_error.c_str());
        ImGui::EndMenu();
        return;
    }

    for (const parser::ReviewProfile& profile : impl_->guideline_catalog->document.review_profiles) {
        const bool compatible =
            selected_element && ai::IsReviewProfileCompatibleWithElement(profile, *selected_element, selected_node);
        const bool enabled = !review_running && loaded_case && selected_element && compatible;
        std::string tooltip = profile.description;
        if (review_running)
            tooltip = AF_TR("AI review is already running.");
        else if (!loaded_case)
            tooltip = AF_TR("Open an assurance case before running SCCG profile reviews.");
        else if (!selected_element)
            tooltip = AF_TR("Select a GSN/SACM element before running SCCG profile reviews.");
        else if (!compatible)
            tooltip = profile.description + "\n\n" + AF_TR("This profile does not apply to the selected element type.");

        ImGui::PushID(profile.id.c_str());
        if (ImGui::MenuItem(profile.display_name.c_str(), nullptr, false, enabled)) {
            RunAiReviewForSelection(profile.id);
        }
        DrawTooltipIfHovered(tooltip);
        ImGui::PopID();
    }

    ImGui::EndMenu();
}

void AppRuntime::StartPendingAiReviewRequest() {
    actions::AiReviewActions(*impl_).StartPendingRequest();
}

void AppRuntime::PollAiReviewTask() {
    actions::AiReviewActions(*impl_).PollTask();
}

} // namespace app
