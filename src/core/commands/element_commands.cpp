#include "core/commands/element_commands.h"

#include "sacm_adapter/document_edit.h"

#include <algorithm>
#include <string>
#include <vector>

namespace core::commands {

namespace {

// Maps the app text field to the adapter's field enum for the Phase 9 Stage 5
// shadow-apply. Kept local: the adapter sits below core and cannot see
// core::ElementTextField.
sacm_adapter::TextField ToAdapterTextField(ElementTextField field) {
    switch (field) {
    case ElementTextField::Name:
        return sacm_adapter::TextField::Name;
    case ElementTextField::Description:
        return sacm_adapter::TextField::Description;
    case ElementTextField::Content:
        return sacm_adapter::TextField::Content;
    }
    return sacm_adapter::TextField::Name;
}

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
    case NewElementKind::Goal:          return sacm_adapter::ChildKind::Goal;
    case NewElementKind::Strategy:      return sacm_adapter::ChildKind::Strategy;
    case NewElementKind::Solution:      return sacm_adapter::ChildKind::Solution;
    case NewElementKind::Context:       return sacm_adapter::ChildKind::Context;
    case NewElementKind::Assumption:    return sacm_adapter::ChildKind::Assumption;
    case NewElementKind::Justification: return sacm_adapter::ChildKind::Justification;
    }
    return sacm_adapter::ChildKind::Goal;
}

sacm_adapter::ChallengeSource ToAdapterChallengeSource(ChallengeSourceType type) {
    switch (type) {
    case ChallengeSourceType::CounterArgument: return sacm_adapter::ChallengeSource::CounterArgument;
    case ChallengeSourceType::CounterEvidence: return sacm_adapter::ChallengeSource::CounterEvidence;
    }
    return sacm_adapter::ChallengeSource::CounterArgument;
}

// Library diagnostics, surfaced verbatim (code/severity/message) so the
// application never has to reinterpret why the library refused an edit.
std::string FormatLibraryDiagnostics(const std::vector<sacm_adapter::LoadDiagnostic>& diagnostics) {
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

std::string LibraryRejection(const std::string& seam,
                             const std::vector<sacm_adapter::LoadDiagnostic>& diagnostics) {
    return "The SACM library rejected " + seam + ": " + FormatLibraryDiagnostics(diagnostics);
}

} // namespace

std::string NewElementKindToToken(NewElementKind kind) {
    switch (kind) {
    case NewElementKind::Goal:           return "Goal";
    case NewElementKind::Strategy:       return "Strategy";
    case NewElementKind::Solution:       return "Solution";
    case NewElementKind::Context:        return "Context";
    case NewElementKind::Assumption:     return "Assumption";
    case NewElementKind::Justification:  return "Justification";
    }
    return "Goal";
}

bool NewElementKindFromToken(const std::string& token, NewElementKind& out) {
    if (token == "Goal")          { out = NewElementKind::Goal; return true; }
    if (token == "Strategy")      { out = NewElementKind::Strategy; return true; }
    if (token == "Solution")      { out = NewElementKind::Solution; return true; }
    if (token == "Context")       { out = NewElementKind::Context; return true; }
    if (token == "Assumption")    { out = NewElementKind::Assumption; return true; }
    if (token == "Justification") { out = NewElementKind::Justification; return true; }
    return false;
}

std::string RemoveModeToToken(RemoveMode mode) {
    switch (mode) {
    case RemoveMode::NodeOnly:           return "NodeOnly";
    case RemoveMode::NodeAndDescendants: return "NodeAndDescendants";
    }
    return "NodeOnly";
}

bool RemoveModeFromToken(const std::string& token, RemoveMode& out) {
    if (token == "NodeOnly")           { out = RemoveMode::NodeOnly; return true; }
    if (token == "NodeAndDescendants") { out = RemoveMode::NodeAndDescendants; return true; }
    return false;
}

std::string ChallengeSourceTypeToToken(ChallengeSourceType type) {
    switch (type) {
    case ChallengeSourceType::CounterArgument: return "CounterArgument";
    case ChallengeSourceType::CounterEvidence: return "CounterEvidence";
    }
    return "CounterArgument";
}

bool ChallengeSourceTypeFromToken(const std::string& token, ChallengeSourceType& out) {
    if (token == "CounterArgument") { out = ChallengeSourceType::CounterArgument; return true; }
    if (token == "CounterEvidence") { out = ChallengeSourceType::CounterEvidence; return true; }
    return false;
}

std::string ArgumentTargetKindToToken(ArgumentTarget::Kind kind) {
    switch (kind) {
    case ArgumentTarget::Kind::Element:      return "Element";
    case ArgumentTarget::Kind::Relationship: return "Relationship";
    }
    return "Element";
}

bool ArgumentTargetKindFromToken(const std::string& token, ArgumentTarget::Kind& out) {
    if (token == "Element")      { out = ArgumentTarget::Kind::Element; return true; }
    if (token == "Relationship") { out = ArgumentTarget::Kind::Relationship; return true; }
    return false;
}

bool CreateTopGoalCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    bool applied_to_library = false;
    if (ctx.library_document != nullptr && ctx.allow_library_primary) {
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
    if (!applied_to_library && !core::AddTopGoal(ctx.model, &ctx.package, generated_id_, out_error))
        return false;

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
    if (ctx.library_document != nullptr && ctx.allow_library_primary) {
        std::string planned_element_id;
        std::string planned_relationship_id;
        if (!core::PlanChildElementIds(ctx.model, &ctx.package, parent_id_, kind_, planned_element_id,
                                       planned_relationship_id, out_error))
            return false;
        const sacm_adapter::AddChildOutcome outcome =
            sacm_adapter::apply_add_child(*ctx.library_document, parent_id_, ToAdapterChildKind(kind_),
                                          planned_element_id, planned_relationship_id);
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
    if (!applied_to_library &&
        !core::AddChildElement(ctx.model, &ctx.package, parent_id_, kind_, generated_id_,
                               generated_relationship_id_, out_error))
        return false;

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
    if (ctx.library_document != nullptr && ctx.allow_library_primary) {
        std::string planned_element_id;
        std::string planned_relationship_id;
        if (!core::PlanChallengeIds(ctx.model, &ctx.package, target_, source_type_, planned_element_id,
                                    planned_relationship_id, out_error))
            return false;
        // The seam resolves an element vs relationship target itself (the
        // library gives every contained element a parent), so `target_.kind` is
        // only used above, for the anchor the legacy id prefix is scoped to.
        const sacm_adapter::AddChildOutcome outcome = sacm_adapter::apply_challenge(
            *ctx.library_document, target_.id, ToAdapterChallengeSource(source_type_),
            planned_element_id, planned_relationship_id);
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
    if (!applied_to_library &&
        !core::AddChallenge(ctx.model, &ctx.package, target_, source_type_, generated_id_,
                            generated_relationship_id_, out_error))
        return false;

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
    // reproduces the legacy removal for every shape, including removing one
    // sub-goal of a strategy whose single inference has several sources (the
    // inference survives, scrubbed to the rest, rather than cascading away). The
    // only remaining gate is the app's hotfix kill switch.
    bool applied_to_library = false;
    if (ctx.library_document != nullptr && ctx.allow_library_primary) {
        // Exactly the ids `PlanRemoval` produced -- the same set the audit event
        // records, walked in the same sorted order `ApplyEventToLibrary` replays,
        // so the live document and the replayed document agree by construction.
        for (const std::string& id : deleted_ids) {
            const sacm_adapter::DeleteOutcome outcome =
                sacm_adapter::apply_delete_element(*ctx.library_document, id);
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
    if (!applied_to_library && !core::RemoveElement(ctx.model, &ctx.package, element_id_, mode_, out_error))
        return false;

    removed_count_ = deleted_ids.size();

    out_event.event_type = "RemoveElement";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["element_id"] = element_id_;
    out_event.payload["mode"] = RemoveModeToToken(mode_);
    out_event.payload["deleted_ids"] = deleted_ids;
    return true;
}

bool UpdateElementTextCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event,
                                     std::string& out_error) {
    if (element_id_.empty()) {
        out_error = "UpdateElementTextCommand requires an element id";
        return false;
    }
    if (language_.empty()) {
        out_error = "UpdateElementTextCommand requires a language code";
        return false;
    }
    if (!core::SetElementTextField(ctx.model, &ctx.package, element_id_, field_, language_,
                                   new_value_, old_value_, out_error))
        return false;

    was_no_op_ = (old_value_ == new_value_);

    // Phase 9 Stage 5: route the same edit through the library-owned document.
    // The legacy models above stay authoritative (save/hash/replay); this only
    // keeps the library document current. If the mapping does not yet cover
    // this (field, kind), leave `library_synced` false so the bus re-derives.
    //
    // Skip a no-op edit: SetElementTextField leaves the legacy model unchanged
    // for old == new, but a library apply could still rewrite the stored
    // language tag, and marking it synced would suppress the bus re-derive.
    if (ctx.library_document != nullptr && !was_no_op_) {
        const sacm_adapter::EditOutcome edit = sacm_adapter::apply_text_edit(
            *ctx.library_document, element_id_, ToAdapterTextField(field_), language_, new_value_);
        ctx.library_synced = edit.supported && edit.applied;
    }

    out_event.event_type = "UpdateElementText";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["element_id"] = element_id_;
    out_event.payload["field"] = core::ElementTextFieldToToken(field_);
    out_event.payload["language"] = language_;
    out_event.payload["old_value"] = old_value_;
    out_event.payload["new_value"] = new_value_;
    return true;
}

} // namespace core::commands
