#include "sacm/model/document.h"

#include "commands/commands.h"
#include "model/traverse.h"

#include "sacm/validation/codes.h"
#include "sacm/validation/validate.h"

#include <cassert>
#include <string>
#include <unordered_set>
#include <format>
#include <utility>

namespace sacm::model {

Document::Document() = default;
Document::~Document() = default;
Document::Document(Document&&) noexcept = default;
Document& Document::operator=(Document&&) noexcept = default;

const SACMElement* Document::find(const ElementId& id) const {
    const auto it = index_.find(id);
    return it == index_.end() ? nullptr : it->second;
}

void Document::for_each_element(const std::function<void(const SACMElement&)>& fn) const {
    for (const auto& root : roots_) {
        traverse::for_each_descendant(*root, fn);
    }
    for (const auto& root : other_roots_) {
        traverse::for_each_descendant(*root, fn);
    }
}

commands::OperationPreview Document::preview(const commands::Operation& operation) const {
    commands::detail::CheckOutcome outcome = commands::detail::check(*this, operation);
    return commands::OperationPreview{
        .operation = std::string(commands::operation_name(operation)),
        .can_apply = outcome.ok(),
        .effects = std::move(outcome.effects),
        .diagnostics = std::move(outcome.diagnostics),
        .document_revision = revision_,
    };
}

commands::MutationResult Document::apply(const commands::Operation& operation,
                                         std::optional<std::uint64_t> expected_revision) {
    commands::MutationResult result;
    result.operation = std::string(commands::operation_name(operation));
    result.document_revision = revision_;

    if (expected_revision.has_value() && *expected_revision != revision_) {
        result.diagnostics.push_back(validation::Diagnostic{
            .code = std::string(validation::codes::kCmdPreviewExpired),
            .severity = validation::Severity::Error,
            .requirement_id = "SACM23-CMD-004",
            .operation = result.operation,
            .affected = {},
            .location = std::nullopt,
            .message = std::format("preview taken at revision {} but the document is at revision "
                                   "{}; take a new preview",
                                   *expected_revision,
                                   revision_),
        });
        return result;
    }

    commands::detail::CheckOutcome outcome = commands::detail::check(*this, operation);
    result.diagnostics = std::move(outcome.diagnostics);
    if (validation::has_errors(result.diagnostics)) {
        return result;
    }

#ifndef NDEBUG
    // SACM23-VAL-002, stated as "introduces no structural error" rather than
    // "leaves the document structurally valid".
    //
    // The stronger form was wrong, and not subtly: a TOLERANT load accepts a
    // document with a dangling reference (the reader keeps endpoint ids verbatim
    // and reports the unresolvable ones), and `validate_structure` grades one as
    // an Error. So the whole-document form made every mutation on such a
    // document abort -- including edits with nothing to do with the broken
    // reference, and including the repair that exists to fix it. Assurance
    // Forge's `UnresolvedEndpoint` quick fix is exactly that repair. A debug
    // build could therefore not edit a document the library had just accepted.
    //
    // What the contract is actually for is unchanged: a command must not corrupt
    // the document. Comparing before against after says that, and says it about
    // the mutation rather than about the file someone opened.
    const std::vector<validation::Diagnostic> errors_before = validation::validate_structure(*this);
    const auto signature = [](const validation::Diagnostic& diagnostic) {
        std::string key = diagnostic.code + "|" + diagnostic.message;
        for (const ElementId& id : diagnostic.affected) {
            key += "|" + id.value();
        }
        return key;
    };
    std::unordered_set<std::string> before_signatures;
    for (const validation::Diagnostic& diagnostic : errors_before) {
        if (diagnostic.severity == validation::Severity::Error) {
            before_signatures.insert(signature(diagnostic));
        }
    }
#endif

    commands::detail::perform(*this, operation, outcome.effects);
    ++revision_;
    result.applied = true;
    result.changes = std::move(outcome.effects);
    result.document_revision = revision_;

#ifndef NDEBUG
    for (const validation::Diagnostic& diagnostic : validation::validate_structure(*this)) {
        if (diagnostic.severity != validation::Severity::Error) {
            continue;
        }
        assert(before_signatures.contains(signature(diagnostic)) &&
               "a mutation introduced a structural error (SACM23-VAL-002)");
    }
#endif
    return result;
}

} // namespace sacm::model
