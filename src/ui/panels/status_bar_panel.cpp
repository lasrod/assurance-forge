#include "ui/panels/status_bar_panel.h"

#include "ui/fonts.h"
#include "ui/i18n/localization.h"
#include "ui/theme.h"
#include "ui/widgets/text_ellipsis.h"

#include "hello_imgui/icons_font_awesome_4.h"
#include "imgui.h"

#include <algorithm>
#include <string>

namespace ui::panels {
namespace {

constexpr float kSegmentGap = 16.0f;

// Draws right-aligned text ending at `right_x`, and returns the x where it
// starts so the next segment can be placed to its left.
float DrawRightAligned(ImDrawList* draw_list, float right_x, float text_y, ImU32 color, const std::string& text) {
    if (text.empty())
        return right_x;
    const float width = ImGui::CalcTextSize(text.c_str()).x;
    const float x = right_x - width;
    draw_list->AddText(ImVec2(x, text_y), color, text.c_str());
    return x;
}

bool HoveredIn(float x0, float x1, float y0, float y1) {
    return ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(ImVec2(x0, y0), ImVec2(x1, y1), false);
}

} // namespace

std::string DocumentLabel(const StatusBarModel& model) {
    if (!model.has_project && model.document_name.empty())
        return AF_TR("No project open");
    if (model.document_name.empty())
        return model.project_name;
    if (model.project_name.empty())
        return model.document_name;
    return model.project_name + "  —  " + model.document_name;
}

std::string SaveStateLabel(const StatusBarModel& model) {
    if (!model.has_project && model.document_name.empty())
        return {};
    return model.has_unsaved_changes ? AF_TR("Unsaved changes") : AF_TR("Saved");
}

float StatusBarHeight() {
    return ImGui::GetFrameHeight();
}

void ShowStatusBar(const StatusBarModel& model, const StatusBarCallbacks& callbacks) {
    const Theme& theme = GetTheme();
    const float height = StatusBarHeight();
    const ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(0.0f, io.DisplaySize.y - height));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(theme.surface_1));

    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    ImGui::Begin("##status_bar", nullptr, kFlags);

    // Braced so the font is popped before End(): an ImGui window must be closed
    // with the font stack at the depth it was opened with.
    {
        // Everything is drawn straight to the draw list: a status bar is a readout,
        // and real widgets here would take focus and keyboard navigation away from
        // the panels that need them.
        fonts::Scoped caption(fonts::Role::Caption);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetWindowPos();
        const float text_y = origin.y + (height - ImGui::GetTextLineHeight()) * 0.5f;
        const float pad = ImGui::GetStyle().FramePadding.x + kSegmentGap * 0.5f;

        // A hairline above separates the bar from the panels; without it the bar
        // reads as more content rather than as chrome.
        draw_list->AddLine(
            ImVec2(origin.x, origin.y), ImVec2(origin.x + io.DisplaySize.x, origin.y), theme.border, 1.0f);

        // ===== Right edge inward: problem counts, then selection =====
        float right_cursor = origin.x + io.DisplaySize.x - pad;

        float counts_left = right_cursor;
        if (model.error_count > 0 || model.warning_count > 0) {
            if (model.warning_count > 0) {
                const std::string warnings =
                    std::string(ICON_FA_EXCLAMATION_TRIANGLE) + " " + std::to_string(model.warning_count);
                counts_left =
                    DrawRightAligned(draw_list, counts_left, text_y, theme.warning, warnings) - kSegmentGap * 0.5f;
            }
            if (model.error_count > 0) {
                const std::string errors = std::string(ICON_FA_TIMES_CIRCLE) + " " + std::to_string(model.error_count);
                counts_left = DrawRightAligned(draw_list, counts_left, text_y, theme.danger, errors);
            }

            if (HoveredIn(counts_left, right_cursor, origin.y, origin.y + height)) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::SetTooltip("%s", AF_TR("Open the Problems panel").c_str());
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && callbacks.open_problems)
                    callbacks.open_problems();
            }
            right_cursor = counts_left - kSegmentGap;
        }

        if (!model.selected_element_id.empty()) {
            const std::string selection = ui::i18n::trf("Selected: {0}", model.selected_element_id);
            right_cursor =
                DrawRightAligned(draw_list, right_cursor, text_y, theme.text_secondary, selection) - kSegmentGap;
        }

        // ===== Left edge outward: document, save state, then the message =====
        float x = origin.x + pad;
        const std::string document = DocumentLabel(model);
        const float document_start = x;
        x += widgets::Ellipsize(document, right_cursor - x).width;
        widgets::AddTextEllipsized(
            draw_list, ImVec2(document_start, text_y), theme.text_primary, document, right_cursor - document_start);
        if (!model.document_full_path.empty() && HoveredIn(document_start, x, origin.y, origin.y + height))
            ImGui::SetTooltip("%s", model.document_full_path.c_str());
        x += kSegmentGap;

        const std::string save_state = SaveStateLabel(model);
        if (!save_state.empty() && x < right_cursor) {
            const ImU32 color = model.has_unsaved_changes ? theme.attention : theme.text_muted;
            if (model.has_unsaved_changes) {
                // A dot as well as the word: the colour alone would carry the state,
                // and colour alone is not something every reader can act on.
                const float radius = ImGui::GetFontSize() * 0.22f;
                draw_list->AddCircleFilled(
                    ImVec2(x + radius, text_y + ImGui::GetTextLineHeight() * 0.5f), radius, color);
                x += radius * 2.0f + 6.0f;
            }
            draw_list->AddText(ImVec2(x, text_y), color, save_state.c_str());
            x += ImGui::CalcTextSize(save_state.c_str()).x + kSegmentGap;
        }

        if (!model.message.empty() && x < right_cursor) {
            const ImU32 color = model.message_is_error ? theme.danger : theme.text_secondary;
            const bool truncated =
                widgets::AddTextEllipsized(draw_list, ImVec2(x, text_y), color, model.message, right_cursor - x);
            if (truncated && HoveredIn(x, right_cursor, origin.y, origin.y + height))
                ImGui::SetTooltip("%s", model.message.c_str());
        }
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

} // namespace ui::panels
