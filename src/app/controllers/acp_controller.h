#pragma once

#include "app/app_events.h"
#include "core/acp/acp_editing.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"

namespace core {
class ProblemsManager;
}

#include <string>

namespace app::controllers {

class AcpController {
public:
    AcpController(AppEvents& events, core::ProblemsManager& problems_manager);

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

private:
    bool HandleResult(const char* action, const core::acp::AcpEditResult& result);
    void SyncProblems(const parser::AssuranceCase& model, const sacm::AssuranceCasePackage* package);

    AppEvents& events_;
    core::ProblemsManager& problems_manager_;
};

} // namespace app::controllers