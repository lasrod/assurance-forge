#pragma once

#include "parser/xml_parser.h"

#include <functional>
#include <string>

namespace ui::panels {

struct AcpPanelCallbacks {
    std::function<bool(const parser::AcpRecord& acp)> upsert_acp;
    std::function<bool(const std::string& acp_id)> remove_acp;
    std::function<bool(const std::string& acp_id)> create_confidence_argument_tree;
    std::function<bool(const std::string& acp_id)> open_confidence_argument_tree;
};

bool ShowAcpPanel(parser::AssuranceCase* model, const AcpPanelCallbacks* callbacks = nullptr);

} // namespace ui::panels