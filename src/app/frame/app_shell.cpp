#include "app/frame/app_shell.h"

#include "app/app_runtime_state.h"
#include "ui/gsn/gsn_dpi.h"
#include "ui/panels/status_bar_panel.h"
#include "ui/panels/toolbar_panel.h"
#include "ui/widgets/splitter.h"

#include <algorithm>

namespace app::frame {
namespace {

constexpr float kSplitterThickness = 4.0f;
constexpr float kSplitterHitPadding = 6.0f;
constexpr float kMinPanelRatio = 0.10f;
constexpr float kMaxPanelRatio = 0.40f;
constexpr float kMinLeftSectionHeight = 120.0f;
constexpr float kMinCenterSectionHeight = 220.0f;
constexpr float kMinProblemsPanelHeight = 320.0f;

void RenderAppSplitters(AppRuntimeState& state,
                        float display_w,
                        float content_h,
                        float left_w,
                        float center_w,
                        float top_y,
                        float hit_padding,
                        ImGuiWindowFlags panel_flags) {
    ui::widgets::DrawVerticalSplitter("##left_splitter",
                                      left_w,
                                      kSplitterThickness,
                                      content_h,
                                      top_y,
                                      display_w,
                                      state.layout.left_ratio,
                                      false,
                                      kMinPanelRatio,
                                      kMaxPanelRatio,
                                      hit_padding,
                                      panel_flags);

    const float center_x = left_w + kSplitterThickness;
    ui::widgets::DrawVerticalSplitter("##right_splitter",
                                      center_x + center_w,
                                      kSplitterThickness,
                                      content_h,
                                      top_y,
                                      display_w,
                                      state.layout.right_ratio,
                                      true,
                                      kMinPanelRatio,
                                      kMaxPanelRatio,
                                      hit_padding,
                                      panel_flags);

    const float available_h = content_h - kSplitterThickness;
    if (available_h <= 0.0f)
        return;

    float min_ratio = kMinLeftSectionHeight / available_h;
    if (min_ratio > 0.30f)
        min_ratio = 0.30f;

    auto clamp_boundaries = [&]() {
        if (state.layout.project_boundary_ratio < min_ratio)
            state.layout.project_boundary_ratio = min_ratio;
        if (state.layout.project_boundary_ratio > 1.0f - min_ratio)
            state.layout.project_boundary_ratio = 1.0f - min_ratio;
    };

    clamp_boundaries();

    const float splitter_y = top_y + available_h * state.layout.project_boundary_ratio;
    const float delta = ui::widgets::DrawHorizontalSplitter(
        "##left_h_splitter_1", 0.0f, splitter_y, left_w, kSplitterThickness, hit_padding, panel_flags);
    if (delta != 0.0f) {
        state.layout.project_boundary_ratio += delta / available_h;
        clamp_boundaries();
    }

    auto clamp_problems_height = [&]() {
        const float min_problems_h = std::min(kMinProblemsPanelHeight, available_h * 0.5f);
        const float min_center_h = std::min(kMinCenterSectionHeight, available_h - min_problems_h);
        const float max_problems_h = std::max(min_problems_h, available_h - min_center_h);
        state.layout.problems_panel_height =
            std::clamp(state.layout.problems_panel_height, min_problems_h, max_problems_h);
    };

    clamp_problems_height();
    const float center_panel_h = std::max(0.0f, available_h - state.layout.problems_panel_height);
    const float center_splitter_y = top_y + center_panel_h;
    const float delta_center = ui::widgets::DrawHorizontalSplitter("##center_problems_splitter",
                                                                   center_x,
                                                                   center_splitter_y,
                                                                   center_w,
                                                                   kSplitterThickness,
                                                                   hit_padding,
                                                                   panel_flags);
    if (delta_center != 0.0f) {
        state.layout.problems_panel_height -= delta_center;
        clamp_problems_height();
    }
}

} // namespace

void NormalizeCenterViewSelection(AppRuntimeState& state, ui::CenterView& center_view) {
    if (!state.workbench.show_overview_tab && !state.workbench.show_gsn_tab && !state.workbench.show_cse_tab &&
        !state.workbench.show_evidence_tab && !state.workbench.show_package_details_tab &&
        !state.workbench.show_terminology_package_tab) {
        state.workbench.show_overview_tab = true;
    }

    auto is_tab_visible = [&](ui::CenterView view) {
        switch (view) {
        case ui::CenterView::ProjectOverview:
            return state.workbench.show_overview_tab;
        case ui::CenterView::GsnCanvas:
            return state.workbench.show_gsn_tab;
        case ui::CenterView::CseRegister:
            return state.workbench.show_cse_tab;
        case ui::CenterView::EvidenceRegister:
            return state.workbench.show_evidence_tab;
        case ui::CenterView::PackageDetails:
            return state.workbench.show_package_details_tab;
        case ui::CenterView::TerminologyPackage:
            return state.workbench.show_terminology_package_tab;
        }
        return false;
    };

    if (!is_tab_visible(center_view)) {
        if (state.workbench.show_overview_tab) {
            center_view = ui::CenterView::ProjectOverview;
        } else if (state.workbench.show_gsn_tab) {
            center_view = ui::CenterView::GsnCanvas;
        } else if (state.workbench.show_cse_tab) {
            center_view = ui::CenterView::CseRegister;
        } else if (state.workbench.show_terminology_package_tab) {
            center_view = ui::CenterView::TerminologyPackage;
        } else if (state.workbench.show_package_details_tab) {
            center_view = ui::CenterView::PackageDetails;
        } else {
            center_view = ui::CenterView::EvidenceRegister;
        }
        state.workbench.force_center_tab_selection = true;
    }
}

AppLayoutRegions RenderAppShell(AppRuntimeState& state, float menu_height, ImGuiWindowFlags panel_flags) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    // Panels stop above the status bar rather than running under it — every
    // height below is derived from content_h, so subtracting once is enough.
    const float status_bar_height = ui::panels::StatusBarHeight();
    const float toolbar_height = ui::panels::ToolbarHeight();
    const float top_y = menu_height + toolbar_height;
    const float content_h = std::max(0.0f, display.y - top_y - status_bar_height);

    float left_w = display.x * state.layout.left_ratio;
    float right_w = display.x * state.layout.right_ratio;
    float center_w = display.x - left_w - right_w - kSplitterThickness * 2.0f;

    const float hit_padding = ui::gsn::DpiSize(kSplitterHitPadding);
    RenderAppSplitters(state, display.x, content_h, left_w, center_w, top_y, hit_padding, panel_flags);

    left_w = display.x * state.layout.left_ratio;
    right_w = display.x * state.layout.right_ratio;
    center_w = display.x - left_w - right_w - kSplitterThickness * 2.0f;

    const float available_h = std::max(0.0f, content_h - kSplitterThickness);
    const float project_h = available_h * state.layout.project_boundary_ratio;
    const float argument_navigator_h = std::max(0.0f, available_h - project_h);
    const float argument_navigator_y = top_y + project_h + kSplitterThickness;

    const float center_x = left_w + kSplitterThickness;
    const float center_available_h = std::max(0.0f, content_h - kSplitterThickness);
    const float feedback_h = std::min(state.layout.problems_panel_height, center_available_h);
    const float workbench_h = std::max(0.0f, center_available_h - feedback_h);
    const float feedback_y = top_y + workbench_h + kSplitterThickness;
    const float inspector_x = center_x + center_w + kSplitterThickness;

    AppLayoutRegions regions;
    regions.menu_height = menu_height;
    regions.toolbar_height = toolbar_height;
    regions.status_bar_height = status_bar_height;
    regions.project_explorer = {{0.0f, top_y}, {left_w, project_h}};
    regions.argument_navigator = {{0.0f, argument_navigator_y}, {left_w, argument_navigator_h}};
    regions.workbench = {{center_x, top_y}, {center_w, workbench_h}};
    regions.feedback_dock = {{center_x, feedback_y}, {center_w, feedback_h}};
    regions.inspector = {{inspector_x, top_y}, {right_w, content_h}};
    return regions;
}

} // namespace app::frame
