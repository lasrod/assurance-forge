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
//     .af/draft-promotions/<audit-transaction-sequence>.json
//
// The third is the pre-promotion workspace kept so that undoing a promotion can
// put back the groups it consumed. It is deliberately **not** under
// `.af/drafts/<key>/`: promoting the last group deletes that whole directory, so
// a snapshot stored inside it would be destroyed by the very operation it exists
// to reverse.
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

#include <cstdint>
#include <filesystem>
#include <string>

namespace core::drafts {

// Bumped when the stored shape changes in a way an older reader cannot handle.
// A workspace whose schema is unknown is reported rather than guessed at: a
// draft silently misread is a draft silently altered.
constexpr const char* kDraftWorkspaceSchemaV1 = "assurance-forge.draft-workspace.v1";
constexpr const char* kDraftWorkspaceSchema = "assurance-forge.draft-workspace.v2";

std::string SerializeDraftWorkspace(const DraftWorkspace& workspace);
bool DeserializeDraftWorkspace(const std::string& content, DraftWorkspace& workspace, std::string& error);

std::filesystem::path DraftsDirectory(const std::filesystem::path& project_root);
std::filesystem::path DraftWorkspaceDirectory(const std::filesystem::path& project_root,
                                              const std::string& argument_key);
std::filesystem::path DraftWorkspacePath(const std::filesystem::path& project_root, const std::string& argument_key);
std::filesystem::path DraftEventsPath(const std::filesystem::path& project_root, const std::string& argument_key);

// Atomically rewrites the event log from `workspace.events`, then atomically
// writes the authoritative workspace snapshot. Temporary file plus rename uses
// the same safe-write utility as SACM autosave and manifest writes, so a crash
// mid-save cannot leave either file truncated.
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

// --- Promotion snapshots -------------------------------------------------
//
// Promotion is one boundary on the accepted undo stack, but draft mutation is
// deliberately not a command, so the workspace has no entry on that stack of its
// own. Undoing a promotion therefore restores an accepted model the remaining
// groups were never rebased onto -- and, when the promotion consumed the last
// group, restores it with the promoted work gone from the draft as well. The
// draft was the only copy of that work.
//
// A snapshot of the workspace as it stood *before* the promotion closes that
// hole. It is keyed by the audit transaction sequence the promotion recorded,
// which is also what identifies the promotion to undo: the file existing is what
// marks transaction N as a promotion, so nothing has to infer it from a command
// name that ordinary proposal application shares.
//
// **These are not pruned.** A snapshot is deleted when an undo consumes it and
// otherwise accumulates, so a long-lived project keeps one small JSON file per
// promotion it has ever made. Deliberate: the obvious cheap rules -- keep the
// last N, drop anything older than a date -- can each delete a snapshot that is
// still reachable from the undo stack, which is the precise failure this file
// exists to prevent. Pruning against the audit undo boundary is correct and
// needs audit knowledge `core` does not have. Left for the app layer.

std::filesystem::path DraftPromotionSnapshotsDirectory(const std::filesystem::path& project_root);
std::filesystem::path DraftPromotionSnapshotPath(const std::filesystem::path& project_root,
                                                 std::uint64_t transaction_sequence);

bool SaveDraftPromotionSnapshot(const std::filesystem::path& project_root,
                                std::uint64_t transaction_sequence,
                                const DraftWorkspace& workspace,
                                std::string& error);

// Reads a snapshot back. Returns false with an empty `error` when there is none
// for that sequence, which is the ordinary case: most transactions are not
// promotions.
bool LoadDraftPromotionSnapshot(const std::filesystem::path& project_root,
                                std::uint64_t transaction_sequence,
                                DraftWorkspace& workspace,
                                std::string& error);

bool DraftPromotionSnapshotExists(const std::filesystem::path& project_root, std::uint64_t transaction_sequence);

bool DeleteDraftPromotionSnapshot(const std::filesystem::path& project_root,
                                  std::uint64_t transaction_sequence,
                                  std::string& error);

} // namespace core::drafts
