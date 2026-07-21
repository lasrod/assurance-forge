#include "sacm_adapter/document_edit.h"

#include "sacm_adapter/library_document_access.h"

#include "sacm/commands/mutation.h"
#include "sacm/commands/operations.h"
#include "sacm/model/document.h"
#include "sacm/model/element_id.h"

namespace sacm_adapter {

namespace {

// Flatten a library MutationResult into the adapter's string-only EditOutcome.
EditOutcome to_outcome(const sacm::commands::MutationResult& result) {
    EditOutcome outcome;
    outcome.applied = result.applied;
    outcome.diagnostics.reserve(result.diagnostics.size());
    for (const sacm::validation::Diagnostic& diagnostic : result.diagnostics) {
        outcome.diagnostics.push_back(LoadDiagnostic{
            .code = diagnostic.code,
            .severity = std::string(sacm::validation::severity_name(diagnostic.severity)),
            .message = diagnostic.message,
        });
    }
    return outcome;
}

} // namespace

EditOutcome apply_set_name(LibraryDocument& document, const std::string& element_id,
                           const std::string& name, const std::string& language) {
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);
    const sacm::commands::Operation operation = sacm::commands::SetName{
        .element = sacm::model::ElementId(element_id),
        .name = name,
        .language = language,
    };
    return to_outcome(doc.apply(operation));
}

} // namespace sacm_adapter
