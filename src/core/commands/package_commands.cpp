#include "core/commands/package_commands.h"

#include "core/acp/assurance_claim_point.h"
#include "core/commands/library_bridge.h"
#include "core/terminology_package_service.h"
#include "sacm_adapter/document_edit.h"

#include <algorithm>
#include <unordered_set>

namespace core {

namespace {

// Match an SACM element by id (preferred) or gid.
bool MatchesIdentity(const sacm::SacmElement& element, const std::string& id, const std::string& gid) {
    return (!id.empty() && element.id == id) || (!gid.empty() && element.gid == gid);
}

} // namespace

bool DeleteArgumentPackage(sacm::AssuranceCasePackage& package,
                           parser::AssuranceCase& model,
                           const std::string& package_id,
                           const std::string& package_gid,
                           std::string& error) {
    auto& vec = package.argumentPackages;
    const auto it = std::find_if(vec.begin(), vec.end(), [&](const sacm::ArgumentPackage& p) {
        return MatchesIdentity(p, package_id, package_gid);
    });
    if (it == vec.end()) {
        error = "Argument package not found in editable model.";
        return false;
    }

    // Capture identities of every element that lived inside the removed
    // argument package so we can scrub matching parser projections and
    // detect ACPs that referenced this package as their separate
    // confidence argument tree.
    const std::string removed_argument_package_id = it->id;
    std::unordered_set<std::string> removed_element_ids;
    std::unordered_set<std::string> removed_element_gids;
    auto record_identity = [&](const sacm::SacmElement& element) {
        if (!element.id.empty())
            removed_element_ids.insert(element.id);
        if (!element.gid.empty())
            removed_element_gids.insert(element.gid);
    };
    for (const sacm::Claim& claim : it->claims)
        record_identity(claim);
    for (const sacm::ArgumentReasoning& reasoning : it->argumentReasonings)
        record_identity(reasoning);
    for (const sacm::ArtifactReference& artifact_reference : it->artifactReferences)
        record_identity(artifact_reference);
    for (const sacm::AssertedInference& inference : it->assertedInferences)
        record_identity(inference);
    for (const sacm::AssertedContext& context : it->assertedContexts)
        record_identity(context);
    for (const sacm::AssertedEvidence& evidence : it->assertedEvidences)
        record_identity(evidence);
    const bool was_confidence_argument_package = core::acp::IsConfidenceArgumentPackage(*it);

    vec.erase(it);

    // Drop parser projections of every element that lived in the removed
    // argument package so they do not show up as orphans in the GSN tree.
    model.elements.erase(std::remove_if(model.elements.begin(),
                                        model.elements.end(),
                                        [&](const parser::SacmElement& element) {
                                            if (removed_element_ids.count(element.id) > 0)
                                                return true;
                                            if (!element.gid.empty() && removed_element_gids.count(element.gid) > 0)
                                                return true;
                                            return false;
                                        }),
                         model.elements.end());

    if (was_confidence_argument_package) {
        for (parser::AcpRecord& acp_record : model.acps) {
            if (acp_record.resolution_kind == "topGoalReference" &&
                acp_record.argument_package_id == removed_argument_package_id) {
                acp_record.argument_package_id.clear();
                acp_record.top_goal_id.clear();
            }
        }
    }
    return true;
}

bool DeleteArtifactPackage(sacm::AssuranceCasePackage& package,
                           const std::string& package_id,
                           const std::string& package_gid,
                           std::string& error) {
    auto& vec = package.artifactPackages;
    const auto it = std::find_if(vec.begin(), vec.end(), [&](const sacm::ArtifactPackage& p) {
        return MatchesIdentity(p, package_id, package_gid);
    });
    if (it == vec.end()) {
        error = "Artifact package not found in editable model.";
        return false;
    }
    vec.erase(it);
    return true;
}

} // namespace core

namespace core::commands {

namespace {

void FillIdentityPayload(audit::AuditEvent& out_event,
                         const std::string& event_type,
                         const std::string& id,
                         const std::string& gid) {
    out_event.event_type = event_type;
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["package_id"] = id;
    out_event.payload["package_gid"] = gid;
}

} // namespace

bool RemoveTerminologyPackageCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    if (id_.empty() && gid_.empty()) {
        out_error = "RemoveTerminologyPackageCommand requires an id or gid.";
        return false;
    }

    // Phase 2a of the legacy-bridge retirement. `apply_delete_package` covers all
    // three package kinds, so the seam is the same one RemoveArgumentPackage
    // already uses -- but the legacy behaviour is NOT the same, and the difference
    // is destructive in the direction that matters:
    //
    //   legacy -- REFUSES a package that still holds categories, terms or
    //             expressions ("Terminology package contains terms.").
    //   seam   -- deletes it and everything in it, recursively.
    //
    // The guard is an Assurance Forge editing rule, not a SACM invariant, so the
    // seam has no opinion about it and flipping without re-stating it would turn
    // "you must empty this first" into "the glossary is gone" on the same click.
    // It is re-stated here, checked against the same projection the legacy mutator
    // ran on. Offering an informed cascade instead -- as the term delete now does
    // -- is a product decision, not a migration one, and is left out of the flip.
    bool applied_to_library = false;
    if (CanApplyLibraryPrimary(ctx) && !id_.empty()) {
        const sacm::TerminologyPackage* terminology_package =
            core::FindTerminologyPackage(ctx.package, core::TerminologyPackageRef{id_, gid_});
        if (terminology_package == nullptr) {
            out_error = "Terminology package not found.";
            return false;
        }
        if (!core::CanDeleteTerminologyPackage(*terminology_package, out_error))
            return false;

        const sacm_adapter::DeleteOutcome outcome = sacm_adapter::apply_delete_package(*ctx.library_document, id_);
        if (outcome.supported && !outcome.applied) {
            out_error = LibraryRejection("the terminology package removal", outcome.diagnostics);
            return false;
        }
        if (outcome.applied) {
            ctx.library_primary = true;
            applied_to_library = true;
        }
    }
    if (!applied_to_library) {
        const LibraryBridgeMutator mutate =
            [&](parser::AssuranceCase&, sacm::AssuranceCasePackage& package, std::string& err) -> bool {
            return core::DeleteTerminologyPackage(package, core::TerminologyPackageRef{id_, gid_}, err);
        };
        if (!ApplyLibraryPrimaryOrLegacy(ctx, mutate, out_error))
            return false;
    }

    FillIdentityPayload(out_event, "RemoveTerminologyPackage", id_, gid_);
    return true;
}

bool RemoveArgumentPackageCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    if (id_.empty() && gid_.empty()) {
        out_error = "RemoveArgumentPackageCommand requires an id or gid.";
        return false;
    }

    // Phase 1 of the legacy-bridge retirement. `apply_delete_package` is the
    // library's recursive DeleteElement, proven equivalent to
    // `core::DeleteArgumentPackage` in test_sacm_library_edit, and the audit
    // replayer has used it for this event since Phase 2 slice 2a -- the live
    // command was the only side still bridging. The seam addresses the package by
    // id, so a gid-only removal keeps the guarded bridge.
    //
    // The legacy mutator additionally scrubbed the removed elements out of the POD
    // render model and cleared any ACP that pointed at the package as its
    // confidence tree. Neither survives the bridge either (it discards the scratch
    // model and re-derives from the serialized package), so nothing is lost here:
    // the frame boundary re-projects both from the library.
    bool applied_to_library = false;
    if (CanApplyLibraryPrimary(ctx) && !id_.empty()) {
        const sacm_adapter::DeleteOutcome outcome = sacm_adapter::apply_delete_package(*ctx.library_document, id_);
        if (outcome.supported && !outcome.applied) {
            out_error = LibraryRejection("the argument package removal", outcome.diagnostics);
            return false;
        }
        if (outcome.applied) {
            ctx.library_primary = true;
            applied_to_library = true;
        }
    }
    if (!applied_to_library) {
        const LibraryBridgeMutator mutate =
            [&](parser::AssuranceCase& model, sacm::AssuranceCasePackage& package, std::string& err) -> bool {
            return core::DeleteArgumentPackage(package, model, id_, gid_, err);
        };
        if (!ApplyLibraryPrimaryOrLegacy(ctx, mutate, out_error))
            return false;
    }

    FillIdentityPayload(out_event, "RemoveArgumentPackage", id_, gid_);
    return true;
}

bool RemoveArtifactPackageCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    if (id_.empty() && gid_.empty()) {
        out_error = "RemoveArtifactPackageCommand requires an id or gid.";
        return false;
    }

    // Phase 2a. Here the seam is CLEANER than the legacy mutator rather than more
    // destructive. `core::DeleteArtifactPackage` erases the ArtifactPackage and
    // nothing else, so every `ArtifactReference::referencedArtifact` naming one of
    // its artifacts is left pointing at an id that no longer resolves; the seam
    // scrubs that reference. The ArtifactReference itself survives either way --
    // it is a drawn Solution node, and removing evidence from the argument is not
    // what "delete this artifact package" asked for. Same direction as the
    // term-delete disclosure in phase 1, and measured on both sides by
    // LibraryPrimaryEditFlip.RemoveArtifactPackageScrubsTheReferenceTheLegacy-
    // MutatorLeftDangling rather than assumed.
    bool applied_to_library = false;
    if (CanApplyLibraryPrimary(ctx) && !id_.empty()) {
        const sacm_adapter::DeleteOutcome outcome = sacm_adapter::apply_delete_package(*ctx.library_document, id_);
        if (outcome.supported && !outcome.applied) {
            out_error = LibraryRejection("the artifact package removal", outcome.diagnostics);
            return false;
        }
        if (outcome.applied) {
            ctx.library_primary = true;
            applied_to_library = true;
        }
    }
    if (!applied_to_library) {
        const LibraryBridgeMutator mutate =
            [&](parser::AssuranceCase&, sacm::AssuranceCasePackage& package, std::string& err) -> bool {
            return core::DeleteArtifactPackage(package, id_, gid_, err);
        };
        if (!ApplyLibraryPrimaryOrLegacy(ctx, mutate, out_error))
            return false;
    }

    FillIdentityPayload(out_event, "RemoveArtifactPackage", id_, gid_);
    return true;
}

} // namespace core::commands
