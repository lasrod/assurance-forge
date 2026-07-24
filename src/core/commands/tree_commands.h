#pragma once

#include "core/commands/command_bus.h"
#include "core/tree_editing.h"

#include <string>

// Audited commands for the structural tree drag-drop: reorder a dragged element
// among its siblings, or move a dragged element (and its subtree) under a new
// parent. `ElementActions::PerformTreeDrop` used to call `core::ReorderSiblings`
// / `core::MoveSubtree` directly on the loaded case + package, bypassing the
// audit bus. That made a drop (a) UNAUDITED -- not replayable, so it surfaced as
// a verify divergence -- and (b) never synced to the library document, so a
// subsequent library-primary edit projected from the STALE library and LOST the
// drop (data loss). `ApplySacmSiblingOrder` reorders the relationships/sources in
// the package, so a reorder genuinely changes the serialized SACM and must be
// audited.
//
// These commands route the SAME `core::*` tree mutators through the library-
// primary bridge chokepoint (`ApplyLibraryPrimaryOrLegacy`), so a drop becomes a
// recorded, replayable, library-synced transaction.
//
// Determinism: `MoveSubtree` mints a new relationship id via
// `GenerateRelationshipId`, which is deterministic on the model state, so the
// live bridge and the replay bridge regenerate the same id and converge WITHOUT a
// `WithId` variant (the same argument as the terminology create commands).
namespace core::commands {

// (De)serialize a `core::TreeDropMode` to/from a stable payload token (mirrors
// `RemoveModeToToken`). Not user-visible -- used only in the audit payload.
std::string TreeDropModeToToken(core::TreeDropMode mode);
bool        TreeDropModeFromToken(const std::string& token, core::TreeDropMode& out);

// Reorder a dragged element above/below a target sibling (same parent, same tree
// group -- enforced by the underlying `core::ReorderSiblings` validation).
class ReorderSiblingsCommand final : public ICommand {
public:
    ReorderSiblingsCommand(std::string dragged_id, std::string target_id, core::TreeDropMode drop_mode)
        : dragged_id_(std::move(dragged_id)), target_id_(std::move(target_id)), drop_mode_(drop_mode) {}

    std::string Name() const override { return "ReorderSiblings"; }
    bool        Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    // The reordered per-parent child order captured from the mutation, so the app
    // can refresh its transient render-order override (`TreeDisplayOrder`)
    // immediately; the frame-boundary re-derive rebuilds the tree from the
    // reordered SACM.
    const core::TreeDisplayOrder& ReorderedOrder() const { return reordered_; }

private:
    std::string           dragged_id_;
    std::string           target_id_;
    core::TreeDropMode    drop_mode_;
    core::TreeDisplayOrder reordered_;
};

// Move a dragged element (and its subtree) under a new parent, changing the
// semantic relationship. Mints a new relationship id deterministically.
class MoveSubtreeCommand final : public ICommand {
public:
    MoveSubtreeCommand(std::string dragged_id, std::string new_parent_id)
        : dragged_id_(std::move(dragged_id)), new_parent_id_(std::move(new_parent_id)) {}

    std::string Name() const override { return "MoveSubtree"; }
    bool        Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

private:
    std::string dragged_id_;
    std::string new_parent_id_;
};

} // namespace core::commands
