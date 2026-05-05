#pragma once

#include "app/app_events.h"
#include "core/element_factory.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"

#include <string>
#include <vector>

namespace app::controllers {

class ElementEditController {
public:
    explicit ElementEditController(AppEvents& events);

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