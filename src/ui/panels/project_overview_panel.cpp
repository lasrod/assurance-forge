#include "ui/panels/project_overview_panel.h"

#include "ui/i18n/localization.h"
#include "ui/theme.h"

#include "hello_imgui/icons_font_awesome_4.h"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace ui::panels {
namespace {

struct MetricCard {
    const char* id;
    const char* icon;
    std::string title;
    std::string value;
    std::string detail;
    ImU32 accent;
    std::function<void()> action;
};

void DrawText(ImDrawList* draw_list, const ImVec2& position, ImU32 color, std::string_view text) {
    draw_list->AddText(position, color, text.data(), text.data() + text.size());
}

bool DrawMetricCard(const MetricCard& card, float width) {
    constexpr float kCardHeight = 116.0f;
    const ui::Theme& theme = ui::GetTheme();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 position = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(card.id, ImVec2(width, kCardHeight));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();

    const ImU32 fill = hovered ? theme.surface_3 : theme.surface_2;
    draw_list->AddRectFilled(
        position, ImVec2(position.x + width, position.y + kCardHeight), fill, theme.rounding_panel);
    draw_list->AddRect(position,
                       ImVec2(position.x + width, position.y + kCardHeight),
                       hovered ? theme.border_strong : theme.border,
                       theme.rounding_panel);
    draw_list->AddRectFilled(position,
                             ImVec2(position.x + 4.0f, position.y + kCardHeight),
                             card.accent,
                             theme.rounding_panel,
                             ImDrawFlags_RoundCornersLeft);

    DrawText(draw_list, ImVec2(position.x + 16.0f, position.y + 14.0f), card.accent, card.icon);
    DrawText(draw_list, ImVec2(position.x + 42.0f, position.y + 14.0f), theme.text_secondary, card.title);
    draw_list->AddText(ImGui::GetFont(),
                       ImGui::GetFontSize() * 1.45f,
                       ImVec2(position.x + 16.0f, position.y + 45.0f),
                       theme.text_primary,
                       card.value.c_str());
    DrawText(draw_list, ImVec2(position.x + 16.0f, position.y + 87.0f), theme.text_muted, card.detail);
    return clicked;
}

void AttentionRow(
    const char* icon, const std::string& message, ImU32 color, const std::function<void()>& action, const char* id) {
    ImGui::PushID(id);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color), "%s", icon);
    ImGui::SameLine();
    if (action) {
        if (ImGui::Selectable(message.c_str()))
            action();
    } else {
        ImGui::TextUnformatted(message.c_str());
    }
    ImGui::PopID();
}

} // namespace

void ShowProjectOverviewPanel(const ProjectOverviewPanelModel& model, const ProjectOverviewPanelCallbacks& callbacks) {
    if (!model.project) {
        ImGui::TextDisabled("%s", AF_TR("No project open.").c_str());
        return;
    }

    const core::AssuranceProject& project = *model.project;
    const core::ProjectSummary& summary = model.summary;
    const ui::Theme& theme = ui::GetTheme();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 18.0f));
    ImGui::BeginChild("##project_overview_content", ImVec2(0.0f, 0.0f), false);

    ImGui::SetWindowFontScale(1.35f);
    ImGui::TextUnformatted(project.name.c_str());
    ImGui::SetWindowFontScale(1.0f);
    if (!project.description.empty())
        ImGui::TextWrapped("%s", project.description.c_str());
    if (!model.active_case_name.empty())
        ImGui::TextDisabled("%s", ui::i18n::trf("Active assurance case: {0}", model.active_case_name).c_str());

    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::TextUnformatted(AF_TR("Project Readiness").c_str());
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 6.0f));

    const std::string argument_detail = summary.undeveloped == 0
                                            ? AF_TR("No undeveloped elements")
                                            : ui::i18n::trf("{0} undeveloped", summary.undeveloped);
    const std::string evidence_detail = summary.unlinked_evidence == 0
                                            ? AF_TR("All evidence is linked")
                                            : ui::i18n::trf("{0} unlinked", summary.unlinked_evidence);
    const std::string review_detail =
        ui::i18n::trf("{0} proposals", summary.valid_proposals + summary.broken_proposals);
    const std::string conformance_detail =
        summary.conformance_files == 0 ? AF_TR("Not assessed") : AF_TR("Assessment workspace available");

    std::array<MetricCard, 4> cards = {{
        {"arguments",
         ICON_FA_BULLSEYE,
         AF_TR("Arguments"),
         ui::i18n::trnf("{0} element", "{0} elements", static_cast<int>(summary.elements), summary.elements),
         argument_detail,
         theme.accent,
         callbacks.open_arguments},
        {"evidence",
         ICON_FA_DATABASE,
         AF_TR("Evidence"),
         ui::i18n::trnf("{0} item", "{0} items", static_cast<int>(summary.evidence), summary.evidence),
         evidence_detail,
         summary.unlinked_evidence == 0 ? theme.success : theme.warning,
         callbacks.open_evidence},
        {"reviews",
         ICON_FA_COMMENTS,
         AF_TR("Reviews"),
         ui::i18n::trnf("{0} open finding",
                        "{0} open findings",
                        static_cast<int>(summary.open_review_items),
                        summary.open_review_items),
         review_detail,
         summary.open_review_items == 0 ? theme.success : theme.warning,
         callbacks.open_reviews},
        {"conformance",
         ICON_FA_TASKS,
         AF_TR("Conformance"),
         summary.conformance_files == 0 ? std::string("—") : std::to_string(summary.conformance_files),
         conformance_detail,
         summary.conformance_files == 0 ? theme.text_muted : theme.info,
         callbacks.open_conformance},
    }};

    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float available_width = ImGui::GetContentRegionAvail().x;
    const int columns = available_width >= 760.0f ? 4 : (available_width >= 380.0f ? 2 : 1);
    const float card_width =
        (available_width - spacing * static_cast<float>(columns - 1)) / static_cast<float>(columns);
    for (std::size_t index = 0; index < cards.size(); ++index) {
        if (index > 0 && static_cast<int>(index) % columns != 0)
            ImGui::SameLine();
        if (DrawMetricCard(cards[index], card_width) && cards[index].action)
            cards[index].action();
    }

    ImGui::Dummy(ImVec2(0.0f, 18.0f));
    ImGui::TextUnformatted(AF_TR("Needs Attention").c_str());
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    bool has_attention = false;
    if (summary.error_problems > 0) {
        has_attention = true;
        AttentionRow(ICON_FA_TIMES_CIRCLE,
                     ui::i18n::trf("{0} errors require attention", summary.error_problems),
                     theme.danger,
                     callbacks.open_reviews,
                     "errors");
    }
    if (summary.warning_problems > 0) {
        has_attention = true;
        AttentionRow(ICON_FA_EXCLAMATION_TRIANGLE,
                     ui::i18n::trf("{0} warnings require review", summary.warning_problems),
                     theme.warning,
                     callbacks.open_reviews,
                     "warnings");
    }
    if (summary.undeveloped > 0) {
        has_attention = true;
        AttentionRow(ICON_FA_BULLSEYE,
                     ui::i18n::trf("{0} argument elements are undeveloped", summary.undeveloped),
                     theme.warning,
                     callbacks.open_arguments,
                     "undeveloped");
    }
    if (summary.unlinked_evidence > 0) {
        has_attention = true;
        AttentionRow(ICON_FA_LINK,
                     ui::i18n::trf("{0} evidence items are not linked to a claim", summary.unlinked_evidence),
                     theme.warning,
                     callbacks.open_evidence,
                     "unlinked");
    }
    if (summary.broken_proposals > 0) {
        has_attention = true;
        AttentionRow(ICON_FA_FILE_ALT,
                     ui::i18n::trf("{0} change proposals no longer apply cleanly", summary.broken_proposals),
                     theme.danger,
                     callbacks.open_reviews,
                     "broken_proposals");
    }
    if (!has_attention) {
        AttentionRow(ICON_FA_CHECK_CIRCLE, AF_TR("No open project alerts."), theme.success, {}, "no_attention");
    }

    ImGui::Dummy(ImVec2(0.0f, 18.0f));
    ImGui::TextUnformatted(AF_TR("Project Details").c_str());
    ImGui::Separator();
    ImGui::TextDisabled("%s", ui::i18n::trf("Project files: {0}", project.files.size()).c_str());
    ImGui::TextDisabled("%s", ui::i18n::trf("Generated reports: {0}", summary.exported_reports).c_str());
    ImGui::TextDisabled("%s", project.rootPath.string().c_str());

    ImGui::EndChild();
    ImGui::PopStyleVar();
}

} // namespace ui::panels
