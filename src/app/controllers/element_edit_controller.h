#pragma once

#include "app/app_events.h"
#include "core/element_factory.h"

#include <string>
#include <vector>

namespace app { struct AppRuntimeState; }

namespace app::controllers {

class ElementEditController {
public:
    explicit ElementEditController(AppEvents& events);

    // All mutating methods route through `app::commands::DispatchAuditedCommand`,
    // which executes the underlying command via the project's audit bus when
    // one is wired on `state.command_bus` and otherwise applies the command
    // directly (no audit transaction). Methods early-return without dispatch
    // when a precondition (selection, model presence, ...) is not met.

    bool AddChildToSelected(AppRuntimeState& state,
                            const std::string& selected_id,
                            core::NewElementKind kind);
    bool AddTopGoal(AppRuntimeState& state);
    bool RemoveSelected(AppRuntimeState& state,
                        const std::string& selected_id,
                        core::RemoveMode mode);
    bool ConfirmPendingRemoval(AppRuntimeState& state);
    void CancelPendingRemoval();

    // Commit a finished text-edit session as a single audited transaction.
    // `original_value` is the value the field held when the user first
    // focused it; `new_value` is the value at deactivation. The panel-side
    // in-place mutation is reverted to `original_value` first so the command
    // captures the correct pre-edit state in its audit payload. Returns true
    // if a transaction was appended (or, in the no-bus case, if the model
    // was updated successfully).
    bool CommitElementTextEdit(AppRuntimeState& state,
                               const std::string& element_id,
                               const std::string& field_token,
                               const std::string& language,
                               const std::string& original_value,
                               const std::string& new_value);

    bool ShouldShowRemoveConfirm() const;
    const std::string& PendingRemoveId() const;
    core::RemoveMode PendingRemoveMode() const;
    const std::vector<std::string>& PendingRemoveIds() const;

private:
    AppEvents& events_;
    bool show_remove_confirm_ = false;
    std::string pending_remove_id_;
    core::RemoveMode pending_remove_mode_ = core::RemoveMode::NodeOnly;
    std::vector<std::string> pending_remove_ids_;
};

} // namespace app::controllers