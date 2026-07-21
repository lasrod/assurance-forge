#pragma once

// The vendor TaggedValue that preserves a GSN node role SACM's
// AssertionDeclaration cannot express on its own (clause 8.12 extension). A GSN
// Justification maps to a Claim with assertionDeclaration = axiomatic, which is
// indistinguishable from an axiomatically-asserted Goal in pure SACM; this tag
// carries the GSN role so the projection can render it correctly and a save
// round-trips it. Written by the edit seam (document_edit.cpp) and read back by
// the projection (case_projection.cpp).

namespace sacm_adapter {

inline constexpr const char* kGsnRoleTagKey = "assuranceForge.gsn.role";
inline constexpr const char* kGsnRoleJustification = "Justification";

// A GSN Strategy is added under a goal before it has any sub-goals, but SACM's
// AssertedInference requires source [1..*] (clause 11.13), so a bare strategy
// inference is invalid. The strategy is created as a standalone ArgumentReasoning
// carrying this tag (the id of the goal it will support); the inference is
// materialized only when the first sub-goal gives it a source (app-defer).
inline constexpr const char* kGsnStrategyTargetTagKey = "assuranceForge.gsn.strategyTarget";

} // namespace sacm_adapter
