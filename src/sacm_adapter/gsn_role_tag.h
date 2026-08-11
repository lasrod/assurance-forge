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

// Reserved compat TaggedValue the *library* writes on import when it normalizes a
// legacy, non-standard `assertionDeclaration="justification"` to `axiomatic`
// (libs/sacm/src/io/xmi_reader.cpp). Its content is the original literal
// ("justification"). This lets the projection recognize a GSN Justification that
// came from an old file that predates the `assuranceForge.gsn.role` encoding. The
// key string must stay in sync with the reader; the justification round-trip test
// guards it.
inline constexpr const char* kImportAssertionDeclarationKey = "sacm.import.assertionDeclaration";
inline constexpr const char* kImportAssertionDeclarationJustification = "justification";

// Reserved compat TaggedValue holding an element's names in every language BUT
// the primary. SACM gives an element one name LangString (clause 8.6), so the
// library's reader parks the surplus here on import
// (libs/sacm/src/io/xmi_reader.cpp) and the projection merges it back into
// `name_langs`. The edit seam now WRITES it too, through `SetTaggedValue`, so a
// translated name goes where the reader will find it. The key string must stay
// in sync with the reader; the translated-name round-trip test guards it.
inline constexpr const char* kImportNameOverflowTagKey = "sacm.import.name";

// A GSN Strategy is added under a goal before it has any sub-goals, but SACM's
// AssertedInference requires source [1..*] (clause 11.13), so a bare strategy
// inference is invalid. The strategy is created as a standalone ArgumentReasoning
// carrying this tag (the id of the goal it will support); the inference is
// materialized only when the first sub-goal gives it a source (app-defer).
inline constexpr const char* kGsnStrategyTargetTagKey = "assuranceForge.gsn.strategyTarget";

} // namespace sacm_adapter
