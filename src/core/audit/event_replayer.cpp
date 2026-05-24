#include "core/audit/event_replayer.h"

#include "core/commands/element_commands.h"
#include "core/commands/package_commands.h"
#include "core/commands/proposal_commands.h"
#include "core/commands/terminology_commands.h"
#include "core/element_factory.h"
#include "core/reviews/review_proposal.h"
#include "core/reviews/review_proposal_patch_service.h"
#include "core/sacm_argument_sync.h"
#include "core/terminology_context_projection.h"
#include "core/terminology_package_service.h"

#include <sstream>

namespace core::audit {

namespace {

std::string FormatLocation(std::uint64_t tx_seq, std::uint64_t ev_seq, const std::string& event_type) {
    std::ostringstream oss;
    oss << "transaction " << tx_seq << " event " << ev_seq << " (" << event_type << ")";
    return oss.str();
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

    out_error = "Unknown event type '" + type + "' at " +
                FormatLocation(tx_seq, event.event_sequence, type);
    return false;
}

} // namespace

std::expected<ReplayState, std::string> Replayer::ReplayFrom(
    const parser::AssuranceCase&         snapshot_model,
    const sacm::AssuranceCasePackage&    snapshot_package,
    const std::vector<AuditTransaction>& transactions,
    std::uint64_t                        up_to_transaction_sequence) {

    ReplayState state{snapshot_model, snapshot_package};

    for (const AuditTransaction& tx : transactions) {
        if (tx.transaction_sequence > up_to_transaction_sequence)
            continue;
        for (const AuditEvent& event : tx.events) {
            std::string err;
            if (!ApplyEvent(state, tx.transaction_sequence, event, err))
                return std::unexpected(std::move(err));
        }
    }

    return state;
}

} // namespace core::audit
