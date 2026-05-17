#include "ui/panels/acp_panel.h"

#include "core/acp/acp_relationship_index.h"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "ui/theme.h"
#include "ui/ui_state.h"

#include <algorithm>
#include <string>
#include <vector>

namespace ui::panels {
namespace {

parser::AcpRecord* FindSelectedAcp(parser::AssuranceCase* model, const std::string& selected_id) {
    if (!model)
        return nullptr;
    auto found = std::find_if(model->acps.begin(), model->acps.end(), [&](const parser::AcpRecord& acp) {
        return acp.id == selected_id;
    });
    return found == model->acps.end() ? nullptr : &*found;
}

const parser::SacmElement* FindElement(const parser::AssuranceCase& model, const std::string& element_id) {
    auto found = std::find_if(model.elements.begin(), model.elements.end(), [&](const parser::SacmElement& element) {
        return element.id == element_id;
    });
    return found == model.elements.end() ? nullptr : &*found;
}

bool IsInstantiated(const parser::AcpRecord& acp) {
    return acp.resolution_kind == "text" || acp.resolution_kind == "topGoalReference";
}

std::string DisplayType(const parser::SacmElement& element) {
    if (element.type == "claim") {
        if (element.assertion_declaration == "assumed")
            return "Assumption";
        if (element.assertion_declaration == "justification")
            return "Justification";
        return "Goal";
    }
    if (element.type == "argumentreasoning")
        return "Strategy";
    if (element.type == "artifactreference")
        return "Solution";
    return element.type;
}

std::string TargetSummary(const parser::AssuranceCase& model, const parser::AcpRecord& acp) {
    if (acp.target_kind == "relationship") {
        const std::vector<core::acp::AcpRelationshipTarget> targets = core::acp::BuildAcpRelationshipTargets(model);
        auto found = std::find_if(targets.begin(), targets.end(), [&](const core::acp::AcpRelationshipTarget& target) {
            return target.relationship_id == acp.target_id && target.eligible_for_acp;
        });
        if (found != targets.end())
            return found->summary;
        found = std::find_if(targets.begin(), targets.end(), [&](const core::acp::AcpRelationshipTarget& target) {
            return target.relationship_id == acp.target_id;
        });
        if (found != targets.end())
            return found->summary;
        return "Relationship " + acp.target_id;
    }

    const parser::SacmElement* element = FindElement(model, acp.target_id);
    if (!element)
        return "Element " + acp.target_id;
    std::string label = DisplayType(*element) + " " + element->id;
    if (!element->name.empty())
        label += ": " + element->name;
    return label;
}

void MetadataRow(const char* label, const std::string& value) {
    const Theme& theme = GetTheme();
    ImGui::PushStyleColor(ImGuiCol_Text, theme.text_secondary);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::TextWrapped("%s", value.empty() ? "-" : value.c_str());
}

} // namespace

bool ShowAcpPanel(parser::AssuranceCase* model, const AcpPanelCallbacks* callbacks) {
    UiState& ui_state = GetUiState();
    if (ui_state.selected_acp_id.empty())
        return false;

    if (!model) {
        ImGui::TextDisabled("No safety case loaded.");
        return false;
    }

    parser::AcpRecord* selected = FindSelectedAcp(model, ui_state.selected_acp_id);
    if (!selected) {
        ImGui::TextDisabled("ACP not found: %s", ui_state.selected_acp_id.c_str());
        return false;
    }

    bool modified = false;
    parser::AcpRecord edited = *selected;

    ImGui::PushID(edited.id.c_str());
    MetadataRow("ID:", edited.id);
    MetadataRow("Target:", TargetSummary(*model, edited));
    MetadataRow("State:", IsInstantiated(edited) ? "Instantiated" : "Uninstantiated");
    MetadataRow("Resolution:", edited.resolution_kind.empty() ? "none" : edited.resolution_kind);

    ImGui::Spacing();
    ImGui::TextUnformatted("Text confidence argument");
    ImGui::Separator();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextMultiline("##acp_text",
                                  &edited.text,
                                  ImVec2(-1.0f, ImGui::GetTextLineHeight() * 7.0f),
                                  ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_NoHorizontalScroll |
                                      ImGuiInputTextFlags_WordWrap)) {
        edited.resolution_kind = edited.text.empty() ? "none" : "text";
        edited.argument_package_id.clear();
        edited.top_goal_id.clear();
        if (callbacks && callbacks->upsert_acp)
            modified = callbacks->upsert_acp(edited) || modified;
    }

    if (edited.resolution_kind == "topGoalReference") {
        ImGui::Spacing();
        MetadataRow("Argument package:", edited.argument_package_id);
        MetadataRow("Top goal:", edited.top_goal_id);
    }

    ImGui::Spacing();
    const bool already_linked = edited.resolution_kind == "topGoalReference" && !edited.argument_package_id.empty() &&
                                !edited.top_goal_id.empty();
    if (already_linked)
        ImGui::BeginDisabled();
    if (ImGui::Button("Create confidence argument tree")) {
        if (callbacks && callbacks->create_confidence_argument_tree)
            modified = callbacks->create_confidence_argument_tree(edited.id) || modified;
    }
    if (already_linked)
        ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(already_linked ? "This ACP already links to a confidence argument tree."
                                         : "Create a normal SACM argument package and link this ACP to its top goal.");
    }

    ImGui::Spacing();
    if (ImGui::Button("Delete ACP")) {
        if (callbacks && callbacks->remove_acp)
            modified = callbacks->remove_acp(edited.id) || modified;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Remove the SACM-backed ACP metadata from its target.");

    ImGui::PopID();
    return modified;
}

} // namespace ui::panels