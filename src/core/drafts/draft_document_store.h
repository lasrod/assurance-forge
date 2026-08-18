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

    // Opens the draft for this argument, creating it from `accepted` when none
    // exists on disk. An existing draft is loaded as it was left; it is NOT
    // re-derived from `accepted`, because re-deriving would silently discard
    // unaccepted work.
    //
    // Creating does not write the file. A draft that has changed nothing is
    // indistinguishable from no draft at all, and writing one on open would put
    // an unaccepted `.sacm` in the project directory for every argument a user
    // merely looked at.
    bool Open(const std::filesystem::path& project_root,
              const std::filesystem::path& argument_path,
              const sacm_adapter::LibraryDocument& accepted,
              std::string& error);

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

    // Persists the draft atomically. Call after any batch of edits; the file is
    // recovery state, so losing the last few seconds of it costs a redo rather
    // than an accepted argument.
    bool Save(std::string& error);

    // Deletes the draft and forgets it.
    //
    // Always available, and succeeds when there is nothing to delete. ADR 0016
    // requires this without exception: a state offering neither accept, nor
    // edit, nor discard is the trap the previous design could reach, and the
    // only exit from it was hand-editing a file.
    bool Discard(std::string& error);

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
