#include "app/app_runtime.h"

#include "imgui.h"

#include "core/app_state.h"
#include "core/element_factory.h"
#include "ui/gsn/gsn_adapter.h"
#include "ui/gsn/gsn_canvas.h"
#include "ui/panels/element_panel.h"
#include "ui/panels/project_files_panel.h"
#include "ui/panels/sacm_viewer_panel.h"
#include "ui/panels/welcome_modal.h"
#include "ui/register_views.h"
#include "ui/tree_view.h"
#include "ui/ui_state.h"
#include "ui/widgets/splitter.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace app {
namespace {

constexpr size_t kPathBufferSize = 512;

constexpr float kInitialLeftPanelRatio = 0.20f;
constexpr float kInitialRightPanelRatio = 0.20f;
constexpr float kInitialProjectBoundaryRatio = 0.22f;
constexpr float kInitialSafetyBoundaryRatio = 0.52f;
constexpr float kMinPanelRatio = 0.10f;
constexpr float kMaxPanelRatio = 0.40f;
constexpr float kSplitterThickness = 4.0f;
constexpr float kMinLeftSectionHeight = 120.0f;

const ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_NoMove
                                   | ImGuiWindowFlags_NoResize
                                   | ImGuiWindowFlags_NoCollapse
                                   | ImGuiWindowFlags_NoBringToFrontOnFocus;

}  // namespace

// Pointer to the currently constructed AppRuntime, used by free functions
// (RequestAddChild / RequestNotImplemented) so UI code can poke the runtime
// without a circular include or callback plumbing.
static AppRuntime* g_active_runtime = nullptr;

struct AppRuntime::Impl {
    core::AppState app_state;

    char file_path_buf[kPathBufferSize] = "data/oasc-ja.xml";
    char dir_path_buf[kPathBufferSize] = "data";

    std::vector<std::string> xml_files;
    int selected_file_idx = -1;

    bool tree_needs_rebuild = false;
    core::AssuranceTree current_tree;
    bool show_overwrite_confirm = false;
    bool force_center_tab_selection = false;
    bool pending_focus_root = false;
    bool show_gsn_tab = true;
    bool show_cse_tab = false;
    bool show_evidence_tab = false;

    float left_ratio = kInitialLeftPanelRatio;
    float right_ratio = kInitialRightPanelRatio;
    float project_boundary_ratio = kInitialProjectBoundaryRatio;
    float safety_boundary_ratio = kInitialSafetyBoundaryRatio;

    // Modal for unimplemented features
    bool show_not_implemented_modal = false;
    std::string not_implemented_feature;
    bool show_startup_project_window = true;

    // Modal for confirming a multi-element removal. Populated by RemoveSelected
    // when the planned removal targets more than one element.
    bool show_remove_confirm = false;
    std::string pending_remove_id;
    core::RemoveMode pending_remove_mode = core::RemoveMode::NodeOnly;
    std::vector<std::string> pending_remove_ids;
};

void NormalizeCenterViewSelection(bool& show_gsn_tab,
                                  bool& show_cse_tab,
                                  bool& show_evidence_tab,
                                  bool& force_center_tab_selection,
                                  ui::CenterView& center_view) {
    if (!show_gsn_tab && !show_cse_tab && !show_evidence_tab) {
        // Keep at least one center tab visible.
        show_gsn_tab = true;
    }

    auto is_tab_visible = [&](ui::CenterView view) {
        switch (view) {
            case ui::CenterView::GsnCanvas: return show_gsn_tab;
            case ui::CenterView::CseRegister: return show_cse_tab;
            case ui::CenterView::EvidenceRegister: return show_evidence_tab;
        }
        return false;
    };

    if (!is_tab_visible(center_view)) {
        if (show_gsn_tab) {
            center_view = ui::CenterView::GsnCanvas;
        } else if (show_cse_tab) {
            center_view = ui::CenterView::CseRegister;
        } else {
            center_view = ui::CenterView::EvidenceRegister;
        }
        force_center_tab_selection = true;
    }
}

AppRuntime::AppRuntime() : impl_(new Impl()) {
    g_active_runtime = this;
    ScanDirectory();

    if (impl_->app_state.load_file(impl_->file_path_buf)) {
        impl_->tree_needs_rebuild = true;
        impl_->pending_focus_root = true;
        ui::GetUiState().center_view = ui::CenterView::GsnCanvas;
    }
}

AppRuntime::~AppRuntime() {
    if (g_active_runtime == this) g_active_runtime = nullptr;
    delete impl_;
}

void RequestAddChild(core::NewElementKind kind) {
    if (g_active_runtime) g_active_runtime->AddChildToSelected(kind);
}

void RequestRemove(core::RemoveMode mode) {
    if (g_active_runtime) g_active_runtime->RemoveSelected(mode);
}

const parser::AssuranceCase* GetActiveAssuranceCase() {
    if (!g_active_runtime) return nullptr;
    return g_active_runtime->GetLoadedCase();
}

void RequestNotImplemented(const char* feature) {
    if (g_active_runtime && feature) {
        g_active_runtime->ShowNotImplementedModal(feature);
    }
}

bool AppRuntime::AddChildToSelected(core::NewElementKind kind) {
    if (!impl_->app_state.loaded_case.has_value()) {
        SetStatus("No assurance case loaded.");
        return false;
    }
    const std::string& selected_id = ui::GetUiState().selected_element_id;
    if (selected_id.empty()) {
        SetStatus("No element selected.");
        return false;
    }

    parser::AssuranceCase& ac = impl_->app_state.loaded_case.value();
    sacm::AssuranceCasePackage* pkg = impl_->app_state.sacm_package.has_value()
                                          ? &impl_->app_state.sacm_package.value()
                                          : nullptr;

    std::string new_id;
    std::string error;
    if (!core::AddChildElement(ac, pkg, selected_id, kind, new_id, error)) {
        SetStatus("Add failed: " + error);
        return false;
    }

    impl_->tree_needs_rebuild = true;
    ui::UiState& s = ui::GetUiState();
    s.selected_element_id = new_id;
    s.center_on_selection = true;
    SetStatus("Added " + new_id);
    return true;
}

void AppRuntime::RemoveSelected(core::RemoveMode mode) {
    if (!impl_->app_state.loaded_case.has_value()) {
        SetStatus("No assurance case loaded.");
        return;
    }
    const std::string& selected_id = ui::GetUiState().selected_element_id;
    if (selected_id.empty()) {
        SetStatus("No element selected.");
        return;
    }

    parser::AssuranceCase& ac = impl_->app_state.loaded_case.value();
    auto planned = core::PlanRemoval(ac, selected_id, mode);
    if (planned.empty()) {
        SetStatus("Nothing to remove for this selection.");
        return;
    }

    sacm::AssuranceCasePackage* pkg = impl_->app_state.sacm_package.has_value()
                                          ? &impl_->app_state.sacm_package.value()
                                          : nullptr;

    // Single-element removal: act immediately, no confirmation.
    if (planned.size() == 1) {
        std::string error;
        if (!core::RemoveElement(ac, pkg, selected_id, mode, error)) {
            SetStatus("Remove failed: " + error);
            return;
        }
        impl_->tree_needs_rebuild = true;
        ui::GetUiState().selected_element_id.clear();
        SetStatus("Removed " + selected_id);
        return;
    }

    // Multi-element removal: stash the plan, mark nodes on the canvas, request
    // a fit-to-view of the marked set, and open the confirmation modal.
    impl_->show_remove_confirm = true;
    impl_->pending_remove_id = selected_id;
    impl_->pending_remove_mode = mode;
    impl_->pending_remove_ids.assign(planned.begin(), planned.end());

    auto& s = ui::GetUiState();
    s.marked_for_removal = std::move(planned);
    s.center_on_marked = true;
}

void AppRuntime::SetStatus(const std::string& message) {
    impl_->app_state.status_message = message;
}

void AppRuntime::ShowNotImplementedModal(const std::string& feature) {
    impl_->show_not_implemented_modal = true;
    impl_->not_implemented_feature = feature;
}

void AppRuntime::ScanDirectory() {
    impl_->xml_files.clear();
    impl_->selected_file_idx = -1;

    std::error_code ec;
    if (!std::filesystem::is_directory(impl_->dir_path_buf, ec)) {
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(impl_->dir_path_buf, ec)) {
        if (!entry.is_regular_file()) continue;

        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".xml") {
            impl_->xml_files.push_back(entry.path().string());
        }
    }

    std::sort(impl_->xml_files.begin(), impl_->xml_files.end());

    std::error_code path_ec;
    std::filesystem::path selected_path = std::filesystem::weakly_canonical(std::filesystem::path(impl_->file_path_buf), path_ec);
    if (path_ec) {
        selected_path = std::filesystem::path(impl_->file_path_buf).lexically_normal();
    }

    for (int i = 0; i < static_cast<int>(impl_->xml_files.size()); ++i) {
        std::filesystem::path candidate_path = std::filesystem::weakly_canonical(std::filesystem::path(impl_->xml_files[i]), path_ec);
        if (path_ec) {
            path_ec.clear();
            candidate_path = std::filesystem::path(impl_->xml_files[i]).lexically_normal();
        }

        if (candidate_path == selected_path) {
            impl_->selected_file_idx = i;
            break;
        }
    }
}

void AppRuntime::RebuildDerivedViewsIfNeeded() {
    if (impl_->tree_needs_rebuild && !impl_->app_state.loaded_case.has_value()) {
        ui::RebuildRegisterViews(nullptr);
        impl_->tree_needs_rebuild = false;
        return;
    }

    if (!impl_->app_state.loaded_case.has_value() || !impl_->tree_needs_rebuild) {
        return;
    }

    const auto& ac = impl_->app_state.loaded_case.value();
    impl_->current_tree = ui::gsn::BuildAssuranceTree(ac);
    ui::gsn::SetCanvasTree(impl_->current_tree);
    ui::RebuildRegisterViews(&ac);
    ui::GetUiState().model_has_translations = ui::ModelHasTranslations(ac);

    if (impl_->pending_focus_root && impl_->current_tree.root) {
        ui::UiState& ui_state = ui::GetUiState();
        ui_state.selected_element_id = impl_->current_tree.root->id;
        ui_state.center_on_selection = true;
        ui_state.center_view = ui::CenterView::GsnCanvas;
        impl_->force_center_tab_selection = true;
        impl_->pending_focus_root = false;
    }

    impl_->tree_needs_rebuild = false;
}

float AppRuntime::RenderMainMenuBar(bool& done) {
    if (!ImGui::BeginMainMenuBar()) {
        return 0.0f;
    }

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Exit")) {
            done = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        ui::UiState& ui_state = ui::GetUiState();
        ImGui::MenuItem("GSN Canvas", nullptr, &impl_->show_gsn_tab);
        ImGui::MenuItem("CSE Register", nullptr, &impl_->show_cse_tab);
        ImGui::MenuItem("Evidence Register", nullptr, &impl_->show_evidence_tab);
        NormalizeCenterViewSelection(
            impl_->show_gsn_tab,
            impl_->show_cse_tab,
            impl_->show_evidence_tab,
            impl_->force_center_tab_selection,
            ui_state.center_view);

        ImGui::Separator();
        if (ImGui::MenuItem("Welcome Screen")) {
            impl_->show_startup_project_window = true;
        }

        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
    return ImGui::GetFrameHeight();
}

void AppRuntime::RenderSplitters(float display_w, float content_h, float left_w, float center_w, float top_y) {
    ui::widgets::DrawVerticalSplitter("##left_splitter",
                             left_w,
                             kSplitterThickness,
                             content_h,
                             top_y,
                             display_w,
                             impl_->left_ratio,
                             false,
                             kMinPanelRatio,
                             kMaxPanelRatio,
                             kPanelFlags);

    float center_x = left_w + kSplitterThickness;
    ui::widgets::DrawVerticalSplitter("##right_splitter",
                             center_x + center_w,
                             kSplitterThickness,
                             content_h,
                             top_y,
                             display_w,
                             impl_->right_ratio,
                             true,
                             kMinPanelRatio,
                             kMaxPanelRatio,
                             kPanelFlags);

    float available_h = content_h - kSplitterThickness * 2.0f;
    if (available_h <= 0.0f) return;

    float min_ratio = kMinLeftSectionHeight / available_h;
    if (min_ratio > 0.30f) min_ratio = 0.30f;

    auto clamp_boundaries = [&]() {
        if (impl_->project_boundary_ratio < min_ratio) impl_->project_boundary_ratio = min_ratio;
        if (impl_->project_boundary_ratio > 1.0f - min_ratio * 2.0f) impl_->project_boundary_ratio = 1.0f - min_ratio * 2.0f;

        if (impl_->safety_boundary_ratio < impl_->project_boundary_ratio + min_ratio) {
            impl_->safety_boundary_ratio = impl_->project_boundary_ratio + min_ratio;
        }
        if (impl_->safety_boundary_ratio > 1.0f - min_ratio) impl_->safety_boundary_ratio = 1.0f - min_ratio;
    };

    clamp_boundaries();

    float splitter1_y = top_y + available_h * impl_->project_boundary_ratio;
    float splitter2_y = top_y + available_h * impl_->safety_boundary_ratio + kSplitterThickness;

    float delta1 = ui::widgets::DrawHorizontalSplitter("##left_h_splitter_1", 0.0f, splitter1_y, left_w, kSplitterThickness, kPanelFlags);
    if (delta1 != 0.0f) {
        impl_->project_boundary_ratio += delta1 / available_h;
        clamp_boundaries();
    }

    float delta2 = ui::widgets::DrawHorizontalSplitter("##left_h_splitter_2", 0.0f, splitter2_y, left_w, kSplitterThickness, kPanelFlags);
    if (delta2 != 0.0f) {
        impl_->safety_boundary_ratio += delta2 / available_h;
        clamp_boundaries();
    }
}

void AppRuntime::RenderTreePanel(float left_w, float safety_tree_h, float top_y) {
    ImGui::SetNextWindowPos(ImVec2(0, top_y));
    ImGui::SetNextWindowSize(ImVec2(left_w, safety_tree_h));
    ImGui::Begin("Safety Case Tree", nullptr, kPanelFlags);
    ui::ShowTreeViewPanel(impl_->current_tree.root ? &impl_->current_tree : nullptr);
    ImGui::End();
}

void AppRuntime::RenderSacmViewerPanel(float left_w, float sacm_h, float top_y) {
    ui::panels::SacmViewerPanelModel model{
        impl_->app_state,
        impl_->dir_path_buf,
        sizeof(impl_->dir_path_buf),
        impl_->file_path_buf,
        sizeof(impl_->file_path_buf),
        impl_->xml_files,
        impl_->selected_file_idx,
        impl_->show_overwrite_confirm,
    };
    ui::panels::SacmViewerPanelCallbacks callbacks{
        [this]() { ScanDirectory(); },
        [this]() {
            impl_->tree_needs_rebuild = true;
            impl_->pending_focus_root = true;
        },
        [this]() {
            impl_->current_tree = core::AssuranceTree();
            ui::gsn::SetCanvasTree(impl_->current_tree);
            ui::RebuildRegisterViews(nullptr);
            ui::GetUiState().selected_element_id.clear();
        },
    };
    ui::panels::ShowSacmViewerPanel(left_w, sacm_h, top_y, kPanelFlags, model, callbacks);
}

void AppRuntime::RenderCenterPanel(float center_x, float center_w, float content_h, float top_y) {
    ImGui::SetNextWindowPos(ImVec2(center_x, top_y));
    ImGui::SetNextWindowSize(ImVec2(center_w, content_h));
    ImGui::Begin("Center View", nullptr, kPanelFlags | ImGuiWindowFlags_NoTitleBar);

    ui::UiState& ui_state = ui::GetUiState();
    NormalizeCenterViewSelection(
        impl_->show_gsn_tab,
        impl_->show_cse_tab,
        impl_->show_evidence_tab,
        impl_->force_center_tab_selection,
        ui_state.center_view);

    if (ImGui::BeginTabBar("##center_tabs")) {
        if (impl_->show_gsn_tab) {
            ImGuiTabItemFlags gsn_flags = (impl_->force_center_tab_selection && ui_state.center_view == ui::CenterView::GsnCanvas)
                                          ? ImGuiTabItemFlags_SetSelected
                                          : 0;
            if (ImGui::BeginTabItem("GSN Canvas", nullptr, gsn_flags)) {
                ui_state.center_view = ui::CenterView::GsnCanvas;
                ui::gsn::ShowGsnCanvasContent();
                ImGui::EndTabItem();
            }
        }

        if (impl_->show_cse_tab) {
            ImGuiTabItemFlags cse_flags = (impl_->force_center_tab_selection && ui_state.center_view == ui::CenterView::CseRegister)
                                          ? ImGuiTabItemFlags_SetSelected
                                          : 0;
            if (ImGui::BeginTabItem("CSE Register", nullptr, cse_flags)) {
                ui_state.center_view = ui::CenterView::CseRegister;
                ui::ShowCseRegisterView();
                ImGui::EndTabItem();
            }
        }

        if (impl_->show_evidence_tab) {
            ImGuiTabItemFlags evidence_flags = (impl_->force_center_tab_selection && ui_state.center_view == ui::CenterView::EvidenceRegister)
                                               ? ImGuiTabItemFlags_SetSelected
                                               : 0;
            if (ImGui::BeginTabItem("Evidence Register", nullptr, evidence_flags)) {
                ui_state.center_view = ui::CenterView::EvidenceRegister;
                ui::ShowEvidenceRegisterView();
                ImGui::EndTabItem();
            }
        }

        ImGui::EndTabBar();
        impl_->force_center_tab_selection = false;
    }

    ImGui::End();
}

void AppRuntime::RenderElementPropertiesPanel(float center_x, float center_w, float right_w, float content_h, float top_y) {
    float right_x = center_x + center_w + kSplitterThickness;
    ImGui::SetNextWindowPos(ImVec2(right_x, top_y));
    ImGui::SetNextWindowSize(ImVec2(right_w, content_h));
    ImGui::Begin("Element Properties", nullptr, kPanelFlags);

    parser::AssuranceCase* ac_ptr = impl_->app_state.loaded_case.has_value() ? &impl_->app_state.loaded_case.value() : nullptr;
    sacm::AssuranceCasePackage* sacm_ptr = impl_->app_state.sacm_package.has_value() ? &impl_->app_state.sacm_package.value() : nullptr;
    if (ui::panels::ShowElementPanel(ac_ptr, sacm_ptr)) {
        impl_->tree_needs_rebuild = true;
    }

    ImGui::End();
}

void AppRuntime::RenderNotImplementedModal() {
    if (!impl_->show_not_implemented_modal) return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("##not_implemented_modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s is not implemented yet.", impl_->not_implemented_feature.c_str());
        ImGui::Spacing();
        ImGui::Spacing();

        float button_width = 100.0f;
        float modal_width = ImGui::GetWindowWidth();
        float center_x = (modal_width - button_width) * 0.5f;
        ImGui::SetCursorPosX(center_x);
        if (ImGui::Button("OK", ImVec2(button_width, 0))) {
            impl_->show_not_implemented_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (impl_->show_not_implemented_modal) {
        ImGui::OpenPopup("##not_implemented_modal");
    }
}

void AppRuntime::RenderRemoveConfirmModal() {
    if (!impl_->show_remove_confirm) return;

    auto cancel = [&]() {
        impl_->show_remove_confirm = false;
        impl_->pending_remove_id.clear();
        impl_->pending_remove_ids.clear();
        ui::GetUiState().marked_for_removal.clear();
    };

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("##remove_confirm_modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const int n = static_cast<int>(impl_->pending_remove_ids.size());
        const char* mode_label =
            impl_->pending_remove_mode == core::RemoveMode::NodeOnly
                ? "this node and its attachments"
                : "this node and its descendants";
        ImGui::Text("Remove %s?", mode_label);
        ImGui::Text("%d element%s will be deleted (highlighted in red).",
                    n, n == 1 ? "" : "s");
        ImGui::Spacing();
        ImGui::Spacing();

        const float button_width = 110.0f;
        const float spacing = 10.0f;
        const float total_width = button_width * 2.0f + spacing;
        const float center_x = (ImGui::GetWindowWidth() - total_width) * 0.5f;
        ImGui::SetCursorPosX(center_x);

        if (ImGui::Button("Remove", ImVec2(button_width, 0))) {
            std::string id = impl_->pending_remove_id;
            core::RemoveMode mode = impl_->pending_remove_mode;
            cancel();
            ImGui::CloseCurrentPopup();

            if (impl_->app_state.loaded_case.has_value()) {
                parser::AssuranceCase& ac = impl_->app_state.loaded_case.value();
                sacm::AssuranceCasePackage* pkg = impl_->app_state.sacm_package.has_value()
                                                      ? &impl_->app_state.sacm_package.value()
                                                      : nullptr;
                std::string error;
                if (!core::RemoveElement(ac, pkg, id, mode, error)) {
                    SetStatus("Remove failed: " + error);
                } else {
                    impl_->tree_needs_rebuild = true;
                    ui::GetUiState().selected_element_id.clear();
                    SetStatus("Removed " + std::to_string(n) + " element" + (n == 1 ? "" : "s"));
                }
            }
        }
        ImGui::SameLine(0.0f, spacing);
        if (ImGui::Button("Cancel", ImVec2(button_width, 0))) {
            cancel();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (impl_->show_remove_confirm) {
        ImGui::OpenPopup("##remove_confirm_modal");
    }
}

void AppRuntime::RenderStartupProjectWindow() {
    // TODO: Populate from persisted recent-projects list. Placeholder data shown for now.
    static const std::vector<ui::panels::RecentProjectEntry> kDemoRecent = {
        { "Open Autonomy Safety Case", "data/oasc-ja.xml", 42, 9, 16, 3 },
    };
    ui::panels::ShowWelcomeModal(impl_->show_startup_project_window, kDemoRecent);
}

const parser::AssuranceCase* AppRuntime::GetLoadedCase() const {
    if (!impl_->app_state.loaded_case.has_value()) return nullptr;
    return &impl_->app_state.loaded_case.value();
}

void AppRuntime::RenderFrame(bool& done) {
    ImVec2 display = ImGui::GetIO().DisplaySize;
    float top_y = RenderMainMenuBar(done);

    float content_h = std::max(0.0f, display.y - top_y);

    float left_w = display.x * impl_->left_ratio;
    float right_w = display.x * impl_->right_ratio;
    float center_w = display.x - left_w - right_w - kSplitterThickness * 2.0f;

    RebuildDerivedViewsIfNeeded();

    RenderSplitters(display.x, content_h, left_w, center_w, top_y);

    left_w = display.x * impl_->left_ratio;
    right_w = display.x * impl_->right_ratio;
    center_w = display.x - left_w - right_w - kSplitterThickness * 2.0f;

    float available_h = std::max(0.0f, content_h - kSplitterThickness * 2.0f);
    float project_h = available_h * impl_->project_boundary_ratio;
    float safety_tree_h = available_h * (impl_->safety_boundary_ratio - impl_->project_boundary_ratio);
    float sacm_h = std::max(0.0f, available_h - project_h - safety_tree_h);

    float project_y = top_y;
    float safety_y = project_y + project_h + kSplitterThickness;
    float sacm_y = safety_y + safety_tree_h + kSplitterThickness;

    ui::panels::ShowProjectFilesPanel(left_w, project_h, project_y, kPanelFlags);
    RenderTreePanel(left_w, safety_tree_h, safety_y);
    RenderSacmViewerPanel(left_w, sacm_h, sacm_y);

    float center_x = left_w + kSplitterThickness;
    RenderCenterPanel(center_x, center_w, content_h, top_y);
    RenderElementPropertiesPanel(center_x, center_w, right_w, content_h, top_y);

    RenderNotImplementedModal();
    RenderRemoveConfirmModal();
    RenderStartupProjectWindow();
}

}  // namespace app
