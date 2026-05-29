#include "ui/panels/terminology_usages_panel.h"

#include "imgui.h"
#include "ui/i18n/localization.h"
#include "ui/theme.h"

#include <string>

namespace ui::panels {
namespace {

ImVec4 StatusColor(core::TerminologyUsageResolutionStatus status) {
    const ui::Theme& theme = ui::GetTheme();
    switch (status) {
    case core::TerminologyUsageResolutionStatus::Resolved:
        return ImGui::ColorConvertU32ToFloat4(theme.success);
    case core::TerminologyUsageResolutionStatus::Ambiguous:
        return ImGui::ColorConvertU32ToFloat4(theme.warning);
    case core::TerminologyUsageResolutionStatus::ExplicitContext:
        return ImGui::ColorConvertU32ToFloat4(theme.accent_hover);
    case core::TerminologyUsageResolutionStatus::OtherMeaning:
        return ImGui::ColorConvertU32ToFloat4(theme.text_secondary);
    case core::TerminologyUsageResolutionStatus::Undefined:
        return ImGui::ColorConvertU32ToFloat4(theme.danger);
    }
    return ImGui::ColorConvertU32ToFloat4(theme.text_primary);
}

std::string ElementLabel(const core::TerminologyTermUsage& usage) {
    if (usage.element_name.empty())
        return usage.element_id.empty() ? usage.element_gid : usage.element_id;
    if (usage.element_id.empty())
        return usage.element_name;
    return usage.element_id + "  " + usage.element_name;
}

std::string PackageLabel(const core::TerminologyTermUsage& usage) {
    if (!usage.argument_package_name.empty())
        return usage.argument_package_name;
    if (!usage.argument_package_id.empty())
        return usage.argument_package_id;
    return usage.argument_package_gid.empty() ? "-" : usage.argument_package_gid;
}

void DrawUsageRow(std::size_t index,
                  const core::TerminologyTermUsage& usage,
                  bool selected,
                  const TerminologyUsagesPanelCallbacks& callbacks) {
    ImGui::PushID(static_cast<int>(index));
    ImGui::TableNextRow();
    if (selected) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ui::WithAlpha(ui::GetTheme().accent, 0.28f));
    }

    ImGui::TableSetColumnIndex(0);
    const std::string element_label = ElementLabel(usage);
    ImGui::Selectable(element_label.c_str(),
                      selected,
                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick |
                          ImGuiSelectableFlags_AllowOverlap);
    if (ImGui::IsItemClicked() && callbacks.select_usage)
        callbacks.select_usage(index);
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && callbacks.activate_usage)
        callbacks.activate_usage(index);

    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(usage.element_type.empty() ? "-" : usage.element_type.c_str());

    ImGui::TableSetColumnIndex(2);
    const std::string package_label = PackageLabel(usage);
    ImGui::TextUnformatted(package_label.c_str());

    ImGui::TableSetColumnIndex(3);
    ImGui::PushStyleColor(ImGuiCol_Text, StatusColor(usage.resolution_status));
    ImGui::TextUnformatted(core::ToString(usage.resolution_status));
    ImGui::PopStyleColor();

    ImGui::TableSetColumnIndex(4);
    ImGui::TextUnformatted(usage.snippet.empty() ? usage.matched_text.c_str() : usage.snippet.c_str());
    if (ImGui::IsItemHovered() && !usage.snippet.empty())
        ImGui::SetTooltip("%s", usage.snippet.c_str());

    ImGui::TableSetColumnIndex(5);
    if (ImGui::SmallButton(AF_TR("Go").c_str()) && callbacks.activate_usage)
        callbacks.activate_usage(index);

    ImGui::PopID();
}

} // namespace

void ShowTerminologyUsagesPanelContent(const TerminologyUsagesPanelModel& model,
                                       const TerminologyUsagesPanelCallbacks& callbacks) {
    ImGui::TextUnformatted(AF_TR("Term Usages").c_str());
    ImGui::Separator();

    if (!model.has_search) {
        ImGui::TextDisabled("%s", AF_TR("Run Find usages from a term card or glossary row.").c_str());
        return;
    }

    const std::string title = model.term_name.empty() || model.term_name == model.term_value
                                  ? ui::i18n::trf("Find usages: {0}", model.term_value)
                                  : ui::i18n::trf("Find usages: {0}  {1}", model.term_value, model.term_name);
    ImGui::TextUnformatted(title.c_str());
    if (!model.message.empty())
        ImGui::TextDisabled("%s", model.message.c_str());
    if (!model.error.empty()) {
        ImGui::TextColored(StatusColor(core::TerminologyUsageResolutionStatus::Undefined), "%s", model.error.c_str());
        return;
    }

    const auto* usages = model.usages;
    const int count = usages ? static_cast<int>(usages->size()) : 0;
    ImGui::TextDisabled("%s", ui::i18n::trnf("{0} usage found", "{0} usages found", count, count).c_str());
    ImGui::Separator();

    if (!usages || usages->empty()) {
        ImGui::TextDisabled("%s", AF_TR("No usages found.").c_str());
        return;
    }

    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("terminology_usages_table", 6, flags, ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(AF_TR("Element").c_str(), ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn(AF_TR("Type").c_str(), ImGuiTableColumnFlags_WidthFixed, 96.0f);
        ImGui::TableSetupColumn(AF_TR("Package").c_str(), ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn(AF_TR("Status").c_str(), ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn(AF_TR("Snippet").c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 48.0f);
        ImGui::TableHeadersRow();

        for (std::size_t index = 0; index < usages->size(); ++index) {
            DrawUsageRow(index, (*usages)[index], model.selected_usage_index == static_cast<int>(index), callbacks);
        }

        ImGui::EndTable();
    }
}

} // namespace ui::panels
