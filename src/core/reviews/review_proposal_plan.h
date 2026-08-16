#pragma once

#include "core/element_factory.h"
#include "core/sacm_model.h"
#include "sacm_adapter/document_edit.h"

#include <string>
#include <vector>

namespace core::reviews {

// The structural change a proposal makes, found by running
// `ReviewProposalPatchService` on a scratch projection and diffing it.
//
// It is a PLAN rather than an application, for the reason `MoveSubtreePlan`
// gives: nothing is written until the caller has seen the whole of it. A
// proposal is many writes, and the library can refuse one of them (a
// relationship whose ends violate clause 11.13 multiplicity, a text write on a
// kind that has nowhere to put it). A caller that had already applied half the
// plan would be left with an argument in a state no operation produced and no
// way back, so `unrepresentable_reason` reports that up front and the caller
// declines to the guarded bridge with the document untouched.
//
// The patch service stays the single authority on what a proposal MEANS -- which
// element kind an operation creates, what a removal cascades to, how a
// `create_ref` resolves. This plan only mirrors what it decided; it never
// re-decides it.
struct ProposalPlan {
    // An element the proposal introduced. The patch service creates a bare
    // element and attaches it in a separate operation, so this carries no
    // relationship -- see `created_relationships`.
    struct CreatedElement {
        std::string id;
        std::string package_id; // the ArgumentPackage it must be created in
        sacm_adapter::NewElementKind kind = sacm_adapter::NewElementKind::Claim;
        std::string name;
        std::string text;
        std::string language;
        // Set only for a strategy attached before it has a sub-goal: the element
        // it will support, deferred to a vendor tag rather than an inference the
        // library would refuse. See `CreateElementFields::strategy_target`.
        std::string strategy_target;
    };

    struct Relationship {
        std::string id;
        std::string type; // parser token: assertedinference / assertedcontext / assertedevidence
        std::vector<std::string> sources;
        std::vector<std::string> targets;
        std::string reasoning;
        bool is_counter = false;
    };

    // One text field, in one language. A translated edit is several of these,
    // because the seam writes one language at a time.
    struct TextWrite {
        std::string element_id;
        ElementTextField field = ElementTextField::Name;
        std::string language;
        std::string value;
    };

    struct FlagWrite {
        std::string element_id;
        bool undeveloped = false;
    };

    // A glossary term the proposal introduced. Terms live in a
    // TerminologyPackage, not an ArgumentPackage, so they carry their own
    // package id -- empty when the case has none yet, in which case the
    // applier creates the case's first TerminologyPackage and puts the term
    // there. Definition translations arrive as ordinary Description
    // `text_writes` after the term exists, like a created claim's do.
    struct CreatedTerm {
        std::string id;
        std::string package_id; // the TerminologyPackage; empty = create one
        std::string value;
        std::string name;
        std::string definition;
    };

    std::vector<CreatedElement> created;
    std::vector<Relationship> created_relationships;
    std::vector<CreatedTerm> created_terms;
    std::vector<TextWrite> text_writes;
    std::vector<FlagWrite> flag_writes;
    std::vector<std::string> deleted_ids;

    // Non-empty when a seam cannot express some part of this proposal. The
    // caller must not apply any of the plan in that case.
    std::string unrepresentable_reason;
};

// Diffs `before` against `after` (the same model with the patch service applied)
// and reports what to write. `after` must come from the patch service, not be
// hand-built: the point is to mirror what it decided, never to re-decide it.
//
// `package_for_created` names the ArgumentPackage a created element belongs in.
// The POD model is flat and records no package, so the caller resolves it from
// the document; an empty value with any created element makes the plan
// unrepresentable rather than guessing.
//
// `terminology_package_for_created` names the TerminologyPackage a created term
// belongs in. Unlike the argument package, empty is NOT unrepresentable: a case
// that has never had a glossary has no package to resolve, and the applier
// creates one -- refusing would make the first term impossible to add over MCP.
ProposalPlan PlanProposalFromDiff(const AssuranceCase& before,
                                  const AssuranceCase& after,
                                  const std::string& package_for_created,
                                  const std::string& terminology_package_for_created);

// Writes `plan` to `document` through the sacm_adapter seams, in an order that
// keeps the document loadable at every step: elements, then the relationships
// that reference them, then field writes, then deletions.
//
// A failure part-way through is a HARD failure, not a fallback. Every reason to
// decline is reported by the planner before the first write.
bool ApplyProposalPlanToLibrary(sacm_adapter::LibraryDocument& document,
                                const ProposalPlan& plan,
                                std::string& out_error);

} // namespace core::reviews
