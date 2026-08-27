#pragma once

#include "core/evidence_attributes.h"
#include "core/registers/register_model.h"
#include "core/sacm_model.h"

#include <functional>
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

    // Where the evidence is (the cited Resource's location), and whether this
    // row can record one: only an ArtifactReference cites a Resource.
    std::string location;
    bool is_artifact_reference = false;

    // The SACM-backed columns, from the Artifact the reference cites. A row
    // whose assessment is still in the project file shows and edits the
    // fields above instead, until the user moves it into the document.
    core::EvidenceRecord record;
    bool stored_in_project_file = false;

    // The claims resting on this evidence, and the relationship carrying each.
    struct Citation {
        std::string claim_id;
        std::string claim_label;
        std::string relationship_id;
        bool shared = false; // the relationship also carries other sources
    };
    std::vector<Citation> citations;
};

// What the evidence register can ask the application to do. Each goes through
// the ordinary edit path, so the register never gets a way to change the
// argument the canvas does not have. An unset callback hides nothing: the
// control is drawn disabled, so the table reads the same either way.
struct EvidenceRegisterCallbacks {
    std::function<void(const std::string& evidence_id)> locate;
    std::function<void(const std::string& evidence_id)> remove;
    std::function<void(const std::string& evidence_id, const std::string& location)> set_location;
    std::function<void(const std::string& location)> open_location;
    std::function<void(const std::string& evidence_id, core::EvidenceAttribute attribute, const std::string& value)>
        set_attribute;
    std::function<void()> migrate_assessments;
    std::function<void(const std::string& evidence_id)> browse_location;
    // Authoring: a new piece of evidence with a statement and an optional claim
    // to support; a link or unlink between existing evidence and a claim.
    std::function<void(const std::string& text, const std::string& claim_id)> create_evidence;
    std::function<void(const std::string& evidence_id, const std::string& claim_id)> link_evidence;
    std::function<void(const std::string& evidence_id, const std::string& claim_id)> unlink_evidence;
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
bool ShowEvidenceRegisterView(core::registers::RegisterStore& store, const EvidenceRegisterCallbacks& callbacks);

} // namespace ui
