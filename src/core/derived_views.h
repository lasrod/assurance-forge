#pragma once

// Terminology-display passes over the derived render views (Phase 9 Stage 7).
//
// The POD `parser::AssuranceCase` the UI draws is a projection of the
// library-owned document; after projecting it, these passes adjust how
// terminology renders -- a term shows as inline underlined clickable text rather
// than a drawn context node. Extracted here (from app_state) so both the load
// path and the upcoming library-primary edit path apply the same passes.

#include "parser/xml_parser.h"
#include "legacy_sacm/sacm_model.h"

namespace sacm_adapter {
class LibraryDocument;
}

namespace core {

// Phase 2 slice 2b-1: rebuild BOTH legacy views from the library-owned
// document, reproducing exactly what `AppState::load_file` does after a library
// load -- the tag-carrying package projection, the POD case projection, and the
// three render passes below in that order.
//
// This is the inverse of the Stage 5 net (which re-derived the library from the
// authoritative package). A command that mutated the library natively sets
// `CommandContext::library_primary`, and the command bus calls this so
// `loaded_case` / `sacm_package` become DERIVED views of the library rather
// than independently-mutated state.
//
// The pass order is load-bearing and must stay in step with `load_file`: a
// missing `SynthesizeBareStrategyPlacements` leaves a bare strategy floating
// with no placement, and a missing terminology pass renders terms as drawn
// context nodes instead of inline chips.
void RebuildDerivedViewsFromLibrary(const sacm_adapter::LibraryDocument& document,
                                    parser::AssuranceCase& out_model,
                                    sacm::AssuranceCasePackage& out_package);

// Remove terminology artifact references (and the contexts sourcing them) that
// are not a *visible* terminology context from the render model, so a term shows
// as inline underlined clickable text rather than a drawn context node.
void HideTerminologyArtifactReferences(parser::AssuranceCase& model, const sacm::AssuranceCasePackage& package);

// Repoint each visible terminology artifact reference's display fields at its
// term, so the inline term chip renders the term's label and definition.
void RefreshVisibleTerminologyContextDisplay(parser::AssuranceCase& model, const sacm::AssuranceCasePackage& package);

// A GSN strategy is created as an ArgumentReasoning carrying a `strategyTarget`
// tag (the goal it will support) with no inference until its first sub-goal
// materializes one (the deferred single-inference encoding). Such a *bare*
// strategy has no relationship to place it under its goal, so synthesize a
// sourceless placeholder AssertedInference `{reasoning=strategy, target=goal}`
// into the render model -- the POD tree tolerates a sourceless inference and
// renders the strategy under its goal. This is a **render-only** step applied to
// the render model, NOT the projection (which feeds the saved package + audit
// hash), so the placeholder is never serialized. Once a real inference
// (reasoning=strategy) exists, none is synthesized.
void SynthesizeBareStrategyPlacements(parser::AssuranceCase& model, const sacm::AssuranceCasePackage& package);

// Same pass scoped to ONE ArgumentPackage, for a render view that was projected
// down to a single package (`core::BuildArgumentPackageProjection`, which feeds
// the workbench's per-package canvas tabs). The placeholder is render-only and
// therefore absent from the package, so a package projection filters it out and
// the bare strategy loses its placement -- it has to be re-synthesized on the
// projected view. Both overloads are idempotent: an existing placeholder counts
// as a placement, so re-running adds nothing.
void SynthesizeBareStrategyPlacements(parser::AssuranceCase& model, const sacm::ArgumentPackage& argument_package);

} // namespace core
