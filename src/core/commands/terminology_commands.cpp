#include "core/commands/terminology_commands.h"

#include "core/commands/library_bridge.h"
#include "core/string_utils.h"
#include "core/terminology_context_projection.h"
#include "sacm_adapter/document_edit.h"

#include <string>

namespace core::commands {

namespace {

// ---------------------------------------------------------------------------
// Phase 1 of the legacy-bridge retirement -- the terminology CRUD flip.
//
// These ten commands were the last tranche whose library seams already existed
// and were already convergence-proven: `ApplyEventToLibrary` has replayed every
// one of them through `sacm_adapter::apply_*_terminology_*` since Phase 2 slice
// 2a, while the LIVE command still projected the document into the legacy POD,
// mutated that, and re-derived. Flipping the live side onto the same seams takes
// eleven commands off the bridge's primary route -- the calls below remain, as
// fallbacks for shapes the seams do not support -- and with them goes the
// refusal: a case carrying an ArgumentGroup or a nested ArgumentPackage can now
// be edited in the glossary, where before the bridge refused rather than delete
// what its projection could not represent.
//
// Two rules make the flip behaviour-preserving rather than merely mechanical.
//
// (1) IDENTITY IS PLANNED, NOT GENERATED. The audit payload records the id and
//     gid the edit minted, and a replay must reproduce them. Each flipped create
//     plans (id, gid) with the SAME `core::Plan*Identity` generators the legacy
//     mutators use and hands both to the seam, so the payload keeps describing
//     what actually happened.
//
// (2) THE APP-LEVEL GUARDS COME WITH IT. The seams enforce SACM structure; they
//     do not enforce Assurance Forge's editing rules -- a required package name,
//     a required term value, a category that is still assigned to terms. Those
//     guards lived inside the legacy mutators, so a flip that just called the
//     seam would quietly drop them. They are re-stated here, checked against
//     `ctx.package` (the projection the legacy mutator would have run on), so a
//     flipped command refuses exactly what the legacy one refused.
//
// Where a seam reports `supported == false` the command falls back to the
// GUARDED bridge, never to a raw legacy mutation -- the file-wide invariant that
// no command mutates the legacy package in place while a document is present.

// (2) above: the guards the legacy mutators applied before touching the model.
// Each returns false and fills `error` with the legacy message verbatim, so the
// UI and the tests that pin these strings see no change.

bool RequireTerminologyPackageName(const std::string& name, std::string& error) {
    if (name.empty()) {
        error = "Terminology package name is required.";
        return false;
    }
    return true;
}

bool RequireCategoryName(const std::string& name, std::string& error) {
    if (TrimWhitespace(name).empty()) {
        error = "Category name is required.";
        return false;
    }
    return true;
}

bool RequireTermValue(const std::string& value, std::string& error) {
    if (TrimWhitespace(value).empty()) {
        error = "Term value is required.";
        return false;
    }
    return true;
}

// `core::DeleteTerminologyCategory` refuses a category still assigned to terms.
// The library's DeleteElement has no opinion about that -- it is an editing rule,
// not a SACM invariant -- so the flipped delete has to ask the same question.
bool RequireCategoryUnused(const sacm::AssuranceCasePackage& package,
                           const TerminologyPackageRef& package_ref,
                           const TerminologyCategoryRef& category_ref,
                           std::string& error) {
    const sacm::TerminologyPackage* terminology_package = core::FindTerminologyPackage(package, package_ref);
    if (terminology_package == nullptr) {
        error = "Terminology package not found.";
        return false;
    }
    if (core::CountTermsUsingCategory(*terminology_package, category_ref) > 0) {
        error = "Category is assigned to one or more terms.";
        return false;
    }
    return true;
}

// The seams address elements by id; the legacy mutators also resolve a ref that
// carries only a gid. A gid-only ref therefore has no seam form and falls back to
// the guarded bridge rather than being approximated.
bool SeamAddressable(const CommandContext& ctx, const std::string& id) {
    return CanApplyLibraryPrimary(ctx) && !id.empty();
}

// The gid of an element a seam REUSED rather than created. The outcome reports
// ids only, and a reused element's gid is whatever it already carried -- which
// the pre-edit projection is what knows.
std::string GidOfProjectedElement(const sacm::AssuranceCasePackage& package, const std::string& id) {
    if (id.empty())
        return {};
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        for (const sacm::ArtifactReference& reference : argument_package.artifactReferences) {
            if (reference.id == id)
                return reference.gid;
        }
        for (const sacm::AssertedContext& context : argument_package.assertedContexts) {
            if (context.id == id)
                return context.gid;
        }
    }
    return {};
}

sacm_adapter::TerminologyTermFields ToAdapterTermFields(const TerminologyTermDraft& draft) {
    sacm_adapter::TerminologyTermFields fields;
    fields.value = draft.value;
    fields.name = draft.name;
    fields.description = draft.description;
    fields.category_refs = draft.category_refs;
    fields.external_reference = draft.externalReference;
    fields.origin = draft.origin;
    return fields;
}

sacm_adapter::TerminologyContextIdentities ToAdapterContextIdentities(const TerminologyContextForcedIds& planned) {
    sacm_adapter::TerminologyContextIdentities identities;
    identities.artifact_reference_id = planned.artifact_reference_id;
    identities.artifact_reference_gid = planned.artifact_reference_gid;
    identities.asserted_context_id = planned.asserted_context_id;
    identities.asserted_context_gid = planned.asserted_context_gid;
    return identities;
}

// A seam that reports a context association's outcome reports which entities it
// created or reused; the command records that in the audit payload, so translate
// it back into the legacy result shape the payload writer already understands.
TerminologyContextAssociationResult FromAdapterContextOutcome(const sacm_adapter::TerminologyContextOutcome& outcome) {
    TerminologyContextAssociationResult result;
    result.success = outcome.applied;
    result.already_associated = outcome.already_associated;
    result.created_artifact_reference = outcome.created_artifact_reference;
    result.created_asserted_context = outcome.created_asserted_context;
    result.artifact_reference_id = outcome.artifact_reference_id;
    result.asserted_context_id = outcome.asserted_context_id;
    return result;
}

void WriteTermDraftToPayload(nlohmann::ordered_json& payload, const core::TerminologyTermDraft& draft) {
    payload["value"] = draft.value;
    payload["name"] = draft.name;
    payload["description"] = draft.description;
    payload["category_refs"] = draft.category_refs;
    payload["external_reference"] = draft.externalReference;
    payload["origin"] = draft.origin;
}

void WriteContextAssociationPayload(nlohmann::ordered_json& payload,
                                    const std::string& element_id,
                                    const core::TerminologyPackageRef& package_ref,
                                    const core::TerminologyTermRef& term_ref,
                                    const core::TerminologyContextAssociationResult& result) {
    payload["element_id"] = element_id;
    payload["package_id"] = package_ref.id;
    payload["package_gid"] = package_ref.gid;
    payload["term_id"] = term_ref.id;
    payload["term_gid"] = term_ref.gid;
    payload["already_associated"] = result.already_associated;
    payload["created_artifact_reference"] = result.created_artifact_reference;
    payload["created_asserted_context"] = result.created_asserted_context;
    payload["artifact_reference_id"] = result.artifact_reference_id;
    payload["artifact_reference_gid"] = result.artifact_reference_gid;
    payload["asserted_context_id"] = result.asserted_context_id;
    payload["asserted_context_gid"] = result.asserted_context_gid;
}

} // namespace

bool CreateTerminologyPackageCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    if (!RequireTerminologyPackageName(name_, out_error))
        return false;

    bool applied_to_library = false;
    if (CanApplyLibraryPrimary(ctx)) {
        const TerminologyPackageRef planned = core::PlanTerminologyPackageIdentity(ctx.package);
        const sacm_adapter::TerminologyCreateOutcome outcome = sacm_adapter::apply_create_terminology_package(
            *ctx.library_document, name_, description_, planned.id, planned.gid);
        if (outcome.supported && !outcome.applied) {
            out_error = LibraryRejection("the new terminology package", outcome.diagnostics);
            return false;
        }
        if (outcome.applied) {
            generated_ref_ = planned;
            ctx.library_primary = true;
            applied_to_library = true;
        }
    }
    if (!applied_to_library) {
        core::TerminologyPackageCreateResult result;
        const LibraryBridgeMutator mutate =
            [&](parser::AssuranceCase&, sacm::AssuranceCasePackage& package, std::string& err) -> bool {
            result = core::CreateTerminologyPackage(package, name_, description_);
            if (!result.success) {
                err = result.error;
                return false;
            }
            return true;
        };
        if (!ApplyLibraryPrimaryOrLegacy(ctx, mutate, out_error))
            return false;
        generated_ref_ = result.package_ref;
    }

    out_event.event_type = "CreateTerminologyPackage";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["name"] = name_;
    out_event.payload["description"] = description_;
    out_event.payload["generated_id"] = generated_ref_.id;
    out_event.payload["generated_gid"] = generated_ref_.gid;
    return true;
}

bool UpdateTerminologyPackageCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    if (!RequireTerminologyPackageName(name_, out_error))
        return false;

    bool applied_to_library = false;
    if (SeamAddressable(ctx, package_ref_.id)) {
        const sacm_adapter::TerminologyEditOutcome outcome =
            sacm_adapter::apply_update_terminology_package(*ctx.library_document, package_ref_.id, name_, description_);
        if (outcome.supported && !outcome.applied) {
            out_error = LibraryRejection("the terminology package edit", outcome.diagnostics);
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
            return core::UpdateTerminologyPackage(package, package_ref_, name_, description_, err);
        };
        if (!ApplyLibraryPrimaryOrLegacy(ctx, mutate, out_error))
            return false;
    }

    out_event.event_type = "UpdateTerminologyPackage";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["package_id"] = package_ref_.id;
    out_event.payload["package_gid"] = package_ref_.gid;
    out_event.payload["name"] = name_;
    out_event.payload["description"] = description_;
    return true;
}

bool CreateTerminologyCategoryCommand::Apply(CommandContext& ctx,
                                             audit::AuditEvent& out_event,
                                             std::string& out_error) {
    if (!RequireCategoryName(draft_.name, out_error))
        return false;

    bool applied_to_library = false;
    if (SeamAddressable(ctx, package_ref_.id)) {
        const TerminologyCategoryRef planned = core::PlanTerminologyCategoryIdentity(ctx.package);
        const sacm_adapter::TerminologyCreateOutcome outcome = sacm_adapter::apply_create_terminology_category(
            *ctx.library_document, package_ref_.id, draft_.name, draft_.description, planned.id, planned.gid);
        if (outcome.supported && !outcome.applied) {
            out_error = LibraryRejection("the new terminology category", outcome.diagnostics);
            return false;
        }
        if (outcome.applied) {
            generated_ref_ = planned;
            ctx.library_primary = true;
            applied_to_library = true;
        }
    }
    if (!applied_to_library) {
        core::TerminologyCategoryCreateResult result;
        const LibraryBridgeMutator mutate =
            [&](parser::AssuranceCase&, sacm::AssuranceCasePackage& package, std::string& err) -> bool {
            result = core::CreateTerminologyCategory(package, package_ref_, draft_);
            if (!result.success) {
                err = result.error;
                return false;
            }
            return true;
        };
        if (!ApplyLibraryPrimaryOrLegacy(ctx, mutate, out_error))
            return false;
        generated_ref_ = result.category_ref;
    }

    out_event.event_type = "CreateTerminologyCategory";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["package_id"] = package_ref_.id;
    out_event.payload["package_gid"] = package_ref_.gid;
    out_event.payload["name"] = draft_.name;
    out_event.payload["description"] = draft_.description;
    out_event.payload["generated_id"] = generated_ref_.id;
    out_event.payload["generated_gid"] = generated_ref_.gid;
    return true;
}

bool UpdateTerminologyCategoryCommand::Apply(CommandContext& ctx,
                                             audit::AuditEvent& out_event,
                                             std::string& out_error) {
    if (!RequireCategoryName(draft_.name, out_error))
        return false;

    bool applied_to_library = false;
    if (SeamAddressable(ctx, category_ref_.id)) {
        const sacm_adapter::TerminologyEditOutcome outcome = sacm_adapter::apply_update_terminology_category(
            *ctx.library_document, category_ref_.id, draft_.name, draft_.description);
        if (outcome.supported && !outcome.applied) {
            out_error = LibraryRejection("the terminology category edit", outcome.diagnostics);
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
            return core::UpdateTerminologyCategory(package, package_ref_, category_ref_, draft_, err);
        };
        if (!ApplyLibraryPrimaryOrLegacy(ctx, mutate, out_error))
            return false;
    }

    out_event.event_type = "UpdateTerminologyCategory";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["package_id"] = package_ref_.id;
    out_event.payload["package_gid"] = package_ref_.gid;
    out_event.payload["category_id"] = category_ref_.id;
    out_event.payload["category_gid"] = category_ref_.gid;
    out_event.payload["name"] = draft_.name;
    out_event.payload["description"] = draft_.description;
    return true;
}

bool DeleteTerminologyCategoryCommand::Apply(CommandContext& ctx,
                                             audit::AuditEvent& out_event,
                                             std::string& out_error) {
    bool applied_to_library = false;
    if (SeamAddressable(ctx, category_ref_.id)) {
        if (!RequireCategoryUnused(ctx.package, package_ref_, category_ref_, out_error))
            return false;
        const sacm_adapter::TerminologyEditOutcome outcome =
            sacm_adapter::apply_delete_terminology_element(*ctx.library_document, category_ref_.id);
        if (outcome.supported && !outcome.applied) {
            out_error = LibraryRejection("the terminology category removal", outcome.diagnostics);
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
            return core::DeleteTerminologyCategory(package, package_ref_, category_ref_, err);
        };
        if (!ApplyLibraryPrimaryOrLegacy(ctx, mutate, out_error))
            return false;
    }

    out_event.event_type = "DeleteTerminologyCategory";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["package_id"] = package_ref_.id;
    out_event.payload["package_gid"] = package_ref_.gid;
    out_event.payload["category_id"] = category_ref_.id;
    out_event.payload["category_gid"] = category_ref_.gid;
    return true;
}

bool CreateTerminologyTermCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    if (!RequireTermValue(draft_.value, out_error))
        return false;

    bool applied_to_library = false;
    if (SeamAddressable(ctx, package_ref_.id)) {
        const TerminologyTermRef planned = core::PlanTerminologyTermIdentity(ctx.package);
        const sacm_adapter::TerminologyCreateOutcome outcome = sacm_adapter::apply_create_terminology_term(
            *ctx.library_document, package_ref_.id, ToAdapterTermFields(draft_), planned.id, planned.gid);
        if (outcome.supported && !outcome.applied) {
            out_error = LibraryRejection("the new terminology term", outcome.diagnostics);
            return false;
        }
        if (outcome.applied) {
            generated_ref_ = planned;
            ctx.library_primary = true;
            applied_to_library = true;
        }
    }
    if (!applied_to_library) {
        core::TerminologyTermCreateResult result;
        const LibraryBridgeMutator mutate =
            [&](parser::AssuranceCase&, sacm::AssuranceCasePackage& package, std::string& err) -> bool {
            result = core::CreateTerminologyTerm(package, package_ref_, draft_);
            if (!result.success) {
                err = result.error;
                return false;
            }
            return true;
        };
        if (!ApplyLibraryPrimaryOrLegacy(ctx, mutate, out_error))
            return false;
        generated_ref_ = result.term_ref;
    }

    out_event.event_type = "CreateTerminologyTerm";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["package_id"] = package_ref_.id;
    out_event.payload["package_gid"] = package_ref_.gid;
    WriteTermDraftToPayload(out_event.payload, draft_);
    out_event.payload["generated_id"] = generated_ref_.id;
    out_event.payload["generated_gid"] = generated_ref_.gid;
    return true;
}

bool UpdateTerminologyTermCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    if (!RequireTermValue(draft_.value, out_error))
        return false;

    bool applied_to_library = false;
    if (SeamAddressable(ctx, term_ref_.id)) {
        const sacm_adapter::TerminologyEditOutcome outcome = sacm_adapter::apply_update_terminology_term(
            *ctx.library_document, term_ref_.id, ToAdapterTermFields(draft_));
        if (outcome.supported && !outcome.applied) {
            out_error = LibraryRejection("the terminology term edit", outcome.diagnostics);
            return false;
        }
        if (outcome.applied) {
            ctx.library_primary = true;
            applied_to_library = true;
        }
    }
    if (!applied_to_library) {
        // The legacy path refreshes the POD render projection in place; the
        // flipped path does not need to, because the frame boundary re-derives
        // the views from the library (RebuildDerivedViewsFromLibrary runs the
        // same terminology passes).
        const LibraryBridgeMutator mutate =
            [&](parser::AssuranceCase& model, sacm::AssuranceCasePackage& package, std::string& err) -> bool {
            if (!core::UpdateTerminologyTerm(package, package_ref_, term_ref_, draft_, err))
                return false;
            core::RefreshVisibleTerminologyContextProjection(model, package);
            return true;
        };
        if (!ApplyLibraryPrimaryOrLegacy(ctx, mutate, out_error))
            return false;
    }

    out_event.event_type = "UpdateTerminologyTerm";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["package_id"] = package_ref_.id;
    out_event.payload["package_gid"] = package_ref_.gid;
    out_event.payload["term_id"] = term_ref_.id;
    out_event.payload["term_gid"] = term_ref_.gid;
    WriteTermDraftToPayload(out_event.payload, draft_);
    return true;
}

bool DeleteTerminologyTermCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    removed_ids_.clear();
    bool applied_to_library = false;
    if (SeamAddressable(ctx, term_ref_.id)) {
        const sacm_adapter::TerminologyEditOutcome outcome =
            sacm_adapter::apply_delete_terminology_element(*ctx.library_document, term_ref_.id, cascade_references_);
        if (outcome.supported && !outcome.applied) {
            out_error = LibraryRejection("the terminology term removal", outcome.diagnostics);
            return false;
        }
        if (outcome.applied) {
            removed_ids_ = outcome.removed_ids;
            ctx.library_primary = true;
            applied_to_library = true;
        }
    }
    if (!applied_to_library) {
        // The legacy mutator erases the term and leaves anything referencing it
        // pointing at nothing -- it has no cascade to opt into. Doing that after
        // a user confirmed "also remove the N things that reference it" would
        // answer their question with a different operation, so refuse instead.
        // Unreachable from the app (the confirmation is only offered when a
        // document is present, which is also when the seam is available).
        if (cascade_references_) {
            out_error = "Cannot remove this term together with what references it: this case is open on the "
                        "legacy edit path, which has no way to remove the references. The case is unchanged.";
            return false;
        }
        const LibraryBridgeMutator mutate =
            [&](parser::AssuranceCase& model, sacm::AssuranceCasePackage& package, std::string& err) -> bool {
            if (!core::DeleteTerminologyTerm(package, package_ref_, term_ref_, err))
                return false;
            core::RefreshVisibleTerminologyContextProjection(model, package);
            return true;
        };
        if (!ApplyLibraryPrimaryOrLegacy(ctx, mutate, out_error))
            return false;
        removed_ids_.push_back(term_ref_.id);
    }

    out_event.event_type = "DeleteTerminologyTerm";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["package_id"] = package_ref_.id;
    out_event.payload["package_gid"] = package_ref_.gid;
    out_event.payload["term_id"] = term_ref_.id;
    out_event.payload["term_gid"] = term_ref_.gid;
    out_event.payload["cascade_references"] = cascade_references_;
    // Evidence, not replay input: the replay reproduces the deletion from
    // (term_id, cascade_references), and the canonical hash is what catches a
    // divergence. This is here so the audit trail and the history view can say
    // WHAT a cascading delete destroyed, which the target id alone does not.
    out_event.payload["removed_ids"] = removed_ids_;
    return true;
}

bool AssociateTerminologyTermWithElementCommand::Apply(CommandContext& ctx,
                                                       audit::AuditEvent& out_event,
                                                       std::string& out_error) {
    bool applied_to_library = false;
    if (SeamAddressable(ctx, term_ref_.id)) {
        const TerminologyContextForcedIds planned = core::PlanTerminologyAssociationIdentities(ctx.package);
        const sacm_adapter::TerminologyContextOutcome outcome = sacm_adapter::apply_associate_terminology_term(
            *ctx.library_document, element_id_, term_ref_.id, ToAdapterContextIdentities(planned));
        if (outcome.supported && !outcome.applied) {
            out_error = LibraryRejection("the terminology association", outcome.diagnostics);
            return false;
        }
        if (outcome.applied) {
            result_ = FromAdapterContextOutcome(outcome);
            // The seam reports ids, not gids. A CREATED element carries the gid
            // this command planned; a REUSED one keeps whatever it already had,
            // which only the projected package knows.
            result_.artifact_reference_gid = result_.created_artifact_reference
                                                 ? planned.artifact_reference_gid
                                                 : GidOfProjectedElement(ctx.package, result_.artifact_reference_id);
            result_.asserted_context_gid = result_.created_asserted_context
                                               ? planned.asserted_context_gid
                                               : GidOfProjectedElement(ctx.package, result_.asserted_context_id);
            ctx.library_primary = true;
            applied_to_library = true;
        }
    }
    if (!applied_to_library) {
        const LibraryBridgeMutator mutate =
            [&](parser::AssuranceCase&, sacm::AssuranceCasePackage& package, std::string& err) -> bool {
            result_ = core::AssociateTerminologyTermWithElement(package, element_id_, package_ref_, term_ref_);
            if (!result_.success) {
                err = result_.error;
                return false;
            }
            return true;
        };
        if (!ApplyLibraryPrimaryOrLegacy(ctx, mutate, out_error))
            return false;
    }

    out_event.event_type = "AssociateTerminologyTermWithElement";
    out_event.payload = nlohmann::ordered_json::object();
    WriteContextAssociationPayload(out_event.payload, element_id_, package_ref_, term_ref_, result_);
    return true;
}

bool AddTerminologyTermAsVisibleContextCommand::Apply(CommandContext& ctx,
                                                      audit::AuditEvent& out_event,
                                                      std::string& out_error) {
    bool applied_to_library = false;
    if (SeamAddressable(ctx, term_ref_.id)) {
        const TerminologyContextForcedIds planned = core::PlanVisibleTerminologyContextIdentities(ctx.package);
        const sacm_adapter::TerminologyContextOutcome outcome = sacm_adapter::apply_add_terminology_visible_context(
            *ctx.library_document, element_id_, term_ref_.id, ToAdapterContextIdentities(planned));
        if (outcome.supported && !outcome.applied) {
            out_error = LibraryRejection("the visible terminology context", outcome.diagnostics);
            return false;
        }
        if (outcome.applied) {
            result_ = FromAdapterContextOutcome(outcome);
            result_.artifact_reference_gid = result_.created_artifact_reference
                                                 ? planned.artifact_reference_gid
                                                 : GidOfProjectedElement(ctx.package, result_.artifact_reference_id);
            result_.asserted_context_gid = result_.created_asserted_context
                                               ? planned.asserted_context_gid
                                               : GidOfProjectedElement(ctx.package, result_.asserted_context_id);
            ctx.library_primary = true;
            applied_to_library = true;
        }
    }
    if (!applied_to_library) {
        const LibraryBridgeMutator mutate =
            [&](parser::AssuranceCase& model, sacm::AssuranceCasePackage& package, std::string& err) -> bool {
            result_ = core::AddTerminologyTermAsVisibleContext(package, element_id_, package_ref_, term_ref_);
            if (!result_.success) {
                err = result_.error;
                return false;
            }
            core::SyncVisibleTerminologyContextToParser(model, package, result_);
            return true;
        };
        if (!ApplyLibraryPrimaryOrLegacy(ctx, mutate, out_error))
            return false;
    }

    out_event.event_type = "AddTerminologyTermAsVisibleContext";
    out_event.payload = nlohmann::ordered_json::object();
    WriteContextAssociationPayload(out_event.payload, element_id_, package_ref_, term_ref_, result_);
    return true;
}

} // namespace core::commands
