#pragma once

#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"

#include <string>

namespace core::acp {

struct AcpEditResult {
    bool changed = false;
    std::string acp_id;
    std::string argument_package_id;
    std::string top_goal_id;
    std::string error;
};

AcpEditResult AddAcp(parser::AssuranceCase& model,
                     sacm::AssuranceCasePackage* package,
                     const std::string& target_kind,
                     const std::string& target_id);

AcpEditResult UpsertAcp(parser::AssuranceCase& model,
                        sacm::AssuranceCasePackage* package,
                        const parser::AcpRecord& acp);

AcpEditResult RemoveAcp(parser::AssuranceCase& model,
                        sacm::AssuranceCasePackage* package,
                        const std::string& acp_id);

AcpEditResult CreateConfidenceArgumentTreeForAcp(parser::AssuranceCase& model,
                                                 sacm::AssuranceCasePackage* package,
                                                 const std::string& acp_id);

const parser::AcpRecord* FindAcp(const parser::AssuranceCase& model, const std::string& acp_id);
parser::AcpRecord* FindAcp(parser::AssuranceCase& model, const std::string& acp_id);

} // namespace core::acp