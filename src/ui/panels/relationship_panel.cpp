#include "ui/panels/relationship_panel.h"

#include "core/acp/acp_relationship_index.h"
#include "imgui.h"
#include "ui/i18n/localization.h"
#include "ui/theme.h"
#include "ui/ui_state.h"

#include <algorithm>
#include <string>
#include <vector>

namespace ui::panels {
namespace {

std::string EdgeKey(const std::string& parent_id, const std::string& child_id) {
    return parent_id + "\x1f" + child_id;
}

const parser::SacmElement* FindElement(const parser::AssuranceCase& model, const std::string& id) {
    auto found = std::find_if(model.elements.begin(), model.elements.end(), [&](const parser::SacmElement& element) {
        return element.id == id;
    });
    return found == model.elements.end() ? nullptr : &*found;
}

const parser::AcpRecord* FindRelationshipAcp(const parser::AssuranceCase& model, const std::string& relationship_id) {
    auto found = std::find_if(model.acps.begin(), model.acps.end(), [&](const parser::AcpRecord& acp) {
        return acp.target_kind == "relationship" && acp.target_id == relationship_id;
    });
    return found == model.acps.end() ? nullptr : &*found;
}

std::string JoinRefs(const std::vector<std::string>& refs) {
    std::string joined;
    for (const std::string& ref : refs) {
        if (!joined.empty())
            joined += ", ";
        joined += ref;
    }
    return joined;
}

void MetadataRow(const char* label, const std::string& value) {
    const Theme& theme = GetTheme();
    ImGui::PushStyleColor(ImGuiCol_Text, theme.text_secondary);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::TextWrapped("%s", value.empty() ? "-" : value.c_str());
}

const core::acp::AcpRelationshipTarget* FindSelectedTarget(const std::vector<core::acp::AcpRelationshipTarget>& targets,
                                                           const UiState& ui_state) {
    if (ui_state.selected_relationship_id.empty())
        return nullptr;
    if (!ui_state.selected_relationship_edge_key.empty()) {
        auto found = std::find_if(targets.begin(), targets.end(), [&](const core::acp::AcpRelationshipTarget& target) {
            return target.relationship_id == ui_state.selected_relationship_id &&
                   EdgeKey(target.parent_id, target.child_id) == ui_state.selected_relationship_edge_key;
        });
        if (found != targets.end())
            return &*found;
    }
    auto found = std::find_if(targets.begin(), targets.end(), [&](const core::acp::AcpRelationshipTarget& target) {
        return target.relationship_id == ui_state.selected_relationship_id;
    });
    return found == targets.end() ? nullptr : &*found;
}

} // namespace

void ShowRelationshipPanel(parser::AssuranceCase* model, const RelationshipPanelCallbacks* callbacks) {
    UiState& ui_state = GetUiState();
    if (ui_state.selected_relationship_id.empty()) {
        ImGui::TextDisabled("%s", AF_TR("No relationship selected.").c_str());
        return;
    }
    if (!model) {
        ImGui::TextDisabled("%s", AF_TR("No safety case loaded.").c_str());
        return;
    }

    const std::vector<core::acp::AcpRelationshipTarget> targets = core::acp::BuildAcpRelationshipTargets(*model);
    const core::acp::AcpRelationshipTarget* selected_target = FindSelectedTarget(targets, ui_state);
    const parser::SacmElement* relationship = FindElement(*model, ui_state.selected_relationship_id);
    if (!selected_target || !relationship) {
        ImGui::TextDisabled("%s",
                            ui::i18n::trf("Relationship not found: {0}", ui_state.selected_relationship_id).c_str());
        return;
    }

    const parser::AcpRecord* existing_acp = FindRelationshipAcp(*model, selected_target->relationship_id);

    MetadataRow(AF_TR("ID:").c_str(), selected_target->relationship_id);
    MetadataRow(AF_TR("Type:").c_str(), relationship->type);
    MetadataRow(AF_TR("Summary:").c_str(), selected_target->summary);
    MetadataRow(AF_TR("Sources:").c_str(), JoinRefs(relationship->source_refs));
    MetadataRow(AF_TR("Targets:").c_str(), JoinRefs(relationship->target_refs));
    if (!relationship->reasoning_ref.empty())
        MetadataRow(AF_TR("Reasoning:").c_str(), relationship->reasoning_ref);
    MetadataRow(AF_TR("ACP target:").c_str(),
                selected_target->eligible_for_acp ? AF_TR("Eligible") : AF_TR("Blocked"));
    if (!selected_target->eligible_for_acp) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", selected_target->blocked_reason.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    if (existing_acp) {
        MetadataRow(AF_TR("ACP:").c_str(), existing_acp->id);
        if (ImGui::Button(AF_TR("Open ACP").c_str())) {
            ui_state.selected_acp_id = existing_acp->id;
            ui_state.selected_relationship_id.clear();
            ui_state.selected_relationship_edge_key.clear();
            ui_state.selected_element_id.clear();
            if (callbacks && callbacks->open_acp)
                callbacks->open_acp(existing_acp->id);
        }
    } else {
        const bool can_add = selected_target->eligible_for_acp && callbacks && callbacks->add_acp;
        if (!can_add)
            ImGui::BeginDisabled();
        if (ImGui::Button(AF_TR("Add ACP").c_str())) {
            callbacks->add_acp(selected_target->relationship_id);
        }
        if (!can_add)
            ImGui::EndDisabled();
    }

    ImGui::Spacing();
    ImGui::Separator();
    const bool can_remove = callbacks && callbacks->remove_relationship;
    if (!can_remove)
        ImGui::BeginDisabled();
    if (ImGui::Button(AF_TR("Remove relationship").c_str()))
        callbacks->remove_relationship(selected_target->relationship_id);
    if (!can_remove)
        ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s",
                          AF_TR("Withdraws the relationship. Both elements are kept; one left with no "
                                "remaining parent shows as an orphan.")
                              .c_str());
    }
}

} // namespace ui::panels