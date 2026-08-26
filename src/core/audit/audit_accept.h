#pragma once

#include "core/audit/audit_manifest.h"
#include "core/audit/audit_transaction.h"
#include "core/audit/event_store.h"

#include <cstdint>
#include <filesystem>
#include <string>

// Recording a whole-document accept in the audit log (ADR 0016, #409).
//
// A working draft is accepted by replacing the accepted `.sacm` with the draft
// document in one atomic write. That is not a command in the audit vocabulary,
// and reconstructing a command sequence from the difference between two
// documents would be guesswork -- a wrong guess writes a false history of a
// safety argument. So the accept is recorded as exactly what it is: one
// transaction whose event carries the accepted document in full.
//
// The event is REPLAYABLE. `Replayer` applies `WorkingDraftAccepted` by
// reloading the document from the event payload, so the history slider still
// reconstructs every state across an accept, and a verifier replaying from
// snapshot 0 reproduces the accepted file. The accept is additionally promoted
// to the trusted replay root: a snapshot of the accepted bytes is taken at the
// accept's own sequence and named in `AuditManifest::replay_root_snapshot_id`,
// so verification of later work starts from the bytes a human approved rather
// than from whatever the earlier log can or cannot reproduce. The same snapshot
// makes the accept an undo boundary -- the draft it consumed is gone, so there
// is nothing an undo could put back.
namespace core::audit {

inline constexpr const char* kAcceptWorkingDraftCommandName = "AcceptWorkingDraft";
inline constexpr const char* kWorkingDraftAcceptedEventType = "WorkingDraftAccepted";

struct RecordAcceptedDocumentResult {
    std::uint64_t transaction_sequence = 0;
    std::string raw_file_hash;
    std::string canonical_model_hash;
    // The trusted-root snapshot taken for this accept. Empty when the snapshot
    // could not be written; the transaction is still recorded and replayable.
    std::string snapshot_id;
    // Non-fatal problems after the transaction was committed: the manifest or
    // the snapshot could not be written. The log is authoritative and the next
    // open rebuilds the manifest from it, so these are reported, not failed.
    std::string warning;
};

// Records that the accepted argument at `sacm_absolute_path` -- already written
// by the accept -- replaced the previous accepted document wholesale. Appends
// the transaction to `store`, refreshes `manifest` (and writes it), takes the
// trusted-root snapshot and names it in the manifest.
//
// `store` and `manifest` are the ones the running command bus holds, so the
// hash chain it continues afterwards is the one this appended to. Returns false
// only when nothing was recorded: the file could not be read or hashed, or the
// append failed.
bool RecordAcceptedDocument(const std::filesystem::path& project_root,
                            const std::filesystem::path& sacm_absolute_path,
                            EventStore& store,
                            AuditManifest& manifest,
                            const std::string& author,
                            const DraftPromotionRecord& provenance,
                            RecordAcceptedDocumentResult& out_result,
                            std::string& error);

// The accepted document a `WorkingDraftAccepted` event carries, or empty when
// the event is malformed. Shared by both replay paths so they cannot disagree
// about where the document lives in the payload.
std::string AcceptedDocumentFromEvent(const AuditEvent& event);

} // namespace core::audit
