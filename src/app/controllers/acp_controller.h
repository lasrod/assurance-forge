#pragma once

#include "app/app_events.h"
#include "core/acp/acp_editing.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"

namespace core {
class ProblemsManager;
}

#include <functional>
#include <string>

namespace app::controllers {

class AcpController {
public:
    // `on_edit_applied` is invoked after every successful ACP mutation. ACP
    // edits bypass the command bus (they are unaudited), so this is where the
    // library-owned document is re-derived to stay consistent (Phase 9 Stage
    // 5). May be empty.
    AcpController(AppEvents& events, core::ProblemsManager& problems_manager,
                  std::function<void()> on_edit_applied = {});

    bool
    AddElementAcp(parser::AssuranceCase& model, sacm::AssuranceCasePackage* package, const std::string& element_id);
    bool AddRelationshipAcp(parser::AssuranceCase& model,
                            sacm::AssuranceCasePackage* package,
                            const std::string& relationship_id);
    bool RemoveAcp(parser::AssuranceCase& model, sacm::AssuranceCasePackage* package, const std::string& acp_id);
    bool UpsertAcp(parser::AssuranceCase& model, sacm::AssuranceCasePackage* package, const parser::AcpRecord& acp);
    bool CreateConfidenceArgumentTreeForAcp(parser::AssuranceCase& model,
                                            sacm::AssuranceCasePackage* package,
                                            const std::string& acp_id);
    bool OpenConfidenceArgumentTreeForAcp(const parser::AssuranceCase& model, const std::string& acp_id);

private:
    bool HandleResult(const char* action, const core::acp::AcpEditResult& result);
    void SyncProblems(const parser::AssuranceCase& model, const sacm::AssuranceCasePackage* package);
    // Notify that a successful ACP mutation was applied (re-derive the library
    // document). Safe to call with an empty callback.
    void NotifyEditApplied();

    AppEvents& events_;
    core::ProblemsManager& problems_manager_;
    std::function<void()> on_edit_applied_;
};

} // namespace app::controllers