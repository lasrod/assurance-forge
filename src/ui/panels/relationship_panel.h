#pragma once

#include "core/sacm_model.h"

#include <functional>
#include <string>

namespace ui::panels {

struct RelationshipPanelCallbacks {
    std::function<bool(const std::string& relationship_id)> add_acp;
    std::function<void(const std::string& acp_id)> open_acp;
    // Withdraw the relationship, keeping both endpoints. The inspector carries
    // it as well as the canvas edge menu because a relationship reached from a
    // problem row is selected without the user ever right-clicking the edge.
    std::function<void(const std::string& relationship_id)> remove_relationship;
};

void ShowRelationshipPanel(parser::AssuranceCase* model, const RelationshipPanelCallbacks* callbacks = nullptr);

} // namespace ui::panels