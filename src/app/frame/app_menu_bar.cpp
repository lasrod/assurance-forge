#include "app/frame/app_menu_bar.h"

#include "app/app_runtime_state.h"
#include "app/frame/app_shell.h"
#include "ui/gsn/gsn_canvas_renderer.h"
#include "ui/localization.h"
#include "ui/theme.h"
#include "ui/ui_state.h"

#include "hello_imgui/hello_imgui.h"
#include "imgui.h"

#include <algorithm>
#include <cstdio>

namespace app::frame {
namespace {

void RenderLanguageMenu() {
    if (!ImGui::BeginMenu(ui::Tr(ui::MessageId::Language)))
        return;

    const ui::Language current = ui::CurrentLanguage();
    if (ImGui::MenuItem(ui::Tr(ui::MessageId::English), nullptr, current == ui::Language::English)) {
        ui::SetCurrentLanguage(ui::Language::English);
    }
    if (ImGui::MenuItem(ui::Tr(ui::MessageId::Japanese), nullptr, current == ui::Language::Japanese)) {
        ui::SetCurrentLanguage(ui::Language::Japanese);
    }

    ImGui::EndMenu();
}

void RenderThemeMenu() {
    if (!ImGui::BeginMenu(ui::Tr(ui::MessageId::Theme)))
        return;

    for (ui::AppTheme theme : ui::kAppThemes) {
        const bool selected = ui::GetCurrentAppTheme() == theme;
        if (ImGui::MenuItem(ui::GetThemeDisplayName(theme), nullptr, selected)) {
            ui::ApplyAppTheme(theme);
        }
    }

    ImGui::EndMenu();
}

void RenderMenuBarStats() {
    HelloImGui::RunnerParams* runner_params = HelloImGui::GetRunnerParams();
    if (!runner_params || !runner_params->imGuiWindowParams.showStatus_Fps)
        return;

    ui::gsn::CanvasRenderStats stats = ui::gsn::GetLastCanvasRenderStats();
    const int node_total = stats.nodes_drawn + stats.nodes_culled;
    const int edge_total = stats.edges_drawn + stats.edges_culled;
    const float node_ratio =
        node_total > 0 ? static_cast<float>(stats.nodes_culled) / static_cast<float>(node_total) : 0.0f;
    const float edge_ratio =
        edge_total > 0 ? static_cast<float>(stats.edges_culled) / static_cast<float>(edge_total) : 0.0f;

    char fps_text[32];
    char nodes_text[32];
    char edges_text[32];
    std::snprintf(fps_text, sizeof(fps_text), "FPS: %.1f", ImGui::GetIO().Framerate);
    std::snprintf(nodes_text, sizeof(nodes_text), "N %d/%d", stats.nodes_drawn, node_total);
    std::snprintf(edges_text, sizeof(edges_text), "E %d/%d", stats.edges_drawn, edge_total);

    const char* sep = "  ";
    const float total_width = ImGui::CalcTextSize(fps_text).x + ImGui::CalcTextSize(sep).x +
                              ImGui::CalcTextSize(nodes_text).x + ImGui::CalcTextSize(sep).x +
                              ImGui::CalcTextSize(edges_text).x;

    const float right_x = ImGui::GetWindowContentRegionMax().x - total_width;
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right_x));

    ImGui::TextUnformatted(fps_text);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextUnformatted(sep);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextColored(ui::CullRatioColor(node_ratio), "%s", nodes_text);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextUnformatted(sep);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextColored(ui::CullRatioColor(edge_ratio), "%s", edges_text);
}

} // namespace

float RenderAppMenuBar(AppRuntimeState& state, bool& done, const AppMenuBarCallbacks& callbacks) {
    if (!ImGui::BeginMainMenuBar()) {
        return 0.0f;
    }

    if (ImGui::BeginMenu(ui::Tr(ui::MessageId::FileMenu))) {
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::CreateEmptyProject)) && callbacks.begin_create_project) {
            callbacks.begin_create_project();
        }
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::OpenProject)) && callbacks.begin_open_project) {
            callbacks.begin_open_project();
        }
        ImGui::Separator();
        bool has_project = state.app_state.current_project.has_value();
        if (!has_project)
            ImGui::BeginDisabled();
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::SaveProject)) && callbacks.save_project) {
            callbacks.save_project();
        }
        if (ImGui::BeginMenu(ui::Tr(ui::MessageId::ExportMenu))) {
            bool can_export_gsn_svg = has_project && state.app_state.loaded_case.has_value();
            if (!can_export_gsn_svg)
                ImGui::BeginDisabled();
            if (ImGui::MenuItem(ui::Tr(ui::MessageId::ExportGsnSvg)) && callbacks.export_gsn_svg) {
                callbacks.export_gsn_svg();
            }
            if (!can_export_gsn_svg)
                ImGui::EndDisabled();
            ImGui::EndMenu();
        }
        if (!has_project)
            ImGui::EndDisabled();
        ImGui::Separator();
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::Exit)) && callbacks.request_exit) {
            callbacks.request_exit(done);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ui::Tr(ui::MessageId::AddMenu))) {
        bool has_project = state.app_state.current_project.has_value();
        if (!has_project)
            ImGui::BeginDisabled();
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::NewGsnSacmFile)) && callbacks.begin_create_project_sacm_file) {
            callbacks.begin_create_project_sacm_file();
        }
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::NewEvidenceRegister)) &&
            callbacks.begin_create_project_evidence_register) {
            callbacks.begin_create_project_evidence_register();
        }
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::NewJ3377CaeRegister)) &&
            callbacks.begin_create_project_j3377_cae_register) {
            callbacks.begin_create_project_j3377_cae_register();
        }
        if (!has_project)
            ImGui::EndDisabled();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ui::Tr(ui::MessageId::EditMenu))) {
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::Preferences))) {
            state.modal_coordinator->show_preferences_window = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ui::Tr(ui::MessageId::ViewMenu))) {
        ui::UiState& ui_state = ui::GetUiState();
        ImGui::MenuItem(ui::Tr(ui::MessageId::GsnCanvas), nullptr, &state.workbench.show_gsn_tab);
        ImGui::MenuItem(ui::Tr(ui::MessageId::CseRegister), nullptr, &state.workbench.show_cse_tab);
        ImGui::MenuItem(ui::Tr(ui::MessageId::EvidenceRegister), nullptr, &state.workbench.show_evidence_tab);
        NormalizeCenterViewSelection(state, ui_state.center_view);

        ImGui::Separator();
        if (ImGui::BeginMenu(ui::Tr(ui::MessageId::Appearance))) {
            RenderThemeMenu();
            RenderLanguageMenu();
            ImGui::EndMenu();
        }

        ImGui::Separator();
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::WelcomeScreen))) {
            state.project_controller->show_startup_project_window = true;
        }

        ImGui::EndMenu();
    }

    RenderMenuBarStats();

    ImGui::EndMainMenuBar();
    return ImGui::GetFrameHeight();
}

} // namespace app::frame
