#pragma once

// Surfaces register assessments whose subject has left the argument.
//
// `core::registers::FindOrphanedMetadata` reports them and deliberately does not
// remove them: deleting a reviewer's assessment because an id moved would be
// silent data loss. That leaves the assessments invisible instead — kept, but in
// a file nothing displays. This turns each one into a problem the reviewer can
// see and act on, and the quick fix is the human decision the core layer
// refuses to make on its own.

#include "core/problems/problem_item.h"
#include "core/problems/problems_manager.h"
#include "core/registers/register_model.h"
#include "core/sacm_model.h"

#include <string>

namespace app {

enum class RegisterAssessmentKind {
    Cse,
    Evidence,
};

struct RegisterAssessmentRef {
    RegisterAssessmentKind kind = RegisterAssessmentKind::Cse;
    std::string key;
};

// CSE keys contain ':' and "->" (see core::registers::MakeCseId), so the kind is
// carried explicitly rather than sniffed from the key's shape.
std::string EncodeRegisterAssessmentPayload(const RegisterAssessmentRef& ref);
bool DecodeRegisterAssessmentPayload(const std::string& payload, RegisterAssessmentRef& ref);

constexpr const char* kRegisterAssessmentOrphanedProblemType = "RegisterAssessmentOrphaned";

void SyncRegisterProblems(core::ProblemsManager& problems_manager,
                          const parser::AssuranceCase* model,
                          const core::registers::RegisterStore* store);

} // namespace app
