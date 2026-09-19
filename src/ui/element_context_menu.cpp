#include "ui/element_context_menu.h"

#include "parser/model_utils.h"
#include "ui/i18n/localization.h"

#include "imgui.h"

#include <cstdio>

namespace ui {

namespace {

// One Add-menu entry: disabled (with the refusal as its tooltip) when the
// selected element cannot take a child of `kind`. The refusal text comes from
// the same core::CanAddChildElement the command validates with, so the menu and
// the command cannot disagree about what is allowed.
void RenderAddChildMenuItem(const parser::SacmElement* parent,
                            const char* label,
                            core::NewElementKind kind,
                            const ElementContextActions& actions) {
    std::string refusal;
    const bool allowed = parent == nullptr || core::CanAddChildElement(*parent, kind, refusal);
    const bool enabled = static_cast<bool>(actions.add_child) && allowed;
    if (ImGui::MenuItem(label, nullptr, false, enabled))
        actions.add_child(kind);
    if (!allowed && !refusal.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", refusal.c_str());
}

// GSN v3 Modular Extension (GSN3-MOD-003): an Away Goal cites a goal defined
// in another module. The menu offers the goals that can be cited rather than a
// free-text id, because a citation that resolves to nothing is not an away
// goal -- it is a goal claiming support from a module that does not have it.
// Disabled, with no rows, when the selection has no other module to cite.
void RenderAwayGoalMenu(const ElementContextActions& actions) {
    const bool available = static_cast<bool>(actions.add_away_goal) && !actions.away_goal_candidates.empty();
    if (!ImGui::BeginMenu(AF_TR("Away Goal").c_str(), available))
        return;

    std::string current_module;
    for (const core::AwayGoalCandidate& candidate : actions.away_goal_candidates) {
        // Candidates arrive sorted by module, so a change of module starts a
        // new group. The module heading is what tells a reader which argument
        // they are reaching into.
        if (candidate.module_identifier != current_module) {
            if (!current_module.empty())
                ImGui::Separator();
            current_module = candidate.module_identifier;
            ImGui::TextDisabled("%s", current_module.c_str());
        }
        if (ImGui::MenuItem(candidate.label.c_str()))
            actions.add_away_goal(candidate.id);
    }
    ImGui::EndMenu();
}

} // namespace

void RenderAddElementMenu(const parser::AssuranceCase* active_case,
                          const std::string& selected_id,
                          const ElementContextActions& actions) {
    if (ImGui::BeginMenu(AF_TR("Add").c_str())) {
        // Null when there is no case or the selection cannot be resolved; every
        // kind then stays enabled and the command's validation decides.
        const parser::SacmElement* parent = (active_case != nullptr && !selected_id.empty())
                                                ? parser::FindElementById(*active_case, selected_id)
                                                : nullptr;
        RenderAddChildMenuItem(parent, AF_TR("Goal").c_str(), core::NewElementKind::Goal, actions);
        RenderAddChildMenuItem(parent, AF_TR("Strategy").c_str(), core::NewElementKind::Strategy, actions);
        RenderAddChildMenuItem(parent, AF_TR("Solution").c_str(), core::NewElementKind::Solution, actions);
        RenderAddChildMenuItem(parent, AF_TR("Context").c_str(), core::NewElementKind::Context, actions);
        RenderAddChildMenuItem(parent, AF_TR("Assumption").c_str(), core::NewElementKind::Assumption, actions);
        RenderAddChildMenuItem(parent, AF_TR("Justification").c_str(), core::NewElementKind::Justification, actions);
        RenderAwayGoalMenu(actions);
        ImGui::Separator();
        if (ImGui::MenuItem(
                AF_TR("ACP").c_str(), nullptr, false, static_cast<bool>(actions.add_acp_to_selected_element)))
            actions.add_acp_to_selected_element();
        ImGui::Separator();
        if (ImGui::MenuItem(
                AF_TR("Add Counter Argument").c_str(), nullptr, false, static_cast<bool>(actions.add_counter_argument)))
            actions.add_counter_argument();
        if (ImGui::MenuItem(
                AF_TR("Add Counter Evidence").c_str(), nullptr, false, static_cast<bool>(actions.add_counter_evidence)))
            actions.add_counter_evidence();
        ImGui::EndMenu();
    }
}

void RenderAiReviewMenu(const ElementContextActions& actions) {
    if (actions.render_ai_review_menu)
        actions.render_ai_review_menu();
}

void RenderRemoveSubmenu(const parser::AssuranceCase* active_case,
                         const std::string& selected_id,
                         const ElementContextActions& actions) {
    if (ImGui::BeginMenu(AF_TR("Remove").c_str())) {
        if (selected_id.empty()) {
            ImGui::TextDisabled("%s", AF_TR("No element selected.").c_str());
            ImGui::EndMenu();
            return;
        }

        auto count_for = [&](core::RemoveMode mode) -> int {
            if (!active_case)
                return 0;
            return static_cast<int>(core::PlanRemoval(*active_case, selected_id, mode).size());
        };

        const int n_only = count_for(core::RemoveMode::NodeOnly);
        const int n_descendants = count_for(core::RemoveMode::NodeAndDescendants);
        const bool can_remove = static_cast<bool>(actions.remove_selected);

        const std::string node_only_label = ui::i18n::trf("This node only ({0})", n_only);
        if (ImGui::MenuItem(node_only_label.c_str(), nullptr, false, can_remove && n_only > 0)) {
            actions.remove_selected(core::RemoveMode::NodeOnly);
        }

        const std::string descendants_label = ui::i18n::trf("Node and descendants ({0})", n_descendants);
        if (ImGui::MenuItem(descendants_label.c_str(), nullptr, false, can_remove && n_descendants > n_only)) {
            actions.remove_selected(core::RemoveMode::NodeAndDescendants);
        }

        ImGui::EndMenu();
    }
}

} // namespace ui
