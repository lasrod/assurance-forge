#include "ui/panels/acp_panel.h"

#include "core/acp/acp_relationship_index.h"
#include "core/acp/assurance_claim_point.h"
#include "hello_imgui/icons_font_awesome_4.h"
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
    auto found = std::find_if(
        model->acps.begin(), model->acps.end(), [&](const parser::AcpRecord& acp) { return acp.id == selected_id; });
    return found == model->acps.end() ? nullptr : &*found;
}

const parser::SacmElement* FindElement(const parser::AssuranceCase& model, const std::string& element_id) {
    auto found = std::find_if(model.elements.begin(), model.elements.end(), [&](const parser::SacmElement& element) {
        return element.id == element_id;
    });
    return found == model.elements.end() ? nullptr : &*found;
}

void UpsertIfAvailable(const AcpPanelCallbacks* callbacks, const parser::AcpRecord& acp, bool& modified) {
    if (callbacks && callbacks->upsert_acp)
        modified = callbacks->upsert_acp(acp) || modified;
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

void NavigateIfAvailable(const AcpPanelCallbacks* callbacks, const std::string& element_id) {
    if (!element_id.empty() && callbacks && callbacks->navigate_to_element)
        callbacks->navigate_to_element(element_id);
}

bool ElementIdLink(const parser::AssuranceCase& model,
                   const std::string& element_id,
                   const AcpPanelCallbacks* callbacks) {
    if (element_id.empty()) {
        ImGui::TextDisabled("-");
        return false;
    }
    const parser::SacmElement* element = FindElement(model, element_id);
    std::string tooltip;
    if (element) {
        tooltip = DisplayType(*element);
        if (!element->name.empty())
            tooltip += ": " + element->name;
    } else {
        tooltip = "Element not found in the active model.";
    }
    const Theme& theme = GetTheme();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.accent_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.accent_pressed);
    ImGui::PushStyleColor(ImGuiCol_Text, theme.accent);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 0.0f));
    const std::string label = element_id + "##acp_target_" + element_id;
    const bool clicked = ImGui::SmallButton(label.c_str());
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip.c_str());
    if (clicked)
        NavigateIfAvailable(callbacks, element_id);
    return clicked;
}

void RenderTargetRow(const parser::AssuranceCase& model,
                     const parser::AcpRecord& acp,
                     const AcpPanelCallbacks* callbacks) {
    const Theme& theme = GetTheme();
    ImGui::PushStyleColor(ImGuiCol_Text, theme.text_secondary);
    ImGui::TextUnformatted("Target:");
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, 6.0f);

    if (acp.target_kind == "relationship") {
        const std::vector<core::acp::AcpRelationshipTarget> targets = core::acp::BuildAcpRelationshipTargets(model);
        auto found = std::find_if(targets.begin(), targets.end(), [&](const core::acp::AcpRelationshipTarget& target) {
            return target.relationship_id == acp.target_id;
        });
        if (found != targets.end()) {
            ElementIdLink(model, found->parent_id, callbacks);
            ImGui::SameLine(0.0f, 6.0f);
            const bool is_context = found->kind == core::acp::AcpRelationshipKind::InContextOf;
            if (is_context) {
                ImGui::TextDisabled("c");
                ImGui::SameLine(0.0f, 3.0f);
            }
            {
                const float font_size = ImGui::GetFontSize();
                const float arrow_w = font_size * 0.9f;
                const float arrow_h = font_size;
                const ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(arrow_w, arrow_h));
                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                const ImU32 col = ImGui::GetColorU32(ImGuiCol_TextDisabled);
                const float cy = p.y + arrow_h * 0.5f;
                const float head_w = font_size * 0.35f;
                const float head_h = font_size * 0.30f;
                const float x0 = p.x;
                const float x1 = p.x + arrow_w;
                draw_list->AddLine(ImVec2(x0, cy), ImVec2(x1 - head_w * 0.6f, cy), col, 1.5f);
                draw_list->AddTriangleFilled(
                    ImVec2(x1, cy),
                    ImVec2(x1 - head_w, cy - head_h * 0.5f),
                    ImVec2(x1 - head_w, cy + head_h * 0.5f),
                    col);
                if (is_context && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("InContextOf");
                }
            }
            ImGui::SameLine(0.0f, 6.0f);
            ElementIdLink(model, found->child_id, callbacks);
            if (!found->reasoning_id.empty()) {
                ImGui::SameLine(0.0f, 6.0f);
                ImGui::TextDisabled("(via");
                ImGui::SameLine(0.0f, 4.0f);
                ElementIdLink(model, found->reasoning_id, callbacks);
                ImGui::SameLine(0.0f, 2.0f);
                ImGui::TextDisabled(")");
            }
        } else {
            ImGui::TextWrapped("Relationship %s", acp.target_id.c_str());
        }
        return;
    }

    const parser::SacmElement* element = FindElement(model, acp.target_id);
    ElementIdLink(model, acp.target_id, callbacks);
    if (element && !element->name.empty()) {
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::TextWrapped("%s: %s", DisplayType(*element).c_str(), element->name.c_str());
    } else if (element) {
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::TextWrapped("%s", DisplayType(*element).c_str());
    }
}

struct ConfidenceTopGoalOption {
    std::string argument_package_id;
    std::string top_goal_id;
    std::string label;
};

std::vector<ConfidenceTopGoalOption> CollectConfidenceTopGoals(const sacm::AssuranceCasePackage& package) {
    std::vector<ConfidenceTopGoalOption> options;
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        if (!core::acp::IsConfidenceArgumentPackage(argument_package))
            continue;
        for (const sacm::Claim& claim : argument_package.claims) {
            ConfidenceTopGoalOption option;
            option.argument_package_id = argument_package.id;
            option.top_goal_id = claim.id;
            option.label = argument_package.id + " / " + claim.id;
            if (!claim.name.empty())
                option.label += ": " + claim.name;
            options.push_back(std::move(option));
        }
    }
    return options;
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

bool ShowAcpPanel(parser::AssuranceCase* model,
                  const sacm::AssuranceCasePackage* sacm_package,
                  const AcpPanelCallbacks* callbacks) {
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

    {
        const Theme& theme = GetTheme();
        ImGui::PushStyleColor(ImGuiCol_Text, theme.text_secondary);
        ImGui::TextUnformatted("Name:");
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##acp_name", &edited.name))
            UpsertIfAvailable(callbacks, edited, modified);
    }

    RenderTargetRow(*model, edited, callbacks);

    ImGui::Spacing();
    ImGui::TextUnformatted("Resolution mode");
    ImGui::Separator();
    if (ImGui::RadioButton("Incomplete",
                           edited.resolution_kind != "text" && edited.resolution_kind != "topGoalReference")) {
        edited.resolution_kind = "none";
        edited.confidence_claim_id.clear();
        edited.argument_package_id.clear();
        edited.top_goal_id.clear();
        UpsertIfAvailable(callbacks, edited, modified);
    }
    if (ImGui::RadioButton("Text confidence argument", edited.resolution_kind == "text")) {
        edited.resolution_kind = "text";
        edited.argument_package_id.clear();
        edited.top_goal_id.clear();
        UpsertIfAvailable(callbacks, edited, modified);
    }
    if (ImGui::RadioButton("Separate confidence argument tree", edited.resolution_kind == "topGoalReference")) {
        edited.resolution_kind = "topGoalReference";
        edited.confidence_claim_id.clear();
        UpsertIfAvailable(callbacks, edited, modified);
    }

    if (edited.resolution_kind == "text") {
        ImGui::Spacing();
        ImGui::TextUnformatted("Text confidence argument");
        ImGui::Separator();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextMultiline("##acp_text",
                                      &edited.text,
                                      ImVec2(-1.0f, ImGui::GetTextLineHeight() * 7.0f),
                                      ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_NoHorizontalScroll |
                                          ImGuiInputTextFlags_WordWrap)) {
            edited.argument_package_id.clear();
            edited.top_goal_id.clear();
            UpsertIfAvailable(callbacks, edited, modified);
        }
        if (!edited.confidence_claim_id.empty())
            MetadataRow("Native claim:", edited.confidence_claim_id);
    }

    if (edited.resolution_kind == "topGoalReference") {
        ImGui::Spacing();
        ImGui::TextUnformatted("Confidence argument tree");
        ImGui::Separator();
        const bool already_linked = !edited.argument_package_id.empty() && !edited.top_goal_id.empty();

        if (already_linked) {
            const Theme& theme = GetTheme();
            ImGui::PushStyleColor(ImGuiCol_Text, theme.text_secondary);
            ImGui::TextUnformatted("Linked:");
            ImGui::PopStyleColor();
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::TextWrapped("%s / %s", edited.argument_package_id.c_str(), edited.top_goal_id.c_str());
            if (ImGui::Button("Open confidence argument tree")) {
                if (callbacks && callbacks->open_confidence_argument_tree)
                    modified = callbacks->open_confidence_argument_tree(edited.id) || modified;
            }
            ImGui::SameLine();
            if (ImGui::Button("Unlink")) {
                edited.argument_package_id.clear();
                edited.top_goal_id.clear();
                UpsertIfAvailable(callbacks, edited, modified);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Detach this ACP from its current confidence argument tree.");
        } else {
            std::vector<ConfidenceTopGoalOption> options;
            if (sacm_package)
                options = CollectConfidenceTopGoals(*sacm_package);

            if (ImGui::Button("Create new confidence argument tree")) {
                if (callbacks && callbacks->create_confidence_argument_tree)
                    modified = callbacks->create_confidence_argument_tree(edited.id) || modified;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Create a new SACM argument package and link this ACP to its top goal.");

            ImGui::Spacing();
            if (options.empty()) {
                ImGui::TextDisabled("No existing confidence argument trees available to link.");
            } else {
                ImGui::TextUnformatted("Or link to an existing confidence argument tree:");
                static std::string s_selected_label;
                // Resolve current selection against options each frame (the
                // SACM package may have changed since the last render).
                int current = -1;
                for (int i = 0; i < static_cast<int>(options.size()); ++i) {
                    if (options[i].label == s_selected_label) {
                        current = i;
                        break;
                    }
                }
                if (current < 0) {
                    current = 0;
                    s_selected_label = options[0].label;
                }
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("##acp_link_tree", options[current].label.c_str())) {
                    for (int i = 0; i < static_cast<int>(options.size()); ++i) {
                        const bool is_selected = (i == current);
                        if (ImGui::Selectable(options[i].label.c_str(), is_selected)) {
                            current = i;
                            s_selected_label = options[i].label;
                        }
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::Button("Link selected confidence argument tree")) {
                    edited.argument_package_id = options[current].argument_package_id;
                    edited.top_goal_id = options[current].top_goal_id;
                    UpsertIfAvailable(callbacks, edited, modified);
                }
            }
        }
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