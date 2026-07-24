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

namespace app {
struct AppRuntimeState;
}

namespace app::controllers {

class AcpController {
public:
    // `on_edit_applied` is invoked after a successful ACP mutation that does NOT
    // route through the command bus (only CreateConfidenceArgumentTreeForAcp now),
    // to re-derive the library-owned document. May be empty.
    AcpController(AppEvents& events, core::ProblemsManager& problems_manager,
                  std::function<void()> on_edit_applied = {});

    // ACP record CRUD is AUDITED: these route through
    // app::commands::DispatchAuditedCommand so each edit is a recorded, replayable
    // transaction and the command bus owns keeping the library document in step
    // (via the flip + frame-boundary re-derive). They deliberately do NOT call the
    // post-edit library re-derive callback -- after a library-primary edit the live
    // package is momentarily stale, so re-deriving the library FROM it would clobber
    // the just-committed edit.
    bool AddElementAcp(AppRuntimeState& state, const std::string& element_id);
    bool AddRelationshipAcp(AppRuntimeState& state, const std::string& relationship_id);
    bool RemoveAcp(AppRuntimeState& state, const std::string& acp_id);
    bool UpsertAcp(AppRuntimeState& state, const parser::AcpRecord& acp);

    // Out of scope for the audited-CRUD flip (it generates many ids): still mutates
    // the model/package directly and re-derives the library via the callback.
    bool CreateConfidenceArgumentTreeForAcp(parser::AssuranceCase& model,
                                            sacm::AssuranceCasePackage* package,
                                            const std::string& acp_id);
    bool OpenConfidenceArgumentTreeForAcp(const parser::AssuranceCase& model, const std::string& acp_id);

private:
    bool DispatchAddAcp(AppRuntimeState& state, const std::string& target_kind, const std::string& target_id);
    void SyncProblems(const parser::AssuranceCase& model, const sacm::AssuranceCasePackage* package);
    // Notify that a successful un-audited ACP mutation was applied (re-derive the
    // library document). Safe to call with an empty callback.
    void NotifyEditApplied();

    AppEvents& events_;
    core::ProblemsManager& problems_manager_;
    std::function<void()> on_edit_applied_;
};

} // namespace app::controllers
