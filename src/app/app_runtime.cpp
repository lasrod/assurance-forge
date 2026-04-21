#include "app/app_runtime.h"

#include "imgui.h"

#include "core/app_state.h"
#include "ui/gsn_canvas.h"
#include "ui/register_views.h"
#include "ui/tree_view.h"
#include "ui/element_panel.h"
#include "ui/gsn_adapter.h"
#include "ui/ui_state.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace app {
namespace {

constexpr size_t kPathBufferSize = 512;

constexpr float kInitialLeftPanelRatio = 0.20f;
constexpr float kInitialRightPanelRatio = 0.20f;
constexpr float kMinPanelRatio = 0.10f;
constexpr float kMaxPanelRatio = 0.40f;
constexpr float kSplitterThickness = 6.0f;
constexpr float kTopLeftHeightRatio = 0.50f;

// Splitter color: medium-dark neutral gray (#262626)
const ImVec4 kSplitterColor = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);

// Element type colors in the SACM viewer list.
const ImVec4 kElementTypeDefaultColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);  // white
const ImVec4 kElementTypeClaimColor = ImVec4(0.4f, 0.8f, 0.4f, 1.0f);    // green
const ImVec4 kElementTypeStrategyColor = ImVec4(0.4f, 0.6f, 0.9f, 1.0f); // blue
const ImVec4 kElementTypeEvidenceColor = ImVec4(0.9f, 0.7f, 0.3f, 1.0f); // amber

constexpr float kOverwriteButtonWidth = 130.0f;

const ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_NoMove
                                   | ImGuiWindowFlags_NoResize
                                   | ImGuiWindowFlags_NoCollapse
                                   | ImGuiWindowFlags_NoBringToFrontOnFocus;

void DrawVerticalSplitter(const char* id,
                          float x,
                          float width,
                          float height,
                          float display_w,
                          float& ratio,
                          bool subtract_delta) {
    ImGui::SetNextWindowPos(ImVec2(x, 0));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kSplitterColor);

    ImGui::Begin(id, nullptr, kPanelFlags | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
    ImGui::InvisibleButton("##splitter_btn", ImVec2(width, height));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        float delta = ImGui::GetIO().MouseDelta.x / display_w;
        ratio += subtract_delta ? -delta : delta;
        if (ratio < kMinPanelRatio) ratio = kMinPanelRatio;
        if (ratio > kMaxPanelRatio) ratio = kMaxPanelRatio;
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

}  // namespace

struct AppRuntime::Impl {
    core::AppState app_state;

    char file_path_buf[kPathBufferSize] = "data/open-autonomy-safety-case.sacm.xml";
    char dir_path_buf[kPathBufferSize] = "data";

    std::vector<std::string> xml_files;
    int selected_file_idx = -1;

    bool tree_needs_rebuild = false;
    core::AssuranceTree current_tree;
    bool show_overwrite_confirm = false;
    bool force_center_tab_selection = false;

    float left_ratio = kInitialLeftPanelRatio;
    float right_ratio = kInitialRightPanelRatio;
};

AppRuntime::AppRuntime() : impl_(new Impl()) {
    ScanDirectory();
}

AppRuntime::~AppRuntime() {
    delete impl_;
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

    for (int i = 0; i < static_cast<int>(impl_->xml_files.size()); ++i) {
        if (impl_->xml_files[i] == impl_->file_path_buf) {
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
    impl_->current_tree = ui::BuildAssuranceTree(ac);
    ui::SetCanvasTree(impl_->current_tree);
    ui::RebuildRegisterViews(&ac);
    ui::GetUiState().model_has_translations = ui::ModelHasTranslations(ac);
    impl_->tree_needs_rebuild = false;
}

void AppRuntime::RenderSplitters(float display_w, float display_h, float left_w, float center_w) {
    DrawVerticalSplitter("##left_splitter", left_w, kSplitterThickness, display_h, display_w, impl_->left_ratio, false);

    float center_x = left_w + kSplitterThickness;
    DrawVerticalSplitter("##right_splitter", center_x + center_w, kSplitterThickness, display_h, display_w, impl_->right_ratio, true);
}

void AppRuntime::RenderTreePanel(float left_w, float top_left_h) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(left_w, top_left_h));
    ImGui::Begin("Safety Case Tree", nullptr, kPanelFlags);
    ui::ShowTreeViewPanel(impl_->current_tree.root ? &impl_->current_tree : nullptr);
    ImGui::End();
}

void AppRuntime::RenderSacmViewerPanel(float left_w, float top_left_h, float bottom_left_h, bool& done) {
    ImGui::SetNextWindowPos(ImVec2(0, top_left_h));
    ImGui::SetNextWindowSize(ImVec2(left_w, bottom_left_h));
    ImGui::Begin("SACM Viewer", nullptr, kPanelFlags | ImGuiWindowFlags_MenuBar);

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Exit")) {
                done = true;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            ui::UiState& ui_state = ui::GetUiState();
            if (ImGui::MenuItem("GSN Canvas", nullptr, ui_state.center_view == ui::CenterView::GsnCanvas)) {
                ui_state.center_view = ui::CenterView::GsnCanvas;
                impl_->force_center_tab_selection = true;
            }
            if (ImGui::MenuItem("CSE Register", nullptr, ui_state.center_view == ui::CenterView::CseRegister)) {
                ui_state.center_view = ui::CenterView::CseRegister;
                impl_->force_center_tab_selection = true;
            }
            if (ImGui::MenuItem("Evidence Register", nullptr, ui_state.center_view == ui::CenterView::EvidenceRegister)) {
                ui_state.center_view = ui::CenterView::EvidenceRegister;
                impl_->force_center_tab_selection = true;
            }
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }

    ImGui::Text("Directory:");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##dirpath", impl_->dir_path_buf, sizeof(impl_->dir_path_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
        ScanDirectory();
    }

    ImGui::Text("XML File:");
    ImGui::SetNextItemWidth(-1);
    if (impl_->xml_files.empty()) {
        ImGui::TextDisabled("No XML files found");
    } else {
        const char* preview = "";
        if (impl_->selected_file_idx >= 0 && impl_->selected_file_idx < static_cast<int>(impl_->xml_files.size())) {
            const std::string& sel = impl_->xml_files[impl_->selected_file_idx];
            auto pos = sel.find_last_of("\\/");
            preview = (pos != std::string::npos) ? sel.c_str() + pos + 1 : sel.c_str();
        }

        if (ImGui::BeginCombo("##fileselect", preview)) {
            for (int i = 0; i < static_cast<int>(impl_->xml_files.size()); ++i) {
                const std::string& path = impl_->xml_files[i];
                auto pos = path.find_last_of("\\/");
                const char* label = (pos != std::string::npos) ? path.c_str() + pos + 1 : path.c_str();
                bool is_selected = (i == impl_->selected_file_idx);

                if (ImGui::Selectable(label, is_selected)) {
                    impl_->selected_file_idx = i;
                    size_t len = path.size();
                    if (len >= sizeof(impl_->file_path_buf)) len = sizeof(impl_->file_path_buf) - 1;
                    memcpy(impl_->file_path_buf, path.c_str(), len);
                    impl_->file_path_buf[len] = '\0';
                }
                if (is_selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    if (ImGui::Button("Load")) {
        impl_->app_state.load_file(impl_->file_path_buf);
        impl_->tree_needs_rebuild = true;
    }

    ImGui::SameLine();
    {
        bool can_save = impl_->app_state.sacm_package.has_value();
        if (!can_save) ImGui::BeginDisabled();
        if (ImGui::Button("Save")) {
            FILE* f = fopen(impl_->file_path_buf, "r");
            if (f) {
                fclose(f);
                impl_->show_overwrite_confirm = true;
            } else {
                impl_->app_state.save_file(impl_->file_path_buf);
            }
        }
        if (!can_save) ImGui::EndDisabled();
    }

    if (impl_->show_overwrite_confirm) {
        ImGui::OpenPopup("Overwrite File?");
        impl_->show_overwrite_confirm = false;
    }

    if (ImGui::BeginPopupModal("Overwrite File?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("File already exists:\n%s", impl_->file_path_buf);
        ImGui::Separator();
        ImGui::Text("Are you sure you want to overwrite it?");
        ImGui::Spacing();

        if (ImGui::Button("Yes, Overwrite", ImVec2(kOverwriteButtonWidth, 0))) {
            impl_->app_state.save_file(impl_->file_path_buf);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(kOverwriteButtonWidth, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (!impl_->app_state.status_message.empty()) {
        ImGui::TextWrapped("%s", impl_->app_state.status_message.c_str());
    }

    ImGui::Separator();

    if (impl_->app_state.loaded_case.has_value()) {
        const auto& ac = impl_->app_state.loaded_case.value();

        ImGui::Text("Assurance Case: %s", ac.name.c_str());
        ImGui::Separator();

        if (ImGui::BeginChild("ElementList", ImVec2(0, 0), true)) {
            for (const auto& elem : ac.elements) {
                ImGui::PushID(elem.id.c_str());

                ImVec4 color = kElementTypeDefaultColor;
                if (elem.type == "claim") {
                    color = kElementTypeClaimColor;
                } else if (elem.type == "argumentreasoning") {
                    color = kElementTypeStrategyColor;
                } else if (elem.type == "artifact" || elem.type == "artifactreference") {
                    color = kElementTypeEvidenceColor;
                }

                ImGui::TextColored(color, "[%s]", elem.type.c_str());
                ImGui::SameLine();
                ImGui::Text("%s: %s", elem.id.c_str(), elem.name.c_str());

                if (!elem.content.empty()) {
                    ImGui::TextWrapped("  Content: %s", elem.content.c_str());
                }

                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }

    ImGui::End();
}

void AppRuntime::RenderCenterPanel(float center_x, float center_w, float display_h) {
    ImGui::SetNextWindowPos(ImVec2(center_x, 0));
    ImGui::SetNextWindowSize(ImVec2(center_w, display_h));
    ImGui::Begin("Center View", nullptr, kPanelFlags | ImGuiWindowFlags_NoTitleBar);

    ui::UiState& ui_state = ui::GetUiState();
    if (ImGui::BeginTabBar("##center_tabs")) {
        ImGuiTabItemFlags gsn_flags = (impl_->force_center_tab_selection && ui_state.center_view == ui::CenterView::GsnCanvas)
                                      ? ImGuiTabItemFlags_SetSelected
                                      : 0;
        if (ImGui::BeginTabItem("GSN Canvas", nullptr, gsn_flags)) {
            ui_state.center_view = ui::CenterView::GsnCanvas;
            ui::ShowGsnCanvasContent();
            ImGui::EndTabItem();
        }

        ImGuiTabItemFlags cse_flags = (impl_->force_center_tab_selection && ui_state.center_view == ui::CenterView::CseRegister)
                                      ? ImGuiTabItemFlags_SetSelected
                                      : 0;
        if (ImGui::BeginTabItem("CSE Register", nullptr, cse_flags)) {
            ui_state.center_view = ui::CenterView::CseRegister;
            ui::ShowCseRegisterView();
            ImGui::EndTabItem();
        }

        ImGuiTabItemFlags evidence_flags = (impl_->force_center_tab_selection && ui_state.center_view == ui::CenterView::EvidenceRegister)
                                           ? ImGuiTabItemFlags_SetSelected
                                           : 0;
        if (ImGui::BeginTabItem("Evidence Register", nullptr, evidence_flags)) {
            ui_state.center_view = ui::CenterView::EvidenceRegister;
            ui::ShowEvidenceRegisterView();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
        impl_->force_center_tab_selection = false;
    }

    ImGui::End();
}

void AppRuntime::RenderElementPropertiesPanel(float center_x, float center_w, float right_w) {
    float right_x = center_x + center_w + kSplitterThickness;
    ImGui::SetNextWindowPos(ImVec2(right_x, 0));
    ImGui::SetNextWindowSize(ImVec2(right_w, ImGui::GetIO().DisplaySize.y));
    ImGui::Begin("Element Properties", nullptr, kPanelFlags);

    parser::AssuranceCase* ac_ptr = impl_->app_state.loaded_case.has_value() ? &impl_->app_state.loaded_case.value() : nullptr;
    sacm::AssuranceCasePackage* sacm_ptr = impl_->app_state.sacm_package.has_value() ? &impl_->app_state.sacm_package.value() : nullptr;
    if (ui::ShowElementPanel(ac_ptr, sacm_ptr)) {
        impl_->tree_needs_rebuild = true;
    }

    ImGui::End();
}

void AppRuntime::RenderFrame(bool& done) {
    ImVec2 display = ImGui::GetIO().DisplaySize;

    float left_w = display.x * impl_->left_ratio;
    float right_w = display.x * impl_->right_ratio;
    float center_w = display.x - left_w - right_w - kSplitterThickness * 2.0f;

    float top_left_h = display.y * kTopLeftHeightRatio;
    float bottom_left_h = display.y - top_left_h;

    RebuildDerivedViewsIfNeeded();

    RenderSplitters(display.x, display.y, left_w, center_w);

    RenderTreePanel(left_w, top_left_h);
    RenderSacmViewerPanel(left_w, top_left_h, bottom_left_h, done);

    float center_x = left_w + kSplitterThickness;
    RenderCenterPanel(center_x, center_w, display.y);
    RenderElementPropertiesPanel(center_x, center_w, right_w);
}

}  // namespace app
