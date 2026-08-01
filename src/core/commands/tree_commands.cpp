#include "core/commands/tree_commands.h"

#include "core/assurance_tree.h"
#include "core/commands/library_bridge.h"

namespace core::commands {

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
    if (!ApplyLibraryPrimaryOrLegacy(ctx, mutate, out_error))
        return false;

    out_event.event_type = "MoveSubtree";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["dragged_id"] = dragged_id_;
    out_event.payload["new_parent_id"] = new_parent_id_;
    return true;
}

} // namespace core::commands
