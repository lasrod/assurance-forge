#include "core/audit/event_replayer.h"

#include "core/evidence_attributes.h"

#include "core/acp/acp_editing.h"
#include "core/assurance_tree.h"
#include "core/audit/audit_accept.h"
#include "core/audit/undo_resolver.h"
#include "core/commands/acp_commands.h"
#include "core/commands/element_commands.h"
#include "core/commands/package_commands.h"
#include "core/commands/proposal_commands.h"
#include "core/commands/terminology_commands.h"
#include "core/commands/tree_commands.h"
#include "core/element_factory.h"
#include "core/tree_editing.h"
#include "core/commands/library_bridge.h"
#include "core/library_package_projection.h"
#include "core/relationship_editing.h"
#include "core/reviews/review_proposal.h"
#include "core/reviews/review_proposal_patch_service.h"
#include "core/reviews/review_proposal_plan.h"
#include "core/sacm_argument_sync.h"
#include "core/sacm_identity.h"
#include "core/terminology_context_projection.h"
#include "core/terminology_package_service.h"
#include "legacy_sacm/sacm_serializer.h"
#include "parser/model_utils.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/document_edit.h"
#include "sacm_adapter/library_load.h"

#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace core::audit {

namespace {

std::string FormatLocation(std::uint64_t tx_seq, std::uint64_t ev_seq, const std::string& event_type) {
    std::ostringstream oss;
    oss << "transaction " << tx_seq << " event " << ev_seq << " (" << event_type << ")";
    return oss.str();
}

// -------------------------------------------------------------------------
// Library-primary replay helpers (Phase 1b slice 3a). These translate the
// legacy `core::*` vocabulary the event payload speaks into the `sacm_adapter`
// library seam vocabulary, and flatten a seam outcome into a diagnosable error.

sacm_adapter::ChildKind ToAdapterChildKind(core::NewElementKind kind) {
    switch (kind) {
    case core::NewElementKind::Goal:
        return sacm_adapter::ChildKind::Goal;
    case core::NewElementKind::Strategy:
        return sacm_adapter::ChildKind::Strategy;
    case core::NewElementKind::Solution:
        return sacm_adapter::ChildKind::Solution;
    case core::NewElementKind::Context:
        return sacm_adapter::ChildKind::Context;
    case core::NewElementKind::Assumption:
        return sacm_adapter::ChildKind::Assumption;
    case core::NewElementKind::Justification:
        return sacm_adapter::ChildKind::Justification;
    }
    return sacm_adapter::ChildKind::Goal;
}

sacm_adapter::ChallengeSource ToAdapterChallengeSource(core::ChallengeSourceType type) {
    switch (type) {
    case core::ChallengeSourceType::CounterArgument:
        return sacm_adapter::ChallengeSource::CounterArgument;
    case core::ChallengeSourceType::CounterEvidence:
        return sacm_adapter::ChallengeSource::CounterEvidence;
    }
    return sacm_adapter::ChallengeSource::CounterArgument;
}

sacm_adapter::TextField ToAdapterTextField(core::ElementTextField field) {
    switch (field) {
    case core::ElementTextField::Name:
        return sacm_adapter::TextField::Name;
    case core::ElementTextField::Description:
        return sacm_adapter::TextField::Description;
    case core::ElementTextField::Content:
        return sacm_adapter::TextField::Content;
    }
    return sacm_adapter::TextField::Name;
}

// The identities a terminology-association event recorded, in the shape the seam
// takes. Each is consulted only if the seam creates that entity; a reused one
// keeps what it has.
sacm_adapter::TerminologyContextIdentities ToSeamIdentities(const core::TerminologyContextForcedIds& forced) {
    sacm_adapter::TerminologyContextIdentities identities;
    identities.artifact_reference_id = forced.artifact_reference_id;
    identities.artifact_reference_gid = forced.artifact_reference_gid;
    identities.asserted_context_id = forced.asserted_context_id;
    identities.asserted_context_gid = forced.asserted_context_gid;
    return identities;
}

std::string SummarizeDiagnostics(const std::vector<sacm_adapter::LoadDiagnostic>& diagnostics) {
    if (diagnostics.empty())
        return "(no library diagnostics)";
    std::string summary;
    for (const sacm_adapter::LoadDiagnostic& diagnostic : diagnostics) {
        if (!summary.empty())
            summary += "; ";
        summary += diagnostic.code + "/" + diagnostic.severity + ": " + diagnostic.message;
    }
    return summary;
}

// Formats the "FAILED at <location>" diagnostic for an unsuccessful seam call,
// echoing the supported/applied flags and any library diagnostics so a
// convergence failure names precisely which seam diverged.
std::string FormatSeamFailure(const std::string& seam,
                              std::uint64_t tx_seq,
                              const AuditEvent& event,
                              bool supported,
                              bool applied,
                              const std::vector<sacm_adapter::LoadDiagnostic>& diagnostics) {
    return seam + " failed at " + FormatLocation(tx_seq, event.event_sequence, event.event_type) +
           " (supported=" + (supported ? "true" : "false") + ", applied=" + (applied ? "true" : "false") +
           "): " + SummarizeDiagnostics(diagnostics);
}

// Shared bridge for library-primary replay. No parity seam exists (the
// library's equivalent diverges from the legacy mutator -- e.g. it mints no
// create-time gid, or maps a claim's content/description differently), so
// project the library to the legacy models, run the SAME legacy mutator the
// live path uses, then re-derive the library from the serialized package. This
// keeps library-primary replay converged with the legacy live path for these
// events. `mutate` receives the projected parser model and package; it returns
// false (setting `out_error`, already formatted with the event location) to
// abort. On success the wrapper serializes the mutated package and reloads it
// back into `document` in place.
using BridgeMutator = std::function<bool(parser::AssuranceCase&, sacm::AssuranceCasePackage&, std::string&)>;

// Replay refuses a shape no seam expresses, rather than rebuilding the document
// from a projection. The live command that recorded such an event refused too, so
// a log carrying one describes an edit that never landed.
bool RefuseUnrepresentableReplay(const std::string& location, std::string& out_error) {
    out_error = "Replaying " + location +
                " needs the legacy compatibility path, which has been removed; the document is unchanged.";
    return false;
}

// Slice 2c: the ACP replay path. Like BridgeViaLegacy it projects the document
// and runs the SAME `core::acp` mutator the live command runs -- those rules
// about resolution kinds and meta-claims live in one place and are not worth a
// second copy. UNLIKE it, the projection is read and thrown away: the result is
// written back through the seams as targeted tag and meta-claim edits, so the
// document is never rebuilt from the projection and nothing outside the ACP is
// touched. Mirrors `core::commands`' ACP flip exactly, which is the point -- a
// live edit and its own replay must be the same code path.
using AcpReplayMutator = std::function<core::acp::AcpEditResult(parser::AssuranceCase&, sacm::AssuranceCasePackage&)>;

std::vector<std::string> ReplayMetaClaimsOf(const sacm::AssuranceCasePackage& package,
                                            const std::string& relationship_id) {
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        for (const sacm::AssertedInference& r : argument_package.assertedInferences)
            if (r.id == relationship_id)
                return r.metaClaims;
        for (const sacm::AssertedContext& r : argument_package.assertedContexts)
            if (r.id == relationship_id)
                return r.metaClaims;
        for (const sacm::AssertedEvidence& r : argument_package.assertedEvidences)
            if (r.id == relationship_id)
                return r.metaClaims;
    }
    return {};
}

sacm_adapter::AcpTagFields ReplayTagFields(const parser::AcpRecord& acp) {
    sacm_adapter::AcpTagFields fields;
    fields.id = acp.id;
    fields.name = acp.name;
    fields.resolution_kind = acp.resolution_kind;
    fields.text = acp.text;
    fields.confidence_claim_id = acp.confidence_claim_id;
    fields.argument_package_id = acp.argument_package_id;
    fields.top_goal_id = acp.top_goal_id;
    return fields;
}

// Runs `mutate` on a scratch projection and mirrors the resulting ACP onto the
// document. `expect_removed` inverts it: the ACP is expected to be gone, so its
// tags are dropped and a relationship target's meta-claims rewritten.
bool ApplyAcpViaSeams(sacm_adapter::LibraryDocument& document,
                      const std::string& location,
                      const std::string& acp_id,
                      bool expect_removed,
                      const AcpReplayMutator& mutate,
                      std::string& out_error) {
    parser::AssuranceCase model = sacm_adapter::project_case(document);
    sacm::AssuranceCasePackage package = core::project_library_package_with_tags(document);

    const parser::AcpRecord* before = core::acp::FindAcp(model, acp_id);
    const std::string previous_target = before != nullptr ? before->target_id : std::string{};
    const bool previous_was_relationship = before != nullptr && before->target_kind == "relationship";

    const core::acp::AcpEditResult result = mutate(model, package);
    if (!result.error.empty()) {
        out_error = "ACP replay failed at " + location + ": " + result.error;
        return false;
    }
    if (!result.changed) {
        out_error = "ACP replay applied nothing at " + location;
        return false;
    }

    const auto rejected = [&](const char* what, const std::vector<sacm_adapter::LoadDiagnostic>& diagnostics) {
        out_error = std::string("ACP replay could not write ") + what + " at " + location + ": " +
                    SummarizeDiagnostics(diagnostics);
    };

    if (expect_removed) {
        if (!previous_target.empty()) {
            const sacm_adapter::EditOutcome removed =
                sacm_adapter::apply_remove_acp_tags(document, previous_target, acp_id);
            if (removed.supported && !removed.applied && !removed.diagnostics.empty()) {
                rejected("the ACP removal", removed.diagnostics);
                return false;
            }
            if (previous_was_relationship) {
                const sacm_adapter::EditOutcome meta = sacm_adapter::apply_set_meta_claims(
                    document, previous_target, ReplayMetaClaimsOf(package, previous_target));
                if (!meta.supported || !meta.applied) {
                    rejected("the meta-claims", meta.diagnostics);
                    return false;
                }
            }
        }
        return true;
    }

    const parser::AcpRecord* after = core::acp::FindAcp(model, acp_id);
    if (after == nullptr) {
        out_error = "ACP replay lost the ACP at " + location;
        return false;
    }

    // A confidence tree materializes a package and a goal before the ACP can
    // point at them.
    if (!result.argument_package_id.empty() && !result.top_goal_id.empty()) {
        const parser::SacmElement* goal = nullptr;
        for (const parser::SacmElement& element : model.elements) {
            if (element.id == result.top_goal_id)
                goal = &element;
        }
        if (goal == nullptr) {
            out_error = "ACP replay lost the confidence top goal at " + location;
            return false;
        }
        const sacm_adapter::AcpOutcome created = sacm_adapter::apply_create_confidence_argument_package(
            document, result.argument_package_id, goal->name, result.top_goal_id, goal->name, goal->content);
        if (!created.supported || !created.applied) {
            rejected("the confidence argument tree", created.diagnostics);
            return false;
        }
    }

    // Re-pointed at a different target: clear the one it left.
    if (!previous_target.empty() && previous_target != after->target_id) {
        const sacm_adapter::EditOutcome cleared =
            sacm_adapter::apply_remove_acp_tags(document, previous_target, acp_id);
        if (cleared.supported && !cleared.applied && !cleared.diagnostics.empty()) {
            rejected("the ACP move", cleared.diagnostics);
            return false;
        }
        if (previous_was_relationship) {
            const sacm_adapter::EditOutcome meta = sacm_adapter::apply_set_meta_claims(
                document, previous_target, ReplayMetaClaimsOf(package, previous_target));
            if (!meta.supported || !meta.applied) {
                rejected("the meta-claims", meta.diagnostics);
                return false;
            }
        }
    }

    const sacm_adapter::EditOutcome tagged =
        sacm_adapter::apply_upsert_acp_tags(document, after->target_id, ReplayTagFields(*after));
    if (!tagged.supported || !tagged.applied) {
        rejected("the ACP tags", tagged.diagnostics);
        return false;
    }
    if (after->target_kind == "relationship") {
        const sacm_adapter::EditOutcome meta = sacm_adapter::apply_set_meta_claims(
            document, after->target_id, ReplayMetaClaimsOf(package, after->target_id));
        if (!meta.supported || !meta.applied) {
            rejected("the meta-claims", meta.diagnostics);
            return false;
        }
    }
    return true;
}

bool ApplyEvent(ReplayState& state, std::uint64_t tx_seq, const AuditEvent& event, std::string& out_error) {
    const std::string& type = event.event_type;
    const auto& payload = event.payload;

    auto require_string = [&](const char* key, std::string& dest) -> bool {
        auto it = payload.find(key);
        if (it == payload.end() || !it->is_string()) {
            out_error = "Missing or non-string payload field '" + std::string(key) + "' at " +
                        FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        dest = it->get<std::string>();
        return true;
    };

    if (type == "CreateTopGoal") {
        std::string element_id;
        if (!require_string("generated_id", element_id))
            return false;
        std::string err;
        if (!core::AddTopGoalWithId(state.model, &state.package, element_id, err)) {
            out_error = "AddTopGoalWithId failed at " + FormatLocation(tx_seq, event.event_sequence, type) + ": " + err;
            return false;
        }
        return true;
    }

    if (type == "CreateChildElement") {
        std::string parent_id, kind_token, element_id, relationship_id;
        if (!require_string("parent_id", parent_id))
            return false;
        if (!require_string("kind", kind_token))
            return false;
        if (!require_string("generated_id", element_id))
            return false;
        if (!require_string("generated_relationship_id", relationship_id))
            return false;
        core::NewElementKind kind;
        if (!commands::NewElementKindFromToken(kind_token, kind)) {
            out_error =
                "Unknown kind token '" + kind_token + "' at " + FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        std::string err;
        if (!core::AddChildElementWithIds(
                state.model, &state.package, parent_id, kind, element_id, relationship_id, err)) {
            out_error =
                "AddChildElementWithIds failed at " + FormatLocation(tx_seq, event.event_sequence, type) + ": " + err;
            return false;
        }
        return true;
    }

    if (type == "CreateChallenge") {
        std::string target_id, target_kind_token, source_type_token, element_id, relationship_id;
        if (!require_string("target_id", target_id))
            return false;
        if (!require_string("target_kind", target_kind_token))
            return false;
        if (!require_string("source_type", source_type_token))
            return false;
        if (!require_string("generated_id", element_id))
            return false;
        if (!require_string("generated_relationship_id", relationship_id))
            return false;
        core::ArgumentTarget::Kind target_kind;
        if (!commands::ArgumentTargetKindFromToken(target_kind_token, target_kind)) {
            out_error = "Unknown target kind token '" + target_kind_token + "' at " +
                        FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        core::ChallengeSourceType source_type;
        if (!commands::ChallengeSourceTypeFromToken(source_type_token, source_type)) {
            out_error = "Unknown source type token '" + source_type_token + "' at " +
                        FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        core::ArgumentTarget target{target_kind, target_id};
        std::string err;
        if (!core::AddChallengeWithIds(
                state.model, &state.package, target, source_type, element_id, relationship_id, err)) {
            out_error =
                "AddChallengeWithIds failed at " + FormatLocation(tx_seq, event.event_sequence, type) + ": " + err;
            return false;
        }
        return true;
    }

    if (type == "RemoveElement") {
        std::string element_id, mode_token;
        if (!require_string("element_id", element_id))
            return false;
        if (!require_string("mode", mode_token))
            return false;
        core::RemoveMode mode;
        if (!commands::RemoveModeFromToken(mode_token, mode)) {
            out_error = "Unknown remove mode token '" + mode_token + "' at " +
                        FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        std::string err;
        if (!core::RemoveElement(state.model, &state.package, element_id, mode, err)) {
            out_error = "RemoveElement failed at " + FormatLocation(tx_seq, event.event_sequence, type) + ": " + err;
            return false;
        }
        return true;
    }

    if (type == "RemoveRelationship") {
        std::string relationship_id;
        if (!require_string("relationship_id", relationship_id))
            return false;
        std::string err;
        if (!core::RemoveRelationship(state.model, &state.package, relationship_id, err)) {
            out_error =
                "RemoveRelationship failed at " + FormatLocation(tx_seq, event.event_sequence, type) + ": " + err;
            return false;
        }
        return true;
    }

    if (type == "DropRelationshipReference") {
        std::string relationship_id, reference;
        if (!require_string("relationship_id", relationship_id))
            return false;
        if (!require_string("reference", reference))
            return false;
        std::string err;
        // `removed_relationship` is recorded in the event but not read back here:
        // the mutator re-derives it from SACM multiplicity, so live and replay
        // reach the same outcome. The recorded value exists so the event
        // describes what happened without a reader having to replay it.
        bool removed_relationship = false;
        if (!core::DropRelationshipReference(
                state.model, &state.package, relationship_id, reference, removed_relationship, err)) {
            out_error = "DropRelationshipReference failed at " + FormatLocation(tx_seq, event.event_sequence, type) +
                        ": " + err;
            return false;
        }
        return true;
    }

    if (type == "MoveStrategyToReasoning") {
        std::string relationship_id, strategy_id;
        if (!require_string("relationship_id", relationship_id))
            return false;
        if (!require_string("strategy_id", strategy_id))
            return false;
        std::string err;
        if (!core::MoveStrategyToReasoning(state.model, &state.package, relationship_id, strategy_id, err)) {
            out_error =
                "MoveStrategyToReasoning failed at " + FormatLocation(tx_seq, event.event_sequence, type) + ": " + err;
            return false;
        }
        return true;
    }

    if (type == "SetElementUndeveloped") {
        std::string element_id;
        if (!require_string("element_id", element_id))
            return false;
        const auto new_value = payload.find("new_value");
        if (new_value == payload.end() || !new_value->is_boolean()) {
            out_error = "Missing or non-boolean payload field 'new_value' at " +
                        FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        std::string err;
        bool old_value_unused = false;
        if (!core::SetElementUndeveloped(
                state.model, &state.package, element_id, new_value->get<bool>(), old_value_unused, err)) {
            out_error =
                "SetElementUndeveloped failed at " + FormatLocation(tx_seq, event.event_sequence, type) + ": " + err;
            return false;
        }
        return true;
    }

    if (type == "SetElementGid") {
        std::string element_id, gid;
        if (!require_string("element_id", element_id))
            return false;
        if (!require_string("gid", gid))
            return false;
        std::string err;
        // Replay forces the recorded gid (the exact value the live command minted),
        // so live and replay converge. An event only exists because a gid WAS
        // assigned, so a false return here is always a real failure to abort on.
        if (!core::SetElementGid(state.model, &state.package, element_id, gid, err)) {
            out_error = "SetElementGid failed at " + FormatLocation(tx_seq, event.event_sequence, type) + ": " + err;
            return false;
        }
        return true;
    }

    if (type == "ReorderSiblings") {
        std::string dragged_id, target_id, drop_mode_token;
        if (!require_string("dragged_id", dragged_id))
            return false;
        if (!require_string("target_id", target_id))
            return false;
        if (!require_string("drop_mode", drop_mode_token))
            return false;
        core::TreeDropMode drop_mode;
        if (!commands::TreeDropModeFromToken(drop_mode_token, drop_mode)) {
            out_error = "Unknown tree drop mode token '" + drop_mode_token + "' at " +
                        FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        const core::AssuranceTree tree = core::AssuranceTree::Build(state.model, "");
        core::TreeDisplayOrder scratch_order;
        std::string err;
        // `ReorderSiblings` returns false on a genuine no-op (nothing to reorder)
        // AND on error, distinguished by whether `err` is set. A non-empty `err`
        // is a real failure; an empty `err` is an idempotent no-op, so replay
        // succeeds either way except on a true error.
        if (!core::ReorderSiblings(state.model,
                                   &state.package,
                                   tree,
                                   scratch_order,
                                   core::ReorderSiblingsCommand{dragged_id, target_id, drop_mode},
                                   err) &&
            !err.empty()) {
            out_error = "ReorderSiblings failed at " + FormatLocation(tx_seq, event.event_sequence, type) + ": " + err;
            return false;
        }
        return true;
    }

    if (type == "MoveSubtree") {
        std::string dragged_id, new_parent_id;
        if (!require_string("dragged_id", dragged_id))
            return false;
        if (!require_string("new_parent_id", new_parent_id))
            return false;
        const core::AssuranceTree tree = core::AssuranceTree::Build(state.model, "");
        std::string err;
        // Same no-op-vs-error distinction as ReorderSiblings above.
        if (!core::MoveSubtree(
                state.model, &state.package, tree, core::MoveSubtreeCommand{dragged_id, new_parent_id}, err) &&
            !err.empty()) {
            out_error = "MoveSubtree failed at " + FormatLocation(tx_seq, event.event_sequence, type) + ": " + err;
            return false;
        }
        return true;
    }

    if (type == "Undo") {
        // The `Undo` event is a marker that records what was cancelled.
        // The replayer applies it as a no-op: the transactions it cancels
        // are filtered out before this loop runs (see `ReplayFrom`), so
        // by the time we reach an Undo event the model is already in the
        // post-undo state. The marker remains in the log for audit and
        // history-viewer rendering only.
        return true;
    }

    if (type == kWorkingDraftAcceptedEventType) {
        // A working draft accepted as one document (ADR 0016). The event carries
        // the accepted document in full, so the state is replaced by it rather
        // than edited towards it: there was no edit sequence, only the write.
        const std::string xml = AcceptedDocumentFromEvent(event);
        if (xml.empty()) {
            out_error =
                "WorkingDraftAccepted carries no document at " + FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        sacm_adapter::LibraryDocument accepted;
        if (!sacm_adapter::reload_document(accepted, xml)) {
            out_error =
                "The accepted document could not be read at " + FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        state.model = sacm_adapter::project_case(accepted);
        state.package = core::project_library_package_with_tags(accepted);
        return true;
    }

    if (type == "SacmRestoredFromAudit") {
        // Provenance event for the non-destructive remediation flow. The
        // SACM file was rewritten on disk to match the replayed state at
        // the moment this event was appended. The replayer does not need
        // to mutate `state` â€” by construction, replaying transactions up
        // to and including this event yields exactly the model that was
        // written to disk, so subsequent events apply on top of the same
        // model the user observed live.
        return true;
    }

    auto require_identity = [&](std::string& id, std::string& gid) -> bool {
        auto id_it = payload.find("package_id");
        auto gid_it = payload.find("package_gid");
        if (id_it == payload.end() || !id_it->is_string() || gid_it == payload.end() || !gid_it->is_string()) {
            out_error =
                "Missing or non-string identity payload at " + FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        id = id_it->get<std::string>();
        gid = gid_it->get<std::string>();
        return true;
    };

    if (type == "RemoveTerminologyPackage") {
        std::string id, gid;
        if (!require_identity(id, gid))
            return false;
        std::string err;
        if (!core::DeleteTerminologyPackage(state.package, core::TerminologyPackageRef{id, gid}, err)) {
            out_error =
                "DeleteTerminologyPackage failed at " + FormatLocation(tx_seq, event.event_sequence, type) + ": " + err;
            return false;
        }
        return true;
    }

    if (type == "CreateTerminologyPackage") {
        std::string name, description, generated_id, generated_gid;
        if (!require_string("name", name))
            return false;
        if (!require_string("description", description))
            return false;
        if (!require_string("generated_id", generated_id))
            return false;
        if (!require_string("generated_gid", generated_gid))
            return false;
        const core::TerminologyPackageCreateResult result =
            core::CreateTerminologyPackageWithIds(state.package, name, description, generated_id, generated_gid);
        if (!result.success) {
            out_error = "CreateTerminologyPackageWithIds failed at " +
                        FormatLocation(tx_seq, event.event_sequence, type) + ": " + result.error;
            return false;
        }
        return true;
    }

    if (type == "UpdateTerminologyPackage") {
        std::string id, gid, name, description;
        if (!require_identity(id, gid))
            return false;
        if (!require_string("name", name))
            return false;
        if (!require_string("description", description))
            return false;
        std::string err;
        if (!core::UpdateTerminologyPackage(
                state.package, core::TerminologyPackageRef{id, gid}, name, description, err)) {
            out_error =
                "UpdateTerminologyPackage failed at " + FormatLocation(tx_seq, event.event_sequence, type) + ": " + err;
            return false;
        }
        return true;
    }

    if (type == "CreateTerminologyCategory") {
        std::string package_id, package_gid, name, description, generated_id, generated_gid;
        if (!require_string("package_id", package_id))
            return false;
        if (!require_string("package_gid", package_gid))
            return false;
        if (!require_string("name", name))
            return false;
        if (!require_string("description", description))
            return false;
        if (!require_string("generated_id", generated_id))
            return false;
        if (!require_string("generated_gid", generated_gid))
            return false;
        core::TerminologyCategoryDraft draft;
        draft.name = name;
        draft.description = description;
        const core::TerminologyCategoryCreateResult result = core::CreateTerminologyCategoryWithIds(
            state.package, core::TerminologyPackageRef{package_id, package_gid}, draft, generated_id, generated_gid);
        if (!result.success) {
            out_error = "CreateTerminologyCategoryWithIds failed at " +
                        FormatLocation(tx_seq, event.event_sequence, type) + ": " + result.error;
            return false;
        }
        return true;
    }

    if (type == "UpdateTerminologyCategory") {
        std::string package_id, package_gid, category_id, category_gid, name, description;
        if (!require_string("package_id", package_id))
            return false;
        if (!require_string("package_gid", package_gid))
            return false;
        if (!require_string("category_id", category_id))
            return false;
        if (!require_string("category_gid", category_gid))
            return false;
        if (!require_string("name", name))
            return false;
        if (!require_string("description", description))
            return false;
        core::TerminologyCategoryDraft draft;
        draft.name = name;
        draft.description = description;
        std::string err;
        if (!core::UpdateTerminologyCategory(state.package,
                                             core::TerminologyPackageRef{package_id, package_gid},
                                             core::TerminologyCategoryRef{category_id, category_gid},
                                             draft,
                                             err)) {
            out_error = "UpdateTerminologyCategory failed at " + FormatLocation(tx_seq, event.event_sequence, type) +
                        ": " + err;
            return false;
        }
        return true;
    }

    if (type == "DeleteTerminologyCategory") {
        std::string package_id, package_gid, category_id, category_gid;
        if (!require_string("package_id", package_id))
            return false;
        if (!require_string("package_gid", package_gid))
            return false;
        if (!require_string("category_id", category_id))
            return false;
        if (!require_string("category_gid", category_gid))
            return false;
        std::string err;
        if (!core::DeleteTerminologyCategory(state.package,
                                             core::TerminologyPackageRef{package_id, package_gid},
                                             core::TerminologyCategoryRef{category_id, category_gid},
                                             err)) {
            out_error = "DeleteTerminologyCategory failed at " + FormatLocation(tx_seq, event.event_sequence, type) +
                        ": " + err;
            return false;
        }
        return true;
    }

    if (type == "RemoveArgumentPackage") {
        std::string id, gid;
        if (!require_identity(id, gid))
            return false;
        std::string err;
        if (!core::DeleteArgumentPackage(state.package, state.model, id, gid, err)) {
            out_error =
                "DeleteArgumentPackage failed at " + FormatLocation(tx_seq, event.event_sequence, type) + ": " + err;
            return false;
        }
        return true;
    }

    if (type == "CreateTerminologyTerm") {
        std::string package_id, package_gid, value, name, description, external_reference, origin, generated_id,
            generated_gid;
        if (!require_string("package_id", package_id))
            return false;
        if (!require_string("package_gid", package_gid))
            return false;
        if (!require_string("value", value))
            return false;
        if (!require_string("name", name))
            return false;
        if (!require_string("description", description))
            return false;
        if (!require_string("external_reference", external_reference))
            return false;
        if (!require_string("origin", origin))
            return false;
        if (!require_string("generated_id", generated_id))
            return false;
        if (!require_string("generated_gid", generated_gid))
            return false;
        core::TerminologyTermDraft draft;
        draft.value = value;
        draft.name = name;
        draft.description = description;
        draft.externalReference = external_reference;
        draft.origin = origin;
        auto category_refs_it = payload.find("category_refs");
        if (category_refs_it != payload.end() && category_refs_it->is_array()) {
            for (const auto& entry : *category_refs_it) {
                if (entry.is_string())
                    draft.category_refs.push_back(entry.get<std::string>());
            }
        }
        const core::TerminologyTermCreateResult result = core::CreateTerminologyTermWithIds(
            state.package, core::TerminologyPackageRef{package_id, package_gid}, draft, generated_id, generated_gid);
        if (!result.success) {
            out_error = "CreateTerminologyTermWithIds failed at " + FormatLocation(tx_seq, event.event_sequence, type) +
                        ": " + result.error;
            return false;
        }
        return true;
    }

    if (type == "UpdateTerminologyTerm") {
        std::string package_id, package_gid, term_id, term_gid, value, name, description, external_reference, origin;
        if (!require_string("package_id", package_id))
            return false;
        if (!require_string("package_gid", package_gid))
            return false;
        if (!require_string("term_id", term_id))
            return false;
        if (!require_string("term_gid", term_gid))
            return false;
        if (!require_string("value", value))
            return false;
        if (!require_string("name", name))
            return false;
        if (!require_string("description", description))
            return false;
        if (!require_string("external_reference", external_reference))
            return false;
        if (!require_string("origin", origin))
            return false;
        core::TerminologyTermDraft draft;
        draft.value = value;
        draft.name = name;
        draft.description = description;
        draft.externalReference = external_reference;
        draft.origin = origin;
        auto category_refs_it = payload.find("category_refs");
        if (category_refs_it != payload.end() && category_refs_it->is_array()) {
            for (const auto& entry : *category_refs_it) {
                if (entry.is_string())
                    draft.category_refs.push_back(entry.get<std::string>());
            }
        }
        std::string err;
        if (!core::UpdateTerminologyTerm(state.package,
                                         core::TerminologyPackageRef{package_id, package_gid},
                                         core::TerminologyTermRef{term_id, term_gid},
                                         draft,
                                         err)) {
            out_error =
                "UpdateTerminologyTerm failed at " + FormatLocation(tx_seq, event.event_sequence, type) + ": " + err;
            return false;
        }
        core::RefreshVisibleTerminologyContextProjection(state.model, state.package);
        return true;
    }

    if (type == "DeleteTerminologyTerm") {
        std::string package_id, package_gid, term_id, term_gid;
        if (!require_string("package_id", package_id))
            return false;
        if (!require_string("package_gid", package_gid))
            return false;
        if (!require_string("term_id", term_id))
            return false;
        if (!require_string("term_gid", term_gid))
            return false;
        // A cascading delete has no legacy equivalent: `core::DeleteTerminologyTerm`
        // erases the term and leaves the referencing ArtifactReference dangling,
        // which is a different result, not a slower route to the same one. This
        // path is the legacy ORACLE (no production caller replays through it), so
        // quietly producing the other result would make it certify a convergence
        // that does not hold. Fail instead.
        if (const auto it = payload.find("cascade_references");
            it != payload.end() && it->is_boolean() && it->get<bool>()) {
            out_error = "DeleteTerminologyTerm with cascade_references has no legacy replay at " +
                        FormatLocation(tx_seq, event.event_sequence, type) +
                        ": the legacy mutator cannot remove the referencing elements.";
            return false;
        }
        std::string err;
        if (!core::DeleteTerminologyTerm(state.package,
                                         core::TerminologyPackageRef{package_id, package_gid},
                                         core::TerminologyTermRef{term_id, term_gid},
                                         err)) {
            out_error =
                "DeleteTerminologyTerm failed at " + FormatLocation(tx_seq, event.event_sequence, type) + ": " + err;
            return false;
        }
        core::RefreshVisibleTerminologyContextProjection(state.model, state.package);
        return true;
    }

    auto read_context_association = [&](std::string& element_id,
                                        std::string& package_id,
                                        std::string& package_gid,
                                        std::string& term_id,
                                        std::string& term_gid,
                                        core::TerminologyContextForcedIds& forced) -> bool {
        if (!require_string("element_id", element_id))
            return false;
        if (!require_string("package_id", package_id))
            return false;
        if (!require_string("package_gid", package_gid))
            return false;
        if (!require_string("term_id", term_id))
            return false;
        if (!require_string("term_gid", term_gid))
            return false;
        if (!require_string("artifact_reference_id", forced.artifact_reference_id))
            return false;
        if (!require_string("artifact_reference_gid", forced.artifact_reference_gid))
            return false;
        if (!require_string("asserted_context_id", forced.asserted_context_id))
            return false;
        if (!require_string("asserted_context_gid", forced.asserted_context_gid))
            return false;
        return true;
    };

    if (type == "AssociateTerminologyTermWithElement") {
        std::string element_id, package_id, package_gid, term_id, term_gid;
        core::TerminologyContextForcedIds forced;
        if (!read_context_association(element_id, package_id, package_gid, term_id, term_gid, forced))
            return false;
        const core::TerminologyContextAssociationResult result =
            core::AssociateTerminologyTermWithElementWithIds(state.package,
                                                             element_id,
                                                             core::TerminologyPackageRef{package_id, package_gid},
                                                             core::TerminologyTermRef{term_id, term_gid},
                                                             forced);
        if (!result.success) {
            out_error = "AssociateTerminologyTermWithElementWithIds failed at " +
                        FormatLocation(tx_seq, event.event_sequence, type) + ": " + result.error;
            return false;
        }
        return true;
    }

    if (type == "AddTerminologyTermAsVisibleContext") {
        std::string element_id, package_id, package_gid, term_id, term_gid;
        core::TerminologyContextForcedIds forced;
        if (!read_context_association(element_id, package_id, package_gid, term_id, term_gid, forced))
            return false;
        const core::TerminologyContextAssociationResult result =
            core::AddTerminologyTermAsVisibleContextWithIds(state.package,
                                                            element_id,
                                                            core::TerminologyPackageRef{package_id, package_gid},
                                                            core::TerminologyTermRef{term_id, term_gid},
                                                            forced);
        if (!result.success) {
            out_error = "AddTerminologyTermAsVisibleContextWithIds failed at " +
                        FormatLocation(tx_seq, event.event_sequence, type) + ": " + result.error;
            return false;
        }
        core::SyncVisibleTerminologyContextToParser(state.model, state.package, result);
        return true;
    }

    if (type == "RemoveArtifactPackage") {
        std::string id, gid;
        if (!require_identity(id, gid))
            return false;
        std::string err;
        if (!core::DeleteArtifactPackage(state.package, id, gid, err)) {
            out_error =
                "DeleteArtifactPackage failed at " + FormatLocation(tx_seq, event.event_sequence, type) + ": " + err;
            return false;
        }
        return true;
    }

    if (type == "UpdateElementText") {
        std::string element_id, field_token, language, new_value;
        if (!require_string("element_id", element_id))
            return false;
        if (!require_string("field", field_token))
            return false;
        if (!require_string("language", language))
            return false;
        if (!require_string("new_value", new_value))
            return false;
        core::ElementTextField field;
        if (!core::ElementTextFieldFromToken(field_token, field)) {
            out_error = "Unknown element text field token '" + field_token + "' at " +
                        FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        std::string old_value_unused;
        std::string err;
        if (!core::SetElementTextField(
                state.model, &state.package, element_id, field, language, new_value, old_value_unused, err)) {
            out_error =
                "SetElementTextField failed at " + FormatLocation(tx_seq, event.event_sequence, type) + ": " + err;
            return false;
        }
        return true;
    }

    if (type == "UpdateGsnIdentifier") {
        std::string element_id, new_identifier;
        if (!require_string("element_id", element_id))
            return false;
        if (!require_string("new_identifier", new_identifier))
            return false;
        std::string old_identifier_unused;
        std::string error;
        if (!core::SetGsnIdentifier(
                state.model, &state.package, element_id, new_identifier, old_identifier_unused, error)) {
            out_error =
                "SetGsnIdentifier failed at " + FormatLocation(tx_seq, event.event_sequence, type) + ": " + error;
            return false;
        }
        return true;
    }

    if (type == "ApplyProposal") {
        std::string proposal_json;
        if (!require_string("proposal_json", proposal_json))
            return false;
        reviews::ReviewProposal proposal;
        std::string parse_error;
        if (!reviews::DeserializeReviewProposal(proposal_json, proposal, parse_error)) {
            out_error = "Failed to deserialize proposal at " + FormatLocation(tx_seq, event.event_sequence, type) +
                        ": " + parse_error;
            return false;
        }
        std::map<std::string, std::string> predetermined_ids;
        auto generated_it = payload.find("generated_ids");
        if (generated_it == payload.end() || !generated_it->is_object()) {
            out_error =
                "Missing or non-object 'generated_ids' at " + FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        for (auto it = generated_it->begin(); it != generated_it->end(); ++it) {
            if (!it->is_string()) {
                out_error =
                    "Non-string value in 'generated_ids' at " + FormatLocation(tx_seq, event.event_sequence, type);
                return false;
            }
            predetermined_ids[it.key()] = it->get<std::string>();
        }
        reviews::ReviewProposalPatchService patch_service;
        reviews::ApplyProposalResult result =
            patch_service.ApplyProposalWithIds(proposal, state.model, predetermined_ids);
        if (!result.success) {
            out_error = "ApplyProposalWithIds failed at " + FormatLocation(tx_seq, event.event_sequence, type) + ": " +
                        result.error;
            return false;
        }
        core::RebuildSacmArgumentPackageFromParser(state.model, state.package);
        return true;
    }

    // ---- ACP events -----------------------------------------------------
    // Legacy replay runs the SAME `core::acp::*` mutators the live command used,
    // directly on the legacy model/package. AddAcp forces the recorded id via
    // AddAcpWithId; Remove/Upsert take explicit ids.

    if (type == "AddAcp") {
        std::string target_kind, target_id, generated_acp_id;
        if (!require_string("target_kind", target_kind))
            return false;
        if (!require_string("target_id", target_id))
            return false;
        if (!require_string("generated_acp_id", generated_acp_id))
            return false;
        const core::acp::AcpEditResult result =
            core::acp::AddAcpWithId(state.model, &state.package, target_kind, target_id, generated_acp_id);
        if (!result.error.empty()) {
            out_error =
                "AddAcpWithId failed at " + FormatLocation(tx_seq, event.event_sequence, type) + ": " + result.error;
            return false;
        }
        return true;
    }

    if (type == "RemoveAcp") {
        std::string acp_id;
        if (!require_string("acp_id", acp_id))
            return false;
        const core::acp::AcpEditResult result = core::acp::RemoveAcp(state.model, &state.package, acp_id);
        if (!result.error.empty()) {
            out_error =
                "RemoveAcp failed at " + FormatLocation(tx_seq, event.event_sequence, type) + ": " + result.error;
            return false;
        }
        return true;
    }

    if (type == "UpsertAcp") {
        auto acp_it = payload.find("acp");
        if (acp_it == payload.end()) {
            out_error = "Missing 'acp' payload at " + FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        parser::AcpRecord record;
        std::string parse_error;
        if (!commands::DeserializeAcpRecord(*acp_it, record, parse_error)) {
            out_error = "Failed to deserialize ACP record at " + FormatLocation(tx_seq, event.event_sequence, type) +
                        ": " + parse_error;
            return false;
        }
        const core::acp::AcpEditResult result = core::acp::UpsertAcp(state.model, &state.package, record);
        if (!result.error.empty()) {
            out_error =
                "UpsertAcp failed at " + FormatLocation(tx_seq, event.event_sequence, type) + ": " + result.error;
            return false;
        }
        return true;
    }

    if (type == "CreateConfidenceArgumentTree") {
        std::string acp_id, argument_package_id, top_goal_id;
        if (!require_string("acp_id", acp_id))
            return false;
        if (!require_string("argument_package_id", argument_package_id))
            return false;
        if (!require_string("top_goal_id", top_goal_id))
            return false;
        const core::acp::AcpEditResult result = core::acp::CreateConfidenceArgumentTreeForAcpWithIds(
            state.model, &state.package, acp_id, argument_package_id, top_goal_id);
        if (!result.error.empty()) {
            out_error = "CreateConfidenceArgumentTreeForAcpWithIds failed at " +
                        FormatLocation(tx_seq, event.event_sequence, type) + ": " + result.error;
            return false;
        }
        return true;
    }

    if (type == "SetEvidenceLocation") {
        // Library-only: the legacy models carry no Resource location, and the
        // live command refused without a document, so a log holding one was
        // written by a library-backed session.
        return RefuseUnrepresentableReplay(FormatLocation(tx_seq, event.event_sequence, type), out_error);
    }

    if (type == "SetEvidenceAttribute" || type == "ImportEvidenceAssessments") {
        // Library-only for the same reason: the legacy models carry no Artifact
        // record for evidence.
        return RefuseUnrepresentableReplay(FormatLocation(tx_seq, event.event_sequence, type), out_error);
    }

    out_error = "Unknown event type '" + type + "' at " + FormatLocation(tx_seq, event.event_sequence, type);
    return false;
}

} // namespace

bool ApplyEventToLibrary(sacm_adapter::LibraryDocument& document,
                         std::uint64_t tx_seq,
                         const AuditEvent& event,
                         std::string& out_error) {
    const std::string& type = event.event_type;
    const auto& payload = event.payload;

    auto require_string = [&](const char* key, std::string& dest) -> bool {
        auto it = payload.find(key);
        if (it == payload.end() || !it->is_string()) {
            out_error = "Missing or non-string payload field '" + std::string(key) + "' at " +
                        FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        dest = it->get<std::string>();
        return true;
    };

    if (type == "CreateTopGoal") {
        std::string element_id;
        if (!require_string("generated_id", element_id))
            return false;
        const sacm_adapter::AddChildOutcome outcome = sacm_adapter::apply_add_top_goal(document, element_id);
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure(
                "apply_add_top_goal", tx_seq, event, outcome.supported, outcome.applied, outcome.diagnostics);
            return false;
        }
        return true;
    }

    if (type == "CreateChildElement") {
        std::string parent_id, kind_token, element_id, relationship_id;
        if (!require_string("parent_id", parent_id))
            return false;
        if (!require_string("kind", kind_token))
            return false;
        if (!require_string("generated_id", element_id))
            return false;
        if (!require_string("generated_relationship_id", relationship_id))
            return false;
        core::NewElementKind kind;
        if (!commands::NewElementKindFromToken(kind_token, kind)) {
            out_error =
                "Unknown kind token '" + kind_token + "' at " + FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        const sacm_adapter::AddChildOutcome outcome =
            sacm_adapter::apply_add_child(document, parent_id, ToAdapterChildKind(kind), element_id, relationship_id);
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure(
                "apply_add_child", tx_seq, event, outcome.supported, outcome.applied, outcome.diagnostics);
            return false;
        }
        return true;
    }

    if (type == "CreateChallenge") {
        std::string target_id, target_kind_token, source_type_token, element_id, relationship_id;
        if (!require_string("target_id", target_id))
            return false;
        if (!require_string("target_kind", target_kind_token))
            return false;
        if (!require_string("source_type", source_type_token))
            return false;
        if (!require_string("generated_id", element_id))
            return false;
        if (!require_string("generated_relationship_id", relationship_id))
            return false;
        core::ArgumentTarget::Kind target_kind;
        if (!commands::ArgumentTargetKindFromToken(target_kind_token, target_kind)) {
            out_error = "Unknown target kind token '" + target_kind_token + "' at " +
                        FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        core::ChallengeSourceType source_type;
        if (!commands::ChallengeSourceTypeFromToken(source_type_token, source_type)) {
            out_error = "Unknown source type token '" + source_type_token + "' at " +
                        FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        // apply_challenge resolves the target kind (element vs relationship)
        // itself, so `target_kind` is validated only to mirror ApplyEvent's
        // failure surface.
        (void)target_kind;
        const sacm_adapter::AddChildOutcome outcome = sacm_adapter::apply_challenge(
            document, target_id, ToAdapterChallengeSource(source_type), element_id, relationship_id);
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure(
                "apply_challenge", tx_seq, event, outcome.supported, outcome.applied, outcome.diagnostics);
            return false;
        }
        return true;
    }

    if (type == "RemoveElement") {
        std::string element_id, mode_token;
        if (!require_string("element_id", element_id))
            return false;
        if (!require_string("mode", mode_token))
            return false;
        core::RemoveMode mode;
        if (!commands::RemoveModeFromToken(mode_token, mode)) {
            out_error = "Unknown remove mode token '" + mode_token + "' at " +
                        FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        // NodeOnly REPARENTS the removed node's structural children onto its
        // parent (core::ReparentChildrenToParent RETARGETS a child's inference
        // from the node to the parent, or clears a strategy's reasoning) before
        // the scrub. That retarget is not expressible as a set of per-id deletes,
        // so replaying `deleted_ids` through the scrub seam would leave the child
        // inference target-less, drop it, and orphan the promoted node. Bridge
        // NodeOnly through the legacy core::RemoveElement (project -> mutate ->
        // reload), which recomputes the same PlanRemoval the live path recorded --
        // exactly as the legacy replay does.
        if (mode == core::RemoveMode::NodeOnly) {
            // Phase 3c: seam-mapped, matching the live command exactly -- reparent
            // ALONE, mirror its endpoint rewrites, then fall through to the
            // recorded-id deletes both modes share. Diffing after the whole of
            // RemoveElement would also pick up its scrub and try to write endpoint
            // sets no relationship may hold.
            parser::AssuranceCase before = sacm_adapter::project_case(document);
            parser::AssuranceCase reparented = before;
            core::ReparentChildrenToParent(reparented, nullptr, element_id);
            for (const parser::SacmElement& updated : reparented.elements) {
                if (!parser::IsRelationshipElement(updated))
                    continue;
                const parser::SacmElement* original = parser::FindElementById(before, updated.id);
                if (original == nullptr)
                    continue;
                if (original->source_refs == updated.source_refs && original->target_refs == updated.target_refs &&
                    original->reasoning_ref == updated.reasoning_ref) {
                    continue;
                }
                // Mirror the live command (ReparentedRelationships in
                // element_commands.cpp): a rewrite the reparent left structurally
                // empty is the scrub showing through -- the library refuses it
                // (clause 11.13 source[1..*]) and the relationship dies with the
                // node via the delete scrub below instead. Same predicate as the
                // legacy RemoveElement drop, so live, replayed and legacy agree.
                // (This projection carries no render placeholders, so the live
                // path's placeholder skip has nothing to skip here.)
                if (core::IsParserRelationshipDangling(updated))
                    continue;
                const sacm_adapter::EditOutcome ends = sacm_adapter::apply_set_relationship_ends(
                    document, updated.id, updated.source_refs, updated.target_refs, updated.reasoning_ref);
                if (!ends.supported || !ends.applied) {
                    out_error = FormatSeamFailure("apply_set_relationship_ends(reparent)",
                                                  tx_seq,
                                                  event,
                                                  ends.supported,
                                                  ends.applied,
                                                  ends.diagnostics);
                    return false;
                }
            }
        }

        // NodeAndDescendants removes a closed subtree with no reparenting, so the
        // library replay depends only on the recorded `deleted_ids` -- the exact
        // PlanRemoval set the live path computed. The scrub seam deletes each
        // planned id through ReferenceDeletePolicy::ScrubReferences, which
        // reproduces the legacy scrub-then-drop for every subtree shape (a
        // strategy's shared inference survives, sourced by the sub-goals that
        // remain, rather than cascading away).
        auto deleted_it = payload.find("deleted_ids");
        if (deleted_it == payload.end() || !deleted_it->is_array()) {
            out_error = "Missing or non-array 'deleted_ids' at " + FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        for (const auto& entry : *deleted_it) {
            if (!entry.is_string()) {
                out_error =
                    "Non-string value in 'deleted_ids' at " + FormatLocation(tx_seq, event.event_sequence, type);
                return false;
            }
            const std::string id = entry.get<std::string>();
            const sacm_adapter::DeleteOutcome outcome = sacm_adapter::apply_delete_element(document, id);
            if (!outcome.supported || !outcome.applied) {
                out_error = FormatSeamFailure("apply_delete_element(" + id + ")",
                                              tx_seq,
                                              event,
                                              outcome.supported,
                                              outcome.applied,
                                              outcome.diagnostics);
                return false;
            }
        }
        return true;
    }

    if (type == "SetElementGid") {
        std::string element_id, gid;
        if (!require_string("element_id", element_id))
            return false;
        if (!require_string("gid", gid))
            return false;
        // Phase 2a: seam-mapped. The gid is not generated here -- the live command
        // minted it and the payload carries it -- so there is nothing for a bridge
        // to reproduce, only a value to store.
        const sacm_adapter::EditOutcome outcome = sacm_adapter::apply_set_gid(document, element_id, gid);
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure(
                "apply_set_gid", tx_seq, event, outcome.supported, outcome.applied, outcome.diagnostics);
            return false;
        }
        return true;
    }

    // The tree drop mutators (reorder siblings / move a subtree) have no parity
    // library seam -- they reorder relationships/sources and re-mint relationship
    // ids in the legacy vocabulary. Bridge them: run the SAME `core::*` mutator the
    // live path uses onto models projected FROM the library, then re-derive the
    // library from the serialized package. `MoveSubtree` mints its new relationship
    // id deterministically (`GenerateRelationshipId`), so the projected scratch
    // regenerates the same id the live bridge did and they converge.
    if (type == "ReorderSiblings") {
        std::string dragged_id, target_id, drop_mode_token;
        if (!require_string("dragged_id", dragged_id))
            return false;
        if (!require_string("target_id", target_id))
            return false;
        if (!require_string("drop_mode", drop_mode_token))
            return false;
        core::TreeDropMode drop_mode;
        if (!commands::TreeDropModeFromToken(drop_mode_token, drop_mode)) {
            out_error = "Unknown tree drop mode token '" + drop_mode_token + "' at " +
                        FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        const std::string location = FormatLocation(tx_seq, event.event_sequence, type);

        // Seam-mapped in the same change as the live command, which is not optional:
        // the live path now applies natively on documents the bridge REFUSES, so a
        // bridged replay of the same event would refuse what the live edit accepted
        // and the log would stop replaying.
        parser::AssuranceCase before = sacm_adapter::project_case(document);
        parser::AssuranceCase after = before;
        const core::AssuranceTree tree = core::AssuranceTree::Build(after, "");
        core::TreeDisplayOrder replayed_order;
        std::string reorder_error;
        if (!core::ReorderSiblings(after,
                                   nullptr,
                                   tree,
                                   replayed_order,
                                   core::ReorderSiblingsCommand{dragged_id, target_id, drop_mode},
                                   reorder_error)) {
            // False with an empty error is the mutator's no-op, not a failure --
            // the same distinction the live command relies on.
            if (!reorder_error.empty()) {
                out_error = "ReorderSiblings failed at " + location + ": " + reorder_error;
                return false;
            }
            return true;
        }
        if (!commands::ApplySiblingReorderToLibrary(document, before, after, out_error)) {
            out_error = "ReorderSiblings failed at " + location + ": " + out_error;
            return false;
        }
        return true;
    }

    if (type == "MoveSubtree") {
        std::string dragged_id, new_parent_id;
        if (!require_string("dragged_id", dragged_id))
            return false;
        if (!require_string("new_parent_id", new_parent_id))
            return false;
        const std::string location = FormatLocation(tx_seq, event.event_sequence, type);
        const BridgeMutator mutate =
            [&](parser::AssuranceCase& model, sacm::AssuranceCasePackage& package, std::string& err) -> bool {
            const core::AssuranceTree tree = core::AssuranceTree::Build(model, "");
            std::string move_error;
            if (!core::MoveSubtree(
                    model, &package, tree, core::MoveSubtreeCommand{dragged_id, new_parent_id}, move_error) &&
                !move_error.empty()) {
                err = "MoveSubtree (bridge) failed at " + location + ": " + move_error;
                return false;
            }
            return true;
        };

        // Seam-mapped, mirroring `MoveSubtreeCommand::Apply` step for step: run the
        // same mutator on a projection, diff what it decided, write that through the
        // seams -- and fall back to the bridge on exactly the two shapes the live
        // path falls back on, before anything is written.
        parser::AssuranceCase before = sacm_adapter::project_case(document);
        parser::AssuranceCase after = before;
        const core::AssuranceTree tree = core::AssuranceTree::Build(after, "");
        std::string move_error;
        if (!core::MoveSubtree(after, nullptr, tree, core::MoveSubtreeCommand{dragged_id, new_parent_id}, move_error) &&
            !move_error.empty()) {
            out_error = "MoveSubtree failed at " + location + ": " + move_error;
            return false;
        }
        core::MoveSubtreePlan plan = core::PlanMoveSubtreeFromDiff(before, after);
        if (plan.touches_non_relationships || !plan.unrepresentable_reason.empty()) {
            return RefuseUnrepresentableReplay(location, out_error);
        }
        // Prefer the RECORDED relationship id over the one this replay's mutator
        // just minted. They agree today -- `GenerateRelationshipId` derives from the
        // model -- but the recorded id is what the document actually got, and an
        // audit log has to keep replaying if that derivation ever changes. Events
        // written before the field existed carry no id, and for those the minted one
        // is exactly what they were always replayed with.
        std::string recorded_relationship_id;
        if (event.payload.contains("new_relationship_id") && event.payload["new_relationship_id"].is_string())
            recorded_relationship_id = event.payload["new_relationship_id"].get<std::string>();
        if (!recorded_relationship_id.empty() && plan.created.size() == 1)
            plan.created.front().id = recorded_relationship_id;

        if (!commands::ApplyMoveSubtreePlanToLibrary(document, plan, out_error)) {
            out_error = "MoveSubtree failed at " + location + ": " + out_error;
            return false;
        }
        return true;
    }

    if (type == "UpdateElementText") {
        std::string element_id, field_token, language, new_value;
        if (!require_string("element_id", element_id))
            return false;
        if (!require_string("field", field_token))
            return false;
        if (!require_string("language", language))
            return false;
        if (!require_string("new_value", new_value))
            return false;
        core::ElementTextField field;
        if (!core::ElementTextFieldFromToken(field_token, field)) {
            out_error = "Unknown element text field token '" + field_token + "' at " +
                        FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        // Name and Content replay through the library seam, matching the live
        // command (phase 3f). They used to bridge, and the reason is worth keeping
        // because it was a MODEL difference, not a missing operation:
        //
        //   The app modelled two claim text slots. The legacy POD kept them as two
        //   independent fields whose values depended on how the claim was LOADED --
        //   a `content=` attribute filled one, a `description=` attribute the other
        //   -- while the library stores clause-8.9 Description[0..*], where a claim
        //   with a single Description is indistinguishable whichever way its text
        //   arrived. Two legacy states collapsed to one library state, so no seam
        //   could reproduce both, and the replay oracle compared against the legacy
        //   mutator. Measured at the time:
        //     library: content="fully safe", description="fully safe"
        //     legacy:  content="fully safe", description="The system is safe."
        //
        // ADR 0012 removed the difference rather than working around it: a claim
        // carries ONE Description and that Description is its statement. There is
        // no second slot for the two routes to disagree about, so live and replay
        // now run the same seam and agree by construction.
        //
        // Description still bridges. It remains meaningful for every kind that
        // genuinely has a note, and on a claim-like element the seam refuses it.
        if (field == core::ElementTextField::Name || field == core::ElementTextField::Content) {
            const sacm_adapter::EditOutcome outcome =
                sacm_adapter::apply_text_edit(document, element_id, ToAdapterTextField(field), language, new_value);
            if (!outcome.supported || !outcome.applied) {
                out_error = FormatSeamFailure(
                    "apply_text_edit", tx_seq, event, outcome.supported, outcome.applied, outcome.diagnostics);
                return false;
            }
            return true;
        }
        const std::string location = FormatLocation(tx_seq, event.event_sequence, type);
        const BridgeMutator mutate =
            [&](parser::AssuranceCase& model, sacm::AssuranceCasePackage& package, std::string& err) -> bool {
            std::string old_value_unused;
            std::string set_error;
            if (!core::SetElementTextField(
                    model, &package, element_id, field, language, new_value, old_value_unused, set_error)) {
                err = "SetElementTextField (bridge) failed at " + location + ": " + set_error;
                return false;
            }
            return true;
        };
        return RefuseUnrepresentableReplay(location, out_error);
    }

    if (type == "UpdateGsnIdentifier") {
        std::string element_id, new_identifier;
        if (!require_string("element_id", element_id))
            return false;
        if (!require_string("new_identifier", new_identifier))
            return false;
        // Slice 2b: seam-mapped, matching the live command. The app's editing
        // rules (non-empty, no surrounding whitespace, unique among the nodes)
        // are NOT re-checked -- they gated whether the event was recorded, and
        // re-applying them here would refuse to reproduce history the user
        // legitimately made. The recorded identifier is written as recorded.
        const sacm_adapter::EditOutcome outcome =
            sacm_adapter::apply_set_gsn_identifier(document, element_id, new_identifier);
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure(
                "apply_set_gsn_identifier", tx_seq, event, outcome.supported, outcome.applied, outcome.diagnostics);
            return false;
        }
        return true;
    }

    // The GSN repair events. Bridged rather than routed to a native seam for the
    // same reason the live commands are: the library has no operation for "drop
    // one reference" or "move a reasoning", and its delete seam would remove the
    // referenced element, which for a broken endpoint does not exist -- that
    // being the whole defect. `RemoveRelationship` could use apply_delete_element
    // directly, but bridging all four keeps live and replay on one mutator each,
    // which is what makes them converge by construction.
    if (type == "RemoveRelationship") {
        std::string relationship_id;
        if (!require_string("relationship_id", relationship_id))
            return false;
        // Slice 2b: seam-mapped, closing an asymmetry that predates the retirement
        // work. `RemoveRelationshipCommand` has applied `apply_delete_element`
        // natively since the Phase 2 slice 2b-1 element flip, while this branch
        // went on bridging -- so a relationship removal and its own replay ran
        // different code, and only agreed because the seam's scrub-then-drop
        // happens to reproduce `core::RemoveRelationship`. That agreement is now
        // structural rather than coincidental.
        const sacm_adapter::DeleteOutcome outcome = sacm_adapter::apply_delete_element(document, relationship_id);
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure("apply_delete_element(relationship)",
                                          tx_seq,
                                          event,
                                          outcome.supported,
                                          outcome.applied,
                                          outcome.diagnostics);
            return false;
        }
        return true;
    }

    if (type == "DropRelationshipReference") {
        std::string relationship_id, reference;
        if (!require_string("relationship_id", relationship_id))
            return false;
        if (!require_string("reference", reference))
            return false;
        // Phase 3a: the same scratch-then-seams route the live command takes, so
        // a repair and its own replay are one code path. The reference being
        // dropped resolves to nothing -- that is the defect being repaired -- so
        // the delete seam is no use; `apply_set_relationship_ends` rewrites the
        // endpoints, or the relationship goes when scrubbing empties it.
        const std::string location = FormatLocation(tx_seq, event.event_sequence, type);
        // Model only, as above.
        parser::AssuranceCase model = sacm_adapter::project_case(document);
        bool removed_relationship = false;
        std::string drop_error;
        if (!core::DropRelationshipReference(
                model, nullptr, relationship_id, reference, removed_relationship, drop_error)) {
            out_error = "DropRelationshipReference failed at " + location + ": " + drop_error;
            return false;
        }
        if (removed_relationship) {
            const sacm_adapter::DeleteOutcome deleted = sacm_adapter::apply_delete_element(document, relationship_id);
            if (!deleted.supported || !deleted.applied) {
                out_error = FormatSeamFailure("apply_delete_element(emptied relationship)",
                                              tx_seq,
                                              event,
                                              deleted.supported,
                                              deleted.applied,
                                              deleted.diagnostics);
                return false;
            }
            return true;
        }
        const parser::SacmElement* repaired = parser::FindElementById(model, relationship_id);
        if (repaired == nullptr) {
            out_error = "Relationship " + relationship_id + " disappeared while replaying at " + location;
            return false;
        }
        const sacm_adapter::EditOutcome ends = sacm_adapter::apply_set_relationship_ends(
            document, relationship_id, repaired->source_refs, repaired->target_refs, repaired->reasoning_ref);
        if (!ends.supported || !ends.applied) {
            out_error = FormatSeamFailure(
                "apply_set_relationship_ends", tx_seq, event, ends.supported, ends.applied, ends.diagnostics);
            return false;
        }
        return true;
    }

    if (type == "MoveStrategyToReasoning") {
        std::string relationship_id, strategy_id;
        if (!require_string("relationship_id", relationship_id))
            return false;
        if (!require_string("strategy_id", strategy_id))
            return false;
        // Phase 3b: the same scratch-then-seam route the live command takes, so
        // the move and its own replay are one code path.
        const std::string location = FormatLocation(tx_seq, event.event_sequence, type);
        // Model only: nothing below reads a legacy package, and projecting one per
        // event is the expensive half of this branch.
        parser::AssuranceCase model = sacm_adapter::project_case(document);
        std::string move_error;
        if (!core::MoveStrategyToReasoning(model, nullptr, relationship_id, strategy_id, move_error)) {
            out_error = "MoveStrategyToReasoning failed at " + location + ": " + move_error;
            return false;
        }
        const parser::SacmElement* moved = parser::FindElementById(model, relationship_id);
        if (moved == nullptr) {
            out_error = "Inference " + relationship_id + " disappeared while replaying at " + location;
            return false;
        }
        const sacm_adapter::EditOutcome ends = sacm_adapter::apply_set_relationship_ends(
            document, relationship_id, moved->source_refs, moved->target_refs, moved->reasoning_ref);
        if (!ends.supported || !ends.applied) {
            out_error = FormatSeamFailure(
                "apply_set_relationship_ends", tx_seq, event, ends.supported, ends.applied, ends.diagnostics);
            return false;
        }
        return true;
    }

    if (type == "SetElementUndeveloped") {
        std::string element_id;
        if (!require_string("element_id", element_id))
            return false;
        const auto new_value = payload.find("new_value");
        if (new_value == payload.end() || !new_value->is_boolean()) {
            out_error = "Missing or non-boolean payload field 'new_value' at " +
                        FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        // Slice 2b: seam-mapped, matching the live command. The seam refuses an
        // element whose declaration is already saying something else, which the
        // live command refused too -- so an event that exists was applicable when
        // it was recorded and stays applicable here.
        const sacm_adapter::EditOutcome outcome =
            sacm_adapter::apply_set_undeveloped(document, element_id, new_value->get<bool>());
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure(
                "apply_set_undeveloped", tx_seq, event, outcome.supported, outcome.applied, outcome.diagnostics);
            return false;
        }
        return true;
    }

    // The three package removals, all seam-mapped. `package_gid` is read and not
    // used: the payload contract requires it, and a replay must reject a malformed
    // event rather than quietly accept one missing a field its own writer promised.
    // The seam addresses the package by id.
    if (type == "RemoveArgumentPackage") {
        std::string id, gid;
        if (!require_string("package_id", id))
            return false;
        if (!require_string("package_gid", gid))
            return false;
        // The library seam (recursive DeleteElement) reproduces legacy
        // `DeleteArgumentPackage` exactly (proven in test_sacm_library_edit),
        // so this is seam-mapped rather than bridged.
        const sacm_adapter::DeleteOutcome outcome = sacm_adapter::apply_delete_package(document, id);
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure(
                "apply_delete_package", tx_seq, event, outcome.supported, outcome.applied, outcome.diagnostics);
            return false;
        }
        return true;
    }

    if (type == "RemoveArtifactPackage") {
        std::string id, gid;
        if (!require_string("package_id", id))
            return false;
        if (!require_string("package_gid", gid))
            return false;
        // Phase 2a: seam-mapped, matching the live command.
        const sacm_adapter::DeleteOutcome outcome = sacm_adapter::apply_delete_package(document, id);
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure("apply_delete_package(artifact)",
                                          tx_seq,
                                          event,
                                          outcome.supported,
                                          outcome.applied,
                                          outcome.diagnostics);
            return false;
        }
        return true;
    }

    if (type == "RemoveTerminologyPackage") {
        std::string id, gid;
        if (!require_string("package_id", id))
            return false;
        if (!require_string("package_gid", gid))
            return false;
        // Phase 2a: seam-mapped. The live command's "the package must be empty"
        // guard is deliberately NOT re-checked here. It is an editing rule that
        // gated whether the event was ever recorded; re-applying it on replay would
        // refuse to reproduce history the user legitimately made -- and the seam
        // deletes recursively, which is what the recorded event means.
        const sacm_adapter::DeleteOutcome outcome = sacm_adapter::apply_delete_package(document, id);
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure("apply_delete_package(terminology)",
                                          tx_seq,
                                          event,
                                          outcome.supported,
                                          outcome.applied,
                                          outcome.diagnostics);
            return false;
        }
        return true;
    }

    // ---- Terminology events ---------------------------------------------
    // Phase 2 slice 2a routed the gid-minting CREATE and ASSOCIATE events back
    // through the library seams; Phase 1 of the bridge retirement flipped the
    // matching LIVE commands onto the same seams, so both sides of the
    // convergence check now run identical code.
    //
    // Both the recorded id AND the recorded gid are handed to the seam. Passing
    // only the id used to be enough on the argument that a fresh element's gid is
    // always the base `gid-<id>` -- but gid space is independent of id space, so a
    // document already carrying `gid-TP1` on an unrelated element makes the legacy
    // generator emit `gid-TP1-2`, and reconstructing from the id would have
    // replayed a different gid than the one the audit log records.
    // UPDATE/DELETE mint no identity and were already seam-mapped.

    if (type == "CreateTerminologyPackage") {
        std::string name, description, generated_id, generated_gid;
        if (!require_string("name", name))
            return false;
        if (!require_string("description", description))
            return false;
        if (!require_string("generated_id", generated_id))
            return false;
        if (!require_string("generated_gid", generated_gid))
            return false;
        const sacm_adapter::TerminologyCreateOutcome outcome =
            sacm_adapter::apply_create_terminology_package(document, name, description, generated_id, generated_gid);
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure("apply_create_terminology_package",
                                          tx_seq,
                                          event,
                                          outcome.supported,
                                          outcome.applied,
                                          outcome.diagnostics);
            return false;
        }
        return true;
    }

    if (type == "UpdateTerminologyPackage") {
        std::string id, gid, name, description;
        if (!require_string("package_id", id))
            return false;
        if (!require_string("package_gid", gid))
            return false;
        if (!require_string("name", name))
            return false;
        if (!require_string("description", description))
            return false;
        const sacm_adapter::TerminologyEditOutcome outcome =
            sacm_adapter::apply_update_terminology_package(document, id, name, description);
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure("apply_update_terminology_package",
                                          tx_seq,
                                          event,
                                          outcome.supported,
                                          outcome.applied,
                                          outcome.diagnostics);
            return false;
        }
        return true;
    }

    if (type == "CreateTerminologyCategory") {
        std::string package_id, package_gid, name, description, generated_id, generated_gid;
        if (!require_string("package_id", package_id))
            return false;
        if (!require_string("package_gid", package_gid))
            return false;
        if (!require_string("name", name))
            return false;
        if (!require_string("description", description))
            return false;
        if (!require_string("generated_id", generated_id))
            return false;
        if (!require_string("generated_gid", generated_gid))
            return false;
        const sacm_adapter::TerminologyCreateOutcome outcome = sacm_adapter::apply_create_terminology_category(
            document, package_id, name, description, generated_id, generated_gid);
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure("apply_create_terminology_category",
                                          tx_seq,
                                          event,
                                          outcome.supported,
                                          outcome.applied,
                                          outcome.diagnostics);
            return false;
        }
        return true;
    }

    if (type == "UpdateTerminologyCategory") {
        std::string package_id, package_gid, category_id, category_gid, name, description;
        if (!require_string("package_id", package_id))
            return false;
        if (!require_string("package_gid", package_gid))
            return false;
        if (!require_string("category_id", category_id))
            return false;
        if (!require_string("category_gid", category_gid))
            return false;
        if (!require_string("name", name))
            return false;
        if (!require_string("description", description))
            return false;
        const sacm_adapter::TerminologyEditOutcome outcome =
            sacm_adapter::apply_update_terminology_category(document, category_id, name, description);
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure("apply_update_terminology_category",
                                          tx_seq,
                                          event,
                                          outcome.supported,
                                          outcome.applied,
                                          outcome.diagnostics);
            return false;
        }
        return true;
    }

    if (type == "DeleteTerminologyCategory") {
        std::string package_id, package_gid, category_id, category_gid;
        if (!require_string("package_id", package_id))
            return false;
        if (!require_string("package_gid", package_gid))
            return false;
        if (!require_string("category_id", category_id))
            return false;
        if (!require_string("category_gid", category_gid))
            return false;
        const sacm_adapter::TerminologyEditOutcome outcome =
            sacm_adapter::apply_delete_terminology_element(document, category_id);
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure("apply_delete_terminology_element(category)",
                                          tx_seq,
                                          event,
                                          outcome.supported,
                                          outcome.applied,
                                          outcome.diagnostics);
            return false;
        }
        return true;
    }

    if (type == "CreateTerminologyTerm") {
        std::string package_id, package_gid, value, name, description, external_reference, origin, generated_id,
            generated_gid;
        if (!require_string("package_id", package_id))
            return false;
        if (!require_string("package_gid", package_gid))
            return false;
        if (!require_string("value", value))
            return false;
        if (!require_string("name", name))
            return false;
        if (!require_string("description", description))
            return false;
        if (!require_string("external_reference", external_reference))
            return false;
        if (!require_string("origin", origin))
            return false;
        if (!require_string("generated_id", generated_id))
            return false;
        if (!require_string("generated_gid", generated_gid))
            return false;
        sacm_adapter::TerminologyTermFields fields;
        fields.value = value;
        fields.name = name;
        fields.description = description;
        fields.external_reference = external_reference;
        fields.origin = origin;
        auto category_refs_it = payload.find("category_refs");
        if (category_refs_it != payload.end() && category_refs_it->is_array()) {
            for (const auto& entry : *category_refs_it) {
                if (entry.is_string())
                    fields.category_refs.push_back(entry.get<std::string>());
            }
        }
        const sacm_adapter::TerminologyCreateOutcome outcome =
            sacm_adapter::apply_create_terminology_term(document, package_id, fields, generated_id, generated_gid);
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure("apply_create_terminology_term",
                                          tx_seq,
                                          event,
                                          outcome.supported,
                                          outcome.applied,
                                          outcome.diagnostics);
            return false;
        }
        return true;
    }

    if (type == "UpdateTerminologyTerm") {
        std::string package_id, package_gid, term_id, term_gid, value, name, description, external_reference, origin;
        if (!require_string("package_id", package_id))
            return false;
        if (!require_string("package_gid", package_gid))
            return false;
        if (!require_string("term_id", term_id))
            return false;
        if (!require_string("term_gid", term_gid))
            return false;
        if (!require_string("value", value))
            return false;
        if (!require_string("name", name))
            return false;
        if (!require_string("description", description))
            return false;
        if (!require_string("external_reference", external_reference))
            return false;
        if (!require_string("origin", origin))
            return false;
        sacm_adapter::TerminologyTermFields fields;
        fields.value = value;
        fields.name = name;
        fields.description = description;
        fields.external_reference = external_reference;
        fields.origin = origin;
        auto category_refs_it = payload.find("category_refs");
        if (category_refs_it != payload.end() && category_refs_it->is_array()) {
            for (const auto& entry : *category_refs_it) {
                if (entry.is_string())
                    fields.category_refs.push_back(entry.get<std::string>());
            }
        }
        const sacm_adapter::TerminologyEditOutcome outcome =
            sacm_adapter::apply_update_terminology_term(document, term_id, fields);
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure("apply_update_terminology_term",
                                          tx_seq,
                                          event,
                                          outcome.supported,
                                          outcome.applied,
                                          outcome.diagnostics);
            return false;
        }
        return true;
    }

    if (type == "DeleteTerminologyTerm") {
        std::string package_id, package_gid, term_id, term_gid;
        if (!require_string("package_id", package_id))
            return false;
        if (!require_string("package_gid", package_gid))
            return false;
        if (!require_string("term_id", term_id))
            return false;
        if (!require_string("term_gid", term_gid))
            return false;
        // The user's confirmed answer to "this also removes N references", read
        // back rather than re-derived: a replay has nobody to ask, and deriving
        // it from the document would let a later state answer the question
        // differently than the user did. ABSENT means false -- events recorded
        // before the cascade existed were written under the non-cascading
        // behaviour and must keep replaying that way, so this is deliberately
        // not `require_bool`.
        bool cascade_references = false;
        if (const auto it = payload.find("cascade_references"); it != payload.end()) {
            if (!it->is_boolean()) {
                out_error = "Non-boolean payload field 'cascade_references' at " +
                            FormatLocation(tx_seq, event.event_sequence, type);
                return false;
            }
            cascade_references = it->get<bool>();
        }
        const sacm_adapter::TerminologyEditOutcome outcome =
            sacm_adapter::apply_delete_terminology_element(document, term_id, cascade_references);
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure("apply_delete_terminology_element(term)",
                                          tx_seq,
                                          event,
                                          outcome.supported,
                                          outcome.applied,
                                          outcome.diagnostics);
            return false;
        }
        return true;
    }

    // Reads the association payload including the FORCED artifact-reference /
    // asserted-context ids AND gids, both of which the seams now take.
    auto read_context_association = [&](std::string& element_id,
                                        std::string& package_id,
                                        std::string& package_gid,
                                        std::string& term_id,
                                        std::string& term_gid,
                                        core::TerminologyContextForcedIds& forced) -> bool {
        if (!require_string("element_id", element_id))
            return false;
        if (!require_string("package_id", package_id))
            return false;
        if (!require_string("package_gid", package_gid))
            return false;
        if (!require_string("term_id", term_id))
            return false;
        if (!require_string("term_gid", term_gid))
            return false;
        if (!require_string("artifact_reference_id", forced.artifact_reference_id))
            return false;
        if (!require_string("artifact_reference_gid", forced.artifact_reference_gid))
            return false;
        if (!require_string("asserted_context_id", forced.asserted_context_id))
            return false;
        if (!require_string("asserted_context_gid", forced.asserted_context_gid))
            return false;
        return true;
    };

    if (type == "AssociateTerminologyTermWithElement") {
        std::string element_id, package_id, package_gid, term_id, term_gid;
        core::TerminologyContextForcedIds forced;
        if (!read_context_association(element_id, package_id, package_gid, term_id, term_gid, forced))
            return false;
        // The forced element ids are passed to the seam verbatim; the seam mints
        // each created element's `gid-<id>` from its id, reproducing the legacy
        // forced gids exactly.
        const sacm_adapter::TerminologyContextOutcome outcome =
            sacm_adapter::apply_associate_terminology_term(document, element_id, term_id, ToSeamIdentities(forced));
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure("apply_associate_terminology_term",
                                          tx_seq,
                                          event,
                                          outcome.supported,
                                          outcome.applied,
                                          outcome.diagnostics);
            return false;
        }
        return true;
    }

    if (type == "AddTerminologyTermAsVisibleContext") {
        std::string element_id, package_id, package_gid, term_id, term_gid;
        core::TerminologyContextForcedIds forced;
        if (!read_context_association(element_id, package_id, package_gid, term_id, term_gid, forced))
            return false;
        const sacm_adapter::TerminologyContextOutcome outcome = sacm_adapter::apply_add_terminology_visible_context(
            document, element_id, term_id, ToSeamIdentities(forced));
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure("apply_add_terminology_visible_context",
                                          tx_seq,
                                          event,
                                          outcome.supported,
                                          outcome.applied,
                                          outcome.diagnostics);
            return false;
        }
        return true;
    }

    // ---- Bridge events ---------------------------------------------------
    // No parity seam exists (the library's equivalent diverges from the legacy
    // mutator), so apply the SAME legacy mutator the live path uses onto models
    // projected FROM the library, then re-derive the library from the serialized
    // package (BridgeViaLegacy). This keeps library-primary replay converged
    // with the legacy live path for these events.

    if (type == "ApplyProposal") {
        std::string proposal_json;
        if (!require_string("proposal_json", proposal_json))
            return false;
        reviews::ReviewProposal proposal;
        std::string parse_error;
        if (!reviews::DeserializeReviewProposal(proposal_json, proposal, parse_error)) {
            out_error = "Failed to deserialize proposal at " + FormatLocation(tx_seq, event.event_sequence, type) +
                        ": " + parse_error;
            return false;
        }
        std::map<std::string, std::string> predetermined_ids;
        auto generated_it = payload.find("generated_ids");
        if (generated_it == payload.end() || !generated_it->is_object()) {
            out_error =
                "Missing or non-object 'generated_ids' at " + FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        for (auto it = generated_it->begin(); it != generated_it->end(); ++it) {
            if (!it->is_string()) {
                out_error =
                    "Non-string value in 'generated_ids' at " + FormatLocation(tx_seq, event.event_sequence, type);
                return false;
            }
            predetermined_ids[it.key()] = it->get<std::string>();
        }
        const std::string location = FormatLocation(tx_seq, event.event_sequence, type);

        // Same scratch-compute the live command runs, for the same reason it must
        // be the same: the recorded ids come from the event payload, so both sides
        // give the patch service identical input and mirror an identical plan.
        // Replaying through the bridge while the live edit went native would make
        // the two agree only as far as the projection happened to be faithful.
        const parser::AssuranceCase before = sacm_adapter::project_case(document);
        parser::AssuranceCase scratch = before;
        reviews::ReviewProposalPatchService patch_service;
        const reviews::ApplyProposalResult result =
            patch_service.ApplyProposalWithIds(proposal, scratch, predetermined_ids);
        if (!result.success) {
            out_error = "ApplyProposalWithIds failed at " + location + ": " + result.error;
            return false;
        }
        const std::string package_id = sacm_adapter::resolve_argument_package_id(document, proposal.anchor_element_id);
        const std::string terminology_package_id = sacm_adapter::resolve_terminology_package_id(document);
        const reviews::ProposalPlan plan =
            reviews::PlanProposalFromDiff(before, scratch, package_id, terminology_package_id);
        if (plan.unrepresentable_reason.empty()) {
            return reviews::ApplyProposalPlanToLibrary(document, plan, out_error);
        }

        // The plan declined, so this proposal was recorded by a live edit that
        // also declined -- replay it the way it was written.
        const BridgeMutator mutate =
            [&](parser::AssuranceCase& model, sacm::AssuranceCasePackage& package, std::string& err) -> bool {
            reviews::ReviewProposalPatchService bridge_patch_service;
            reviews::ApplyProposalResult bridge_result =
                bridge_patch_service.ApplyProposalWithIds(proposal, model, predetermined_ids);
            if (!bridge_result.success) {
                err = "ApplyProposalWithIds (bridge) failed at " + location + ": " + bridge_result.error;
                return false;
            }
            core::RebuildSacmArgumentPackageFromParser(model, package);
            return true;
        };
        return RefuseUnrepresentableReplay(location, out_error);
    }

    // ACP events have no parity library seam (ACPs are legacy vendor TaggedValues,
    // not a library-native operation), so they bridge: run the SAME legacy
    // `core::acp::*` mutator the live path used onto a projected package, then
    // re-derive the library. AddAcp forces the recorded id via AddAcpWithId, so
    // the bridged replay reproduces the legacy result and converges by construction.
    if (type == "AddAcp") {
        std::string target_kind, target_id, generated_acp_id;
        if (!require_string("target_kind", target_kind))
            return false;
        if (!require_string("target_id", target_id))
            return false;
        if (!require_string("generated_acp_id", generated_acp_id))
            return false;
        const std::string location = FormatLocation(tx_seq, event.event_sequence, type);
        return ApplyAcpViaSeams(
            document,
            location,
            generated_acp_id,
            /*expect_removed=*/false,
            [&](parser::AssuranceCase& model, sacm::AssuranceCasePackage& package) {
                return core::acp::AddAcpWithId(model, &package, target_kind, target_id, generated_acp_id);
            },
            out_error);
    }

    if (type == "RemoveAcp") {
        std::string acp_id;
        if (!require_string("acp_id", acp_id))
            return false;
        const std::string location = FormatLocation(tx_seq, event.event_sequence, type);
        return ApplyAcpViaSeams(
            document,
            location,
            acp_id,
            /*expect_removed=*/true,
            [&](parser::AssuranceCase& model, sacm::AssuranceCasePackage& package) {
                return core::acp::RemoveAcp(model, &package, acp_id);
            },
            out_error);
    }

    if (type == "UpsertAcp") {
        auto acp_it = payload.find("acp");
        if (acp_it == payload.end()) {
            out_error = "Missing 'acp' payload at " + FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        parser::AcpRecord record;
        std::string parse_error;
        if (!commands::DeserializeAcpRecord(*acp_it, record, parse_error)) {
            out_error =
                "Invalid ACP record at " + FormatLocation(tx_seq, event.event_sequence, type) + ": " + parse_error;
            return false;
        }
        const std::string location = FormatLocation(tx_seq, event.event_sequence, type);
        return ApplyAcpViaSeams(
            document,
            location,
            record.id,
            /*expect_removed=*/false,
            [&](parser::AssuranceCase& model, sacm::AssuranceCasePackage& package) {
                return core::acp::UpsertAcp(model, &package, record);
            },
            out_error);
    }

    if (type == "CreateConfidenceArgumentTree") {
        std::string acp_id, argument_package_id, top_goal_id;
        if (!require_string("acp_id", acp_id))
            return false;
        if (!require_string("argument_package_id", argument_package_id))
            return false;
        if (!require_string("top_goal_id", top_goal_id))
            return false;
        const std::string location = FormatLocation(tx_seq, event.event_sequence, type);
        return ApplyAcpViaSeams(
            document,
            location,
            acp_id,
            /*expect_removed=*/false,
            [&](parser::AssuranceCase& model, sacm::AssuranceCasePackage& package) {
                return core::acp::CreateConfidenceArgumentTreeForAcpWithIds(
                    model, &package, acp_id, argument_package_id, top_goal_id);
            },
            out_error);
    }

    if (type == kWorkingDraftAcceptedEventType) {
        // The same replacement as `ApplyEvent`, on the document itself: the
        // accepted bytes become the document, and later events apply on top of
        // exactly what the user accepted.
        const std::string xml = AcceptedDocumentFromEvent(event);
        if (xml.empty()) {
            out_error =
                "WorkingDraftAccepted carries no document at " + FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        if (!sacm_adapter::reload_document(document, xml)) {
            out_error =
                "The accepted document could not be read at " + FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        return true;
    }

    if (type == "Undo" || type == "SacmRestoredFromAudit") {
        // No-op markers: the same rationale as `ApplyEvent`. Undo's cancelled
        // transactions are filtered before replay; SacmRestoredFromAudit is a
        // provenance marker that requires no mutation.
        return true;
    }

    if (type == "SetEvidenceLocation") {
        std::string element_id, new_location;
        if (!require_string("element_id", element_id))
            return false;
        if (!require_string("new_location", new_location))
            return false;
        // Seam-mapped, matching the live command. The Resource a first location
        // creates gets its id from the document's counter, which the replayed
        // history has advanced exactly as the live one did, so the recorded and
        // replayed documents agree without the id being in the payload.
        const sacm_adapter::EditOutcome outcome =
            sacm_adapter::apply_set_evidence_location(document, element_id, new_location);
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure(
                "apply_set_evidence_location", tx_seq, event, outcome.supported, outcome.applied, outcome.diagnostics);
            return false;
        }
        return true;
    }

    if (type == "SetEvidenceAttribute") {
        std::string element_id, attribute_token, new_value;
        if (!require_string("element_id", element_id))
            return false;
        if (!require_string("attribute", attribute_token))
            return false;
        if (!require_string("new_value", new_value))
            return false;
        core::EvidenceAttribute attribute = core::EvidenceAttribute::Owner;
        if (!core::ParseEvidenceAttribute(attribute_token, attribute)) {
            out_error = "Unknown evidence attribute '" + attribute_token + "' at " +
                        FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        const sacm_adapter::EditOutcome outcome =
            sacm_adapter::apply_set_evidence_attribute(document, element_id, attribute, new_value);
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure(
                "apply_set_evidence_attribute", tx_seq, event, outcome.supported, outcome.applied, outcome.diagnostics);
            return false;
        }
        return true;
    }

    if (type == "ImportEvidenceAssessments") {
        const auto items = payload.find("items");
        if (items == payload.end() || !items->is_array()) {
            out_error =
                "Missing or non-array payload field 'items' at " + FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        for (const auto& item : *items) {
            if (!item.is_object() || !item.contains("element_id") || !item.contains("attribute") ||
                !item.contains("value")) {
                out_error = "Malformed import item at " + FormatLocation(tx_seq, event.event_sequence, type);
                return false;
            }
            core::EvidenceAttribute attribute = core::EvidenceAttribute::Owner;
            const std::string attribute_token = item["attribute"].get<std::string>();
            if (!core::ParseEvidenceAttribute(attribute_token, attribute)) {
                out_error = "Unknown evidence attribute '" + attribute_token + "' at " +
                            FormatLocation(tx_seq, event.event_sequence, type);
                return false;
            }
            const sacm_adapter::EditOutcome outcome = sacm_adapter::apply_set_evidence_attribute(
                document, item["element_id"].get<std::string>(), attribute, item["value"].get<std::string>());
            if (!outcome.supported || !outcome.applied) {
                out_error = FormatSeamFailure("apply_set_evidence_attribute",
                                              tx_seq,
                                              event,
                                              outcome.supported,
                                              outcome.applied,
                                              outcome.diagnostics);
                return false;
            }
        }
        return true;
    }

    out_error = "Unknown event type '" + type + "' at " + FormatLocation(tx_seq, event.event_sequence, type);
    return false;
}

std::expected<ReplayState, std::string> Replayer::ReplayFrom(const parser::AssuranceCase& snapshot_model,
                                                             const sacm::AssuranceCasePackage& snapshot_package,
                                                             const std::vector<AuditTransaction>& transactions,
                                                             std::uint64_t up_to_transaction_sequence,
                                                             std::uint64_t from_transaction_sequence) {

    ReplayState state{snapshot_model, snapshot_package};

    // Pre-pass: compute the set of transactions cancelled by an active
    // Undo event (see `undo_resolver.h`). We do this over the replayed
    // window `(from, up_to]` â€” not just up to `up_to_seq` â€” so that an Undo
    // emitted *after* `up_to_transaction_sequence` does not retroactively
    // suppress an earlier transaction when the user navigates back to a
    // past point. Honest audit semantics: at any historical point, only
    // the undos that were already on the books at that point count. Events at
    // or before `from` are excluded because the trusted replay root has
    // already baked them in (an undo cannot cross a trusted baseline).
    std::unordered_set<std::uint64_t> skipped;
    {
        std::vector<AuditTransaction> in_window;
        in_window.reserve(transactions.size());
        for (const AuditTransaction& tx : transactions) {
            if (tx.transaction_sequence > from_transaction_sequence &&
                tx.transaction_sequence <= up_to_transaction_sequence)
                in_window.push_back(tx);
        }
        skipped = ComputeUndoSkipSet(in_window);
    }

    for (const AuditTransaction& tx : transactions) {
        if (tx.transaction_sequence <= from_transaction_sequence)
            continue;
        if (tx.transaction_sequence > up_to_transaction_sequence)
            continue;
        if (skipped.count(tx.transaction_sequence) != 0)
            continue;
        for (const AuditEvent& event : tx.events) {
            std::string err;
            if (!ApplyEvent(state, tx.transaction_sequence, event, err))
                return std::unexpected(std::move(err));
        }
    }

    return state;
}

std::expected<std::unique_ptr<sacm_adapter::LibraryDocument>, std::string>
Replayer::ReplayToLibrary(std::unique_ptr<sacm_adapter::LibraryDocument> snapshot_document,
                          const std::vector<AuditTransaction>& transactions,
                          std::uint64_t up_to_transaction_sequence,
                          std::uint64_t from_transaction_sequence) {

    if (snapshot_document == nullptr)
        return std::unexpected(std::string("ReplayToLibrary: snapshot document is null"));

    // Identical undo-skip pre-pass to `ReplayFrom`: resolve undos over the
    // replayed window `(from, up_to]` only, so an undo cannot cross a trusted
    // baseline and a later undo does not retroactively suppress an earlier
    // transaction when navigating to a past point.
    std::unordered_set<std::uint64_t> skipped;
    {
        std::vector<AuditTransaction> in_window;
        in_window.reserve(transactions.size());
        for (const AuditTransaction& tx : transactions) {
            if (tx.transaction_sequence > from_transaction_sequence &&
                tx.transaction_sequence <= up_to_transaction_sequence)
                in_window.push_back(tx);
        }
        skipped = ComputeUndoSkipSet(in_window);
    }

    for (const AuditTransaction& tx : transactions) {
        if (tx.transaction_sequence <= from_transaction_sequence)
            continue;
        if (tx.transaction_sequence > up_to_transaction_sequence)
            continue;
        if (skipped.count(tx.transaction_sequence) != 0)
            continue;
        for (const AuditEvent& event : tx.events) {
            std::string err;
            if (!ApplyEventToLibrary(*snapshot_document, tx.transaction_sequence, event, err))
                return std::unexpected(std::move(err));
        }
    }

    return snapshot_document;
}

} // namespace core::audit
