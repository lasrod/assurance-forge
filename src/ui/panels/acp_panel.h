#pragma once

#include "core/sacm_model.h"
#include "legacy_sacm/sacm_model.h"

#include <functional>
#include <string>

namespace ui::panels {

struct AcpPanelCallbacks {
    std::function<bool(const parser::AcpRecord& acp)> upsert_acp;
    std::function<bool(const std::string& acp_id)> remove_acp;
    std::function<bool(const std::string& acp_id)> create_confidence_argument_tree;
    std::function<bool(const std::string& acp_id)> open_confidence_argument_tree;
    // Centers the GSN canvas on the given element and selects it.
    std::function<void(const std::string& element_id)> navigate_to_element;
};

bool ShowAcpPanel(parser::AssuranceCase* model,
                  const sacm::AssuranceCasePackage* sacm_package = nullptr,
                  const AcpPanelCallbacks* callbacks = nullptr);

} // namespace ui::panels