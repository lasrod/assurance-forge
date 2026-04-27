#include "ui/panels/sacm_viewer_panel.h"

#include "ui/register_views.h"
#include "ui/theme.h"

#include <cstdio>
#include <cstring>
#include <string_view>

namespace ui::panels {
namespace {

constexpr float kOverwriteButtonWidth = 130.0f;
constexpr float kSummaryStripHeight = 88.0f;

ImVec4 ElementTypeColor(const char* type) {
    const ui::Theme& theme = ui::GetTheme();
    if (!type) return ImGui::ColorConvertU32ToFloat4(theme.text_primary);
    std::string_view sv(type);
    if (sv == "claim") return ImGui::ColorConvertU32ToFloat4(theme.node_claim);
    if (sv == "argumentreasoning") return ImGui::ColorConvertU32ToFloat4(theme.node_strategy);
    if (sv == "artifact" || sv == "artifactreference") {
        return ImGui::ColorConvertU32ToFloat4(theme.node_solution);
    }
    return ImGui::ColorConvertU32ToFloat4(theme.text_primary);
}

ImVec4 SummaryLabelColor() {
    return ImGui::ColorConvertU32ToFloat4(ui::GetTheme().text_secondary);
}

ImVec4 SummaryValueColor() {
    return ImGui::ColorConvertU32ToFloat4(ui::GetTheme().text_primary);
}

int CountElementsOfType(const parser::AssuranceCase& ac, const char* type_a, const char* type_b = nullptr) {
    int count = 0;
    for (const auto& elem : ac.elements) {
        if (elem.type == type_a || (type_b && elem.type == type_b)) {
            ++count;
        }
    }
    return count;
}

void SummaryMetric(const char* label, int value) {
    ImGui::BeginGroup();
    ImGui::TextColored(SummaryLabelColor(), "%s", label);
    ImGui::TextColored(SummaryValueColor(), "%d", value);
    ImGui::EndGroup();
}

void ShowFileSelector(SacmViewerPanelModel& model) {
    ImGui::Text("XML File:");
    ImGui::SetNextItemWidth(-1);
    if (model.xml_files.empty()) {
        ImGui::TextDisabled("No XML files found");
        return;
    }

    const char* preview = "";
    if (model.selected_file_idx >= 0 && model.selected_file_idx < static_cast<int>(model.xml_files.size())) {
        const std::string& selected = model.xml_files[model.selected_file_idx];
        auto pos = selected.find_last_of("\\/");
        preview = (pos != std::string::npos) ? selected.c_str() + pos + 1 : selected.c_str();
    }

    if (ImGui::BeginCombo("##fileselect", preview)) {
        for (int i = 0; i < static_cast<int>(model.xml_files.size()); ++i) {
            const std::string& path = model.xml_files[i];
            auto pos = path.find_last_of("\\/");
            const char* label = (pos != std::string::npos) ? path.c_str() + pos + 1 : path.c_str();
            bool is_selected = (i == model.selected_file_idx);

            if (ImGui::Selectable(label, is_selected)) {
                model.selected_file_idx = i;
                std::size_t len = path.size();
                if (len >= model.file_path_buf_size) len = model.file_path_buf_size - 1;
                std::memcpy(model.file_path_buf, path.c_str(), len);
                model.file_path_buf[len] = '\0';
            }
            if (is_selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

void ShowOverwriteModal(SacmViewerPanelModel& model) {
    if (model.show_overwrite_confirm) {
        ImGui::OpenPopup("Overwrite File?");
        model.show_overwrite_confirm = false;
    }

    if (ImGui::BeginPopupModal("Overwrite File?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("File already exists:\n%s", model.file_path_buf);
        ImGui::Separator();
        ImGui::Text("Are you sure you want to overwrite it?");
        ImGui::Spacing();

        if (ImGui::Button("Yes, Overwrite", ImVec2(kOverwriteButtonWidth, 0))) {
            model.app_state.save_file(model.file_path_buf);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(kOverwriteButtonWidth, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void ShowProjectSummary(const parser::AssuranceCase& ac) {
    if (ImGui::BeginChild("ProjectSummary", ImVec2(0, kSummaryStripHeight), true, ImGuiWindowFlags_NoScrollbar)) {
        ImGui::Text("Project Summary");
        if (ImGui::BeginTable("ProjectSummaryMetrics", 5, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); SummaryMetric("Claims", CountElementsOfType(ac, "claim"));
            ImGui::TableNextColumn(); SummaryMetric("Strategies", CountElementsOfType(ac, "argumentreasoning"));
            ImGui::TableNextColumn(); SummaryMetric("Evidence", CountElementsOfType(ac, "artifact", "artifactreference") + CountElementsOfType(ac, "expression"));
            ImGui::TableNextColumn(); SummaryMetric("CSE Rows", static_cast<int>(ui::GetCseRegisterRowCount()));
            ImGui::TableNextColumn(); SummaryMetric("Evidence Rows", static_cast<int>(ui::GetEvidenceRegisterRowCount()));
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
}

void ShowElementList(const parser::AssuranceCase& ac) {
    ImGui::Text("Assurance Case: %s", ac.name.c_str());
    ImGui::Separator();

    if (ImGui::BeginChild("ElementList", ImVec2(0, 0), true)) {
        for (const auto& elem : ac.elements) {
            ImGui::PushID(elem.id.c_str());

            ImVec4 color = ElementTypeColor(elem.type.c_str());
            ImGui::TextColored(color, "[%s]", elem.type.c_str());
            ImGui::SameLine();
            ImGui::Text("%s: %s", elem.id.c_str(), elem.name.c_str());

            if (!elem.content.empty()) {
                ImGui::TextWrapped("  Content: %s", elem.content.c_str());
            }

            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

}  // namespace

void ShowSacmViewerPanel(float width,
                         float height,
                         float top_y,
                         ImGuiWindowFlags panel_flags,
                         SacmViewerPanelModel model,
                         const SacmViewerPanelCallbacks& callbacks) {
    ImGui::SetNextWindowPos(ImVec2(0, top_y));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::Begin("SACM Viewer", nullptr, panel_flags);

    ImGui::Text("Directory:");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##dirpath", model.dir_path_buf, model.dir_path_buf_size, ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (callbacks.scan_directory) callbacks.scan_directory();
    }

    ShowFileSelector(model);

    if (ImGui::Button("Load")) {
        if (model.app_state.load_file(model.file_path_buf)) {
            if (callbacks.on_load_success) callbacks.on_load_success();
        } else {
            if (callbacks.on_load_failure) callbacks.on_load_failure();
        }
    }

    ImGui::SameLine();
    bool can_save = model.app_state.sacm_package.has_value();
    if (!can_save) ImGui::BeginDisabled();
    if (ImGui::Button("Save")) {
        FILE* file = std::fopen(model.file_path_buf, "r");
        if (file) {
            std::fclose(file);
            model.show_overwrite_confirm = true;
        } else {
            model.app_state.save_file(model.file_path_buf);
        }
    }
    if (!can_save) ImGui::EndDisabled();

    ShowOverwriteModal(model);

    if (!model.app_state.status_message.empty()) {
        ImGui::TextWrapped("%s", model.app_state.status_message.c_str());
    }

    ImGui::Separator();

    if (model.app_state.loaded_case.has_value()) {
        const auto& ac = model.app_state.loaded_case.value();
        ShowProjectSummary(ac);
        ShowElementList(ac);
    }

    ImGui::End();
}

}  // namespace ui::panels
