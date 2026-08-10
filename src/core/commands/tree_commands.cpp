#include "core/commands/tree_commands.h"

#include "core/assurance_tree.h"
#include "core/tree_editing.h"
#include "sacm_adapter/document_edit.h"
#include "core/commands/library_bridge.h"

namespace core::commands {

namespace {

// The parser's relationship token as the seam's SACM kind. The tokens come from
// `core::MoveSubtree`, which is the only thing that decides them.
sacm_adapter::RelationshipKind RelationshipKindOfParserType(const std::string& type) {
    if (type == "assertedcontext")
        return sacm_adapter::RelationshipKind::AssertedContext;
    if (type == "assertedevidence")
        return sacm_adapter::RelationshipKind::AssertedEvidence;
    return sacm_adapter::RelationshipKind::AssertedInference;
}

// `new_relationship_id` is recorded so REPLAY reuses the id the live move minted
// rather than minting its own. `core::MoveSubtree` derives it from the model, so a
// replay over the same history does land on the same value -- but only while the
// derivation and the history agree, and an audit log has to survive the derivation
// changing. Absent (an event recorded before this field existed), the replay falls
// back to re-deriving it, which is what those events were replayed with all along.
void RecordMoveSubtreeEvent(audit::AuditEvent& out_event,
                            const std::string& dragged_id,
                            const std::string& new_parent_id,
                            const std::string& new_relationship_id) {
    out_event.event_type = "MoveSubtree";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["dragged_id"] = dragged_id;
    out_event.payload["new_parent_id"] = new_parent_id;
    if (!new_relationship_id.empty())
        out_event.payload["new_relationship_id"] = new_relationship_id;
}

} // namespace

std::string TreeDropModeToToken(core::TreeDropMode mode) {
    switch (mode) {
    case core::TreeDropMode::Before:
        return "Before";
    case core::TreeDropMode::After:
        return "After";
    case core::TreeDropMode::AsChild:
        return "AsChild";
    }
    return "Before";
}

bool TreeDropModeFromToken(const std::string& token, core::TreeDropMode& out) {
    if (token == "Before") {
        out = core::TreeDropMode::Before;
        return true;
    }
    if (token == "After") {
        out = core::TreeDropMode::After;
        return true;
    }
    if (token == "AsChild") {
        out = core::TreeDropMode::AsChild;
        return true;
    }
    return false;
}

bool ApplyMoveSubtreePlanToLibrary(sacm_adapter::LibraryDocument& document,
                                   const core::MoveSubtreePlan& plan,
                                   std::string& out_error) {
    // Create first, retarget second, delete last. The old relationship can only be
    // removed once nothing needs it, and the new one has to exist before the
    // endpoints that point into it are rewritten.
    for (const core::MoveSubtreePlan::Relationship& created : plan.created) {
        const sacm_adapter::EditOutcome added =
            sacm_adapter::apply_add_relationship(document,
                                                 created.id,
                                                 RelationshipKindOfParserType(created.type),
                                                 created.sources,
                                                 created.targets,
                                                 created.reasoning);
        if (!added.supported || !added.applied) {
            out_error = LibraryRejection("the new " + created.type, added.diagnostics);
            return false;
        }
    }
    for (const core::MoveSubtreePlan::Relationship& retargeted : plan.retargeted) {
        const sacm_adapter::EditOutcome ends = sacm_adapter::apply_set_relationship_ends(
            document, retargeted.id, retargeted.sources, retargeted.targets, retargeted.reasoning);
        if (!ends.supported || !ends.applied) {
            out_error = LibraryRejection("the endpoints of " + retargeted.id, ends.diagnostics);
            return false;
        }
    }
    for (const std::string& id : plan.deleted_ids) {
        const sacm_adapter::DeleteOutcome deleted = sacm_adapter::apply_delete_element(document, id);
        if (!deleted.applied) {
            out_error = LibraryRejection("the removal of " + id, deleted.diagnostics);
            return false;
        }
    }
    return true;
}

bool ReorderSiblingsCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    const LibraryBridgeMutator mutate =
        [&](parser::AssuranceCase& model, sacm::AssuranceCasePackage& package, std::string& err) -> bool {
        // A CORE tree build with no UI dependency; preserves source_refs /
        // relationship order exactly as the UI wrapper does.
        const core::AssuranceTree tree = core::AssuranceTree::Build(model, "");
        core::TreeDisplayOrder scratch_order;
        if (!core::ReorderSiblings(model,
                                   &package,
                                   tree,
                                   scratch_order,
                                   core::ReorderSiblingsCommand{dragged_id_, target_id_, drop_mode_},
                                   err))
            return false;
        // Capture the reordered order so the app can update the live (transient)
        // display order after a successful dispatch.
        reordered_ = scratch_order;
        return true;
    };
    // `core::ReorderSiblings` returns false BOTH on a genuine no-op (nothing to
    // reorder) and on a real failure, distinguished only by whether `err` is set. A
    // false-with-empty-error propagates here so `Apply` returns false with an empty
    // `out_error` -- the bus appends NO transaction (the mutation did nothing), and
    // the empty error string is what distinguishes a no-op from a real failure for
    // the caller. (In practice `ValidateTreeDrop` gates invalid drops before dispatch,
    // so a no-op does not reach here.)
    if (!ApplyLibraryPrimaryOrLegacy(ctx, mutate, out_error))
        return false;

    out_event.event_type = "ReorderSiblings";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["dragged_id"] = dragged_id_;
    out_event.payload["target_id"] = target_id_;
    out_event.payload["drop_mode"] = TreeDropModeToToken(drop_mode_);
    return true;
}

bool MoveSubtreeCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    const LibraryBridgeMutator mutate =
        [&](parser::AssuranceCase& model, sacm::AssuranceCasePackage& package, std::string& err) -> bool {
        const core::AssuranceTree tree = core::AssuranceTree::Build(model, "");
        return core::MoveSubtree(model, &package, tree, core::MoveSubtreeCommand{dragged_id_, new_parent_id_}, err);
    };

    // Library-primary via the scratch-compute pattern: run the SAME
    // `core::MoveSubtree` on a scratch projection, diff what it decided, and write
    // that through the seams. The mutator stays the single authority on what a move
    // means -- which relationship kind connects the two elements, whether the old
    // one dies, what the new one's ends are -- so the flip cannot re-decide the GSN
    // reading behind its back. This is not the bridge: the scratch is read and
    // discarded, and the document is never rebuilt from it.
    std::string planned_relationship_id;
    bool applied_to_library = false;
    if (CanApplyLibraryPrimary(ctx)) {
        parser::AssuranceCase scratch = ctx.model;
        const core::AssuranceTree tree = core::AssuranceTree::Build(scratch, "");
        if (!core::MoveSubtree(
                scratch, nullptr, tree, core::MoveSubtreeCommand{dragged_id_, new_parent_id_}, out_error))
            return false;

        const core::MoveSubtreePlan plan = core::PlanMoveSubtreeFromDiff(ctx.model, scratch);
        // Both bail-outs happen BEFORE the first write, so the bridge fallback
        // below still sees an untouched document. Once a seam has applied, a later
        // refusal is a hard failure instead -- the same rule the cascading delete
        // in `RemoveElementCommand` follows.
        if (plan.touches_non_relationships || !plan.unrepresentable_reason.empty()) {
            if (!ApplyLibraryPrimaryOrLegacy(ctx, mutate, out_error))
                return false;
            RecordMoveSubtreeEvent(out_event, dragged_id_, new_parent_id_, {});
            return true;
        }

        if (!plan.created.empty())
            planned_relationship_id = plan.created.front().id;
        // Set BEFORE the first write, not after the last one. A plan is several
        // writes, and a failure between them leaves the document changed. The bus
        // does not rebuild the live models on its failure path -- that would free
        // containers the canvas is still rendering from this frame -- so the caller
        // re-derives them at the next frame boundary, and only when `library_primary`
        // says the flip engaged (see the failure path in `CommandBus::Execute`). Set
        // afterwards, a part-applied move would leave the UI rendering a model the
        // document no longer matches.
        //
        // Safe here precisely because the fallback decision is already made: the two
        // shapes that bridge were reported by the plan above and returned early. A
        // command that could still fall back must NOT claim the flip -- the bridge
        // would then run with the flag already set. If the very first write fails
        // nothing was mutated, and the flag costs one redundant re-derive from an
        // unchanged document.
        ctx.library_primary = true;
        if (!ApplyMoveSubtreePlanToLibrary(*ctx.library_document, plan, out_error))
            return false;
        applied_to_library = true;
    }
    if (!applied_to_library && !ApplyLibraryPrimaryOrLegacy(ctx, mutate, out_error))
        return false;

    RecordMoveSubtreeEvent(out_event, dragged_id_, new_parent_id_, planned_relationship_id);
    return true;
}

} // namespace core::commands
