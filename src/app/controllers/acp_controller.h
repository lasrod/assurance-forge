#pragma once

#include "app/app_events.h"
#include "parser/xml_parser.h"

#include <string>

namespace app {
struct AppRuntimeState;
}

namespace app::controllers {

// Every ACP (Assurance Claim Point) mutation is AUDITED: each routes through
// app::commands::DispatchAuditedCommand so it is a recorded, replayable transaction,
// and the command bus owns keeping the library document in step (via the flip +
// frame-boundary re-derive). None re-derives the library FROM the live package --
// after a library-primary edit that package is momentarily stale, so doing so would
// clobber the just-committed edit; the deferred `problems_dirty.acp` flag re-syncs
// ACP problems from the fresh model next frame.
class AcpController {
public:
    explicit AcpController(AppEvents& events);

    bool AddElementAcp(AppRuntimeState& state, const std::string& element_id);
    bool AddRelationshipAcp(AppRuntimeState& state, const std::string& relationship_id);
    bool RemoveAcp(AppRuntimeState& state, const std::string& acp_id);
    bool UpsertAcp(AppRuntimeState& state, const parser::AcpRecord& acp);
    bool CreateConfidenceArgumentTreeForAcp(AppRuntimeState& state, const std::string& acp_id);
    bool OpenConfidenceArgumentTreeForAcp(const parser::AssuranceCase& model, const std::string& acp_id);

private:
    bool DispatchAddAcp(AppRuntimeState& state, const std::string& target_kind, const std::string& target_id);

    AppEvents& events_;
};

} // namespace app::controllers
