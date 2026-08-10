#include "core/commands/acp_commands.h"

#include "core/acp/acp_editing.h"
#include "core/commands/library_bridge.h"
#include "sacm_adapter/document_edit.h"

#include <vector>

namespace core::commands {

namespace {

// ---------------------------------------------------------------------------
// Slice 2c of the legacy-bridge retirement -- the ACP tranche.
//
// An Assurance Claim Point is a GSN v3 concept SACM 2.3 has no class for, so it
// is stored as vendor TaggedValues (clause 8.12) on the element it annotates --
// and, when the target is a RELATIONSHIP, partly as SACM's own clause-11.6
// `metaClaim`. That second half is why this slice needed a new library
// operation: `AddMetaClaim` could attach one, and nothing could detach it.
//
// HOW THESE FLIP. `core::acp` holds several hundred lines of rules about which
// targets are eligible, what each resolution kind writes, and which meta-claim a
// relationship ACP implies. Reimplementing that beside the seams would give two
// copies to drift apart, so instead each command runs the SAME `core::acp`
// mutator on a SCRATCH copy of the projection to work out the result, then
// writes that result to the document through the seams.
//
// That is not the bridge. The bridge projects, mutates, and then REBUILDS the
// live document by reloading the projection -- which is why it loses everything
// the legacy POD cannot carry, and why it refuses documents it would damage.
// Here the scratch is read and discarded: the document is edited in place by
// targeted tag and meta-claim writes, so nothing outside those is touched.
struct AcpScratch {
    parser::AssuranceCase model;
    sacm::AssuranceCasePackage package;
};

AcpScratch MakeScratch(const CommandContext& ctx) {
    return AcpScratch{ctx.model, ctx.package};
}

sacm_adapter::AcpTagFields ToTagFields(const parser::AcpRecord& acp) {
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

// The meta-claims a relationship carries in `package`, or an empty list when the
// target is not a relationship (or is not there). The seam writes the whole list,
// so the caller reads the scratch's post-mutation value and mirrors it.
std::vector<std::string> MetaClaimsOf(const sacm::AssuranceCasePackage& package, const std::string& relationship_id) {
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

// Mirror one ACP's tag set, and a relationship target's meta-claims, from the
// scratch onto the document. `record` is the ACP as the mutator left it.
bool WriteAcpToDocument(CommandContext& ctx,
                        const AcpScratch& scratch,
                        const parser::AcpRecord& record,
                        std::string& out_error) {
    const sacm_adapter::EditOutcome tagged =
        sacm_adapter::apply_upsert_acp_tags(*ctx.library_document, record.target_id, ToTagFields(record));
    if (!tagged.supported || !tagged.applied) {
        out_error = LibraryRejection("the Assurance Claim Point on " + record.target_id, tagged.diagnostics);
        return false;
    }
    if (record.target_kind == "relationship") {
        const sacm_adapter::EditOutcome meta = sacm_adapter::apply_set_meta_claims(
            *ctx.library_document, record.target_id, MetaClaimsOf(scratch.package, record.target_id));
        if (!meta.supported || !meta.applied) {
            out_error = LibraryRejection("the meta-claims on " + record.target_id, meta.diagnostics);
            return false;
        }
    }
    return true;
}

} // namespace

nlohmann::ordered_json SerializeAcpRecord(const parser::AcpRecord& acp) {
    nlohmann::ordered_json json = nlohmann::ordered_json::object();
    json["id"] = acp.id;
    json["name"] = acp.name;
    json["target_kind"] = acp.target_kind;
    json["target_id"] = acp.target_id;
    json["resolution_kind"] = acp.resolution_kind;
    json["text"] = acp.text;
    json["confidence_claim_id"] = acp.confidence_claim_id;
    json["argument_package_id"] = acp.argument_package_id;
    json["top_goal_id"] = acp.top_goal_id;
    return json;
}

bool DeserializeAcpRecord(const nlohmann::ordered_json& json, parser::AcpRecord& out, std::string& error) {
    if (!json.is_object()) {
        error = "ACP record payload is not a JSON object.";
        return false;
    }
    const auto read = [&](const char* key, std::string& dest) -> bool {
        auto it = json.find(key);
        if (it == json.end() || !it->is_string()) {
            error = "Missing or non-string ACP record field '" + std::string(key) + "'.";
            return false;
        }
        dest = it->get<std::string>();
        return true;
    };
    parser::AcpRecord record;
    if (!read("id", record.id) || !read("name", record.name) || !read("target_kind", record.target_kind) ||
        !read("target_id", record.target_id) || !read("resolution_kind", record.resolution_kind) ||
        !read("text", record.text) || !read("confidence_claim_id", record.confidence_claim_id) ||
        !read("argument_package_id", record.argument_package_id) || !read("top_goal_id", record.top_goal_id)) {
        return false;
    }
    out = std::move(record);
    return true;
}

bool AddAcpCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    core::acp::AcpEditResult result;
    bool applied_to_library = false;
    if (CanApplyLibraryPrimary(ctx)) {
        AcpScratch scratch = MakeScratch(ctx);
        result = core::acp::AddAcp(scratch.model, &scratch.package, target_kind_, target_id_);
        if (!result.error.empty()) {
            out_error = result.error;
            return false;
        }
        if (!result.changed) {
            // A benign no-op records no transaction, matching the legacy
            // contract: false with an empty error.
            out_error.clear();
            return false;
        }
        const parser::AcpRecord* record = core::acp::FindAcp(scratch.model, result.acp_id);
        if (record == nullptr) {
            out_error = "The new Assurance Claim Point was not found after it was created.";
            return false;
        }
        if (!WriteAcpToDocument(ctx, scratch, *record, out_error))
            return false;
        ctx.library_primary = true;
        applied_to_library = true;
    }
    if (!applied_to_library) {
        const LibraryBridgeMutator mutate =
            [&](parser::AssuranceCase& model, sacm::AssuranceCasePackage& package, std::string& err) -> bool {
            result = core::acp::AddAcp(model, &package, target_kind_, target_id_);
            if (!result.error.empty()) {
                err = result.error;
                return false;
            }
            if (!result.changed)
                return false;
            return true;
        };
        if (!ApplyLibraryPrimaryOrLegacy(ctx, mutate, out_error))
            return false;
    }
    generated_acp_id_ = result.acp_id;

    out_event.event_type = "AddAcp";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["target_kind"] = target_kind_;
    out_event.payload["target_id"] = target_id_;
    out_event.payload["generated_acp_id"] = generated_acp_id_;
    return true;
}

bool RemoveAcpCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    bool applied_to_library = false;
    if (CanApplyLibraryPrimary(ctx)) {
        const parser::AcpRecord* existing = core::acp::FindAcp(ctx.model, acp_id_);
        if (existing == nullptr) {
            out_error = "ACP was not found.";
            return false;
        }
        const std::string target_id = existing->target_id;
        const bool target_is_relationship = existing->target_kind == "relationship";

        AcpScratch scratch = MakeScratch(ctx);
        const core::acp::AcpEditResult result = core::acp::RemoveAcp(scratch.model, &scratch.package, acp_id_);
        if (!result.error.empty()) {
            out_error = result.error;
            return false;
        }
        if (!result.changed) {
            out_error.clear();
            return false;
        }

        const sacm_adapter::EditOutcome removed =
            sacm_adapter::apply_remove_acp_tags(*ctx.library_document, target_id, acp_id_);
        if (removed.supported && !removed.applied && !removed.diagnostics.empty()) {
            out_error = LibraryRejection("the removal of " + acp_id_, removed.diagnostics);
            return false;
        }
        // A relationship ACP also holds a clause-11.6 meta-claim, which the
        // legacy mutator detaches. Mirror the list the scratch was left with.
        if (target_is_relationship) {
            const sacm_adapter::EditOutcome meta = sacm_adapter::apply_set_meta_claims(
                *ctx.library_document, target_id, MetaClaimsOf(scratch.package, target_id));
            if (!meta.supported || !meta.applied) {
                out_error = LibraryRejection("the meta-claims on " + target_id, meta.diagnostics);
                return false;
            }
        }
        ctx.library_primary = true;
        applied_to_library = true;
    }
    if (!applied_to_library) {
        const LibraryBridgeMutator mutate =
            [&](parser::AssuranceCase& model, sacm::AssuranceCasePackage& package, std::string& err) -> bool {
            core::acp::AcpEditResult result = core::acp::RemoveAcp(model, &package, acp_id_);
            if (!result.error.empty()) {
                err = result.error;
                return false;
            }
            // A benign no-op (nothing removed, no error) records no transaction:
            // the mutator returns false with `err` left empty, so the bus appends
            // nothing and the caller treats an empty-error failure as "nothing
            // happened", mirroring the controller's `if (!result.changed)`.
            if (!result.changed)
                return false;
            return true;
        };
        if (!ApplyLibraryPrimaryOrLegacy(ctx, mutate, out_error))
            return false;
    }

    out_event.event_type = "RemoveAcp";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["acp_id"] = acp_id_;
    return true;
}

bool UpsertAcpCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    bool applied_to_library = false;
    if (CanApplyLibraryPrimary(ctx)) {
        // The PREVIOUS target matters: re-pointing an ACP at a different element
        // has to clear the tags off the one it left, which the scratch mutator
        // does on its copy and the seams have to be told about.
        const parser::AcpRecord* previous = core::acp::FindAcp(ctx.model, acp_.id);
        const std::string previous_target = previous != nullptr ? previous->target_id : std::string{};
        const bool previous_was_relationship = previous != nullptr && previous->target_kind == "relationship";

        AcpScratch scratch = MakeScratch(ctx);
        const core::acp::AcpEditResult result = core::acp::UpsertAcp(scratch.model, &scratch.package, acp_);
        if (!result.error.empty()) {
            out_error = result.error;
            return false;
        }
        if (!result.changed) {
            out_error.clear();
            return false;
        }
        const parser::AcpRecord* record = core::acp::FindAcp(scratch.model, acp_.id);
        if (record == nullptr) {
            out_error = "The Assurance Claim Point was not found after it was updated.";
            return false;
        }
        if (!previous_target.empty() && previous_target != record->target_id) {
            const sacm_adapter::EditOutcome cleared =
                sacm_adapter::apply_remove_acp_tags(*ctx.library_document, previous_target, acp_.id);
            if (cleared.supported && !cleared.applied && !cleared.diagnostics.empty()) {
                out_error = LibraryRejection("clearing " + acp_.id + " from " + previous_target, cleared.diagnostics);
                return false;
            }
            if (previous_was_relationship) {
                const sacm_adapter::EditOutcome meta = sacm_adapter::apply_set_meta_claims(
                    *ctx.library_document, previous_target, MetaClaimsOf(scratch.package, previous_target));
                if (!meta.supported || !meta.applied) {
                    out_error = LibraryRejection("the meta-claims on " + previous_target, meta.diagnostics);
                    return false;
                }
            }
        }
        if (!WriteAcpToDocument(ctx, scratch, *record, out_error))
            return false;
        ctx.library_primary = true;
        applied_to_library = true;
    }
    if (!applied_to_library) {
        const LibraryBridgeMutator mutate =
            [&](parser::AssuranceCase& model, sacm::AssuranceCasePackage& package, std::string& err) -> bool {
            core::acp::AcpEditResult result = core::acp::UpsertAcp(model, &package, acp_);
            if (!result.error.empty()) {
                err = result.error;
                return false;
            }
            if (!result.changed)
                return false;
            return true;
        };
        if (!ApplyLibraryPrimaryOrLegacy(ctx, mutate, out_error))
            return false;
    }

    out_event.event_type = "UpsertAcp";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["acp"] = SerializeAcpRecord(acp_);
    return true;
}

bool CreateConfidenceArgumentTreeForAcpCommand::Apply(CommandContext& ctx,
                                                      audit::AuditEvent& out_event,
                                                      std::string& out_error) {
    core::acp::AcpEditResult result;
    bool applied_to_library = false;
    if (CanApplyLibraryPrimary(ctx)) {
        AcpScratch scratch = MakeScratch(ctx);
        result = core::acp::CreateConfidenceArgumentTreeForAcp(scratch.model, &scratch.package, acp_id_);
        if (!result.error.empty()) {
            out_error = result.error;
            return false;
        }
        if (!result.changed) {
            out_error.clear();
            return false;
        }
        const parser::AcpRecord* record = core::acp::FindAcp(scratch.model, acp_id_);
        const parser::SacmElement* goal = nullptr;
        for (const parser::SacmElement& element : scratch.model.elements) {
            if (element.id == result.top_goal_id)
                goal = &element;
        }
        if (record == nullptr || goal == nullptr) {
            out_error = "The confidence argument tree was not found after it was created.";
            return false;
        }
        // Create the package and its goal, then point the ACP at them. In that
        // order: an ACP naming a package that does not exist yet is a dangling
        // reference for as long as the two writes are apart.
        const sacm_adapter::AcpOutcome created =
            sacm_adapter::apply_create_confidence_argument_package(*ctx.library_document,
                                                                   result.argument_package_id,
                                                                   goal->name,
                                                                   result.top_goal_id,
                                                                   goal->name,
                                                                   goal->content);
        if (!created.supported || !created.applied) {
            out_error = LibraryRejection("the confidence argument tree for " + acp_id_, created.diagnostics);
            return false;
        }
        if (!WriteAcpToDocument(ctx, scratch, *record, out_error))
            return false;
        ctx.library_primary = true;
        applied_to_library = true;
    }
    if (!applied_to_library) {
        const LibraryBridgeMutator mutate =
            [&](parser::AssuranceCase& model, sacm::AssuranceCasePackage& package, std::string& err) -> bool {
            result = core::acp::CreateConfidenceArgumentTreeForAcp(model, &package, acp_id_);
            if (!result.error.empty()) {
                err = result.error;
                return false;
            }
            if (!result.changed)
                return false;
            return true;
        };
        if (!ApplyLibraryPrimaryOrLegacy(ctx, mutate, out_error))
            return false;
    }
    argument_package_id_ = result.argument_package_id;
    top_goal_id_ = result.top_goal_id;

    out_event.event_type = "CreateConfidenceArgumentTree";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["acp_id"] = acp_id_;
    out_event.payload["argument_package_id"] = argument_package_id_;
    out_event.payload["top_goal_id"] = top_goal_id_;
    return true;
}

} // namespace core::commands
