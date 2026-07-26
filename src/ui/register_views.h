#pragma once

#include "core/registers/register_model.h"
#include "core/sacm_model.h"

#include <string>
#include <vector>

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

// Rejoins the rows derived from `ac` with the assessments held in `store`.
// `ac == nullptr` clears the rows (no model open).
void RebuildRegisterViews(const parser::AssuranceCase* ac, const core::registers::RegisterStore& store);

size_t GetCseRegisterRowCount();
size_t GetEvidenceRegisterRowCount();

// Render the register tables, writing edited cells straight into `store`.
// Return true when the user changed a cell this frame, which is the caller's
// cue to mark the store dirty so it gets saved. Only edited rows are stored, so
// a register nobody has assessed leaves no entries behind.
bool ShowCseRegisterView(core::registers::RegisterStore& store);
bool ShowEvidenceRegisterView(core::registers::RegisterStore& store);

} // namespace ui
