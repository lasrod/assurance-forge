#pragma once

#include "imgui.h"

#include <string>
#include <vector>

namespace ui::gsn {

enum class ElementRole { Claim, Context, Justification, Assumption, SubClaim, Solution, Evidence, Strategy, Other };

enum class ElementGroup {
    Group1, // Structural (below parent)
    Group2  // Contextual (side-attached)
};

struct CanvasElement {
    std::string id;
    ElementRole role = ElementRole::Other;
    std::string label;
    std::string label_secondary;
    bool undeveloped = false;
    std::string parent_id; // empty if none
};

struct LayoutNode {
    std::string id;
    ElementRole role = ElementRole::Other;
    ElementGroup group = ElementGroup::Group1;
    std::string label;
    std::string label_secondary;
    bool undeveloped = false;
    bool uninstantiated = false;
    ImVec2 position;
    ImVec2 size;
    std::string parent_id;
    int side_stack_index = 0; // for Group2: 0-based index in stack on same side
    bool is_left_side = true; // for Group2: which side of parent
    // Straight-down distance (content px) for edges to this node's children
    // before they curve out — clears a tall side challenge tree.
    double child_edge_drop = 0.0;

    // GSN v3 dialectic: when true, this node is a counter argument / counter
    // evidence drawn with a dashed open-arrow challenge edge to its target.
    // The arrow points at `challenge_target_id` — that element's border when
    // `challenge_target_is_relationship` is false, otherwise the midpoint of the
    // target relationship's edge.
    bool is_counter_source = false;
    std::string challenge_target_id;
    bool challenge_target_is_relationship = false;
    // This counter's own Challenges relationship id (lets the dashed arrow itself
    // be challenged) and the host element it is laid out beside/below.
    std::string challenge_relationship_id;
    std::string challenge_anchor_id;
};

} // namespace ui::gsn
