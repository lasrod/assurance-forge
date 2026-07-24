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

// Identical to AddAcp but uses the supplied `acp_id` verbatim instead of
// generating one via NextAcpId. AddAcp generates the deterministic ACP<n> id and
// delegates here; audit replay calls this directly with the recorded id so a
// replayed AddAcp reproduces the exact identity the live edit minted.
AcpEditResult AddAcpWithId(parser::AssuranceCase& model,
                           sacm::AssuranceCasePackage* package,
                           const std::string& target_kind,
                           const std::string& target_id,
                           const std::string& acp_id);

AcpEditResult
UpsertAcp(parser::AssuranceCase& model, sacm::AssuranceCasePackage* package, const parser::AcpRecord& acp);

AcpEditResult RemoveAcp(parser::AssuranceCase& model, sacm::AssuranceCasePackage* package, const std::string& acp_id);

AcpEditResult CreateConfidenceArgumentTreeForAcp(parser::AssuranceCase& model,
                                                 sacm::AssuranceCasePackage* package,
                                                 const std::string& acp_id);

// Identical to CreateConfidenceArgumentTreeForAcp but uses the supplied
// `argument_package_id` and `top_goal_id` verbatim instead of generating them.
// The plain form generates the two ids (the only non-deterministic outputs of
// this compound op) and delegates here; audit replay calls this directly with
// the recorded ids so a replayed create reproduces the exact identities minted.
AcpEditResult CreateConfidenceArgumentTreeForAcpWithIds(parser::AssuranceCase& model,
                                                        sacm::AssuranceCasePackage* package,
                                                        const std::string& acp_id,
                                                        const std::string& argument_package_id,
                                                        const std::string& top_goal_id);

const parser::AcpRecord* FindAcp(const parser::AssuranceCase& model, const std::string& acp_id);
parser::AcpRecord* FindAcp(parser::AssuranceCase& model, const std::string& acp_id);

} // namespace core::acp