#include "sacm/model/document.h"

#include "commands/commands.h"
#include "model/traverse.h"

#include "sacm/validation/codes.h"
#include "sacm/validation/validate.h"

#include <cassert>
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

    commands::detail::perform(*this, operation, outcome.effects);
    ++revision_;
    result.applied = true;
    result.changes = std::move(outcome.effects);
    result.document_revision = revision_;

#ifndef NDEBUG
    // The check/perform contract must leave the document structurally valid
    // after every successful mutation (SACM23-VAL-002).
    assert(!validation::has_errors(validation::validate_structure(*this)));
#endif
    return result;
}

} // namespace sacm::model
