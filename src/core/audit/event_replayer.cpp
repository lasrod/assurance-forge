#include "core/audit/event_replayer.h"

#include "core/acp/acp_editing.h"
#include "core/audit/undo_resolver.h"
#include "core/commands/acp_commands.h"
#include "core/commands/element_commands.h"
#include "core/commands/package_commands.h"
#include "core/commands/proposal_commands.h"
#include "core/commands/terminology_commands.h"
#include "core/element_factory.h"
#include "core/library_package_projection.h"
#include "core/reviews/review_proposal.h"
#include "core/reviews/review_proposal_patch_service.h"
#include "core/sacm_argument_sync.h"
#include "core/terminology_context_projection.h"
#include "core/terminology_package_service.h"
#include "sacm/sacm_serializer.h"
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
    case core::NewElementKind::Goal:          return sacm_adapter::ChildKind::Goal;
    case core::NewElementKind::Strategy:      return sacm_adapter::ChildKind::Strategy;
    case core::NewElementKind::Solution:      return sacm_adapter::ChildKind::Solution;
    case core::NewElementKind::Context:       return sacm_adapter::ChildKind::Context;
    case core::NewElementKind::Assumption:    return sacm_adapter::ChildKind::Assumption;
    case core::NewElementKind::Justification: return sacm_adapter::ChildKind::Justification;
    }
    return sacm_adapter::ChildKind::Goal;
}

sacm_adapter::ChallengeSource ToAdapterChallengeSource(core::ChallengeSourceType type) {
    switch (type) {
    case core::ChallengeSourceType::CounterArgument: return sacm_adapter::ChallengeSource::CounterArgument;
    case core::ChallengeSourceType::CounterEvidence: return sacm_adapter::ChallengeSource::CounterEvidence;
    }
    return sacm_adapter::ChallengeSource::CounterArgument;
}

sacm_adapter::TextField ToAdapterTextField(core::ElementTextField field) {
    switch (field) {
    case core::ElementTextField::Name:        return sacm_adapter::TextField::Name;
    case core::ElementTextField::Description: return sacm_adapter::TextField::Description;
    case core::ElementTextField::Content:     return sacm_adapter::TextField::Content;
    }
    return sacm_adapter::TextField::Name;
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
std::string FormatSeamFailure(const std::string& seam, std::uint64_t tx_seq, const AuditEvent& event,
                              bool supported, bool applied,
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
using BridgeMutator =
    std::function<bool(parser::AssuranceCase&, sacm::AssuranceCasePackage&, std::string&)>;

bool BridgeViaLegacy(sacm_adapter::LibraryDocument& document, const std::string& location,
                     const BridgeMutator& mutate, std::string& out_error) {
    parser::AssuranceCase      model   = sacm_adapter::project_case(document);
    sacm::AssuranceCasePackage package = core::project_library_package(document);
    if (!mutate(model, package, out_error))
        return false;
    if (!sacm_adapter::reload_document(document, sacm::serialize_sacm(package))) {
        out_error = "Bridge re-derive (reload_document) failed at " + location;
        return false;
    }
    return true;
}

bool ApplyEvent(ReplayState& state,
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
        std::string err;
        if (!core::AddTopGoalWithId(state.model, &state.package, element_id, err)) {
            out_error = "AddTopGoalWithId failed at " + FormatLocation(tx_seq, event.event_sequence, type) +
                        ": " + err;
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
            out_error = "Unknown kind token '" + kind_token + "' at " +
                        FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        std::string err;
        if (!core::AddChildElementWithIds(state.model, &state.package, parent_id, kind, element_id,
                                          relationship_id, err)) {
            out_error = "AddChildElementWithIds failed at " +
                        FormatLocation(tx_seq, event.event_sequence, type) + ": " + err;
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
        if (!core::AddChallengeWithIds(state.model, &state.package, target, source_type, element_id, relationship_id,
                                       err)) {
            out_error = "AddChallengeWithIds failed at " +
                        FormatLocation(tx_seq, event.event_sequence, type) + ": " + err;
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
            out_error = "RemoveElement failed at " + FormatLocation(tx_seq, event.event_sequence, type) +
                        ": " + err;
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

    if (type == "SacmRestoredFromAudit") {
        // Provenance event for the non-destructive remediation flow. The
        // SACM file was rewritten on disk to match the replayed state at
        // the moment this event was appended. The replayer does not need
        // to mutate `state` — by construction, replaying transactions up
        // to and including this event yields exactly the model that was
        // written to disk, so subsequent events apply on top of the same
        // model the user observed live.
        return true;
    }

    auto require_identity = [&](std::string& id, std::string& gid) -> bool {
        auto id_it = payload.find("package_id");
        auto gid_it = payload.find("package_gid");
        if (id_it == payload.end() || !id_it->is_string() ||
            gid_it == payload.end() || !gid_it->is_string()) {
            out_error = "Missing or non-string identity payload at " +
                        FormatLocation(tx_seq, event.event_sequence, type);
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
            out_error = "DeleteTerminologyPackage failed at " +
                        FormatLocation(tx_seq, event.event_sequence, type) + ": " + err;
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
        const core::TerminologyPackageCreateResult result = core::CreateTerminologyPackageWithIds(
            state.package, name, description, generated_id, generated_gid);
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
        if (!core::UpdateTerminologyPackage(state.package, core::TerminologyPackageRef{id, gid},
                                            name, description, err)) {
            out_error = "UpdateTerminologyPackage failed at " +
                        FormatLocation(tx_seq, event.event_sequence, type) + ": " + err;
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
            state.package, core::TerminologyPackageRef{package_id, package_gid}, draft, generated_id,
            generated_gid);
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
                                             draft, err)) {
            out_error = "UpdateTerminologyCategory failed at " +
                        FormatLocation(tx_seq, event.event_sequence, type) + ": " + err;
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
            out_error = "DeleteTerminologyCategory failed at " +
                        FormatLocation(tx_seq, event.event_sequence, type) + ": " + err;
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
            out_error = "DeleteArgumentPackage failed at " +
                        FormatLocation(tx_seq, event.event_sequence, type) + ": " + err;
            return false;
        }
        return true;
    }

    if (type == "CreateTerminologyTerm") {
        std::string package_id, package_gid, value, name, description, external_reference, origin,
            generated_id, generated_gid;
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
        draft.value             = value;
        draft.name              = name;
        draft.description       = description;
        draft.externalReference = external_reference;
        draft.origin            = origin;
        auto category_refs_it   = payload.find("category_refs");
        if (category_refs_it != payload.end() && category_refs_it->is_array()) {
            for (const auto& entry : *category_refs_it) {
                if (entry.is_string())
                    draft.category_refs.push_back(entry.get<std::string>());
            }
        }
        const core::TerminologyTermCreateResult result = core::CreateTerminologyTermWithIds(
            state.package, core::TerminologyPackageRef{package_id, package_gid}, draft, generated_id,
            generated_gid);
        if (!result.success) {
            out_error = "CreateTerminologyTermWithIds failed at " +
                        FormatLocation(tx_seq, event.event_sequence, type) + ": " + result.error;
            return false;
        }
        return true;
    }

    if (type == "UpdateTerminologyTerm") {
        std::string package_id, package_gid, term_id, term_gid, value, name, description,
            external_reference, origin;
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
        draft.value             = value;
        draft.name              = name;
        draft.description       = description;
        draft.externalReference = external_reference;
        draft.origin            = origin;
        auto category_refs_it   = payload.find("category_refs");
        if (category_refs_it != payload.end() && category_refs_it->is_array()) {
            for (const auto& entry : *category_refs_it) {
                if (entry.is_string())
                    draft.category_refs.push_back(entry.get<std::string>());
            }
        }
        std::string err;
        if (!core::UpdateTerminologyTerm(state.package,
                                         core::TerminologyPackageRef{package_id, package_gid},
                                         core::TerminologyTermRef{term_id, term_gid}, draft, err)) {
            out_error = "UpdateTerminologyTerm failed at " +
                        FormatLocation(tx_seq, event.event_sequence, type) + ": " + err;
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
        std::string err;
        if (!core::DeleteTerminologyTerm(state.package,
                                         core::TerminologyPackageRef{package_id, package_gid},
                                         core::TerminologyTermRef{term_id, term_gid}, err)) {
            out_error = "DeleteTerminologyTerm failed at " +
                        FormatLocation(tx_seq, event.event_sequence, type) + ": " + err;
            return false;
        }
        core::RefreshVisibleTerminologyContextProjection(state.model, state.package);
        return true;
    }

    auto read_context_association = [&](std::string& element_id, std::string& package_id,
                                        std::string& package_gid, std::string& term_id,
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
            core::AssociateTerminologyTermWithElementWithIds(
                state.package, element_id, core::TerminologyPackageRef{package_id, package_gid},
                core::TerminologyTermRef{term_id, term_gid}, forced);
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
            core::AddTerminologyTermAsVisibleContextWithIds(
                state.package, element_id, core::TerminologyPackageRef{package_id, package_gid},
                core::TerminologyTermRef{term_id, term_gid}, forced);
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
            out_error = "DeleteArtifactPackage failed at " +
                        FormatLocation(tx_seq, event.event_sequence, type) + ": " + err;
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
        if (!core::SetElementTextField(state.model, &state.package, element_id, field, language,
                                       new_value, old_value_unused, err)) {
            out_error = "SetElementTextField failed at " +
                        FormatLocation(tx_seq, event.event_sequence, type) + ": " + err;
            return false;
        }
        return true;
    }

    if (type == "ApplyProposal") {
        std::string proposal_json;
        if (!require_string("proposal_json", proposal_json))
            return false;
        reviews::ReviewProposal proposal;
        std::string             parse_error;
        if (!reviews::DeserializeReviewProposal(proposal_json, proposal, parse_error)) {
            out_error = "Failed to deserialize proposal at " +
                        FormatLocation(tx_seq, event.event_sequence, type) + ": " + parse_error;
            return false;
        }
        std::map<std::string, std::string> predetermined_ids;
        auto                               generated_it = payload.find("generated_ids");
        if (generated_it == payload.end() || !generated_it->is_object()) {
            out_error = "Missing or non-object 'generated_ids' at " +
                        FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        for (auto it = generated_it->begin(); it != generated_it->end(); ++it) {
            if (!it->is_string()) {
                out_error = "Non-string value in 'generated_ids' at " +
                            FormatLocation(tx_seq, event.event_sequence, type);
                return false;
            }
            predetermined_ids[it.key()] = it->get<std::string>();
        }
        reviews::ReviewProposalPatchService patch_service;
        reviews::ApplyProposalResult        result =
            patch_service.ApplyProposalWithIds(proposal, state.model, predetermined_ids);
        if (!result.success) {
            out_error = "ApplyProposalWithIds failed at " +
                        FormatLocation(tx_seq, event.event_sequence, type) + ": " + result.error;
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
            out_error = "AddAcpWithId failed at " +
                        FormatLocation(tx_seq, event.event_sequence, type) + ": " + result.error;
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
            out_error = "RemoveAcp failed at " +
                        FormatLocation(tx_seq, event.event_sequence, type) + ": " + result.error;
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
        std::string       parse_error;
        if (!commands::DeserializeAcpRecord(*acp_it, record, parse_error)) {
            out_error = "Failed to deserialize ACP record at " +
                        FormatLocation(tx_seq, event.event_sequence, type) + ": " + parse_error;
            return false;
        }
        const core::acp::AcpEditResult result = core::acp::UpsertAcp(state.model, &state.package, record);
        if (!result.error.empty()) {
            out_error = "UpsertAcp failed at " +
                        FormatLocation(tx_seq, event.event_sequence, type) + ": " + result.error;
            return false;
        }
        return true;
    }

    out_error = "Unknown event type '" + type + "' at " +
                FormatLocation(tx_seq, event.event_sequence, type);
    return false;
}

} // namespace

bool ApplyEventToLibrary(sacm_adapter::LibraryDocument& document,
                         std::uint64_t                  tx_seq,
                         const AuditEvent&              event,
                         std::string&                   out_error) {
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
            out_error = FormatSeamFailure("apply_add_top_goal", tx_seq, event, outcome.supported,
                                          outcome.applied, outcome.diagnostics);
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
            out_error = "Unknown kind token '" + kind_token + "' at " +
                        FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        const sacm_adapter::AddChildOutcome outcome = sacm_adapter::apply_add_child(
            document, parent_id, ToAdapterChildKind(kind), element_id, relationship_id);
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure("apply_add_child", tx_seq, event, outcome.supported,
                                          outcome.applied, outcome.diagnostics);
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
            out_error = FormatSeamFailure("apply_challenge", tx_seq, event, outcome.supported,
                                          outcome.applied, outcome.diagnostics);
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
            const std::string   location = FormatLocation(tx_seq, event.event_sequence, type);
            const BridgeMutator mutate   = [&](parser::AssuranceCase& model,
                                             sacm::AssuranceCasePackage& package, std::string& err) -> bool {
                return core::RemoveElement(model, &package, element_id, core::RemoveMode::NodeOnly, err);
            };
            return BridgeViaLegacy(document, location, mutate, out_error);
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
            out_error = "Missing or non-array 'deleted_ids' at " +
                        FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        for (const auto& entry : *deleted_it) {
            if (!entry.is_string()) {
                out_error = "Non-string value in 'deleted_ids' at " +
                            FormatLocation(tx_seq, event.event_sequence, type);
                return false;
            }
            const std::string id = entry.get<std::string>();
            const sacm_adapter::DeleteOutcome outcome = sacm_adapter::apply_delete_element(document, id);
            if (!outcome.supported || !outcome.applied) {
                out_error = FormatSeamFailure("apply_delete_element(" + id + ")", tx_seq, event,
                                              outcome.supported, outcome.applied, outcome.diagnostics);
                return false;
            }
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
        // `Name` maps cleanly onto the library seam (SetName). `Content` and
        // `Description` on a CLAIM stay BRIDGED -- and this is not a missing op
        // (Phase 2 slice 2a added SetDescriptionAt so the seam can address both
        // Description slots) but an IRRECONCILABLE data-model difference, proven
        // by measuring the replay:
        //
        //   The app models a claim with two text slots (content = statement,
        //   description = note). The legacy POD keeps them as two independent
        //   fields whose values depend on how the claim was LOADED: a `content=`
        //   attribute fills `content`, a `description=` attribute fills
        //   `description`. The library instead stores clause-8.9 Description[0..*]
        //   as an ordered list, and a claim with a single Description is
        //   indistinguishable whether that text arrived as content or as a note.
        //
        //   So a claim loaded from `description="X"` (no content) has, in the
        //   library, one Description at slot 0 = the app's content. Editing
        //   Content overwrites it (correct for the library-primary model), which
        //   DESTROYS "X". The legacy mutator instead sets a separate `content`
        //   field and KEEPS `description="X"` as the note. Measured:
        //     library: content="fully safe", description="fully safe"
        //     legacy:  content="fully safe", description="The system is safe."
        //   No seam or projection can recover "The system is safe." -- the
        //   overwrite already dropped it -- and preserving the old slot 0 as a
        //   note would instead break a content-only claim (whose legacy
        //   description is empty). The two legacy states collapse to one library
        //   state, so the seam cannot reproduce both.
        //
        //   This divergence is the migration being MORE correct (a lone
        //   Description is the statement, clause 8.9), but the replay oracle
        //   compares against the legacy mutator, so Content/Description stay
        //   bridged: run the SAME legacy `SetElementTextField` onto a projected
        //   package and re-derive the library, reproducing the legacy fields
        //   exactly. The seam's SetDescriptionAt slot mapping still powers the
        //   LIVE library-primary edit path (apply_text_edit), where the
        //   description-only claim's text is correctly a statement.
        if (field == core::ElementTextField::Name) {
            const sacm_adapter::EditOutcome outcome = sacm_adapter::apply_text_edit(
                document, element_id, ToAdapterTextField(field), language, new_value);
            if (!outcome.supported || !outcome.applied) {
                out_error = FormatSeamFailure("apply_text_edit", tx_seq, event, outcome.supported,
                                              outcome.applied, outcome.diagnostics);
                return false;
            }
            return true;
        }
        const std::string location = FormatLocation(tx_seq, event.event_sequence, type);
        const BridgeMutator mutate = [&](parser::AssuranceCase& model,
                                         sacm::AssuranceCasePackage& package, std::string& err) -> bool {
            std::string old_value_unused;
            std::string set_error;
            if (!core::SetElementTextField(model, &package, element_id, field, language, new_value,
                                           old_value_unused, set_error)) {
                err = "SetElementTextField (bridge) failed at " + location + ": " + set_error;
                return false;
            }
            return true;
        };
        return BridgeViaLegacy(document, location, mutate, out_error);
    }

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
            out_error = FormatSeamFailure("apply_delete_package", tx_seq, event, outcome.supported,
                                          outcome.applied, outcome.diagnostics);
            return false;
        }
        return true;
    }

    // ---- Terminology events ---------------------------------------------
    // Phase 2 slice 2a routes the gid-minting CREATE and ASSOCIATE events back
    // through the library seams. The seams now mint the legacy `gid-<id>` (via
    // the SetGid operation, matching core::GenerateUniqueGid) on the created
    // terminology package/category/term and on the association's
    // ArtifactReference/AssertedContext, so these converge on the RAW canonical
    // hash without bridging. The forced element ids the audit recorded are
    // passed to the seams verbatim; each element's gid is `gid-<id>`, so the
    // seam reconstructs the recorded gid from the id alone (no forced-gid
    // plumbing needed). UPDATE/DELETE were already seam-mapped.

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
            sacm_adapter::apply_create_terminology_package(document, name, description, generated_id);
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure("apply_create_terminology_package", tx_seq, event,
                                          outcome.supported, outcome.applied, outcome.diagnostics);
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
            out_error = FormatSeamFailure("apply_update_terminology_package", tx_seq, event,
                                          outcome.supported, outcome.applied, outcome.diagnostics);
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
        const sacm_adapter::TerminologyCreateOutcome outcome =
            sacm_adapter::apply_create_terminology_category(document, package_id, name, description,
                                                            generated_id);
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure("apply_create_terminology_category", tx_seq, event,
                                          outcome.supported, outcome.applied, outcome.diagnostics);
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
            out_error = FormatSeamFailure("apply_update_terminology_category", tx_seq, event,
                                          outcome.supported, outcome.applied, outcome.diagnostics);
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
            out_error = FormatSeamFailure("apply_delete_terminology_element(category)", tx_seq, event,
                                          outcome.supported, outcome.applied, outcome.diagnostics);
            return false;
        }
        return true;
    }

    if (type == "CreateTerminologyTerm") {
        std::string package_id, package_gid, value, name, description, external_reference, origin,
            generated_id, generated_gid;
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
        fields.value              = value;
        fields.name               = name;
        fields.description        = description;
        fields.external_reference = external_reference;
        fields.origin             = origin;
        auto category_refs_it     = payload.find("category_refs");
        if (category_refs_it != payload.end() && category_refs_it->is_array()) {
            for (const auto& entry : *category_refs_it) {
                if (entry.is_string())
                    fields.category_refs.push_back(entry.get<std::string>());
            }
        }
        const sacm_adapter::TerminologyCreateOutcome outcome =
            sacm_adapter::apply_create_terminology_term(document, package_id, fields, generated_id);
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure("apply_create_terminology_term", tx_seq, event,
                                          outcome.supported, outcome.applied, outcome.diagnostics);
            return false;
        }
        return true;
    }

    if (type == "UpdateTerminologyTerm") {
        std::string package_id, package_gid, term_id, term_gid, value, name, description,
            external_reference, origin;
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
        fields.value             = value;
        fields.name              = name;
        fields.description       = description;
        fields.external_reference = external_reference;
        fields.origin            = origin;
        auto category_refs_it    = payload.find("category_refs");
        if (category_refs_it != payload.end() && category_refs_it->is_array()) {
            for (const auto& entry : *category_refs_it) {
                if (entry.is_string())
                    fields.category_refs.push_back(entry.get<std::string>());
            }
        }
        const sacm_adapter::TerminologyEditOutcome outcome =
            sacm_adapter::apply_update_terminology_term(document, term_id, fields);
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure("apply_update_terminology_term", tx_seq, event,
                                          outcome.supported, outcome.applied, outcome.diagnostics);
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
        const sacm_adapter::TerminologyEditOutcome outcome =
            sacm_adapter::apply_delete_terminology_element(document, term_id);
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure("apply_delete_terminology_element(term)", tx_seq, event,
                                          outcome.supported, outcome.applied, outcome.diagnostics);
            return false;
        }
        return true;
    }

    // Reads the association payload including the FORCED artifact-reference /
    // asserted-context ids AND gids. The bridged legacy mutators reproduce those
    // gids verbatim -- the exact data the library seams could not assign.
    auto read_context_association = [&](std::string& element_id, std::string& package_id,
                                        std::string& package_gid, std::string& term_id,
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
            sacm_adapter::apply_associate_terminology_term(document, element_id, term_id,
                                                           forced.artifact_reference_id,
                                                           forced.asserted_context_id);
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure("apply_associate_terminology_term", tx_seq, event,
                                          outcome.supported, outcome.applied, outcome.diagnostics);
            return false;
        }
        return true;
    }

    if (type == "AddTerminologyTermAsVisibleContext") {
        std::string element_id, package_id, package_gid, term_id, term_gid;
        core::TerminologyContextForcedIds forced;
        if (!read_context_association(element_id, package_id, package_gid, term_id, term_gid, forced))
            return false;
        const sacm_adapter::TerminologyContextOutcome outcome =
            sacm_adapter::apply_add_terminology_visible_context(document, element_id, term_id,
                                                                forced.artifact_reference_id,
                                                                forced.asserted_context_id);
        if (!outcome.supported || !outcome.applied) {
            out_error = FormatSeamFailure("apply_add_terminology_visible_context", tx_seq, event,
                                          outcome.supported, outcome.applied, outcome.diagnostics);
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

    if (type == "RemoveArtifactPackage") {
        std::string id, gid;
        if (!require_string("package_id", id))
            return false;
        if (!require_string("package_gid", gid))
            return false;
        const std::string location = FormatLocation(tx_seq, event.event_sequence, type);
        const BridgeMutator mutate = [&](parser::AssuranceCase& /*model*/,
                                         sacm::AssuranceCasePackage& package, std::string& err) -> bool {
            std::string delete_error;
            if (!core::DeleteArtifactPackage(package, id, gid, delete_error)) {
                err = "DeleteArtifactPackage (bridge) failed at " + location + ": " + delete_error;
                return false;
            }
            return true;
        };
        return BridgeViaLegacy(document, location, mutate, out_error);
    }

    if (type == "RemoveTerminologyPackage") {
        std::string id, gid;
        if (!require_string("package_id", id))
            return false;
        if (!require_string("package_gid", gid))
            return false;
        const std::string location = FormatLocation(tx_seq, event.event_sequence, type);
        const BridgeMutator mutate = [&](parser::AssuranceCase& /*model*/,
                                         sacm::AssuranceCasePackage& package, std::string& err) -> bool {
            std::string delete_error;
            if (!core::DeleteTerminologyPackage(package, core::TerminologyPackageRef{id, gid},
                                                delete_error)) {
                err = "DeleteTerminologyPackage (bridge) failed at " + location + ": " + delete_error;
                return false;
            }
            return true;
        };
        return BridgeViaLegacy(document, location, mutate, out_error);
    }

    if (type == "ApplyProposal") {
        std::string proposal_json;
        if (!require_string("proposal_json", proposal_json))
            return false;
        reviews::ReviewProposal proposal;
        std::string             parse_error;
        if (!reviews::DeserializeReviewProposal(proposal_json, proposal, parse_error)) {
            out_error = "Failed to deserialize proposal at " +
                        FormatLocation(tx_seq, event.event_sequence, type) + ": " + parse_error;
            return false;
        }
        std::map<std::string, std::string> predetermined_ids;
        auto                               generated_it = payload.find("generated_ids");
        if (generated_it == payload.end() || !generated_it->is_object()) {
            out_error = "Missing or non-object 'generated_ids' at " +
                        FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        for (auto it = generated_it->begin(); it != generated_it->end(); ++it) {
            if (!it->is_string()) {
                out_error = "Non-string value in 'generated_ids' at " +
                            FormatLocation(tx_seq, event.event_sequence, type);
                return false;
            }
            predetermined_ids[it.key()] = it->get<std::string>();
        }
        const std::string location = FormatLocation(tx_seq, event.event_sequence, type);
        const BridgeMutator mutate = [&](parser::AssuranceCase& model,
                                         sacm::AssuranceCasePackage& package, std::string& err) -> bool {
            reviews::ReviewProposalPatchService patch_service;
            reviews::ApplyProposalResult        result =
                patch_service.ApplyProposalWithIds(proposal, model, predetermined_ids);
            if (!result.success) {
                err = "ApplyProposalWithIds (bridge) failed at " + location + ": " + result.error;
                return false;
            }
            core::RebuildSacmArgumentPackageFromParser(model, package);
            return true;
        };
        return BridgeViaLegacy(document, location, mutate, out_error);
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
        const std::string   location = FormatLocation(tx_seq, event.event_sequence, type);
        const BridgeMutator mutate   = [&](parser::AssuranceCase&         model,
                                         sacm::AssuranceCasePackage& package, std::string& err) -> bool {
            const core::acp::AcpEditResult result =
                core::acp::AddAcpWithId(model, &package, target_kind, target_id, generated_acp_id);
            if (!result.error.empty()) {
                err = "AddAcpWithId (bridge) failed at " + location + ": " + result.error;
                return false;
            }
            return true;
        };
        return BridgeViaLegacy(document, location, mutate, out_error);
    }

    if (type == "RemoveAcp") {
        std::string acp_id;
        if (!require_string("acp_id", acp_id))
            return false;
        const std::string   location = FormatLocation(tx_seq, event.event_sequence, type);
        const BridgeMutator mutate   = [&](parser::AssuranceCase&         model,
                                         sacm::AssuranceCasePackage& package, std::string& err) -> bool {
            const core::acp::AcpEditResult result = core::acp::RemoveAcp(model, &package, acp_id);
            if (!result.error.empty()) {
                err = "RemoveAcp (bridge) failed at " + location + ": " + result.error;
                return false;
            }
            return true;
        };
        return BridgeViaLegacy(document, location, mutate, out_error);
    }

    if (type == "UpsertAcp") {
        auto acp_it = payload.find("acp");
        if (acp_it == payload.end()) {
            out_error = "Missing 'acp' payload at " + FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        parser::AcpRecord record;
        std::string       parse_error;
        if (!commands::DeserializeAcpRecord(*acp_it, record, parse_error)) {
            out_error = "Failed to deserialize ACP record at " +
                        FormatLocation(tx_seq, event.event_sequence, type) + ": " + parse_error;
            return false;
        }
        const std::string   location = FormatLocation(tx_seq, event.event_sequence, type);
        const BridgeMutator mutate   = [&](parser::AssuranceCase&         model,
                                         sacm::AssuranceCasePackage& package, std::string& err) -> bool {
            const core::acp::AcpEditResult result = core::acp::UpsertAcp(model, &package, record);
            if (!result.error.empty()) {
                err = "UpsertAcp (bridge) failed at " + location + ": " + result.error;
                return false;
            }
            return true;
        };
        return BridgeViaLegacy(document, location, mutate, out_error);
    }

    if (type == "Undo" || type == "SacmRestoredFromAudit") {
        // No-op markers: the same rationale as `ApplyEvent`. Undo's cancelled
        // transactions are filtered before replay; SacmRestoredFromAudit is a
        // provenance marker that requires no mutation.
        return true;
    }

    out_error = "Unknown event type '" + type + "' at " +
                FormatLocation(tx_seq, event.event_sequence, type);
    return false;
}

std::expected<ReplayState, std::string> Replayer::ReplayFrom(
    const parser::AssuranceCase&         snapshot_model,
    const sacm::AssuranceCasePackage&    snapshot_package,
    const std::vector<AuditTransaction>& transactions,
    std::uint64_t                        up_to_transaction_sequence,
    std::uint64_t                        from_transaction_sequence) {

    ReplayState state{snapshot_model, snapshot_package};

    // Pre-pass: compute the set of transactions cancelled by an active
    // Undo event (see `undo_resolver.h`). We do this over the replayed
    // window `(from, up_to]` — not just up to `up_to_seq` — so that an Undo
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

std::expected<std::unique_ptr<sacm_adapter::LibraryDocument>, std::string> Replayer::ReplayToLibrary(
    std::unique_ptr<sacm_adapter::LibraryDocument> snapshot_document,
    const std::vector<AuditTransaction>&           transactions,
    std::uint64_t                                  up_to_transaction_sequence,
    std::uint64_t                                  from_transaction_sequence) {

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
