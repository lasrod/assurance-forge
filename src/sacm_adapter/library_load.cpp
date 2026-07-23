#include "sacm_adapter/library_load.h"

#include "sacm_adapter/library_document_access.h"

#include "sacm/io/xmi.h"
#include "sacm/validation/diagnostic.h"

namespace sacm_adapter {

LibraryDocument::LibraryDocument() : impl_(std::make_unique<Impl>()) {}
LibraryDocument::~LibraryDocument() = default;
LibraryDocument::LibraryDocument(LibraryDocument&&) noexcept = default;
LibraryDocument& LibraryDocument::operator=(LibraryDocument&&) noexcept = default;

const sacm::model::Document& LibraryDocumentAccess::document(const LibraryDocument& wrapper) {
    return wrapper.impl_->document;
}

sacm::model::Document& LibraryDocumentAccess::mutable_document(LibraryDocument& wrapper) {
    return wrapper.impl_->document;
}

void LibraryDocumentAccess::set_document(LibraryDocument& wrapper,
                                         sacm::model::Document&& document) {
    wrapper.impl_->document = std::move(document);
}

LoadOutcome load_document(const std::filesystem::path& path) {
    LoadOutcome outcome;
    // Tolerant, not strict: project files legitimately contain pre-2.3
    // revisions and third-party dialects, and refusing them here would make the
    // comparison measure the wrong thing.
    sacm::io::LoadResult result = sacm::io::load_xmi_file(path);

    outcome.diagnostics.reserve(result.diagnostics.size());
    for (const sacm::validation::Diagnostic& diagnostic : result.diagnostics) {
        outcome.diagnostics.push_back(LoadDiagnostic{
            .code = diagnostic.code,
            .severity = std::string(sacm::validation::severity_name(diagnostic.severity)),
            .message = diagnostic.message,
        });
    }
    outcome.source_namespace = result.source_namespace;
    outcome.source_version =
        std::string(sacm::metadata::namespaces::standard_version_name(result.source_version));
    outcome.ok = result.ok;

    if (result.ok && result.document.has_value()) {
        outcome.document = std::make_unique<LibraryDocument>();
        LibraryDocumentAccess::set_document(*outcome.document, std::move(*result.document));
    }
    return outcome;
}

std::string summarize_load_diagnostics(const std::vector<LoadDiagnostic>& diagnostics) {
    std::string summary;
    for (const LoadDiagnostic& diagnostic : diagnostics) {
        if (!summary.empty())
            summary += "; ";
        summary += diagnostic.code + "/" + diagnostic.severity + ": " + diagnostic.message;
    }
    return summary;
}

bool reload_document(LibraryDocument& document, std::string_view xml) {
    sacm::io::LoadResult result = sacm::io::load_xmi_string(xml);
    if (!result.ok || !result.document.has_value()) {
        return false;
    }
    LibraryDocumentAccess::set_document(document, std::move(*result.document));
    return true;
}

SaveOutcome save_document(const LibraryDocument& document, bool tolerant) {
    sacm::io::SaveOptions options;
    options.mode = tolerant ? sacm::io::Mode::Tolerant : sacm::io::Mode::Strict;
    sacm::io::SaveResult result =
        sacm::io::save_xmi_string(LibraryDocumentAccess::document(document), options);

    SaveOutcome outcome;
    outcome.ok = result.ok;
    outcome.xml = std::move(result.xml);
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

} // namespace sacm_adapter
