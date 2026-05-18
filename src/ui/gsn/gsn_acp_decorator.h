#pragma once

#include "core/acp/acp_relationship_index.h"
#include "core/sacm_model.h"
#include "ui/element_context_menu.h"
#include "ui/gsn/gsn_layout.h"
#include "ui/ui_state.h"

#include <imgui.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace ui::gsn {

// Build a map: relationship_id -> ACPs that target that relationship.
std::unordered_map<std::string, std::vector<parser::AcpRecord>>
BuildRelationshipAcpLookup(const parser::AssuranceCase* active_case);

// Build a map: element_id -> ACPs that target that element.
std::unordered_map<std::string, std::vector<parser::AcpRecord>>
BuildElementAcpLookup(const parser::AssuranceCase* active_case);

// Build a map: EdgeKey(parent_id, child_id) -> AcpRelationshipTarget*.
std::unordered_map<std::string, const core::acp::AcpRelationshipTarget*>
BuildRelationshipTargetLookup(const std::vector<core::acp::AcpRelationshipTarget>& targets);

// Draw the small ACP badge attached to a relationship edge midpoint, plus
// click/context-menu handling.
void DrawAcpRelationshipDecorator(ImDrawList* draw_list,
                                  ImVec2 center,
                                  float zoom,
                                  const core::acp::AcpRelationshipTarget& target,
                                  const std::vector<parser::AcpRecord>& acps,
                                  const ElementContextActions& actions,
                                  UiState& ui_state);

// Draw the small ACP badge attached below an element node, plus click/context-menu
// handling.
void DrawAcpElementDecorator(ImDrawList* draw_list,
                             const LayoutNode& node,
                             ImVec2 node_min,
                             ImVec2 node_max,
                             float zoom,
                             const std::vector<parser::AcpRecord>& acps,
                             const ElementContextActions& actions,
                             UiState& ui_state);

// Render the relationship-edge context menu (left-click select / right-click "Add ACP").
// Returns true if this frame consumed a context click that the caller should not
// re-dispatch.
bool RenderAcpRelationshipContextMenu(const core::acp::AcpRelationshipTarget* target,
                                      const std::vector<parser::AcpRecord>* acps,
                                      const ElementContextActions& actions,
                                      UiState& ui_state,
                                      const std::string& edge_key,
                                      const std::string& parent_id,
                                      const std::string& child_id,
                                      bool edge_picked);

} // namespace ui::gsn
