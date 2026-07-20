#pragma once

#include "sacm/metadata/element_kind.h"
#include "sacm/model/element_id.h"
#include "sacm/validation/diagnostic.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sacm::commands {

// One effect of a mutation, in application order. `before`/`after` carry
// serialized values where practical (deleted elements carry an XMI-style
// fragment) so mutation results stay audit- and undo-ready without
// committing to an undo mechanism (SACM23-CMD-006).
struct ChangeRecord {
    enum class Change {
        Created,
        Modified,
        Deleted,
        RelationshipDeleted,
    };

    model::ElementId id;
    metadata::ElementKind kind;
    Change change = Change::Modified;
    std::optional<model::ElementId> parent;
    std::optional<std::string> property;
    std::optional<std::string> before;
    std::optional<std::string> after;
};

// Result of Document::apply. On failure the document is unchanged, applied
// is false, and diagnostics explain why.
struct MutationResult {
    std::string operation;
    bool applied = false;
    std::vector<ChangeRecord> changes;
    std::vector<validation::Diagnostic> diagnostics;
    // Revision after a successful apply; the current (unchanged) revision on
    // failure.
    std::uint64_t document_revision = 0;

    std::vector<model::ElementId> created_ids() const;
    std::vector<model::ElementId> deleted_ids() const;
};

// Result of Document::preview: what an apply would do, without mutating.
// A preview is valid while the document revision equals document_revision;
// applying against a changed document fails with SACM-CMD-003.
struct OperationPreview {
    std::string operation;
    bool can_apply = false;
    std::vector<ChangeRecord> effects;
    std::vector<validation::Diagnostic> diagnostics;
    std::uint64_t document_revision = 0;
};

}  // namespace sacm::commands
