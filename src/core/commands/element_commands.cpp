#include "core/commands/element_commands.h"

#include "core/commands/library_bridge.h"
#include "core/derived_views.h"
#include "core/relationship_editing.h"
#include "core/string_utils.h"
#include "parser/model_utils.h"
#include "sacm_adapter/document_edit.h"

#include <algorithm>
#include <string>
#include <vector>

namespace core::commands {

namespace {

// ---------------------------------------------------------------------------
// Phase 2 slice 2b-1 -- the LIVE edit flip for the core GSN element commands.
//
// A flipped command mutates the library FIRST (through the seams Phase 1b
// proved canonical-hash-equivalent to the legacy mutators) and sets
// `ctx.library_primary`, which makes the bus rebuild `ctx.model` /
// `ctx.package` from the library. The audit payload is unchanged: the ids are
// planned by the SAME generator the legacy factory uses
// (`core::Plan*Ids`) and handed to the seam verbatim.
//
// The flip is conditional on a library document being present, because two
// legitimate callers have none: a file opened through the legacy-parser
// fallback, and the no-bus dispatch path (plus most unit tests). Those keep the
// legacy mutator, and the bus's Stage 5 net keeps the library in step -- i.e.
// exactly the pre-flip behaviour.
//
// The seams report two distinct failures and this routing keeps them distinct:
//   * supported == false  -- no library mapping for this shape; NOTHING was
//     mutated, so fall back to the legacy mutator.
//   * applied == false    -- the library REJECTED the edit. Its diagnostics are
//     surfaced verbatim and the command fails. Quietly applying the legacy edit
//     instead would reinterpret a library rejection, which is precisely the
//     divergence this migration exists to remove.

sacm_adapter::ChildKind ToAdapterChildKind(NewElementKind kind) {
    switch (kind) {
    case NewElementKind::Goal:
        return sacm_adapter::ChildKind::Goal;
    case NewElementKind::Strategy:
        return sacm_adapter::ChildKind::Strategy;
    case NewElementKind::Solution:
        return sacm_adapter::ChildKind::Solution;
    case NewElementKind::Context:
        return sacm_adapter::ChildKind::Context;
    case NewElementKind::Assumption:
        return sacm_adapter::ChildKind::Assumption;
    case NewElementKind::Justification:
        return sacm_adapter::ChildKind::Justification;
    }
    return sacm_adapter::ChildKind::Goal;
}

sacm_adapter::ChallengeSource ToAdapterChallengeSource(ChallengeSourceType type) {
    switch (type) {
    case ChallengeSourceType::CounterArgument:
        return sacm_adapter::ChallengeSource::CounterArgument;
    case ChallengeSourceType::CounterEvidence:
        return sacm_adapter::ChallengeSource::CounterEvidence;
    }
    return sacm_adapter::ChallengeSource::CounterArgument;
}

// One relationship whose endpoints a reparent rewrote, flattened for the seam.
struct RetargetedRelationship {
    std::string id;
    std::vector<std::string> sources;
    std::vector<std::string> targets;
    std::string reasoning;
};

// The endpoint rewrites `core::ReparentChildrenToParent` performed, found by
// diffing the model before against the model after.
//
// The reparent MUST be run alone for this to be right. Diffing after the whole of
// `RemoveElement` also picks up its scrub, and a context whose only target was
// the removed node then comes back with an empty target list -- a set no
// relationship may hold and the seam rightly refuses. The scrub is not this
// caller's job: `apply_delete_element` performs it, under the same
// ScrubReferences policy the legacy pass used.
std::vector<RetargetedRelationship> ReparentedRelationships(const parser::AssuranceCase& before,
                                                            const parser::AssuranceCase& after) {
    std::vector<RetargetedRelationship> changed;
    for (const parser::SacmElement& updated : after.elements) {
        if (!parser::IsRelationshipElement(updated))
            continue;
        const parser::SacmElement* original = parser::FindElementById(before, updated.id);
        if (original == nullptr)
            continue;
        if (original->source_refs == updated.source_refs && original->target_refs == updated.target_refs &&
            original->reasoning_ref == updated.reasoning_ref) {
            continue;
        }
        // The render-only placeholder that places a bare strategy under its goal
        // exists in ctx.model but has never existed in the library, so a rewrite
        // of it can only be refused (SACM-CMD-001). Whatever the placeholder was
        // doing is re-synthesized from the strategyTarget tag on the next
        // rebuild.
        if (core::IsBareStrategyPlacementPlaceholder(updated))
            continue;
        // A rewrite that leaves the relationship with nothing to relate is not
        // written: it is the removal's own scrub showing through the reparent (a
        // strategy's inference whose sources are already gone loses its last
        // endpoint when the reasoning is cleared), and the library rightly
        // refuses such an endpoint set (an AssertedInference needs a source,
        // clause 11.13, SACM-CMD-005). The relationship dies with the deleted
        // node instead -- `apply_delete_element`'s ScrubReferences drops it, the
        // same scrub-then-drop the legacy RemoveElement applies with this same
        // predicate.
        if (core::IsParserRelationshipDangling(updated))
            continue;
        changed.push_back(RetargetedRelationship{
            .id = updated.id,
            .sources = updated.source_refs,
            .targets = updated.target_refs,
            .reasoning = updated.reasoning_ref,
        });
    }
    return changed;
}
} // namespace

std::string NewElementKindToToken(NewElementKind kind) {
    switch (kind) {
    case NewElementKind::Goal:
        return "Goal";
    case NewElementKind::Strategy:
        return "Strategy";
    case NewElementKind::Solution:
        return "Solution";
    case NewElementKind::Context:
        return "Context";
    case NewElementKind::Assumption:
        return "Assumption";
    case NewElementKind::Justification:
        return "Justification";
    }
    return "Goal";
}

bool NewElementKindFromToken(const std::string& token, NewElementKind& out) {
    if (token == "Goal") {
        out = NewElementKind::Goal;
        return true;
    }
    if (token == "Strategy") {
        out = NewElementKind::Strategy;
        return true;
    }
    if (token == "Solution") {
        out = NewElementKind::Solution;
        return true;
    }
    if (token == "Context") {
        out = NewElementKind::Context;
        return true;
    }
    if (token == "Assumption") {
        out = NewElementKind::Assumption;
        return true;
    }
    if (token == "Justification") {
        out = NewElementKind::Justification;
        return true;
    }
    return false;
}

std::string RemoveModeToToken(RemoveMode mode) {
    switch (mode) {
    case RemoveMode::NodeOnly:
        return "NodeOnly";
    case RemoveMode::NodeAndDescendants:
        return "NodeAndDescendants";
    }
    return "NodeOnly";
}

bool RemoveModeFromToken(const std::string& token, RemoveMode& out) {
    if (token == "NodeOnly") {
        out = RemoveMode::NodeOnly;
        return true;
    }
    if (token == "NodeAndDescendants") {
        out = RemoveMode::NodeAndDescendants;
        return true;
    }
    return false;
}

std::string ChallengeSourceTypeToToken(ChallengeSourceType type) {
    switch (type) {
    case ChallengeSourceType::CounterArgument:
        return "CounterArgument";
    case ChallengeSourceType::CounterEvidence:
        return "CounterEvidence";
    }
    return "CounterArgument";
}

bool ChallengeSourceTypeFromToken(const std::string& token, ChallengeSourceType& out) {
    if (token == "CounterArgument") {
        out = ChallengeSourceType::CounterArgument;
        return true;
    }
    if (token == "CounterEvidence") {
        out = ChallengeSourceType::CounterEvidence;
        return true;
    }
    return false;
}

std::string ArgumentTargetKindToToken(ArgumentTarget::Kind kind) {
    switch (kind) {
    case ArgumentTarget::Kind::Element:
        return "Element";
    case ArgumentTarget::Kind::Relationship:
        return "Relationship";
    }
    return "Element";
}

bool ArgumentTargetKindFromToken(const std::string& token, ArgumentTarget::Kind& out) {
    if (token == "Element") {
        out = ArgumentTarget::Kind::Element;
        return true;
    }
    if (token == "Relationship") {
        out = ArgumentTarget::Kind::Relationship;
        return true;
    }
    return false;
}

bool CreateTopGoalCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    bool applied_to_library = false;
    if (ctx.library_document != nullptr) {
        const std::string planned_id = core::PlanTopGoalId(ctx.model);
        const sacm_adapter::AddChildOutcome outcome =
            sacm_adapter::apply_add_top_goal(*ctx.library_document, planned_id);
        if (outcome.supported && !outcome.applied) {
            out_error = LibraryRejection("the new top goal", outcome.diagnostics);
            return false;
        }
        if (outcome.applied) {
            generated_id_ = outcome.new_element_id;
            ctx.library_primary = true;
            applied_to_library = true;
        }
    }
    // The seam-unsupported fallback goes through the GUARDED bridge, never the
    // raw legacy mutator. The invariant, held file-wide: no command mutates the
    // legacy package while a library document is present except inside
    // BridgeLegacyMutationToLibrary, whose guard refuses any document the
    // projection cannot fully represent. Round-4 verification measured the raw
    // fallback on artifact-full-valid.sacm.xmi (no ArgumentPackage, so the seam
    // reports unsupported): success reported, 8 of 9 clause-12 elements deleted
    // from the tracked file. Without a document, ApplyLegacyOrRefuse
    // runs the legacy mutator directly -- the pre-flip behavior.
    if (!applied_to_library) {
        const LibraryBridgeMutator mutate =
            [this](parser::AssuranceCase& model, sacm::AssuranceCasePackage& package, std::string& err) -> bool {
            return core::AddTopGoal(model, &package, generated_id_, err);
        };
        if (!ApplyLegacyOrRefuse(ctx, mutate, out_error))
            return false;
    }

    out_event.event_type = "CreateTopGoal";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["generated_id"] = generated_id_;
    return true;
}

bool CreateChildElementCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    if (parent_id_.empty()) {
        out_error = "CreateChildElementCommand requires a parent id";
        return false;
    }

    bool applied_to_library = false;
    if (ctx.library_document != nullptr) {
        std::string planned_element_id;
        std::string planned_relationship_id;
        if (!core::PlanChildElementIds(
                ctx.model, &ctx.package, parent_id_, kind_, planned_element_id, planned_relationship_id, out_error))
            return false;
        const sacm_adapter::AddChildOutcome outcome = sacm_adapter::apply_add_child(
            *ctx.library_document, parent_id_, ToAdapterChildKind(kind_), planned_element_id, planned_relationship_id);
        if (outcome.supported && !outcome.applied) {
            out_error = LibraryRejection("the new child element", outcome.diagnostics);
            return false;
        }
        if (outcome.applied) {
            generated_id_ = outcome.new_element_id;
            // Empty for a Strategy (its inference is deferred to the first
            // sub-goal) and for a sub-goal that EXTENDS an existing strategy
            // inference -- the same "id actually created" semantics the legacy
            // factory records, so the audit payload is unchanged.
            generated_relationship_id_ = outcome.new_relationship_id;
            ctx.library_primary = true;
            applied_to_library = true;
        }
    }
    // Same invariant as CreateTopGoal above: seam-unsupported falls to the
    // guarded bridge, not the raw legacy mutator.
    if (!applied_to_library) {
        const LibraryBridgeMutator mutate =
            [this](parser::AssuranceCase& model, sacm::AssuranceCasePackage& package, std::string& err) -> bool {
            return core::AddChildElement(
                model, &package, parent_id_, kind_, generated_id_, generated_relationship_id_, err);
        };
        if (!ApplyLegacyOrRefuse(ctx, mutate, out_error))
            return false;
    }

    out_event.event_type = "CreateChildElement";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["parent_id"] = parent_id_;
    out_event.payload["kind"] = NewElementKindToToken(kind_);
    out_event.payload["generated_id"] = generated_id_;
    out_event.payload["generated_relationship_id"] = generated_relationship_id_;
    return true;
}

bool CreateChallengeCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    if (target_.id.empty()) {
        out_error = "CreateChallengeCommand requires a target id";
        return false;
    }

    bool applied_to_library = false;
    if (ctx.library_document != nullptr) {
        std::string planned_element_id;
        std::string planned_relationship_id;
        if (!core::PlanChallengeIds(
                ctx.model, &ctx.package, target_, source_type_, planned_element_id, planned_relationship_id, out_error))
            return false;
        // The seam resolves an element vs relationship target itself (the
        // library gives every contained element a parent), so `target_.kind` is
        // only used above, for the anchor the legacy id prefix is scoped to.
        const sacm_adapter::AddChildOutcome outcome =
            sacm_adapter::apply_challenge(*ctx.library_document,
                                          target_.id,
                                          ToAdapterChallengeSource(source_type_),
                                          planned_element_id,
                                          planned_relationship_id);
        if (outcome.supported && !outcome.applied) {
            out_error = LibraryRejection("the new challenge", outcome.diagnostics);
            return false;
        }
        if (outcome.applied) {
            generated_id_ = outcome.new_element_id;
            generated_relationship_id_ = outcome.new_relationship_id;
            ctx.library_primary = true;
            applied_to_library = true;
        }
    }
    // Same invariant as CreateTopGoal above: seam-unsupported falls to the
    // guarded bridge, not the raw legacy mutator.
    if (!applied_to_library) {
        const LibraryBridgeMutator mutate =
            [this](parser::AssuranceCase& model, sacm::AssuranceCasePackage& package, std::string& err) -> bool {
            return core::AddChallenge(
                model, &package, target_, source_type_, generated_id_, generated_relationship_id_, err);
        };
        if (!ApplyLegacyOrRefuse(ctx, mutate, out_error))
            return false;
    }

    out_event.event_type = "CreateChallenge";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["target_id"] = target_.id;
    out_event.payload["target_kind"] = ArgumentTargetKindToToken(target_.kind);
    out_event.payload["source_type"] = ChallengeSourceTypeToToken(source_type_);
    out_event.payload["generated_id"] = generated_id_;
    out_event.payload["generated_relationship_id"] = generated_relationship_id_;
    return true;
}

bool RemoveElementCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    if (element_id_.empty()) {
        out_error = "RemoveElementCommand requires an element id";
        return false;
    }

    auto planned = core::PlanRemoval(ctx.model, element_id_, mode_);
    if (planned.empty()) {
        out_error = "Nothing to remove for element " + element_id_;
        return false;
    }

    std::vector<std::string> deleted_ids(planned.begin(), planned.end());
    std::sort(deleted_ids.begin(), deleted_ids.end());

    // The library seam deletes ONE element and SCRUBS it out of every
    // referencing relationship, dropping a relationship only once it is left
    // structurally empty -- exactly the legacy core::RemoveElement scrub-then-drop
    // (ReferenceDeletePolicy::ScrubReferences in the adapter). So the seam
    // reproduces the legacy removal for a closed subtree of any shape, including
    // removing one sub-goal of a strategy whose single inference has several
    // sources (the inference survives, scrubbed to the rest, rather than cascading
    // away).
    //
    // Both modes reach the library natively now, which is phase 3's exit criterion
    // of `RemoveElement` having one code path.
    //
    // NodeOnly was the last element command on the guarded bridge because it
    // REPARENTS: a child's inference is retargeted from the removed node onto its
    // parent, and a strategy interposed as a reasoning has that reasoning cleared.
    // A retarget is not expressible as a set of per-id deletes. Before the bridge
    // caught it, this path fell through to the raw legacy mutator and the bus
    // autosaved lossy projection bytes over the tracked file -- silently deleting
    // every element kind the projection cannot hold, from a context-menu action,
    // with no diagnostic (round-3 verification, probe a).
    //
    // `SetRelationshipEnds` expresses the retarget. The ordering below is the part
    // that took two attempts:
    //
    //   1. Run the reparent ALONE (`core::ReparentChildrenToParent`) on a scratch
    //      and mirror the endpoint rewrites. Running the whole of
    //      `core::RemoveElement` instead also applies its scrub, and diffing that
    //      cannot tell the two apart -- a context whose only target was the
    //      removed node comes back with an empty target list, which no
    //      relationship may hold and the seam rightly refuses.
    //   2. THEN delete the planned ids. `apply_delete_element` scrubs under the
    //      same ScrubReferences policy the legacy pass used, so the scrub is not
    //      reimplemented here -- and by now nothing points at the node except what
    //      is meant to die with it.
    bool applied_to_library = false;
    if (ctx.library_document != nullptr && mode_ == RemoveMode::NodeOnly) {
        // Before the first write: this is several writes, and a failure between them
        // leaves the document changed. The bus re-derives the live views on its
        // failure path only when `library_primary` says the flip engaged, so setting
        // this afterwards would leave the UI rendering a part-removed document. Safe
        // because nothing below falls back -- an unsupported endpoint rewrite is a
        // hard failure here, for the reason given on the refusal check.
        ctx.library_primary = true;
        parser::AssuranceCase reparented = ctx.model;
        core::ReparentChildrenToParent(reparented, nullptr, element_id_);
        for (const RetargetedRelationship& changed : ReparentedRelationships(ctx.model, reparented)) {
            const sacm_adapter::EditOutcome ends = sacm_adapter::apply_set_relationship_ends(
                *ctx.library_document, changed.id, changed.sources, changed.targets, changed.reasoning);
            // `!supported` fails here rather than falling through, which is the
            // opposite of how the seams elsewhere treat it. Falling through would
            // skip the retarget and then apply the deletes anyway, so the child
            // inference would be SCRUBBED instead of reparented -- a NodeOnly
            // removal quietly behaving like NodeAndDescendants, which is the one
            // outcome this command exists to avoid. The replay branch in
            // `event_replayer.cpp` fails on it too, so the two cannot disagree.
            // (The seam only reports unsupported for an empty relationship id,
            // which a projected relationship cannot have; this is the guard being
            // right rather than a reachable path.)
            if (!ends.supported || !ends.applied) {
                out_error = LibraryRejection("the reparent of " + changed.id, ends.diagnostics);
                return false;
            }
        }
        for (const std::string& id : deleted_ids) {
            const sacm_adapter::DeleteOutcome outcome = sacm_adapter::apply_delete_element(*ctx.library_document, id);
            if (!outcome.applied) {
                out_error = LibraryRejection("the deletion of " + id, outcome.diagnostics);
                return false;
            }
        }
        applied_to_library = true;
    }
    if (ctx.library_document != nullptr && mode_ == RemoveMode::NodeAndDescendants) {
        // Exactly the ids `PlanRemoval` produced -- the same set the audit event
        // records, walked in the same sorted order `ApplyEventToLibrary` replays,
        // so the live document and the replayed document agree by construction.
        for (const std::string& id : deleted_ids) {
            const sacm_adapter::DeleteOutcome outcome = sacm_adapter::apply_delete_element(*ctx.library_document, id);
            if (!outcome.applied) {
                // Mid-cascade failure: earlier ids are already gone, so the bus
                // re-derives the views from the library on the failure path
                // rather than leaving them describing a document that no longer
                // exists.
                out_error = LibraryRejection("the deletion of " + id, outcome.diagnostics);
                return false;
            }
            ctx.library_primary = true;
            applied_to_library = true;
        }
    }
    if (!applied_to_library) {
        if (mode_ == RemoveMode::NodeOnly) {
            const LibraryBridgeMutator mutate =
                [this](parser::AssuranceCase& model, sacm::AssuranceCasePackage& package, std::string& err) -> bool {
                return core::RemoveElement(model, &package, element_id_, RemoveMode::NodeOnly, err);
            };
            if (!ApplyLegacyOrRefuse(ctx, mutate, out_error))
                return false;
        } else if (!core::RemoveElement(ctx.model, &ctx.package, element_id_, mode_, out_error)) {
            return false;
        }
    }

    removed_count_ = deleted_ids.size();

    out_event.event_type = "RemoveElement";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["element_id"] = element_id_;
    out_event.payload["mode"] = RemoveModeToToken(mode_);
    out_event.payload["deleted_ids"] = deleted_ids;
    return true;
}

bool UpdateElementTextCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    if (element_id_.empty()) {
        out_error = "UpdateElementTextCommand requires an element id";
        return false;
    }
    if (language_.empty()) {
        out_error = "UpdateElementTextCommand requires a language code";
        return false;
    }

    // ADR 0012: a claim-like element carries ONE clause-8.9 Description and that
    // Description is its statement, which the POD holds in `content`. It has no
    // note slot, so this edit has nowhere to land.
    //
    // Refused rather than run. The bridge would write the legacy `description`
    // field quite happily, the next projection would drop it, and the command
    // would have reported success -- and written an audit event -- for an edit
    // that vanished. A recorded event nobody can replay to a visible result is
    // worse than a refusal.
    if (field_ == ElementTextField::Description) {
        const parser::SacmElement* element = parser::FindElementById(ctx.model, element_id_);
        if (element != nullptr && core::ClaimLikeCarriesStatementAsDescription(*element)) {
            out_error = "Element " + element_id_ + " is a " + element->type +
                        ", which carries one description and that description is its statement. "
                        "Edit the content field instead.";
            return false;
        }
    }

    // Content still goes through the BRIDGE rather than the apply_text_edit seam.
    // ADR 0012 removed the model difference that made the seam diverge from replay,
    // so the native flip is now unblocked -- but flipping it is its own slice, with
    // its own convergence evidence, and it is not done here.
    // `old_value_` is captured whether the bridge runs it on the scratch projection
    // (the library's last committed value) or the legacy fallback runs it in place.
    // Phase 3f: Name and Content apply through the `apply_text_edit` seam. ADR 0012
    // removed what kept them bridged -- a claim's lone Description used to mean one
    // thing to the seam and another to the legacy mutator, so the replay oracle
    // could not agree with the live edit. With one Description that is the
    // statement, both routes write the same thing.
    //
    // The seam is not universal: it declines shapes it cannot express (a name in a
    // non-primary language, Content on a kind that carries its text elsewhere), and
    // those still fall through to the guarded bridge below. A REFUSAL is different
    // from a decline and must not fall through -- `apply_text_edit` reports one as
    // `supported && !applied`, and routing that to the bridge would reinstate the
    // very edit the seam rejected.
    bool applied_natively = false;
    if (ctx.library_document != nullptr && (field_ == ElementTextField::Name || field_ == ElementTextField::Content)) {
        const parser::SacmElement* before = parser::FindElementById(ctx.model, element_id_);
        // The same reading the legacy mutator makes, so both routes record the same
        // audit `old_value` for the same edit.
        old_value_ = before == nullptr ? std::string() : core::ReadParserField(*before, field_, language_);
        const sacm_adapter::EditOutcome outcome = sacm_adapter::apply_text_edit(
            *ctx.library_document,
            element_id_,
            field_ == ElementTextField::Name ? sacm_adapter::TextField::Name : sacm_adapter::TextField::Content,
            language_,
            new_value_);
        if (outcome.supported && !outcome.applied) {
            out_error = outcome.diagnostics.empty() ? "The library refused the text edit on " + element_id_ + "."
                                                    : outcome.diagnostics.front().message;
            return false;
        }
        if (outcome.applied) {
            ctx.library_primary = true;
            applied_natively = true;
        }
    }
    const LibraryBridgeMutator mutate =
        [&](parser::AssuranceCase& model, sacm::AssuranceCasePackage& package, std::string& err) -> bool {
        return core::SetElementTextField(model, &package, element_id_, field_, language_, new_value_, old_value_, err);
    };
    if (!applied_natively && !ApplyLegacyOrRefuse(ctx, mutate, out_error))
        return false;

    was_no_op_ = (old_value_ == new_value_);

    out_event.event_type = "UpdateElementText";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["element_id"] = element_id_;
    out_event.payload["field"] = core::ElementTextFieldToToken(field_);
    out_event.payload["language"] = language_;
    out_event.payload["old_value"] = old_value_;
    out_event.payload["new_value"] = new_value_;
    return true;
}

bool UpdateGsnIdentifierCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    if (element_id_.empty()) {
        out_error = "UpdateGsnIdentifierCommand requires an element id";
        return false;
    }

    // Slice 2b of the legacy-bridge retirement. The GSN identifier is a vendor
    // TaggedValue, which the library can carry natively, so this needs no
    // projection round trip.
    //
    // `core::ValidateGsnIdentifierChange` is the legacy mutator's own front half,
    // split out rather than reimplemented: non-empty, no surrounding whitespace,
    // the target exists and is a node, and the identifier is not already taken.
    // Those are Assurance Forge's editing rules -- SACM has no notion of a
    // diagram label at all -- so the seam does not enforce them and a flip that
    // just called it would have dropped every one. Running them first also means
    // the no-op case (same identifier) never reaches the library.
    bool applied_to_library = false;
    if (CanApplyLibraryPrimary(ctx)) {
        std::string normalized;
        if (!core::ValidateGsnIdentifierChange(
                ctx.model, element_id_, new_identifier_, normalized, old_identifier_, out_error))
            return false;
        if (old_identifier_ != normalized) {
            const sacm_adapter::EditOutcome outcome =
                sacm_adapter::apply_set_gsn_identifier(*ctx.library_document, element_id_, normalized);
            if (outcome.supported && !outcome.applied) {
                out_error = LibraryRejection("the GSN identifier for " + element_id_, outcome.diagnostics);
                return false;
            }
            if (outcome.applied)
                ctx.library_primary = true;
        }
        // A no-op still counts as handled: the legacy mutator returns success
        // without touching anything, and so must this.
        applied_to_library = true;
    }
    if (!applied_to_library) {
        const LibraryBridgeMutator mutate =
            [&](parser::AssuranceCase& model, sacm::AssuranceCasePackage& package, std::string& error) {
                return core::SetGsnIdentifier(model, &package, element_id_, new_identifier_, old_identifier_, error);
            };
        if (!ApplyLegacyOrRefuse(ctx, mutate, out_error))
            return false;
    }

    was_no_op_ = old_identifier_ == new_identifier_;
    out_event.event_type = "UpdateGsnIdentifier";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["element_id"] = element_id_;
    out_event.payload["old_identifier"] = old_identifier_;
    out_event.payload["new_identifier"] = new_identifier_;
    return true;
}

namespace {

// Shared by the single write and the import: the evidence the write names,
// or why it cannot be written.
const parser::SacmElement*
EvidenceElementOrError(const CommandContext& ctx, const std::string& element_id, std::string& out_error) {
    const parser::SacmElement* element = parser::FindElementById(ctx.model, element_id);
    if (element == nullptr) {
        out_error = "Element " + element_id + " was not found";
        return nullptr;
    }
    if (element->type != "artifactreference") {
        out_error = "Element " + element_id + " is not evidence (an ArtifactReference)";
        return nullptr;
    }
    return element;
}

} // namespace

bool SetEvidenceAttributeCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    if (element_id_.empty()) {
        out_error = "SetEvidenceAttributeCommand requires an element id";
        return false;
    }
    if (!CanApplyLibraryPrimary(ctx)) {
        out_error = "Recording an evidence attribute needs the SACM library document, and this file was loaded "
                    "through the compatibility parser. The document is unchanged.";
        return false;
    }
    const parser::SacmElement* element = EvidenceElementOrError(ctx, element_id_, out_error);
    if (element == nullptr)
        return false;
    // The seam trims before writing; compare and record what will be stored.
    value_ = core::TrimWhitespace(value_);
    old_value_ = EvidenceRecordField(element->evidence, attribute_);
    was_no_op_ = old_value_ == value_;
    if (!was_no_op_) {
        const sacm_adapter::EditOutcome outcome =
            sacm_adapter::apply_set_evidence_attribute(*ctx.library_document, element_id_, attribute_, value_);
        if (!outcome.supported || !outcome.applied) {
            out_error = LibraryRejection(
                std::string("the ") + EvidenceAttributeToken(attribute_) + " of " + element_id_, outcome.diagnostics);
            return false;
        }
        ctx.library_primary = true;
    }

    out_event.event_type = "SetEvidenceAttribute";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["element_id"] = element_id_;
    out_event.payload["attribute"] = EvidenceAttributeToken(attribute_);
    out_event.payload["old_value"] = old_value_;
    out_event.payload["new_value"] = value_;
    return true;
}

bool ImportEvidenceAssessmentsCommand::Apply(CommandContext& ctx,
                                             audit::AuditEvent& out_event,
                                             std::string& out_error) {
    if (writes_.empty()) {
        out_error = "ImportEvidenceAssessmentsCommand has nothing to import";
        return false;
    }
    if (!CanApplyLibraryPrimary(ctx)) {
        out_error = "Moving assessments into the SACM document needs the SACM library document, and this file was "
                    "loaded through the compatibility parser. The document is unchanged.";
        return false;
    }
    // Checked before the first write, so a stale entry fails the import while
    // the document is still untouched rather than after half of it landed.
    for (const EvidenceAttributeWrite& write : writes_) {
        if (EvidenceElementOrError(ctx, write.element_id, out_error) == nullptr)
            return false;
    }
    // Several writes; set before the first so a failure between them makes the
    // bus re-derive the views from the library rather than leave them stale.
    ctx.library_primary = true;
    nlohmann::ordered_json items = nlohmann::ordered_json::array();
    for (const EvidenceAttributeWrite& write : writes_) {
        const std::string value = core::TrimWhitespace(write.value);
        const sacm_adapter::EditOutcome outcome =
            sacm_adapter::apply_set_evidence_attribute(*ctx.library_document, write.element_id, write.attribute, value);
        if (!outcome.supported || !outcome.applied) {
            out_error = LibraryRejection(std::string("the ") + EvidenceAttributeToken(write.attribute) + " of " +
                                             write.element_id,
                                         outcome.diagnostics);
            return false;
        }
        ++applied_count_;
        nlohmann::ordered_json item = nlohmann::ordered_json::object();
        item["element_id"] = write.element_id;
        item["attribute"] = EvidenceAttributeToken(write.attribute);
        item["value"] = value;
        items.push_back(std::move(item));
    }

    out_event.event_type = "ImportEvidenceAssessments";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["items"] = std::move(items);
    return true;
}

namespace {

// The AssertedEvidence already carrying `evidence_id` to `claim_id`, if any.
const parser::SacmElement*
ExistingSupportLink(const parser::AssuranceCase& model, const std::string& claim_id, const std::string& evidence_id) {
    for (const parser::SacmElement& element : model.elements) {
        if (element.type != "assertedevidence")
            continue;
        const bool from_evidence =
            std::find(element.source_refs.begin(), element.source_refs.end(), evidence_id) != element.source_refs.end();
        const bool to_claim =
            std::find(element.target_refs.begin(), element.target_refs.end(), claim_id) != element.target_refs.end();
        if (from_evidence && to_claim)
            return &element;
    }
    return nullptr;
}

} // namespace

bool CreateEvidenceCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    if (!CanApplyLibraryPrimary(ctx)) {
        out_error = "Creating evidence from the register needs the SACM library document, and this file was loaded "
                    "through the compatibility parser. The document is unchanged.";
        return false;
    }
    if (!core::PlanEvidenceIds(
            ctx.model, &ctx.package, claim_id_, generated_id_, generated_relationship_id_, out_error))
        return false;
    text_ = core::TrimWhitespace(text_);

    if (!claim_id_.empty()) {
        const sacm_adapter::AddChildOutcome outcome =
            sacm_adapter::apply_add_child(*ctx.library_document,
                                          claim_id_,
                                          ToAdapterChildKind(NewElementKind::Solution),
                                          generated_id_,
                                          generated_relationship_id_);
        if (!outcome.supported || !outcome.applied) {
            out_error = LibraryRejection("the new evidence under " + claim_id_, outcome.diagnostics);
            return false;
        }
        generated_id_ = outcome.new_element_id;
        generated_relationship_id_ = outcome.new_relationship_id;
    } else {
        const std::string package_id = sacm_adapter::resolve_argument_package_id(*ctx.library_document, "");
        if (package_id.empty()) {
            out_error = "The document has no argument package to create evidence in";
            return false;
        }
        sacm_adapter::CreateElementFields fields;
        fields.element_id = generated_id_;
        const sacm_adapter::AddChildOutcome outcome = sacm_adapter::apply_create_element(
            *ctx.library_document, package_id, sacm_adapter::NewElementKind::ArtifactReference, fields);
        if (!outcome.supported || !outcome.applied) {
            out_error = LibraryRejection("the new evidence", outcome.diagnostics);
            return false;
        }
        generated_id_ = outcome.new_element_id;
        generated_relationship_id_.clear();
    }
    ctx.library_primary = true;

    if (!text_.empty()) {
        const sacm_adapter::EditOutcome written = sacm_adapter::apply_text_edit(
            *ctx.library_document, generated_id_, sacm_adapter::TextField::Description, "en", text_);
        if (!written.supported || !written.applied) {
            // Leave the document as it was rather than with a blank element.
            sacm_adapter::apply_delete_element(*ctx.library_document, generated_id_);
            out_error = LibraryRejection("the statement of " + generated_id_, written.diagnostics);
            return false;
        }
    }

    out_event.event_type = "CreateEvidence";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["claim_id"] = claim_id_;
    out_event.payload["text"] = text_;
    out_event.payload["generated_id"] = generated_id_;
    out_event.payload["generated_relationship_id"] = generated_relationship_id_;
    return true;
}

bool LinkEvidenceCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    if (!CanApplyLibraryPrimary(ctx)) {
        out_error = "Linking evidence needs the SACM library document, and this file was loaded through the "
                    "compatibility parser. The document is unchanged.";
        return false;
    }
    if (EvidenceElementOrError(ctx, evidence_id_, out_error) == nullptr)
        return false;
    if (ExistingSupportLink(ctx.model, claim_id_, evidence_id_) != nullptr) {
        out_error = "Claim " + claim_id_ + " already rests on " + evidence_id_;
        return false;
    }
    if (!core::PlanSupportRelationshipId(ctx.model, &ctx.package, claim_id_, generated_relationship_id_, out_error))
        return false;

    const sacm_adapter::AddChildOutcome outcome =
        sacm_adapter::apply_attach_child(*ctx.library_document, claim_id_, evidence_id_, generated_relationship_id_);
    if (!outcome.supported || !outcome.applied) {
        out_error = LibraryRejection("the link from " + evidence_id_ + " to " + claim_id_, outcome.diagnostics);
        return false;
    }
    generated_relationship_id_ = outcome.new_relationship_id;
    ctx.library_primary = true;

    out_event.event_type = "LinkEvidence";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["claim_id"] = claim_id_;
    out_event.payload["evidence_id"] = evidence_id_;
    out_event.payload["generated_relationship_id"] = generated_relationship_id_;
    return true;
}

bool SetEvidenceLocationCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    if (element_id_.empty()) {
        out_error = "SetEvidenceLocationCommand requires an element id";
        return false;
    }
    if (!CanApplyLibraryPrimary(ctx)) {
        out_error = "Recording where evidence is needs the SACM library document, and this file was loaded "
                    "through the compatibility parser. The document is unchanged.";
        return false;
    }
    const parser::SacmElement* element = parser::FindElementById(ctx.model, element_id_);
    if (element == nullptr) {
        out_error = "Element " + element_id_ + " was not found";
        return false;
    }
    if (element->type != "artifactreference") {
        out_error = "Element " + element_id_ + " is not evidence (an ArtifactReference)";
        return false;
    }
    // The seam trims before writing, so compare and record what will actually be
    // stored: a location that differs only by surrounding whitespace is not an
    // edit, and recording it as one would dirty the document for nothing.
    location_ = core::TrimWhitespace(location_);
    old_location_ = element->artifact_location;
    was_no_op_ = old_location_ == location_;
    if (!was_no_op_) {
        const sacm_adapter::EditOutcome outcome =
            sacm_adapter::apply_set_evidence_location(*ctx.library_document, element_id_, location_);
        if (!outcome.supported || !outcome.applied) {
            out_error = LibraryRejection("the location of " + element_id_, outcome.diagnostics);
            return false;
        }
        ctx.library_primary = true;
    }

    out_event.event_type = "SetEvidenceLocation";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["element_id"] = element_id_;
    out_event.payload["old_location"] = old_location_;
    out_event.payload["new_location"] = location_;
    return true;
}

bool RemoveRelationshipCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    if (relationship_id_.empty()) {
        out_error = "RemoveRelationshipCommand requires a relationship id";
        return false;
    }

    // The library's delete seam takes any element id and scrubs the deleted id
    // out of whatever still references it, so a relationship needs no special
    // case there -- unlike the app's node-shaped removal plan.
    bool applied_to_library = false;
    if (ctx.library_document != nullptr) {
        const sacm_adapter::DeleteOutcome outcome =
            sacm_adapter::apply_delete_element(*ctx.library_document, relationship_id_);
        if (outcome.supported && !outcome.applied) {
            out_error = LibraryRejection("the deletion of relationship " + relationship_id_, outcome.diagnostics);
            return false;
        }
        if (outcome.applied) {
            ctx.library_primary = true;
            applied_to_library = true;
        }
    }
    if (!applied_to_library && !core::RemoveRelationship(ctx.model, &ctx.package, relationship_id_, out_error))
        return false;

    out_event.event_type = "RemoveRelationship";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["relationship_id"] = relationship_id_;
    return true;
}

bool DropRelationshipReferenceCommand::Apply(CommandContext& ctx,
                                             audit::AuditEvent& out_event,
                                             std::string& out_error) {
    if (relationship_id_.empty() || reference_.empty()) {
        out_error = "DropRelationshipReferenceCommand requires a relationship id and a reference";
        return false;
    }

    // Phase 3a of the legacy-bridge retirement. This is the quick fix for an
    // `UnresolvedEndpoint` finding: the reference being dropped names an element
    // that does not exist, which is why the delete seam is no use here -- there
    // is nothing to delete. `SetRelationshipEnds` was added to the library for
    // it, and accepts an unresolved id only where the relationship already
    // carried one, so a two-fault relationship is repairable one endpoint at a
    // time while a new dangling reference still cannot be introduced.
    //
    // The scratch pattern the ACP tranche established: run the SAME
    // `core::DropRelationshipReference` on a projected copy to decide what the
    // result should be -- which references survive, and whether the relationship
    // is left structurally empty and must go -- then write that decision through
    // the seams. The scratch is read and discarded; the document is edited in
    // place, so nothing outside this relationship is touched.
    bool applied_to_library = false;
    if (CanApplyLibraryPrimary(ctx)) {
        // Model only, for the reason given on the strategy move below: the
        // package copy this used to make was never read.
        // Before the first write, for the reason given on the NodeOnly removal
        // above: this branch can both delete a relationship and rewrite another
        // relationship's endpoints, and a failure between the two must still
        // trigger the re-derive.
        ctx.library_primary = true;
        parser::AssuranceCase scratch_model = ctx.model;
        if (!core::DropRelationshipReference(
                scratch_model, nullptr, relationship_id_, reference_, removed_relationship_, out_error))
            return false;

        if (removed_relationship_) {
            // Scrubbing the last endpoint leaves nothing that relates anything,
            // so the relationship goes -- a delete, not an ends rewrite.
            const sacm_adapter::DeleteOutcome deleted =
                sacm_adapter::apply_delete_element(*ctx.library_document, relationship_id_);
            if (deleted.supported && !deleted.applied) {
                out_error = LibraryRejection("the removal of relationship " + relationship_id_, deleted.diagnostics);
                return false;
            }
        } else {
            const parser::SacmElement* repaired = parser::FindElementById(scratch_model, relationship_id_);
            if (repaired == nullptr) {
                out_error = "Relationship " + relationship_id_ + " disappeared while dropping " + reference_ + ".";
                return false;
            }
            const sacm_adapter::EditOutcome ends = sacm_adapter::apply_set_relationship_ends(*ctx.library_document,
                                                                                             relationship_id_,
                                                                                             repaired->source_refs,
                                                                                             repaired->target_refs,
                                                                                             repaired->reasoning_ref);
            // `!supported` is a failure, not a fall-through: see the NodeOnly
            // reparent above. Continuing would set `library_primary` with the
            // endpoints unwritten, reporting success for an edit the document
            // never received.
            if (!ends.supported || !ends.applied) {
                out_error = LibraryRejection("the endpoints of " + relationship_id_, ends.diagnostics);
                return false;
            }
        }
        applied_to_library = true;
    }
    if (!applied_to_library) {
        const LibraryBridgeMutator mutate =
            [&](parser::AssuranceCase& model, sacm::AssuranceCasePackage& package, std::string& error) {
                return core::DropRelationshipReference(
                    model, &package, relationship_id_, reference_, removed_relationship_, error);
            };
        if (!ApplyLegacyOrRefuse(ctx, mutate, out_error))
            return false;
    }

    out_event.event_type = "DropRelationshipReference";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["relationship_id"] = relationship_id_;
    out_event.payload["reference"] = reference_;
    out_event.payload["removed_relationship"] = removed_relationship_;
    return true;
}

bool MoveStrategyToReasoningCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    if (relationship_id_.empty() || strategy_id_.empty()) {
        out_error = "MoveStrategyToReasoningCommand requires a relationship id and a strategy id";
        return false;
    }

    // Phase 3b of the legacy-bridge retirement. Moving a strategy into an
    // inference's reasoning slot is one endpoint rewrite: the strategy leaves
    // `source` and becomes `reasoning`, which is precisely what
    // `SetRelationshipEnds` (added in slice 3a) does. No new library capability
    // was needed -- the retarget operation the tree work called for turned out to
    // cover this too.
    //
    // The GSN mapping is not re-decided here. A Strategy is an ArgumentReasoning
    // that hangs off a relationship rather than connecting elements, and the
    // legacy mutator already encodes that; this runs the SAME mutator on a
    // scratch projection and mirrors the endpoints it produced, so the mapping
    // recorded in docs/sacm/sacm-gsn-mapping.md is the only one in play.
    bool applied_to_library = false;
    if (CanApplyLibraryPrimary(ctx)) {
        // Model only. The mutator syncs a legacy package when given one, and
        // nothing here reads it -- the endpoints come off `scratch_model` below --
        // so passing nullptr skips a deep copy of the whole package and the sync
        // loop inside the mutator.
        parser::AssuranceCase scratch_model = ctx.model;
        if (!core::MoveStrategyToReasoning(scratch_model, nullptr, relationship_id_, strategy_id_, out_error))
            return false;
        const parser::SacmElement* moved = parser::FindElementById(scratch_model, relationship_id_);
        if (moved == nullptr) {
            out_error = "Inference " + relationship_id_ + " disappeared while moving " + strategy_id_ + ".";
            return false;
        }
        const sacm_adapter::EditOutcome ends = sacm_adapter::apply_set_relationship_ends(
            *ctx.library_document, relationship_id_, moved->source_refs, moved->target_refs, moved->reasoning_ref);
        // "the endpoints", not "the reasoning": the seam rewrites source, target
        // and reasoning together, so it can also refuse for a multiplicity reason
        // that has nothing to do with the reasoning slot. `!supported` fails for
        // the reason given on the NodeOnly reparent above -- this command IS the
        // endpoint rewrite, so continuing would report success for a document that
        // was never touched.
        if (!ends.supported || !ends.applied) {
            out_error = LibraryRejection("the endpoints of " + relationship_id_, ends.diagnostics);
            return false;
        }
        if (ends.applied) {
            ctx.library_primary = true;
            applied_to_library = true;
        }
    }
    if (!applied_to_library) {
        const LibraryBridgeMutator mutate =
            [&](parser::AssuranceCase& model, sacm::AssuranceCasePackage& package, std::string& error) {
                return core::MoveStrategyToReasoning(model, &package, relationship_id_, strategy_id_, error);
            };
        if (!ApplyLegacyOrRefuse(ctx, mutate, out_error))
            return false;
    }

    out_event.event_type = "MoveStrategyToReasoning";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["relationship_id"] = relationship_id_;
    out_event.payload["strategy_id"] = strategy_id_;
    return true;
}

bool SetElementUndevelopedCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    if (element_id_.empty()) {
        out_error = "SetElementUndevelopedCommand requires an element id";
        return false;
    }

    // Slice 2b of the legacy-bridge retirement, and a defect fix. GSN's
    // `undeveloped` is SACM `assertionDeclaration = needsSupport`, one enum --
    // whereas the legacy POD carried `undeveloped` as a boolean beside the
    // declaration, wrote both, and lost the boolean on reload, because the reader
    // honours that shorthand only when the declaration is still `asserted`.
    // Marking a GSN Assumption undeveloped therefore reported success and did
    // nothing, in memory and on disk.
    //
    // The seam writes the declaration, so the value sticks -- and refuses when
    // the declaration is already saying something else, rather than overwriting
    // it. See apply_set_undeveloped.
    bool applied_to_library = false;
    if (CanApplyLibraryPrimary(ctx)) {
        const parser::SacmElement* element = parser::FindElementById(ctx.model, element_id_);
        if (element == nullptr) {
            out_error = "Element not found in model: " + element_id_ + ".";
            return false;
        }
        old_value_ = element->undeveloped;
        const sacm_adapter::EditOutcome outcome =
            sacm_adapter::apply_set_undeveloped(*ctx.library_document, element_id_, undeveloped_);
        if (outcome.supported && !outcome.applied) {
            out_error = LibraryRejection("the undeveloped decorator on " + element_id_, outcome.diagnostics);
            return false;
        }
        if (outcome.applied) {
            ctx.library_primary = true;
            applied_to_library = true;
        }
    }
    if (!applied_to_library) {
        const LibraryBridgeMutator mutate =
            [&](parser::AssuranceCase& model, sacm::AssuranceCasePackage& package, std::string& error) {
                return core::SetElementUndeveloped(model, &package, element_id_, undeveloped_, old_value_, error);
            };
        if (!ApplyLegacyOrRefuse(ctx, mutate, out_error))
            return false;
    }

    was_no_op_ = old_value_ == undeveloped_;
    out_event.event_type = "SetElementUndeveloped";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["element_id"] = element_id_;
    out_event.payload["old_value"] = old_value_;
    out_event.payload["new_value"] = undeveloped_;
    return true;
}

} // namespace core::commands
