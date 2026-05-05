#include "ui/panels/welcome_modal.h"

#include "imgui.h"
#include "ui/gsn/gsn_canvas.h"
#include "ui/gsn/gsn_dpi.h"
#include "ui/localization.h"
#include "ui/theme.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>

namespace ui::panels {
namespace {

constexpr float kWelcomeBodyFontScale = 1.04f;
constexpr float kWelcomeSectionTitleFontScale = kWelcomeBodyFontScale * 1.15f;
constexpr float kWelcomeTitleFontScale = 2.30f;
constexpr float kWelcomeWindowAnchor = 0.0f;
constexpr float kWelcomeHoverSurfaceAlpha = 0.72f;

constexpr float kWelcomeWindowPaddingX = 28.0f;
constexpr float kWelcomeWindowPaddingY = 18.0f;
constexpr float kWelcomeItemSpacingX = 10.0f;
constexpr float kWelcomeItemSpacingY = 6.0f;

constexpr float kWelcomeTaglineTopSpacing = 14.0f;
constexpr float kWelcomeTableHeight = 460.0f;
constexpr float kWelcomeTableMaxWidth = 1320.0f;
constexpr float kWelcomeStartColumnRatio = 0.48f;
constexpr float kWelcomeWalkthroughColumnRatio = 0.52f;
constexpr float kWelcomeSectionTopSpacing = 6.0f;
constexpr float kWelcomeRecentSectionSpacing = 18.0f;
constexpr float kWelcomeWalkthroughCardSpacing = 8.0f;

constexpr float kGetStartedProgress = 0.34f;
constexpr float kFundamentalsProgress = 0.18f;
constexpr float kConformanceProgress = 0.12f;

constexpr const char* kWelcomePopupId = "Welcome!";
constexpr const char* kWelcomeLayoutTableId = "WelcomeLayout";
constexpr const char* kWelcomeStartColumnId = "StartColumn";
constexpr const char* kWelcomeWalkthroughColumnId = "WalkthroughColumn";
constexpr const char* kTemplatePopupId = "CreateTemplate##not_implemented_popup";
constexpr const char* kWalkthroughPopupId = "Walkthroughs##not_implemented_popup";
constexpr const char* kImportSacmPopupId = "ImportSACM##not_implemented_popup";
constexpr float kNotImplementedPopupButtonWidth = 110.0f;

constexpr const char* kUtf8MiddleDot = "\xC2\xB7";

constexpr int kWelcomeStyleVarCount = 2;

struct ItemInteraction {
    bool hovered;
    bool clicked;
};

float Px(float reference_pixels) {
    return ui::gsn::DpiSize(reference_pixels);
}

float DefaultUiScale() {
    const ImGuiIO& io = ImGui::GetIO();
    const float dpi_scale_x = io.DisplayFramebufferScale.x;
    const float dpi_scale_y = io.DisplayFramebufferScale.y;
    const float dpi_scale = std::max(dpi_scale_x, dpi_scale_y);
    return dpi_scale > 0.0f ? dpi_scale : 1.0f;
}

float PxAtScale(float reference_pixels, float scale) {
    return reference_pixels * scale;
}

ImVec4 ToVec4(ImU32 color) {
    return ImGui::ColorConvertU32ToFloat4(color);
}

ItemInteraction InvisibleButtonInteraction(std::string_view id, const ImVec2& size) {
    const std::string id_string(id);
    ImGui::InvisibleButton(id_string.c_str(), size);
    return ItemInteraction{ImGui::IsItemHovered(), ImGui::IsItemClicked()};
}

void DrawText(ImDrawList* draw_list, const ImVec2& pos, ImU32 color, std::string_view text) {
    draw_list->AddText(pos, color, text.data(), text.data() + text.size());
}

std::string FormatRecentStats(const RecentProjectEntry& entry) {
    constexpr int kRecentStatsBufferSize = 128;
    char stats[kRecentStatsBufferSize];
    std::snprintf(stats,
                  sizeof(stats),
                  "%d claims %s %d strategies %s %d evidence %s %d undeveloped",
                  entry.claims,
                  kUtf8MiddleDot,
                  entry.strategies,
                  kUtf8MiddleDot,
                  entry.evidence,
                  kUtf8MiddleDot,
                  entry.undeveloped);
    return std::string(stats);
}

void SectionTitle(std::string_view label) {
    ImGui::SetWindowFontScale(kWelcomeSectionTitleFontScale);
    ImGui::PushFont(ui::gsn::g_BoldFont);
    ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(ui::GetTheme().text_primary));
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SetWindowFontScale(kWelcomeBodyFontScale);
}

bool ActionLink(std::string_view id, std::string_view title, std::string_view subtitle) {
    constexpr float kLinkHeightWithSubtitle = 50.0f;
    constexpr float kLinkHeightWithoutSubtitle = 34.0f;
    constexpr float kLinkTextOffsetX = 10.0f;
    constexpr float kLinkTitleOffsetY = 6.0f;
    constexpr float kLinkSubtitleOffsetY = 24.0f;

    const ui::Theme& theme = ui::GetTheme();
    ImDrawList* const draw_list = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const bool has_subtitle = !subtitle.empty();
    const float height = has_subtitle ? Px(kLinkHeightWithSubtitle) : Px(kLinkHeightWithoutSubtitle);
    const ItemInteraction interaction = InvisibleButtonInteraction(id, ImVec2(width, height));

    if (interaction.hovered) {
        draw_list->AddRectFilled(pos,
                                 ImVec2(pos.x + width, pos.y + height),
                                 ui::WithAlpha(theme.surface_3, kWelcomeHoverSurfaceAlpha),
                                 theme.rounding_ui);
    }

    const ImU32 title_color = interaction.hovered ? theme.accent_hover : theme.accent;
    DrawText(draw_list, ImVec2(pos.x + Px(kLinkTextOffsetX), pos.y + Px(kLinkTitleOffsetY)), title_color, title);
    if (has_subtitle) {
        DrawText(draw_list,
                 ImVec2(pos.x + Px(kLinkTextOffsetX), pos.y + Px(kLinkSubtitleOffsetY)),
                 theme.text_secondary,
                 subtitle);
    }

    return interaction.clicked;
}

bool RecentLink(std::string_view id, const RecentProjectEntry& entry) {
    constexpr float kRecentItemHeight = 72.0f;
    constexpr float kRecentTextOffsetX = 10.0f;
    constexpr float kRecentNameOffsetY = 6.0f;
    constexpr float kRecentStatsOffsetY = 24.0f;
    constexpr float kRecentPathOffsetY = 40.0f;
    const ui::Theme& theme = ui::GetTheme();
    ImDrawList* const draw_list = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = Px(kRecentItemHeight);
    const ItemInteraction interaction = InvisibleButtonInteraction(id, ImVec2(width, height));

    if (interaction.hovered) {
        draw_list->AddRectFilled(pos,
                                 ImVec2(pos.x + width, pos.y + height),
                                 ui::WithAlpha(theme.surface_3, kWelcomeHoverSurfaceAlpha),
                                 theme.rounding_ui);
    }

    const ImU32 name_color = interaction.hovered ? theme.accent_hover : theme.accent;
    DrawText(draw_list, ImVec2(pos.x + Px(kRecentTextOffsetX), pos.y + Px(kRecentNameOffsetY)), name_color, entry.name);

    const std::string stats = FormatRecentStats(entry);
    DrawText(draw_list,
             ImVec2(pos.x + Px(kRecentTextOffsetX), pos.y + Px(kRecentStatsOffsetY)),
             theme.text_secondary,
             stats);
    DrawText(draw_list,
             ImVec2(pos.x + Px(kRecentTextOffsetX), pos.y + Px(kRecentPathOffsetY)),
             theme.text_muted,
             entry.path);

    return interaction.clicked;
}

bool WalkthroughCard(std::string_view id, std::string_view title, std::string_view subtitle, float progress) {
    constexpr float kCardHeight = 84.0f;
    constexpr float kCardBorderThickness = 1.0f;
    constexpr float kCardStripeHeight = 4.0f;
    constexpr float kCardTextOffsetX = 16.0f;
    constexpr float kCardTitleOffsetY = 13.0f;
    constexpr float kCardSubtitleOffsetY = 43.0f;

    const ui::Theme& theme = ui::GetTheme();
    ImDrawList* const draw_list = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = Px(kCardHeight);
    const ItemInteraction interaction = InvisibleButtonInteraction(id, ImVec2(width, height));

    const ImU32 fill = interaction.hovered ? theme.surface_3 : theme.surface_2;
    draw_list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), fill, theme.rounding_ui);
    draw_list->AddRect(
        pos, ImVec2(pos.x + width, pos.y + height), theme.border, theme.rounding_ui, 0, Px(kCardBorderThickness));

    const float stripe_width = width * progress;
    draw_list->AddRectFilled(ImVec2(pos.x, pos.y + height - Px(kCardStripeHeight)),
                             ImVec2(pos.x + stripe_width, pos.y + height),
                             theme.accent,
                             theme.rounding_ui,
                             ImDrawFlags_RoundCornersBottomLeft);

    DrawText(draw_list, ImVec2(pos.x + Px(kCardTextOffsetX), pos.y + Px(kCardTitleOffsetY)), theme.text_primary, title);
    DrawText(draw_list,
             ImVec2(pos.x + Px(kCardTextOffsetX), pos.y + Px(kCardSubtitleOffsetY)),
             theme.text_secondary,
             subtitle);

    return interaction.clicked;
}

} // namespace

void ShowWelcomeModal(bool& is_open,
                      const std::vector<RecentProjectEntry>& recent,
                      const WelcomeModalCallbacks& callbacks) {
    if (is_open && !ImGui::IsPopupOpen(kWelcomePopupId)) {
        ImGui::OpenPopup(kWelcomePopupId);
    }

    if (!is_open && !ImGui::IsPopupOpen(kWelcomePopupId)) {
        return;
    }

    auto dismiss = [&is_open]() {
        is_open = false;
        ImGui::CloseCurrentPopup();
    };

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 viewport_pos = viewport->WorkPos;
    const ImVec2 viewport_size = viewport->WorkSize;
    const float welcome_layout_scale = DefaultUiScale() * kWelcomeBodyFontScale;
    ImGui::SetNextWindowPos(viewport_pos, ImGuiCond_Always, ImVec2(kWelcomeWindowAnchor, kWelcomeWindowAnchor));
    ImGui::SetNextWindowSize(viewport_size, ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(PxAtScale(kWelcomeWindowPaddingX, welcome_layout_scale),
                               PxAtScale(kWelcomeWindowPaddingY, welcome_layout_scale)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(PxAtScale(kWelcomeItemSpacingX, welcome_layout_scale),
                               PxAtScale(kWelcomeItemSpacingY, welcome_layout_scale)));

    if (ImGui::BeginPopupModal(kWelcomePopupId,
                               &is_open,
                               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoSavedSettings)) {
        const ui::Theme& theme = ui::GetTheme();
        bool show_template_not_implemented = false;
        bool show_import_sacm_not_implemented = false;
        bool show_walkthrough_not_implemented = false;

        ImGui::SetWindowFontScale(kWelcomeBodyFontScale);

        ImGui::SetWindowFontScale(kWelcomeTitleFontScale);
        ImGui::PushFont(ui::gsn::g_BoldFont);
        ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(theme.text_primary));
        ImGui::TextUnformatted("Assurance Forge");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::SetWindowFontScale(kWelcomeBodyFontScale);

        ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(theme.text_secondary));
        ImGui::TextUnformatted(ui::Tr(ui::MessageId::WelcomeTagline));
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0.0f, Px(kWelcomeTaglineTopSpacing)));

        const float layout_width = std::min(ImGui::GetContentRegionAvail().x, Px(kWelcomeTableMaxWidth));
        if (ImGui::BeginTable(kWelcomeLayoutTableId,
                              2,
                              ImGuiTableFlags_SizingStretchProp,
                              ImVec2(layout_width, Px(kWelcomeTableHeight)))) {
            ImGui::TableSetupColumn(
                kWelcomeStartColumnId, ImGuiTableColumnFlags_WidthStretch, kWelcomeStartColumnRatio);
            ImGui::TableSetupColumn(
                kWelcomeWalkthroughColumnId, ImGuiTableColumnFlags_WidthStretch, kWelcomeWalkthroughColumnRatio);
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            SectionTitle(ui::Tr(ui::MessageId::WelcomeStartSection));
            ImGui::Dummy(ImVec2(0.0f, Px(kWelcomeSectionTopSpacing)));
            if (ActionLink("##create_empty",
                           ui::Tr(ui::MessageId::WelcomeActionCreateEmptyTitle),
                           ui::Tr(ui::MessageId::WelcomeActionCreateEmptySubtitle))) {
                if (callbacks.create_empty_project)
                    callbacks.create_empty_project();
            }
            if (ActionLink("##create_template",
                           ui::Tr(ui::MessageId::WelcomeActionCreateFromTemplateTitle),
                           ui::Tr(ui::MessageId::WelcomeActionCreateFromTemplateSubtitle))) {
                if (callbacks.create_project_from_template)
                    callbacks.create_project_from_template();
                show_template_not_implemented = true;
            }
            if (ActionLink("##open_project",
                           ui::Tr(ui::MessageId::WelcomeActionOpenProjectTitle),
                           ui::Tr(ui::MessageId::WelcomeActionOpenProjectSubtitle))) {
                if (callbacks.open_project)
                    callbacks.open_project();
            }
            if (ActionLink("##import_sacm",
                           ui::Tr(ui::MessageId::WelcomeActionImportSacmTitle),
                           ui::Tr(ui::MessageId::WelcomeActionImportSacmSubtitle))) {
                if (callbacks.import_sacm)
                    callbacks.import_sacm();
                show_import_sacm_not_implemented = true;
            }

            ImGui::Dummy(ImVec2(0.0f, Px(kWelcomeRecentSectionSpacing)));
            SectionTitle(ui::Tr(ui::MessageId::WelcomeOpenRecentProjectsSection));
            ImGui::Dummy(ImVec2(0.0f, Px(kWelcomeSectionTopSpacing)));
            if (recent.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(theme.text_muted));
                ImGui::TextUnformatted(ui::Tr(ui::MessageId::WelcomeNoRecentProjects));
                ImGui::PopStyleColor();
            } else {
                for (int i = 0; i < static_cast<int>(recent.size()); ++i) {
                    char row_id[32];
                    std::snprintf(row_id, sizeof(row_id), "##recent_%d", i);
                    if (RecentLink(row_id, recent[i])) {
                        if (callbacks.open_recent_project)
                            callbacks.open_recent_project(recent[i]);
                        dismiss();
                    }
                }
            }

            ImGui::TableNextColumn();
            SectionTitle(ui::Tr(ui::MessageId::Walkthroughs));
            ImGui::Dummy(ImVec2(0.0f, Px(kWelcomeSectionTopSpacing)));
            if (WalkthroughCard("##walkthrough_get_started",
                                ui::Tr(ui::MessageId::WelcomeWalkthroughGetStartedTitle),
                                ui::Tr(ui::MessageId::WelcomeWalkthroughGetStartedSubtitle),
                                kGetStartedProgress)) {
                if (callbacks.walkthrough_get_started)
                    callbacks.walkthrough_get_started();
                show_walkthrough_not_implemented = true;
            }
            ImGui::Dummy(ImVec2(0.0f, Px(kWelcomeWalkthroughCardSpacing)));
            if (WalkthroughCard("##walkthrough_fundamentals",
                                ui::Tr(ui::MessageId::WelcomeWalkthroughFundamentalsTitle),
                                ui::Tr(ui::MessageId::WelcomeWalkthroughFundamentalsSubtitle),
                                kFundamentalsProgress)) {
                if (callbacks.walkthrough_fundamentals)
                    callbacks.walkthrough_fundamentals();
                show_walkthrough_not_implemented = true;
            }
            ImGui::Dummy(ImVec2(0.0f, Px(kWelcomeWalkthroughCardSpacing)));
            if (WalkthroughCard("##walkthrough_conformance",
                                ui::Tr(ui::MessageId::WelcomeWalkthroughConformanceTitle),
                                ui::Tr(ui::MessageId::WelcomeWalkthroughConformanceSubtitle),
                                kConformanceProgress)) {
                if (callbacks.walkthrough_conformance)
                    callbacks.walkthrough_conformance();
                show_walkthrough_not_implemented = true;
            }

            ImGui::EndTable();
        }

        if (show_template_not_implemented) {
            ImGui::OpenPopup(kTemplatePopupId);
        }
        if (show_import_sacm_not_implemented) {
            ImGui::OpenPopup(kImportSacmPopupId);
        }
        if (show_walkthrough_not_implemented) {
            ImGui::OpenPopup(kWalkthroughPopupId);
        }

        if (ImGui::BeginPopupModal(kTemplatePopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(ui::Tr(ui::MessageId::WelcomeActionCreateFromTemplateTitle));
            ImGui::Separator();
            ImGui::TextUnformatted(ui::Tr(ui::MessageId::WelcomeActionCreateFromTemplateNotImplemented));
            ImGui::Spacing();
            const float button_width = Px(kNotImplementedPopupButtonWidth);
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - button_width) * 0.5f);
            if (ImGui::Button(ui::Tr(ui::MessageId::Ok), ImVec2(button_width, 0.0f))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal(kImportSacmPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(ui::Tr(ui::MessageId::ImportSacm));
            ImGui::Separator();
            ImGui::TextUnformatted(ui::Tr(ui::MessageId::ImportSacmNotImplemented));
            ImGui::Spacing();
            const float button_width = Px(kNotImplementedPopupButtonWidth);
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - button_width) * 0.5f);
            if (ImGui::Button(ui::Tr(ui::MessageId::Ok), ImVec2(button_width, 0.0f))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal(kWalkthroughPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(ui::Tr(ui::MessageId::Walkthroughs));
            ImGui::Separator();
            ImGui::TextUnformatted(ui::Tr(ui::MessageId::WalkthroughsNotImplemented));
            ImGui::Spacing();
            const float button_width = Px(kNotImplementedPopupButtonWidth);
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - button_width) * 0.5f);
            if (ImGui::Button(ui::Tr(ui::MessageId::Ok), ImVec2(button_width, 0.0f))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::EndPopup();
    }

    if (!ImGui::IsPopupOpen(kWelcomePopupId)) {
        is_open = false;
    }

    ImGui::PopStyleVar(kWelcomeStyleVarCount);
}

} // namespace ui::panels
