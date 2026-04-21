#pragma once

#include <string>
#include <vector>

#include "parser/xml_parser.h"

namespace ui {

struct CseRegisterRow {
    std::string cse_id;
    std::string claim_id;
    std::string claim;
    std::string evidence_id;
    std::string evidence;

    std::string claim_owner;
    std::string evidence_owner;
    std::string safety_case_owner;
    std::string claim_criteria;
    std::string evidence_criteria;
    std::string assessment_status;
    std::string notes;
};

struct EvidenceRegisterRow {
    std::string evidence_id;
    std::string evidence;

    std::string evidence_owner;
    std::string type;
    std::string recency;
    std::string maturity;
    std::string controlled_environment;
    int used_by_cse_count = 0;
    std::string notes;
};

void RebuildRegisterViews(const parser::AssuranceCase* ac);

void ShowCseRegisterView();
void ShowEvidenceRegisterView();

}  // namespace ui
