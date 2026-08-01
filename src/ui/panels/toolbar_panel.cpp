#include "ui/panels/toolbar_panel.h"

#include "ui/i18n/localization.h"
#include "ui/theme.h"

#include "hello_imgui/icons_font_awesome_4.h"
#include "imgui.h"

#include <algorithm>
#include <string>

namespace ui::panels {
namespace {

constexpr float kButtonPadding = 8.0f;
constexpr float kSeparatorPadding = 6.0f;

// Every glyph here was checked against the bundled fontawesome-webfont.ttf
// cmap, not just against the header: the header carries FontAwesome 5 names
// that compile fine and then render as the missing-glyph box.
struct ToolbarButton {
    ToolbarAction action;
    const char* icon;
    const char* tooltip; // English msgid; translated at draw time.
};

// Draws one icon button. Returns true when clicked and enabled.
bool DrawButton(const ToolbarButton& button, bool enabled, float height, const char* shortcut) {
    ImGui::PushID(static_cast<int>(button.action));
    if (!enabled)
        ImGui::BeginDisabled();

    const float width = ImGui::GetFrameHeight();
    const bool clicked = ImGui::Button(button.icon, ImVec2(width, height));

    if (!enabled)
        ImGui::EndDisabled();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        std::string tip = AF_TR(button.tooltip);
        if (shortcut != nullptr)
            tip += "  (" + std::string(shortcut) + ")";
        if (!enabled)
            tip += "\n" + AF_TR("Unavailable right now.");
        ImGui::SetTooltip("%s", tip.c_str());
    }
    ImGui::PopID();
    return clicked && enabled;
}

// A hairline divider that occupies layout space, so the buttons after it are
// spaced by it rather than drawn over it.
void DrawSeparator(float height) {
    ImGui::SameLine(0.0f, kSeparatorPadding);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float inset = height * 0.25f;
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(pos.x, pos.y + inset), ImVec2(pos.x, pos.y + height - inset), GetTheme().border, 1.0f);
    ImGui::Dummy(ImVec2(1.0f, height));
}

} // namespace

bool IsActionEnabled(const ToolbarModel& model, ToolbarAction action) {
    switch (action) {
    case ToolbarAction::OpenProject:
    case ToolbarAction::Preferences:
        // Always reachable â€” they are how a user gets out of having no project.
        return true;
    case ToolbarAction::SaveProject:
    case ToolbarAction::NewSacmFile:
        return model.has_project;
    case ToolbarAction::Undo:
        return model.can_undo;
    case ToolbarAction::FitToView:
        return model.gsn_canvas_active;
    case ToolbarAction::ExportGsnSvg:
        return model.has_project && model.has_loaded_case;
    }
    return false;
}

float ToolbarHeight() {
    return ImGui::GetFrameHeight() + kButtonPadding * 2.0f;
}

void ShowToolbar(const ToolbarModel& model, const ToolbarCallbacks& callbacks, float top_y) {
    const Theme& theme = GetTheme();
    const float height = ToolbarHeight();
    const ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(0.0f, top_y));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kButtonPadding, kButtonPadding));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(theme.surface_1));
    // Flat until hovered, so the bar reads as chrome rather than as a row of
    // competing controls.
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    ImGui::Begin("##toolbar", nullptr, kFlags);

    const float button_height = ImGui::GetFrameHeight();
    bool first = true;
    auto run = [&](ToolbarAction action,
                   const char* icon,
                   const char* tooltip,
                   const char* shortcut,
                   const std::function<void()>& handler) {
        // Buttons sit on one row; without this each would start a new line and
        // only the first would be inside the bar's height.
        if (!first)
            ImGui::SameLine(0.0f, 2.0f);
        first = false;
        if (DrawButton({action, icon, tooltip}, IsActionEnabled(model, action), button_height, shortcut) && handler)
            handler();
    };

    run(ToolbarAction::OpenProject, ICON_FA_FOLDER_OPEN, "Open Project", nullptr, callbacks.open_project);
    run(ToolbarAction::SaveProject, ICON_FA_SAVE, "Save Project", nullptr, callbacks.save_project);
    run(ToolbarAction::Undo, ICON_FA_UNDO, "Undo", "Ctrl+Z", callbacks.undo);

    DrawSeparator(height);
    run(ToolbarAction::NewSacmFile, ICON_FA_PLUS, "New GSN / SACM File", nullptr, callbacks.new_sacm_file);
    run(ToolbarAction::FitToView, ICON_FA_EXPAND, "Fit to view", nullptr, callbacks.fit_to_view);
    run(ToolbarAction::ExportGsnSvg, ICON_FA_DOWNLOAD, "Export GSN SVG", nullptr, callbacks.export_gsn_svg);

    // Preferences sits at the far right, away from the document actions.
    ImGui::SameLine();
    const float right = ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right));
    first = true; // Already positioned; do not let `run` add another SameLine.
    run(ToolbarAction::Preferences, ICON_FA_COG, "Preferences...", nullptr, callbacks.open_preferences);

    // Hairline below, matching the status bar's rule above it.
    const ImVec2 origin = ImGui::GetWindowPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(origin.x, origin.y + height - 1.0f),
                                        ImVec2(origin.x + io.DisplaySize.x, origin.y + height - 1.0f),
                                        theme.border,
                                        1.0f);

    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
}

} // namespace ui::panels
