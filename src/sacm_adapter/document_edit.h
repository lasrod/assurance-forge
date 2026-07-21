#pragma once

// Phase 9 Stage 5: the edit seam onto the library-owned document.
//
// Stage 4 made the library the *load* source of truth: `AppState` retains a
// `LibraryDocument` and projects `loaded_case` from it. Editing still runs on
// the legacy models (`parser::AssuranceCase` + `sacm::AssuranceCasePackage`)
// through `core::element_factory`, which the audit log serializes and hashes
// and the replayer re-applies. Flipping all of that at once is Stage 6.
//
// This seam is the incremental bridge: one library mutation Operation at a
// time, each proven to reproduce the corresponding legacy edit before anything
// depends on it. `apply_text_edit` translates an app text edit into a
// `sacm::commands::Operation`, applies it to the opaque `LibraryDocument`, and
// flattens the result to strings so no library type crosses into `core`.
//
// Only (field, element-kind) combinations whose library mapping is proven
// equivalent to `core::SetElementTextField` are wired; the rest report
// `supported == false` rather than being approximated. The mapping mirrors the
// projection (src/sacm_adapter/case_projection.cpp):
//
//   * Name    -> SetName (clause 8.6 single name LangString, primary language).
//   * Content -> SetDescription on a Claim/ArgumentReasoning: the app's
//     `content` is the element's primary Description (clause 8.9), which the
//     library's SetDescription edits.
//
// Deliberately not yet mapped (later slices, some needing new library
// operations): a Claim's secondary-note Description, Term/Expression `value`,
// and multi-language names/Descriptions that exceed a single LangString.

#include "sacm_adapter/library_load.h"  // LibraryDocument, LoadDiagnostic

#include <string>
#include <vector>

namespace sacm_adapter {

// The app text field being edited, mirroring core::ElementTextField without
// depending on `core` (this layer sits below it). The caller maps between the
// two.
enum class TextField {
    Name,
    Content,
    Description,
};

// Result of applying one text edit to a LibraryDocument.
//   * supported == false: this (field, element-kind) has no library mapping
//     yet, so nothing was attempted and the document is unchanged. Not an
//     error -- the caller keeps the legacy edit authoritative and leaves the
//     library document untouched for this field until a later slice wires it.
//   * supported == true, applied == false: the library rejected the operation;
//     the document is unchanged and `diagnostics` explain why.
//   * supported == true, applied == true: the edit was applied.
struct EditOutcome {
    bool supported = true;
    bool applied = false;
    std::vector<LoadDiagnostic> diagnostics;
};

// Applies a text edit mirroring
// `core::SetElementTextField(field, language, value)`. `language` is typically
// "en"; SACM's name is a single LangString (clause 8.6), so a non-primary
// language overwrites the stored name rather than accumulating a map the way
// the legacy POD does -- callers editing secondary-language names must not rely
// on this until that impedance is handled in a later slice.
EditOutcome apply_text_edit(LibraryDocument& document, const std::string& element_id,
                            TextField field, const std::string& language, const std::string& value);

// The kind of child element to add, mirroring core::NewElementKind. Each maps
// to a new element plus an asserted relationship linking it to the parent.
enum class ChildKind {
    Goal,          // Claim   <- AssertedInference
    Strategy,      // not wired: bare strategy inference has no source (see below)
    Solution,      // ArtifactReference <- AssertedEvidence
    Context,       // ArtifactReference <- AssertedContext
    Assumption,    // Claim (assumed)   <- AssertedContext
    Justification, // not wired: see below
};

// Result of adding a child element through the library. Reports the ids the
// library generated (they will not match the legacy id generator, so callers
// comparing against a legacy edit must compare structure, not ids).
struct AddChildOutcome {
    bool supported = true;
    bool applied = false;
    std::string new_element_id;
    std::string new_relationship_id;
    std::vector<LoadDiagnostic> diagnostics;
};

// Adds a child element under `parent_id`, mirroring `core::AddChildElement`:
// creates the element in the parent's owning ArgumentPackage and an asserted
// relationship whose target is the parent (conclusion) and whose source is the
// new element (premise) -- except a Strategy, whose ArgumentReasoning attaches
// through the inference's `reasoning` end rather than a source.
//
// Two kinds report `supported == false` for now:
//   * Strategy -- its AssertedInference is created before any sub-goal exists,
//     so it would have no source, which SACM's source [1..*] forbids. The
//     legacy app produced that invalid transient state; representing a bare
//     strategy in valid SACM is an open decision.
//   * Justification -- the standards-correct GSN mapping is
//     `assertionDeclaration = axiomatic` (docs/sacm/sacm-gsn-mapping.md), which
//     deliberately diverges from the legacy app's non-standard "justification"
//     literal, so it is not a like-for-like reproduction.
// The parent must be a claim-like container in an ArgumentPackage; otherwise
// the outcome reports unsupported.
// When `element_id`/`relationship_id` are non-empty they are used verbatim
// instead of library-generated ids (the library's create operations accept a
// caller-supplied id). This makes the operation id-deterministic, which a
// library-primary audit replay needs to reproduce exact ids -- a Phase 9 Stage
// 7 prerequisite. Empty means the library generates the id.
AddChildOutcome apply_add_child(LibraryDocument& document, const std::string& parent_id,
                                ChildKind kind, const std::string& element_id = {},
                                const std::string& relationship_id = {});

// The source of a dialectic challenge, mirroring core::ChallengeSourceType.
enum class ChallengeSource {
    CounterArgument, // Claim             <- AssertedInference (isCounter)
    CounterEvidence, // ArtifactReference <- AssertedEvidence   (isCounter)
};

// Adds a dialectic challenge against `target_id`, mirroring
// `core::AddChallenge`: creates a counter element in the target's owning
// ArgumentPackage and a counter relationship (`isCounter = true`) whose source
// is the counter element and whose target is the challenged element. The target
// may itself be a relationship (challenging an inference) -- SACM allows it
// because an AssertedRelationship is a SACMElement. Reuses AddChildOutcome: the
// result is likewise a new element plus a new relationship.
// `element_id`/`relationship_id`: see apply_add_child -- non-empty ids are used
// verbatim for id-deterministic replay.
AddChildOutcome apply_challenge(LibraryDocument& document, const std::string& target_id,
                                ChallengeSource source, const std::string& element_id = {},
                                const std::string& relationship_id = {});

// Result of adding an Assurance Claim Point. `acp_id` is the id the seam
// generated (deterministic `ACP<n>`, matching core's NextAcpId), empty unless
// applied.
struct AcpOutcome {
    bool supported = true;
    bool applied = false;
    std::string acp_id;
    std::vector<LoadDiagnostic> diagnostics;
};

// Adds an Assurance Claim Point to `target_id`, mirroring `core::acp::AddAcp`:
// generates the next `ACP<n>` id and writes the vendor TaggedValues the
// projection reads back (`assuranceForge.acp` marker + `.name` + a
// `.resolutionKind = none`). ACP is a vendor extension (clause 8.12), not a
// SACM concept, so this is `AddTaggedValue` under the hood.
//
// Scoped to *element* ACPs on an `ArtifactReference` (Solution/Context) -- the
// only element kind `core::acp::ElementEligibleForAcp` accepts. Claims and
// relationships report `supported == false`: an ACP on a goal attaches through
// its supporting relationship, and relationship-ACP eligibility needs the
// wider `RelationshipEligibleForAcp` rules, both later slices.
AcpOutcome apply_add_acp(LibraryDocument& document, const std::string& target_id);

// Result of deleting an element.
struct DeleteOutcome {
    bool supported = true;
    bool applied = false;
    std::vector<LoadDiagnostic> diagnostics;
};

// Deletes a single element and the relationships that reference it, the
// primitive `core::RemoveElement` composes. Uses the library's
// DeleteReferencingRelationships policy so no relationship is left dangling --
// matching the legacy helper, which drops relationships that become empty.
//
// This is the single-element building block. For a *leaf* it reproduces
// `RemoveElement` in either mode (they coincide when there are no children).
// Cascading a whole subtree (composing this over the removal plan) and the
// NodeOnly *reparent* case (which needs a relationship-retarget operation the
// library does not yet expose) are later slices; see
// docs/sacm/sacm-gsn-metamodel-gaps.md.
DeleteOutcome apply_delete_element(LibraryDocument& document, const std::string& element_id);

} // namespace sacm_adapter
