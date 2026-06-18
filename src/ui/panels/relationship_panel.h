#pragma once

#include "core/pattern_model.h"
#include "core/sacm_model.h"

#include <functional>
#include <string>

namespace sacm {
struct AssuranceCasePackage;
}

namespace ui::panels {

struct RelationshipPanelCallbacks {
    std::function<bool(const std::string& relationship_id)> add_acp;
    std::function<void(const std::string& acp_id)> open_acp;
    // Apply a pattern abstraction (optionality / multiplicity) to a relationship
    // (Pattern mode only). Null when the canvas is not a pattern.
    std::function<void(const std::string& relationship_id, const core::PatternRelationshipData& data)> set_pattern;
};

// `sacm_package` (may be null) provides the stored pattern abstraction for the
// selected relationship; `pattern_mode` gates the Pattern Abstraction editor.
void ShowRelationshipPanel(parser::AssuranceCase* model,
                           const sacm::AssuranceCasePackage* sacm_package = nullptr,
                           bool pattern_mode = false,
                           const RelationshipPanelCallbacks* callbacks = nullptr);

} // namespace ui::panels