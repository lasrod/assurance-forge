#pragma once

// Recovery state for the working draft.
//
// Draft work is not accepted assurance content and must never be mistaken for
// it, so none of this lives in the `.sacm` file. SACM has no assertion state
// meaning "proposed by an AI and not accepted by the author" -- `needsSupport`,
// `defeated`, `isAbstract` and citation state all mean something else, and a
// private Assurance Forge tag would read as ordinary argument content to every
// tool that ignores unknown tags. The accepted file therefore stays exactly what
// a human accepted, and the draft is reconstructed from it plus these
// operations.
//
// Stored under the project's internal directory:
//
//     .af/drafts/<argument-stable-key>/workspace.json
//     .af/drafts/<argument-stable-key>/events.jsonl
//
// **`.af/` is generated with a `.gitignore`.** ADR 0008 kept transient AI state
// out of the project directory because the project directory reaches a colleague
// through version control, and that objection applies here with more force:
// these files hold unaccepted, AI-authored safety-argument text. See
// `core::ProjectService`, which writes the ignore file when it scaffolds `.af/`.
//
// Not tracked as a file role in `af.proj`, not included in SACM export, and not
// part of the canonical accepted-model hash.
//
// See docs/architecture/decisions/0010-draft-provenance-persistence-and-human-promotion.md.

#include "core/drafts/draft_workspace.h"

#include <filesystem>
#include <string>

namespace core::drafts {

// Bumped when the stored shape changes in a way an older reader cannot handle.
// A workspace whose schema is unknown is reported rather than guessed at: a
// draft silently misread is a draft silently altered.
constexpr const char* kDraftWorkspaceSchema = "assurance-forge.draft-workspace.v1";

std::string SerializeDraftWorkspace(const DraftWorkspace& workspace);
bool DeserializeDraftWorkspace(const std::string& content, DraftWorkspace& workspace, std::string& error);

std::filesystem::path DraftsDirectory(const std::filesystem::path& project_root);
std::filesystem::path DraftWorkspaceDirectory(const std::filesystem::path& project_root,
                                              const std::string& argument_key);
std::filesystem::path DraftWorkspacePath(const std::filesystem::path& project_root, const std::string& argument_key);
std::filesystem::path DraftEventsPath(const std::filesystem::path& project_root, const std::string& argument_key);

// Atomically writes the workspace and appends any events not yet on disk.
// Temporary file plus rename, through the same safe-write utility SACM autosave
// and manifest writes use, so a crash mid-save cannot leave a truncated draft.
bool SaveDraftWorkspace(const std::filesystem::path& project_root,
                        const std::string& argument_key,
                        const DraftWorkspace& workspace,
                        std::string& error);

// Reads the workspace back. Returns false with an empty `error` when there is
// simply nothing stored, which is not a failure -- most projects have no draft.
bool LoadDraftWorkspace(const std::filesystem::path& project_root,
                        const std::string& argument_key,
                        DraftWorkspace& workspace,
                        std::string& error);

bool DraftWorkspaceExists(const std::filesystem::path& project_root, const std::string& argument_key);

bool DeleteDraftWorkspace(const std::filesystem::path& project_root,
                          const std::string& argument_key,
                          std::string& error);

} // namespace core::drafts
