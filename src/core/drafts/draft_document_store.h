#pragma once

// The working draft, as a SACM document (ADR 0016).
//
// A draft is a copy of the accepted argument that contributors -- MCP clients,
// SCCG AI review, and the user -- edit directly, through the same library
// operations the application performs on the accepted document. There is no
// staging model and no materialization step, which is the whole point: a change
// the document cannot hold is refused when it is made, rather than accepted into
// a flat model, drawn on the canvas as pending, and refused at accept by a seam
// nothing consulted earlier.
//
// This owns the draft's lifetime and its file. What a draft *changes* is a
// comparison against the accepted projection (`draft_document_diff.h`); what may
// be changed is the library's business, not this class's.
//
// Ownership follows ADR 0008 unchanged: only the running application writes here.

#include "core/sacm_model.h"
#include "legacy_sacm/sacm_model.h"
#include "sacm_adapter/library_load.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace core::drafts {

// The TaggedValue key prefix every piece of draft provenance is filed under, and
// the one `AcceptInto` strips. A single constant because the writer and the
// stripper disagreeing would either lose provenance while a draft is open or
// leak it into an accepted safety case.
inline constexpr char kDraftProvenanceTagPrefix[] = "assuranceForge.draft.";

// Where the draft for `argument_path` lives inside `project_root`.
//
// Keyed by the project-relative path of the argument file, as ADR 0010
// established: ids such as `G1` legitimately repeat across a project's
// arguments, so a draft written against one must never be opened against
// another.
std::filesystem::path DraftDocumentPath(const std::filesystem::path& project_root,
                                        const std::filesystem::path& argument_path);

class DraftDocumentStore {
public:
    DraftDocumentStore();
    ~DraftDocumentStore();
    DraftDocumentStore(const DraftDocumentStore&) = delete;
    DraftDocumentStore& operator=(const DraftDocumentStore&) = delete;

    // Opens the draft for this argument if there is one on disk, and otherwise
    // leaves no draft active. Succeeds either way: an argument nobody has
    // drafted against is the ordinary case, not a failure.
    //
    // An existing draft is loaded as it was left; it is NOT re-derived from
    // `accepted`, because re-deriving would silently discard unaccepted work.
    //
    // **Opening an argument does not create a draft**, and that is load-bearing
    // rather than a saving. The comparison against the accepted document is only
    // meaningful while the draft descends from the argument it is compared with,
    // and a draft cloned for every argument anyone opened stopped descending from
    // it the moment the user edited the accepted argument -- every element they
    // added was then in the accepted document and absent from the draft, which
    // the comparison reported, correctly and uselessly, as the draft removing
    // their own new work.
    bool Open(const std::filesystem::path& project_root,
              const std::filesystem::path& argument_path,
              const sacm_adapter::LibraryDocument& accepted,
              std::string& error);

    // Creates the draft from `accepted` if there is not already one open, so a
    // contributor about to make the first unaccepted change has something to
    // make it to. A no-op when a draft is already active -- a second contributor
    // joins the draft in progress rather than replacing it.
    //
    // Called at the moment of the first edit rather than on open, so the copy is
    // taken from the argument as it stands then. Creating does not write the
    // file: the caller saves once the edit has actually been applied, so a
    // refused edit leaves no unaccepted `.sacm` in the project directory.
    bool EnsureDraft(const sacm_adapter::LibraryDocument& accepted, std::string& error);

    // Forgets the in-memory draft without touching the file. For closing a
    // project, not for discarding work.
    void Close();

    bool active() const;

    // The draft document, for editing through the `sacm_adapter` seams. Null
    // when no draft is open.
    sacm_adapter::LibraryDocument* document();
    const sacm_adapter::LibraryDocument* document() const;

    // The draft as the application's flat projection, rebuilt on demand. This is
    // what reads, the canvas and the comparison consume.
    core::AssuranceCase Projection() const;

    // Both derived views at once: the flat projection above and the SACM
    // package the terminology surfaces read the glossary from. Rebuilt in one
    // pass so the two cannot describe different drafts. Both come back empty
    // when no draft is active.
    void ProjectViews(core::AssuranceCase& out_model, sacm::AssuranceCasePackage& out_package) const;

    // Persists the draft atomically. Call after any batch of edits; the file is
    // recovery state, so losing the last few seconds of it costs a redo rather
    // than an accepted argument.
    bool Save(std::string& error);

    // Deletes the draft and forgets it.
    //
    // Returns nothing, and that is the point. ADR 0016 requires discard to be
    // available in every state without exception -- a state offering neither
    // accept, nor edit, nor discard is the trap the previous design could reach,
    // and the only exit from it was hand-editing a file. A `bool` invites a
    // caller to treat discard as refusable and leave the draft applied to
    // everything the user is looking at, so there is no `bool` to misread.
    //
    // `out_warning` describes a leftover the user may want to know about -- a
    // file that would not delete -- but the draft is gone from this session
    // either way. It is a note, not a failure.
    void Discard(std::string& out_warning);

    // Writes the draft to `accepted_path` as the accepted argument, strips the
    // draft provenance on the way, and discards the draft.
    //
    // All or nothing, and it cannot half-apply: the stripped document is
    // serialized in full and then written by one atomic replace. There is no
    // sequence of operations against the accepted argument to fail partway
    // through, which is what makes "accept cannot leave the case in a state
    // nobody chose" a property of the design rather than of the error handling.
    //
    // The draft is discarded only after the accepted file is safely in place.
    bool AcceptInto(const std::filesystem::path& accepted_path, std::string& error);

    const std::filesystem::path& path() const;

    // Increments on every mutation this class performs. Lets a caller notice the
    // draft moved without diffing the document, the way the canvas notices it
    // must redraw.
    std::uint64_t revision() const;

    // Records that the draft changed underneath this store -- a caller edited
    // `document()` directly through a seam, which is the ordinary path.
    void MarkChanged();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace core::drafts
