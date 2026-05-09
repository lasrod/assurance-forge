#include "app/app_layout_controller.h"

#include "ui/widgets/splitter.h"

#include <algorithm>

namespace app {
namespace {

constexpr float kSplitterThickness = 4.0f;
constexpr float kMinPanelRatio = 0.10f;
constexpr float kMaxPanelRatio = 0.40f;
constexpr float kMinLeftSectionHeight = 120.0f;
constexpr float kMinCenterSectionHeight = 220.0f;
constexpr float kMinProblemsPanelHeight = 160.0f;

} // namespace

void NormalizeCenterViewSelection(AppRuntimeState& state, ui::CenterView& center_view) {
    if (!state.show_gsn_tab && !state.show_cse_tab && !state.show_evidence_tab && !state.show_package_details_tab &&
        !state.show_terminology_package_tab) {
        state.show_gsn_tab = true;
    }

    auto is_tab_visible = [&](ui::CenterView view) {
        switch (view) {
        case ui::CenterView::GsnCanvas:
            return state.show_gsn_tab;
        case ui::CenterView::CseRegister:
            return state.show_cse_tab;
        case ui::CenterView::EvidenceRegister:
            return state.show_evidence_tab;
        case ui::CenterView::PackageDetails:
            return state.show_package_details_tab;
        case ui::CenterView::TerminologyPackage:
            return state.show_terminology_package_tab;
        }
        return false;
    };

    if (!is_tab_visible(center_view)) {
        if (state.show_gsn_tab) {
            center_view = ui::CenterView::GsnCanvas;
        } else if (state.show_cse_tab) {
            center_view = ui::CenterView::CseRegister;
        } else if (state.show_terminology_package_tab) {
            center_view = ui::CenterView::TerminologyPackage;
        } else if (state.show_package_details_tab) {
            center_view = ui::CenterView::PackageDetails;
        } else {
            center_view = ui::CenterView::EvidenceRegister;
        }
        state.force_center_tab_selection = true;
    }
}

void RenderAppSplitters(AppRuntimeState& state,
                        float display_w,
                        float content_h,
                        float left_w,
                        float center_w,
                        float top_y,
                        ImGuiWindowFlags panel_flags) {
    ui::widgets::DrawVerticalSplitter("##left_splitter",
                                      left_w,
                                      kSplitterThickness,
                                      content_h,
                                      top_y,
                                      display_w,
                                      state.left_ratio,
                                      false,
                                      kMinPanelRatio,
                                      kMaxPanelRatio,
                                      panel_flags);

    const float center_x = left_w + kSplitterThickness;
    ui::widgets::DrawVerticalSplitter("##right_splitter",
                                      center_x + center_w,
                                      kSplitterThickness,
                                      content_h,
                                      top_y,
                                      display_w,
                                      state.right_ratio,
                                      true,
                                      kMinPanelRatio,
                                      kMaxPanelRatio,
                                      panel_flags);

    const float available_h = content_h - kSplitterThickness;
    if (available_h <= 0.0f)
        return;

    float min_ratio = kMinLeftSectionHeight / available_h;
    if (min_ratio > 0.30f)
        min_ratio = 0.30f;

    auto clamp_boundaries = [&]() {
        if (state.project_boundary_ratio < min_ratio)
            state.project_boundary_ratio = min_ratio;
        if (state.project_boundary_ratio > 1.0f - min_ratio)
            state.project_boundary_ratio = 1.0f - min_ratio;
    };

    clamp_boundaries();

    const float splitter_y = top_y + available_h * state.project_boundary_ratio;
    const float delta = ui::widgets::DrawHorizontalSplitter(
        "##left_h_splitter_1", 0.0f, splitter_y, left_w, kSplitterThickness, panel_flags);
    if (delta != 0.0f) {
        state.project_boundary_ratio += delta / available_h;
        clamp_boundaries();
    }

    auto clamp_problems_height = [&]() {
        const float min_problems_h = std::min(kMinProblemsPanelHeight, available_h * 0.5f);
        const float min_center_h = std::min(kMinCenterSectionHeight, available_h - min_problems_h);
        const float max_problems_h = std::max(min_problems_h, available_h - min_center_h);
        state.problems_panel_height = std::clamp(state.problems_panel_height, min_problems_h, max_problems_h);
    };

    clamp_problems_height();
    const float center_panel_h = std::max(0.0f, available_h - state.problems_panel_height);
    const float center_splitter_y = top_y + center_panel_h;
    const float delta_center = ui::widgets::DrawHorizontalSplitter(
        "##center_problems_splitter", center_x, center_splitter_y, center_w, kSplitterThickness, panel_flags);
    if (delta_center != 0.0f) {
        state.problems_panel_height -= delta_center;
        clamp_problems_height();
    }
}

} // namespace app
