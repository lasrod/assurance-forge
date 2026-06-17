#pragma once

#include "core/element_factory.h"
#include "core/tree_editing.h"

#include <string>

namespace app {
struct AppRuntimeState;
}

namespace app::actions {

class ElementActions {
public:
    explicit ElementActions(AppRuntimeState& state);

    bool AddChildToSelected(core::NewElementKind kind);
    bool AddTopGoal();

    // GSN v3 dialectic challenges. The "*ToSelectedElement" variants challenge
    // the currently selected element; the "*ToRelationship" variants challenge
    // the relationship identified by `relationship_id`.
    bool AddChallengeToSelectedElement(core::ChallengeSourceType source_type);
    bool AddChallengeToRelationship(const std::string& relationship_id, core::ChallengeSourceType source_type);
    bool AddAcpToSelectedElement();
    bool AddAcpToRelationship(const std::string& relationship_id);
    bool RemoveAcp(const std::string& acp_id);
    void RemoveSelected(core::RemoveMode mode);
    core::TreeDropValidationResult
    ValidateTreeDrop(const std::string& dragged_id, const std::string& target_id, core::TreeDropMode drop_mode);
    bool PerformTreeDrop(const std::string& dragged_id, const std::string& target_id, core::TreeDropMode drop_mode);

private:
    AppRuntimeState& state_;
};

} // namespace app::actions