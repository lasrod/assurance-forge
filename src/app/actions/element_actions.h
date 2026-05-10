#pragma once

#include "core/element_factory.h"
#include "core/tree_editing.h"

#include <string>

namespace app {

struct AppRuntimeState;

class ElementActions {
public:
    explicit ElementActions(AppRuntimeState& state);

    bool AddChildToSelected(core::NewElementKind kind);
    bool AddTopGoal();
    void RemoveSelected(core::RemoveMode mode);
    core::TreeDropValidationResult
    ValidateTreeDrop(const std::string& dragged_id, const std::string& target_id, core::TreeDropMode drop_mode);
    bool PerformTreeDrop(const std::string& dragged_id, const std::string& target_id, core::TreeDropMode drop_mode);

private:
    AppRuntimeState& state_;
};

} // namespace app