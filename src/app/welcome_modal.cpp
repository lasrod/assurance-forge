#include "app/welcome_modal.h"

#include "imgui.h"
#include "ui/gsn_canvas.h"
#include "ui/theme.h"

namespace {

ImVec4 ToVec4(ImU32 color) {
    return ImGui::ColorConvertU32ToFloat4(color);
}

void SectionTitle(const char* label) {
    ImGui::PushFont(ui::g_BoldFont);
    ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(ui::GetTheme().text_primary));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

bool ActionLink(const char* id, const char* title, const char* subtitle) {
    const ui::Theme& theme = ui::GetTheme();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = subtitle && subtitle[0] ? 44.0f : 30.0f;

    ImGui::InvisibleButton(id, ImVec2(width, height));
    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked();

    if (hovered) {
        draw_list->AddRectFilled(
            pos,
            ImVec2(pos.x + width, pos.y + height),
            ui::WithAlpha(theme.surface_3, 0.72f),
            theme.rounding_ui);
    }

    ImU32 title_color = hovered ? theme.accent_hover : theme.accent;
    draw_list->AddText(ImVec2(pos.x + 8.0f, pos.y + 5.0f), title_color, title);
    if (subtitle && subtitle[0]) {
        draw_list->AddText(ImVec2(pos.x + 8.0f, pos.y + 25.0f), theme.text_secondary, subtitle);
    }

    return clicked;
}

void WalkthroughCard(const char* id, const char* title, const char* subtitle, float progress) {
    const ui::Theme& theme = ui::GetTheme();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = 74.0f;

    ImGui::InvisibleButton(id, ImVec2(width, height));
    bool hovered = ImGui::IsItemHovered();

    ImU32 fill = hovered ? theme.surface_3 : theme.surface_2;
    draw_list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), fill, theme.rounding_ui);
    draw_list->AddRect(pos, ImVec2(pos.x + width, pos.y + height), theme.border, theme.rounding_ui, 0, 1.0f);

    float stripe_width = width * progress;
    draw_list->AddRectFilled(
        ImVec2(pos.x, pos.y + height - 4.0f),
        ImVec2(pos.x + stripe_width, pos.y + height),
        theme.accent,
        theme.rounding_ui,
        ImDrawFlags_RoundCornersBottomLeft);

    draw_list->AddText(ImVec2(pos.x + 14.0f, pos.y + 13.0f), theme.text_primary, title);
    draw_list->AddText(ImVec2(pos.x + 14.0f, pos.y + 38.0f), theme.text_secondary, subtitle);
}

}  // namespace

void ShowWelcomeModal(bool& is_open) {
    if (!is_open) return;

    auto dismiss = [&is_open]() {
        is_open = false;
        ImGui::CloseCurrentPopup();
    };

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(920.0f, 560.0f), ImGuiCond_FirstUseEver);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 24.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 10.0f));

    if (ImGui::BeginPopupModal("Welcome!", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const ui::Theme& theme = ui::GetTheme();

        ImGui::SetWindowFontScale(2.0f);
        ImGui::PushFont(ui::g_BoldFont);
        ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(theme.text_primary));
        ImGui::TextUnformatted("Assurance Forge");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::SetWindowFontScale(1.0f);

        ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(theme.text_secondary));
        ImGui::TextUnformatted("Forge Confidence in Safety");
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0.0f, 20.0f));

        if (ImGui::BeginTable("WelcomeLayout", 2, ImGuiTableFlags_SizingStretchProp, ImVec2(0.0f, 390.0f))) {
            ImGui::TableSetupColumn("StartColumn", ImGuiTableColumnFlags_WidthStretch, 0.48f);
            ImGui::TableSetupColumn("WalkthroughColumn", ImGuiTableColumnFlags_WidthStretch, 0.52f);
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            SectionTitle("Start");
            ImGui::Dummy(ImVec2(0.0f, 8.0f));
            if (ActionLink("##new_project", "New Project...", "Create a new assurance project workspace")) {
                // TODO: Implement project folder creation and af.proj generation.
                dismiss();
            }
            if (ActionLink("##open_project", "Open Project...", "Open an existing Assurance Forge project")) {
                // TODO: Implement opening an existing af.proj project manifest.
                dismiss();
            }
            if (ActionLink("##open_sacm", "Open SACM File...", "Load a SACM XML assurance case directly")) {
                // TODO: Implement direct SACM file picker.
                dismiss();
            }

            ImGui::Dummy(ImVec2(0.0f, 28.0f));
            SectionTitle("Recent");
            ImGui::Dummy(ImVec2(0.0f, 8.0f));
            if (ActionLink("##recent_demo", "Open Autonomy Safety Case", "data/oasc-ja.xml")) {
                dismiss();
            }
            ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(theme.text_muted));
            ImGui::TextUnformatted("More recent projects will appear here.");
            ImGui::PopStyleColor();

            ImGui::TableNextColumn();
            SectionTitle("Walkthroughs");
            ImGui::Dummy(ImVec2(0.0f, 8.0f));
            WalkthroughCard("##walkthrough_get_started", "Get started with Assurance Forge", "Create, inspect, and navigate a safety case", 0.34f);
            ImGui::Dummy(ImVec2(0.0f, 14.0f));
            WalkthroughCard("##walkthrough_fundamentals", "Learn the Fundamentals", "GSN structure, SACM imports, evidence, and registers", 0.18f);
            ImGui::Dummy(ImVec2(0.0f, 14.0f));
            WalkthroughCard("##walkthrough_conformance", "Prepare a Conformance Review", "Trace claims, evidence, and review outputs", 0.12f);

            ImGui::EndTable();
        }

        ImGui::EndPopup();
    } else {
        ImGui::OpenPopup("Welcome!");
    }

    ImGui::PopStyleVar(2);
}
