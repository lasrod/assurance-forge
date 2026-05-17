#pragma once

#include "parser/xml_parser.h"

#include <functional>
#include <string>

namespace ui::panels {

struct RelationshipPanelCallbacks {
    std::function<bool(const std::string& relationship_id)> add_acp;
    std::function<void(const std::string& acp_id)> open_acp;
};

void ShowRelationshipPanel(parser::AssuranceCase* model, const RelationshipPanelCallbacks* callbacks = nullptr);

} // namespace ui::panels