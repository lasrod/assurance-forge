#include "ui/panels/welcome_modal.h"

#include "imgui.h"
#include "ui/gsn/gsn_canvas.h"
#include "ui/gsn/gsn_dpi.h"
#include "ui/theme.h"

#include <algorithm>
#include <cstdio>

namespace ui::panels {
namespace {

constexpr float kWelcomeBodyFontScale = 1.04f;
constexpr float kWelcomeSectionTitleFontScale = kWelcomeBodyFontScale * 1.15f;
constexpr float kWelcomeTitleFontScale = 2.30f;
constexpr float kWelcomeCenterPivot = 0.5f;

constexpr float kWelcomeViewportPadding = 32.0f;
constexpr float kWelcomeMinWidth = 450.0f;
constexpr float kWelcomeMinHeight = 450.0f;
constexpr float kWelcomeTargetWidthRatio = 0.72f;
constexpr float kWelcomeTargetHeightRatio = 0.78f;
constexpr float kWelcomeHoverSurfaceAlpha = 0.72f;

constexpr float kWelcomeWindowPaddingX = 28.0f;
constexpr float kWelcomeWindowPaddingY = 18.0f;
constexpr float kWelcomeItemSpacingX = 10.0f;
constexpr float kWelcomeItemSpacingY = 6.0f;

constexpr float kWelcomeTaglineTopSpacing = 14.0f;
constexpr float kWelcomeTableHeight = 460.0f;
constexpr float kWelcomeStartColumnRatio = 0.48f;
constexpr float kWelcomeWalkthroughColumnRatio = 0.52f;
constexpr float kWelcomeSectionTopSpacing = 6.0f;
constexpr float kWelcomeRecentSectionSpacing = 18.0f;
constexpr float kWelcomeWalkthroughCardSpacing = 8.0f;

constexpr float kGetStartedProgress = 0.34f;
constexpr float kFundamentalsProgress = 0.18f;
constexpr float kConformanceProgress = 0.12f;

constexpr int kWelcomeStyleVarCount = 2;

float Px(float reference_pixels) {
    return ui::gsn::DpiSize(reference_pixels);
}

ImVec4 ToVec4(ImU32 color) {
    return ImGui::ColorConvertU32ToFloat4(color);
}

void SectionTitle(const char* label) {
    ImGui::SetWindowFontScale(kWelcomeSectionTitleFontScale);
    ImGui::PushFont(ui::gsn::g_BoldFont);
    ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(ui::GetTheme().text_primary));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SetWindowFontScale(kWelcomeBodyFontScale);
}

bool ActionLink(const char* id, const char* title, const char* subtitle) {
    constexpr float kLinkHeightWithSubtitle = 50.0f;
    constexpr float kLinkHeightWithoutSubtitle = 34.0f;
    constexpr float kLinkTextOffsetX = 10.0f;
    constexpr float kLinkTitleOffsetY = 6.0f;
    constexpr float kLinkSubtitleOffsetY = 24.0f;

    const ui::Theme& theme = ui::GetTheme();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = subtitle && subtitle[0] ? Px(kLinkHeightWithSubtitle) : Px(kLinkHeightWithoutSubtitle);

    ImGui::InvisibleButton(id, ImVec2(width, height));
    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked();

    if (hovered) {
        draw_list->AddRectFilled(
            pos,
            ImVec2(pos.x + width, pos.y + height),
            ui::WithAlpha(theme.surface_3, kWelcomeHoverSurfaceAlpha),
            theme.rounding_ui);
    }

    ImU32 title_color = hovered ? theme.accent_hover : theme.accent;
    draw_list->AddText(ImVec2(pos.x + Px(kLinkTextOffsetX), pos.y + Px(kLinkTitleOffsetY)), title_color, title);
    if (subtitle && subtitle[0]) {
        draw_list->AddText(ImVec2(pos.x + Px(kLinkTextOffsetX), pos.y + Px(kLinkSubtitleOffsetY)), theme.text_secondary, subtitle);
    }

    return clicked;
}

bool RecentLink(const char* id, const RecentProjectEntry& entry) {
    constexpr float kRecentItemHeight = 72.0f;
    constexpr float kRecentTextOffsetX = 10.0f;
    constexpr float kRecentNameOffsetY = 6.0f;
    constexpr float kRecentStatsOffsetY = 28.0f;
    constexpr float kRecentPathOffsetY = 50.0f;
    constexpr int kRecentStatsBufferSize = 128;

    const ui::Theme& theme = ui::GetTheme();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    const float height = Px(kRecentItemHeight);

    ImGui::InvisibleButton(id, ImVec2(width, height));
    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked();

    if (hovered) {
        draw_list->AddRectFilled(
            pos,
            ImVec2(pos.x + width, pos.y + height),
                ui::WithAlpha(theme.surface_3, kWelcomeHoverSurfaceAlpha),
            theme.rounding_ui);
    }

    ImU32 name_color = hovered ? theme.accent_hover : theme.accent;
            draw_list->AddText(ImVec2(pos.x + Px(kRecentTextOffsetX), pos.y + Px(kRecentNameOffsetY)), name_color, entry.name.c_str());

            char stats[kRecentStatsBufferSize];
    std::snprintf(stats, sizeof(stats),
        "%d claims \xC2\xB7 %d strategies \xC2\xB7 %d evidence \xC2\xB7 %d undeveloped",
        entry.claims, entry.strategies, entry.evidence, entry.undeveloped);
            draw_list->AddText(ImVec2(pos.x + Px(kRecentTextOffsetX), pos.y + Px(kRecentStatsOffsetY)), theme.text_secondary, stats);
            draw_list->AddText(ImVec2(pos.x + Px(kRecentTextOffsetX), pos.y + Px(kRecentPathOffsetY)), theme.text_muted, entry.path.c_str());

    return clicked;
}

void WalkthroughCard(const char* id, const char* title, const char* subtitle, float progress) {
    constexpr float kCardHeight = 84.0f;
    constexpr float kCardBorderThickness = 1.0f;
    constexpr float kCardStripeHeight = 4.0f;
    constexpr float kCardTextOffsetX = 16.0f;
    constexpr float kCardTitleOffsetY = 13.0f;
    constexpr float kCardSubtitleOffsetY = 43.0f;

    const ui::Theme& theme = ui::GetTheme();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = Px(kCardHeight);

    ImGui::InvisibleButton(id, ImVec2(width, height));
    bool hovered = ImGui::IsItemHovered();

    ImU32 fill = hovered ? theme.surface_3 : theme.surface_2;
    draw_list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), fill, theme.rounding_ui);
    draw_list->AddRect(pos, ImVec2(pos.x + width, pos.y + height), theme.border, theme.rounding_ui, 0, Px(kCardBorderThickness));

    float stripe_width = width * progress;
    draw_list->AddRectFilled(
        ImVec2(pos.x, pos.y + height - Px(kCardStripeHeight)),
        ImVec2(pos.x + stripe_width, pos.y + height),
        theme.accent,
        theme.rounding_ui,
        ImDrawFlags_RoundCornersBottomLeft);

    draw_list->AddText(ImVec2(pos.x + Px(kCardTextOffsetX), pos.y + Px(kCardTitleOffsetY)), theme.text_primary, title);
    draw_list->AddText(ImVec2(pos.x + Px(kCardTextOffsetX), pos.y + Px(kCardSubtitleOffsetY)), theme.text_secondary, subtitle);
}

}  // namespace

void ShowWelcomeModal(bool& is_open,
                      const std::vector<RecentProjectEntry>& recent,
                      const WelcomeModalCallbacks& callbacks) {
    if (is_open && !ImGui::IsPopupOpen("Welcome!")) {
        ImGui::OpenPopup("Welcome!");
    }

    if (!is_open && !ImGui::IsPopupOpen("Welcome!")) {
        return;
    }

    auto dismiss = [&is_open]() {
        is_open = false;
        ImGui::CloseCurrentPopup();
    };

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 viewport_center(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                           viewport->WorkPos.y + viewport->WorkSize.y * 0.5f);
    ImGui::SetNextWindowPos(viewport_center, ImGuiCond_Always, ImVec2(kWelcomeCenterPivot, kWelcomeCenterPivot));
    ImVec2 viewport_size = viewport->WorkSize;
    float viewport_padding = Px(kWelcomeViewportPadding);
    float desired_width = std::max(Px(kWelcomeMinWidth), viewport_size.x * kWelcomeTargetWidthRatio);
    float desired_height = std::max(Px(kWelcomeMinHeight), viewport_size.y * kWelcomeTargetHeightRatio);
    float max_width = std::max(Px(kWelcomeMinWidth), viewport_size.x - viewport_padding);
    float max_height = std::max(Px(kWelcomeMinHeight), viewport_size.y - viewport_padding);
    ImGui::SetNextWindowSize(ImVec2(std::min(desired_width, max_width),
                                    std::min(desired_height, max_height)),
                             ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Px(kWelcomeWindowPaddingX), Px(kWelcomeWindowPaddingY)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(Px(kWelcomeItemSpacingX), Px(kWelcomeItemSpacingY)));

    if (ImGui::BeginPopupModal("Welcome!", &is_open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        const ui::Theme& theme = ui::GetTheme();

        ImGui::SetWindowFontScale(kWelcomeBodyFontScale);

        ImGui::SetWindowFontScale(kWelcomeTitleFontScale);
        ImGui::PushFont(ui::gsn::g_BoldFont);
        ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(theme.text_primary));
        ImGui::TextUnformatted("Assurance Forge");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::SetWindowFontScale(kWelcomeBodyFontScale);

        ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(theme.text_secondary));
        ImGui::TextUnformatted("Forge Confidence in Safety");
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0.0f, Px(kWelcomeTaglineTopSpacing)));

        if (ImGui::BeginTable("WelcomeLayout", 2, ImGuiTableFlags_SizingStretchProp, ImVec2(0.0f, Px(kWelcomeTableHeight)))) {
            ImGui::TableSetupColumn("StartColumn", ImGuiTableColumnFlags_WidthStretch, kWelcomeStartColumnRatio);
            ImGui::TableSetupColumn("WalkthroughColumn", ImGuiTableColumnFlags_WidthStretch, kWelcomeWalkthroughColumnRatio);
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            SectionTitle("Start");
            ImGui::Dummy(ImVec2(0.0f, Px(kWelcomeSectionTopSpacing)));
            if (ActionLink("##create_empty", "Create Empty Assurance Project",
                           "Start with a blank assurance project workspace")) {
                if (callbacks.create_empty_project) callbacks.create_empty_project();
                dismiss();
            }
            if (ActionLink("##create_template", "Create Assurance Project from Template",
                           "Create a project from a predefined assurance case template")) {
                if (callbacks.create_project_from_template) callbacks.create_project_from_template();
                dismiss();
            }
            if (ActionLink("##open_project", "Open Project",
                           "Open an existing Assurance Forge project")) {
                if (callbacks.open_project) callbacks.open_project();
                dismiss();
            }
            if (ActionLink("##import_sacm", "Import SACM",
                           "Import a SACM XML assurance case")) {
                if (callbacks.import_sacm) callbacks.import_sacm();
                dismiss();
            }

            ImGui::Dummy(ImVec2(0.0f, Px(kWelcomeRecentSectionSpacing)));
            SectionTitle("Open Recent Projects");
            ImGui::Dummy(ImVec2(0.0f, Px(kWelcomeSectionTopSpacing)));
            if (recent.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(theme.text_muted));
                ImGui::TextUnformatted("No recent projects.");
                ImGui::PopStyleColor();
            } else {
                for (int i = 0; i < static_cast<int>(recent.size()); ++i) {
                    char row_id[32];
                    std::snprintf(row_id, sizeof(row_id), "##recent_%d", i);
                    if (RecentLink(row_id, recent[i])) {
                        if (callbacks.open_recent_project) callbacks.open_recent_project(recent[i]);
                        dismiss();
                    }
                }
            }

            ImGui::TableNextColumn();
            SectionTitle("Walkthroughs");
            ImGui::Dummy(ImVec2(0.0f, Px(kWelcomeSectionTopSpacing)));
            WalkthroughCard("##walkthrough_get_started", "Get started with Assurance Forge", "Create, inspect, and navigate a safety case", kGetStartedProgress);
            ImGui::Dummy(ImVec2(0.0f, Px(kWelcomeWalkthroughCardSpacing)));
            WalkthroughCard("##walkthrough_fundamentals", "Learn the Fundamentals", "GSN structure, SACM imports, evidence, and registers", kFundamentalsProgress);
            ImGui::Dummy(ImVec2(0.0f, Px(kWelcomeWalkthroughCardSpacing)));
            WalkthroughCard("##walkthrough_conformance", "Prepare a Conformance Review", "Trace claims, evidence, and review outputs", kConformanceProgress);

            ImGui::EndTable();
        }

        ImGui::EndPopup();
    }

    if (!ImGui::IsPopupOpen("Welcome!")) {
        is_open = false;
    }

    ImGui::PopStyleVar(kWelcomeStyleVarCount);
}

}  // namespace ui::panels
