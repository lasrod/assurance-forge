#include "app/frame/app_menu_bar.h"

#include "app/app_runtime_state.h"
#include "app/frame/app_shell.h"
#include "ui/gsn/gsn_canvas_renderer.h"
#include "ui/i18n/localization.h"
#include "ui/theme.h"
#include "ui/ui_state.h"

#include "hello_imgui/hello_imgui.h"
#include "imgui.h"

#include <algorithm>
#include <cstdio>

namespace app::frame {
namespace {

void RenderLanguageMenu() {
    if (!ImGui::BeginMenu(AF_TR("Language").c_str()))
        return;

    const ui::i18n::Language current = ui::i18n::CurrentLanguage();
    const char* english_shortcut = current == ui::i18n::Language::English ? nullptr : "F8";
    if (ImGui::MenuItem(ui::i18n::LanguageDisplayName(ui::i18n::Language::English).c_str(),
                        english_shortcut,
                        current == ui::i18n::Language::English)) {
        ui::i18n::SetLanguage(ui::i18n::Language::English);
    }
    const char* japanese_shortcut = current == ui::i18n::Language::Japanese ? nullptr : "F8";
    if (ImGui::MenuItem(ui::i18n::LanguageDisplayName(ui::i18n::Language::Japanese).c_str(),
                        japanese_shortcut,
                        current == ui::i18n::Language::Japanese)) {
        ui::i18n::SetLanguage(ui::i18n::Language::Japanese);
    }

    ImGui::EndMenu();
}

void RenderThemeMenu() {
    if (!ImGui::BeginMenu(AF_TR("Theme").c_str()))
        return;

    for (ui::AppTheme theme : ui::kAppThemes) {
        const bool selected = ui::GetCurrentAppTheme() == theme;
        const char* shortcut = selected ? nullptr : "F9";
        if (ImGui::MenuItem(AF_TR(ui::GetThemeDisplayName(theme)).c_str(), shortcut, selected)) {
            ui::ApplyAppTheme(theme);
        }
    }

    ImGui::EndMenu();
}

void RenderMenuBarStats() {
    if (!ui::GetUiState().show_developer_tools)
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
    const float sep_w = ImGui::CalcTextSize(sep).x;
    const float content_w = ImGui::CalcTextSize(fps_text).x + sep_w + ImGui::CalcTextSize(nodes_text).x + sep_w +
                            ImGui::CalcTextSize(edges_text).x;

    // The whole stats readout (FPS + node/edge counts) doubles as a button:
    // clicking it opens the Performance overlay. Requirements:
    //   * Looks like a plain status label when not hovered.
    //   * Vertically centered in the menu bar (drawn manually, so we must add
    //     the centering offset ImGui normally applies to menu-bar text).
    //   * Fixed width so it does not jiggle as the numbers change — sized from
    //     a worst-case template, with the live text right-aligned inside it.
    const float pad_x = 8.0f;
    const float button_w = ImGui::CalcTextSize("FPS: 999.9  N 9999/9999  E 9999/9999").x + pad_x * 2.0f;
    const float bar_h = ImGui::GetFrameHeight();

    const float right_x = ImGui::GetWindowContentRegionMax().x - button_w;
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right_x));

    ui::UiState& ui_state = ui::GetUiState();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton("##fps_open_perf", ImVec2(button_w, bar_h));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("%s", AF_TR("Open Performance overlay").c_str());
    }
    if (ImGui::IsItemClicked())
        ui_state.show_perf_overlay = true;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered) {
        const ui::Theme& th = ui::GetTheme();
        dl->AddRectFilled(ImVec2(p0.x, p0.y + 2.0f),
                          ImVec2(p0.x + button_w, p0.y + bar_h - 2.0f),
                          ui::WithAlpha(th.accent, 0.18f),
                          4.0f);
        dl->AddRect(ImVec2(p0.x, p0.y + 2.0f),
                    ImVec2(p0.x + button_w, p0.y + bar_h - 2.0f),
                    ui::WithAlpha(th.accent, 0.9f),
                    4.0f);
    }

    // Right-align the live text inside the fixed-width button and center it
    // vertically, then draw the three segments left-to-right.
    const float text_y = p0.y + (bar_h - ImGui::GetTextLineHeight()) * 0.5f;
    float x = p0.x + button_w - pad_x - content_w;
    dl->AddText(ImVec2(x, text_y), ImGui::GetColorU32(ImGuiCol_Text), fps_text);
    x += ImGui::CalcTextSize(fps_text).x + sep_w;
    dl->AddText(ImVec2(x, text_y), ImGui::ColorConvertFloat4ToU32(ui::CullRatioColor(node_ratio)), nodes_text);
    x += ImGui::CalcTextSize(nodes_text).x + sep_w;
    dl->AddText(ImVec2(x, text_y), ImGui::ColorConvertFloat4ToU32(ui::CullRatioColor(edge_ratio)), edges_text);
}

} // namespace

float RenderAppMenuBar(AppRuntimeState& state, bool& done, const AppMenuBarCallbacks& callbacks) {
    if (!ImGui::BeginMainMenuBar()) {
        return 0.0f;
    }

    if (ImGui::BeginMenu(AF_TR("File").c_str())) {
        if (ImGui::MenuItem(AF_TR("Create Empty Assurance Project").c_str()) && callbacks.begin_create_project) {
            callbacks.begin_create_project();
        }
        if (ImGui::MenuItem(AF_TR("Create Project from Existing SACM...").c_str()) &&
            callbacks.begin_create_project_from_sacm) {
            callbacks.begin_create_project_from_sacm();
        }
        if (ImGui::MenuItem(AF_TR("Open Project").c_str()) && callbacks.begin_open_project) {
            callbacks.begin_open_project();
        }
        ImGui::Separator();
        bool has_project = state.app_state.current_project.has_value();
        if (!has_project)
            ImGui::BeginDisabled();
        // Adds an argument to the project that is open, so it sits with the
        // project-scoped items rather than beside the creates above.
        if (ImGui::MenuItem(AF_TR("Import SACM File...").c_str()) && callbacks.begin_import_sacm_file) {
            callbacks.begin_import_sacm_file();
        }
        if (ImGui::MenuItem(AF_TR("Save Project").c_str()) && callbacks.save_project) {
            callbacks.save_project();
        }
        if (ImGui::BeginMenu(AF_TR("Export").c_str())) {
            bool can_export_gsn_svg = has_project && state.app_state.loaded_case.has_value();
            if (!can_export_gsn_svg)
                ImGui::BeginDisabled();
            if (ImGui::MenuItem(AF_TR("GSN SVG").c_str()) && callbacks.export_gsn_svg) {
                callbacks.export_gsn_svg();
            }
            if (!can_export_gsn_svg)
                ImGui::EndDisabled();
            ImGui::EndMenu();
        }
        if (!has_project)
            ImGui::EndDisabled();
        ImGui::Separator();
        if (ImGui::MenuItem(AF_TR("Exit").c_str()) && callbacks.request_exit) {
            callbacks.request_exit(done);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(AF_TR("Add").c_str())) {
        bool has_project = state.app_state.current_project.has_value();
        if (!has_project)
            ImGui::BeginDisabled();
        if (ImGui::MenuItem(AF_TR("New GSN / SACM File").c_str()) && callbacks.begin_create_project_sacm_file) {
            callbacks.begin_create_project_sacm_file();
        }
        if (ImGui::MenuItem(AF_TR("New Evidence Register").c_str()) &&
            callbacks.begin_create_project_evidence_register) {
            callbacks.begin_create_project_evidence_register();
        }
        if (ImGui::MenuItem(AF_TR("New J3377 CAE Register").c_str()) &&
            callbacks.begin_create_project_j3377_cae_register) {
            callbacks.begin_create_project_j3377_cae_register();
        }
        if (!has_project)
            ImGui::EndDisabled();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(AF_TR("Edit").c_str())) {
        const bool can_undo = callbacks.can_undo && callbacks.can_undo();
        if (!can_undo)
            ImGui::BeginDisabled();
        if (ImGui::MenuItem(AF_TR("Undo").c_str(), "Ctrl+Z") && callbacks.undo) {
            callbacks.undo();
        }
        if (!can_undo) {
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip(
                    "%s", AF_TR("Reached snapshot or baseline — restore from history to go further back.").c_str());
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem(AF_TR("Preferences...").c_str())) {
            state.modal_coordinator->show_preferences_window = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(AF_TR("View").c_str())) {
        ui::UiState& ui_state = ui::GetUiState();
        ImGui::MenuItem(AF_TR("Project Overview").c_str(), nullptr, &state.workbench.show_overview_tab);
        ImGui::MenuItem(AF_TR("GSN Canvas").c_str(), nullptr, &state.workbench.show_gsn_tab);
        ImGui::MenuItem(AF_TR("CSE Register").c_str(), nullptr, &state.workbench.show_cse_tab);
        ImGui::MenuItem(AF_TR("Evidence Register").c_str(), nullptr, &state.workbench.show_evidence_tab);
        NormalizeCenterViewSelection(state, ui_state.center_view);

        ImGui::Separator();
        if (ImGui::BeginMenu(AF_TR("Appearance").c_str())) {
            RenderThemeMenu();
            RenderLanguageMenu();
            ImGui::EndMenu();
        }

        ImGui::Separator();
        if (ImGui::MenuItem(AF_TR("Welcome Screen").c_str())) {
            state.project_controller->show_startup_project_window = true;
        }

        ImGui::Separator();
        if (ImGui::BeginMenu(AF_TR("Developer").c_str())) {
            ImGui::MenuItem(AF_TR("Developer tools").c_str(), nullptr, &ui_state.show_developer_tools);
            if (!ui_state.show_developer_tools)
                ImGui::BeginDisabled();
            ImGui::MenuItem(AF_TR("Performance overlay").c_str(), nullptr, &ui_state.show_perf_overlay);
            if (!ui_state.show_developer_tools)
                ImGui::EndDisabled();
            ImGui::EndMenu();
        }

        ImGui::EndMenu();
    }

    RenderMenuBarStats();

    ImGui::EndMainMenuBar();
    return ImGui::GetFrameHeight();
}

} // namespace app::frame
