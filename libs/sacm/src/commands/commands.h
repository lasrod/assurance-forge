#pragma once

// Internal command engine (not installed). Every operation implements a
// pure `check` producing the planned effects and diagnostics, and an
// infallible `perform` executing exactly those effects. Document::preview
// runs check only; Document::apply runs check then perform, which is what
// guarantees fail-unchanged atomicity (SACM23-VAL-002).

#include "sacm/commands/mutation.h"
#include "sacm/commands/operations.h"
#include "sacm/model/document.h"

#include <vector>

namespace sacm::commands::detail {

struct CheckOutcome {
    std::vector<ChangeRecord> effects;
    std::vector<validation::Diagnostic> diagnostics;

    bool ok() const;
};

CheckOutcome check(const model::Document& document, const Operation& operation);

// Executes the effects planned by a successful check. Must not fail; any
// precondition belongs in check.
void perform(model::Document& document, const Operation& operation,
             const std::vector<ChangeRecord>& effects);

}  // namespace sacm::commands::detail
