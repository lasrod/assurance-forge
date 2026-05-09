#include "ui/panels/terminology_package_panel.h"

#include "imgui.h"

namespace ui::panels {
namespace {

void RenderDisabledAction(const char* label) {
    ImGui::BeginDisabled();
    ImGui::Button(label);
    ImGui::EndDisabled();
}

void RenderTermsTable(const sacm::TerminologyPackage& package) {
    if (!ImGui::BeginTable("##terms_table", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
        return;

    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 120.0f);
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Description");
    ImGui::TableHeadersRow();

    for (const auto& expression : package.expressions) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(expression.id.empty() ? expression.gid.c_str() : expression.id.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(expression.name.empty() ? expression.value.c_str() : expression.name.c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(expression.description.c_str());
    }

    if (package.expressions.empty()) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("No terms");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("-");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextDisabled("-");
    }

    ImGui::EndTable();
}

} // namespace

void ShowTerminologyPackagePanel(TerminologyPackagePanelModel model,
                                 const TerminologyPackagePanelCallbacks& callbacks) {
    ImGui::BeginChild("TerminologyPackagePanel", ImVec2(0, 0), false, ImGuiWindowFlags_None);

    if (!model.package) {
        ImGui::TextDisabled("No terminology package selected.");
        ImGui::EndChild();
        return;
    }

    ImGui::TextUnformatted("Terminology Package");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", model.package->id.empty() ? model.package->gid.c_str() : model.package->id.c_str());
    if (!model.source_file_path.empty())
        ImGui::TextDisabled("%s", model.source_file_path.string().c_str());

    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("Package name", model.name_buffer, model.name_buffer_size) && callbacks.apply_changes) {
        callbacks.apply_changes();
    }

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextMultiline("Package description",
                                  model.description_buffer,
                                  model.description_buffer_size,
                                  ImVec2(-1.0f, 96.0f)) && callbacks.apply_changes) {
        callbacks.apply_changes();
    }

    ImGui::Spacing();
    if (!model.can_delete)
        ImGui::BeginDisabled();
    if (ImGui::Button("Delete Package") && callbacks.delete_package)
        callbacks.delete_package();
    if (!model.can_delete)
        ImGui::EndDisabled();
    if (!model.can_delete && !model.delete_block_reason.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", model.delete_block_reason.c_str());
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Terms");
    RenderDisabledAction("Add Term");
    ImGui::Spacing();
    RenderTermsTable(*model.package);

    ImGui::Spacing();
    ImGui::SeparatorText("Categories");
    RenderDisabledAction("Add Category");
    ImGui::Spacing();
    if (ImGui::BeginTable("##categories_table", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Name");
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("No categories");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("-");
        ImGui::EndTable();
    }

    ImGui::EndChild();
}

} // namespace ui::panels