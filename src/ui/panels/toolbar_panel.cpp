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
};

// Literal AF_TR per action so tools/i18n/extract_msgids.py can discover the
// strings. Routing a msgid through a `const char*` field and calling
// AF_TR(field) compiles and reads fine, but the extractor only sees literals --
// so the catalog check passes while the tooltip stays permanently English.
std::string TranslateTooltip(ToolbarAction action) {
    switch (action) {
    case ToolbarAction::OpenProject:
        return AF_TR("Open Project");
    case ToolbarAction::SaveProject:
        return AF_TR("Save Project");
    case ToolbarAction::Undo:
        return AF_TR("Undo");
    case ToolbarAction::NewSacmFile:
        return AF_TR("New GSN / SACM File");
    case ToolbarAction::FitToView:
        return AF_TR("Fit to view");
    case ToolbarAction::ExportGsnSvg:
        return AF_TR("Export GSN SVG");
    case ToolbarAction::Preferences:
        return AF_TR("Preferences...");
    }
    return {};
}

// Draws one icon button. Returns true when clicked and enabled.
bool DrawButton(const ToolbarButton& button,
                bool enabled,
                float height,
                const char* shortcut,
                bool show_label,
                bool needs_attention) {
    ImGui::PushID(static_cast<int>(button.action));
    if (!enabled)
        ImGui::BeginDisabled();

    const std::string label = TranslateTooltip(button.action);
    const std::string button_text = show_label ? std::string(button.icon) + "  " + label : std::string(button.icon);
    const float width = show_label
                            ? ImGui::CalcTextSize(button_text.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f
                            : ImGui::GetFrameHeight();

    if (enabled && needs_attention) {
        const Theme& theme = GetTheme();
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(WithAlpha(theme.accent, 0.18f)));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(WithAlpha(theme.accent, 0.28f)));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.accent_hover));
    }

    const bool clicked = ImGui::Button(button_text.c_str(), ImVec2(width, height));
    if (enabled && needs_attention) {
        const ImVec2 item_min = ImGui::GetItemRectMin();
        const ImVec2 item_max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(item_max.x - 5.0f, item_min.y + 5.0f), 2.25f, GetTheme().attention);
        ImGui::PopStyleColor(3);
    }

    if (!enabled)
        ImGui::EndDisabled();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        std::string tip = label;
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
        // Always reachable — they are how a user gets out of having no project.
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
                   const char* shortcut,
                   bool show_label,
                   bool needs_attention,
                   const std::function<void()>& handler) {
        // Buttons sit on one row; without this each would start a new line and
        // only the first would be inside the bar's height.
        if (!first)
            ImGui::SameLine(0.0f, 2.0f);
        first = false;
        if (DrawButton(
                {action, icon}, IsActionEnabled(model, action), button_height, shortcut, show_label, needs_attention) &&
            handler) {
            handler();
        }
    };

    run(ToolbarAction::OpenProject, ICON_FA_FOLDER_OPEN, nullptr, true, false, callbacks.open_project);
    run(ToolbarAction::SaveProject, ICON_FA_SAVE, nullptr, true, model.has_unsaved_changes, callbacks.save_project);
    run(ToolbarAction::Undo, ICON_FA_UNDO, "Ctrl+Z", false, false, callbacks.undo);

    DrawSeparator(button_height);
    run(ToolbarAction::NewSacmFile, ICON_FA_PLUS, nullptr, false, false, callbacks.new_sacm_file);
    run(ToolbarAction::FitToView, ICON_FA_EXPAND, nullptr, false, false, callbacks.fit_to_view);
    run(ToolbarAction::ExportGsnSvg, ICON_FA_DOWNLOAD, nullptr, false, false, callbacks.export_gsn_svg);

    // Preferences sits at the far right, away from the document actions.
    ImGui::SameLine();
    const float right = ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right));
    first = true; // Already positioned; do not let `run` add another SameLine.
    run(ToolbarAction::Preferences, ICON_FA_COG, nullptr, false, false, callbacks.open_preferences);

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
