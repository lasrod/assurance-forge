#include "core/drafts/draft_document_store.h"

#include "core/derived_views.h"

#include "core/project_file_io.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/document_edit.h"

#include <system_error>
#include <utility>

namespace core::drafts {

namespace {

// Mirrors the key `.af/drafts/<key>/` has always used: the project-relative path
// of the argument, with separators folded so it is one directory name rather
// than a tree.
std::string StableKeyFor(const std::filesystem::path& project_root, const std::filesystem::path& argument_path) {
    std::error_code relative_error;
    std::filesystem::path relative = std::filesystem::relative(argument_path, project_root, relative_error);
    if (relative_error || relative.empty()) {
        // An argument outside the project still needs a key rather than a
        // failure: the caller is opening a file the user chose, and refusing to
        // draft against it would be a worse answer than filing it by name.
        relative = argument_path.filename();
    }
    std::string key = relative.generic_string();
    for (char& character : key) {
        if (character == '/' || character == '\\' || character == ':')
            character = '_';
    }
    return key;
}

} // namespace

std::filesystem::path DraftDocumentPath(const std::filesystem::path& project_root,
                                        const std::filesystem::path& argument_path) {
    return project_root / ".af" / "drafts" / StableKeyFor(project_root, argument_path) / "draft.sacm";
}

struct DraftDocumentStore::Impl {
    std::unique_ptr<sacm_adapter::LibraryDocument> document;
    std::filesystem::path path;
    std::uint64_t revision = 0;
};

DraftDocumentStore::DraftDocumentStore() : impl_(std::make_unique<Impl>()) {}
DraftDocumentStore::~DraftDocumentStore() = default;

bool DraftDocumentStore::Open(const std::filesystem::path& project_root,
                              const std::filesystem::path& argument_path,
                              const sacm_adapter::LibraryDocument& accepted,
                              std::string& error) {
    error.clear();
    (void)accepted;
    const std::filesystem::path draft_path = DraftDocumentPath(project_root, argument_path);

    // Recorded whether or not a draft is found, so `EnsureDraft` knows where to
    // put one without being told the argument a second time.
    impl_->document.reset();
    impl_->path = draft_path;
    ++impl_->revision;

    std::error_code exists_error;
    if (!std::filesystem::exists(draft_path, exists_error) || exists_error) {
        // No draft, which is the ordinary state of an argument. Not an error, and
        // deliberately not a reason to make one: see the header.
        return true;
    }

    // Loaded as it was left. Re-deriving from the accepted document would be
    // simpler and would silently destroy every unaccepted change, which is the
    // one thing a recovery file exists to prevent.
    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(draft_path);
    if (!loaded.ok || loaded.document == nullptr) {
        error = "The working draft could not be read: " + sacm_adapter::summarize_load_diagnostics(loaded.diagnostics);
        return false;
    }
    impl_->document = std::move(loaded.document);
    ++impl_->revision;
    return true;
}

bool DraftDocumentStore::EnsureDraft(const sacm_adapter::LibraryDocument& accepted, std::string& error) {
    error.clear();
    if (impl_->document != nullptr)
        return true;
    if (impl_->path.empty()) {
        error = "There is no argument open to draft against.";
        return false;
    }

    // Cloned by serializing the accepted document and reading it back, which is
    // the same round trip the application already relies on being lossless.
    // Tolerant, so vendor content carried in on import survives into the draft
    // and back out again -- a draft that quietly dropped another tool's data
    // would break round-trip integrity the moment it was accepted.
    const sacm_adapter::SaveOutcome serialized = sacm_adapter::save_document(accepted);
    if (!serialized.ok) {
        error = "The accepted argument could not be copied into a working draft: " +
                sacm_adapter::summarize_load_diagnostics(serialized.diagnostics);
        return false;
    }
    auto clone = std::make_unique<sacm_adapter::LibraryDocument>();
    if (!sacm_adapter::reload_document(*clone, serialized.xml)) {
        error = "The copied working draft could not be read back.";
        return false;
    }
    impl_->document = std::move(clone);
    ++impl_->revision;
    return true;
}

void DraftDocumentStore::Close() {
    impl_->document.reset();
    impl_->path.clear();
    ++impl_->revision;
}

bool DraftDocumentStore::active() const {
    return impl_->document != nullptr;
}

sacm_adapter::LibraryDocument* DraftDocumentStore::document() {
    return impl_->document.get();
}

const sacm_adapter::LibraryDocument* DraftDocumentStore::document() const {
    return impl_->document.get();
}

core::AssuranceCase DraftDocumentStore::Projection() const {
    if (impl_->document == nullptr)
        return core::AssuranceCase{};
    // The same render passes the accepted view gets, because this is drawn on
    // the same canvas by the same code. A bare `project_case` renders a term as
    // a drawn context node with unrefreshed display fields -- so a term the user
    // had just defined showed on their canvas as though it had no definition,
    // and stayed that way until a restart re-ran the passes through load_file.
    // A bare strategy loses its placement the same way.
    core::AssuranceCase model;
    sacm::AssuranceCasePackage package;
    core::RebuildDerivedViewsFromLibrary(*impl_->document, model, package);
    return model;
}

const std::filesystem::path& DraftDocumentStore::path() const {
    return impl_->path;
}

std::uint64_t DraftDocumentStore::revision() const {
    return impl_->revision;
}

void DraftDocumentStore::MarkChanged() {
    ++impl_->revision;
}

bool DraftDocumentStore::Save(std::string& error) {
    error.clear();
    if (impl_->document == nullptr) {
        error = "There is no working draft to save.";
        return false;
    }
    const sacm_adapter::SaveOutcome serialized = sacm_adapter::save_document(*impl_->document);
    if (!serialized.ok) {
        error = "The working draft could not be serialized: " +
                sacm_adapter::summarize_load_diagnostics(serialized.diagnostics);
        return false;
    }

    std::error_code create_error;
    std::filesystem::create_directories(impl_->path.parent_path(), create_error);
    if (create_error) {
        error = "The working draft directory could not be created: " + create_error.message();
        return false;
    }

    const std::expected<void, std::string> written = core::WriteTextFileAtomic(impl_->path, serialized.xml);
    if (!written.has_value()) {
        error = "The working draft could not be written: " + written.error();
        return false;
    }
    return true;
}

void DraftDocumentStore::Discard(std::string& out_warning) {
    out_warning.clear();
    std::error_code remove_error;
    if (!impl_->path.empty()) {
        std::filesystem::remove(impl_->path, remove_error);
        if (remove_error) {
            // Noted, not failed. The in-memory draft is dropped regardless: a
            // file that will not delete must not be able to hold the user in a
            // state where the draft is still applied to everything they look at.
            out_warning =
                "The working draft was discarded, but its file could not be deleted: " + remove_error.message();
        }
        // The per-argument directory goes too when it is empty, so a project
        // does not accumulate a directory per argument anyone ever drafted
        // against. Failure is not reported: an empty directory is untidy, not
        // wrong.
        std::error_code directory_error;
        std::filesystem::remove(impl_->path.parent_path(), directory_error);
    }
    Close();
}

bool DraftDocumentStore::AcceptInto(const std::filesystem::path& accepted_path, std::string& error) {
    error.clear();
    if (impl_->document == nullptr) {
        error = "There is no working draft to accept.";
        return false;
    }

    // Stripped first, and on the draft itself rather than on a copy: if this
    // fails, nothing has been written and the draft is still there to retry or
    // discard. The accepted argument is not touched until the strip has already
    // succeeded.
    const sacm_adapter::EditOutcome stripped =
        sacm_adapter::apply_remove_tagged_values_with_prefix(*impl_->document, kDraftProvenanceTagPrefix);
    if (!stripped.supported || !stripped.applied) {
        error = "The draft's provenance could not be removed, so it was not accepted";
        if (!stripped.diagnostics.empty())
            error += ": " + stripped.diagnostics.front().message;
        else
            error += ".";
        return false;
    }
    ++impl_->revision;

    const sacm_adapter::SaveOutcome serialized = sacm_adapter::save_document(*impl_->document);
    if (!serialized.ok) {
        error = "The accepted argument could not be serialized: " +
                sacm_adapter::summarize_load_diagnostics(serialized.diagnostics);
        return false;
    }

    // One atomic replace. Everything that could refuse has already refused, so
    // there is no sequence left in which the accepted file ends up holding half
    // a draft.
    const std::expected<void, std::string> written = core::WriteTextFileAtomic(accepted_path, serialized.xml);
    if (!written.has_value()) {
        error = "The accepted argument could not be written: " + written.error();
        return false;
    }

    // Only now. A draft discarded before the accepted file was safely in place
    // would be work destroyed by a failed write.
    std::string discard_error;
    Discard(discard_error);
    return true;
}

} // namespace core::drafts
