#pragma once

#include "app/app_events.h"
#include "core/element_factory.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"

#include <string>
#include <vector>

namespace core::commands { class CommandBus; }

namespace app::controllers {

class ElementEditController {
public:
    explicit ElementEditController(AppEvents& events);

    // Optional: when set, mutations are dispatched through the audited
    // command bus instead of calling the core mutators directly. The runtime
    // wires this on project open and clears it on close.
    void SetCommandBus(core::commands::CommandBus* bus) { command_bus_ = bus; }
    core::commands::CommandBus* GetCommandBus() const { return command_bus_; }

    bool AddChildToSelected(parser::AssuranceCase& model,
                            sacm::AssuranceCasePackage* package,
                            const std::string& selected_id,
                            core::NewElementKind kind);
    bool AddTopGoal(parser::AssuranceCase& model, sacm::AssuranceCasePackage* package);
    bool RemoveSelected(parser::AssuranceCase& model,
                        sacm::AssuranceCasePackage* package,
                        const std::string& selected_id,
                        core::RemoveMode mode);
    bool ConfirmPendingRemoval(parser::AssuranceCase& model, sacm::AssuranceCasePackage* package);
    void CancelPendingRemoval();

    // Commit a finished text-edit session as a single audited transaction.
    // `original_value` is the value the field held when the user first
    // focused it; `new_value` is the value at deactivation. When a command
    // bus is wired the panel-side mutation is reverted to `original_value`
    // first so the command captures the correct pre-edit state in its audit
    // payload. Returns true if an audit transaction was appended.
    bool CommitElementTextEdit(parser::AssuranceCase& model,
                               sacm::AssuranceCasePackage* package,
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
    core::commands::CommandBus* command_bus_ = nullptr;
    bool show_remove_confirm_ = false;
    std::string pending_remove_id_;
    core::RemoveMode pending_remove_mode_ = core::RemoveMode::NodeOnly;
    std::vector<std::string> pending_remove_ids_;
};

} // namespace app::controllers