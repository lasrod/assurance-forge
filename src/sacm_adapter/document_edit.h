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
//     `content` is the element's primary Description (clause 8.9). A primary-
//     language edit overwrites the front Description's stored entry in place; a
//     non-primary edit adds/updates that language's LangString.
//   * Description -> SetDescription on a *non*-claim ModelElement (artifact
//     reference, relationship, ...), whose POD `description` IS the front
//     Description; same language handling as Content.
//
// Deliberately not yet mapped (later slices, some needing new library
// operations): a multi-language *name* (SACM's name is one LangString, so extra
// languages need the reserved "sacm.import.name" TaggedValue), a Claim's
// secondary-note Description (the POD `description` on a claim is a *second*
// Description, which SetDescription's front-only edit cannot target), and
// Term/Expression `value`.

#include "sacm_adapter/library_load.h" // LibraryDocument, LoadDiagnostic

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
EditOutcome apply_text_edit(LibraryDocument& document,
                            const std::string& element_id,
                            TextField field,
                            const std::string& language,
                            const std::string& value);

// The kind of child element to add, mirroring core::NewElementKind. Each maps
// to a new element plus an asserted relationship linking it to the parent.
enum class ChildKind {
    Goal,          // Claim   <- AssertedInference
    Strategy,      // ArgumentReasoning + strategyTarget tag (inference deferred)
    Solution,      // ArtifactReference <- AssertedEvidence
    Context,       // ArtifactReference <- AssertedContext
    Assumption,    // Claim (assumed)      <- AssertedContext
    Justification, // Claim (axiomatic + gsn.role tag) <- AssertedContext
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
// Strategy is added before any sub-goal exists (a bare strategy inference would
// have no source, which SACM's source [1..*] forbids), so this creates only the
// ArgumentReasoning plus a `strategyTarget` vendor tag naming the goal it will
// support; the inference is materialized when the first sub-goal is added (that
// materialization, and extending an inference's sources for later sub-goals, is
// a following increment). Justification maps to a Claim with axiomatic + a
// gsn.role tag (see gsn_role_tag.h). The parent must be a claim-like container
// in an ArgumentPackage; otherwise the outcome reports unsupported.
// When `element_id`/`relationship_id` are non-empty they are used verbatim
// instead of library-generated ids (the library's create operations accept a
// caller-supplied id). This makes the operation id-deterministic, which a
// library-primary audit replay needs to reproduce exact ids -- a Phase 9 Stage
// 7 prerequisite. Empty means the library generates the id. `relationship_id`
// applies only when a relationship is created; a Strategy creates none yet (its
// inference is deferred to the first sub-goal), so it is ignored for
// `ChildKind::Strategy`.
AddChildOutcome apply_add_child(LibraryDocument& document,
                                const std::string& parent_id,
                                ChildKind kind,
                                const std::string& element_id = {},
                                const std::string& relationship_id = {});

// Adds a top goal, mirroring `core::AddTopGoal`: creates a Claim with no parent
// relationship in the document's first ArgumentPackage (mirroring
// `core::FindOwningArgumentPackage`'s fallback for an element with no owning
// relationship). Reuses AddChildOutcome; `new_relationship_id` is always empty
// (a top goal has no incoming relationship). When `element_id` is non-empty it
// is used verbatim (id-deterministic replay); empty means the library generates
// it. Reports `supported == false` only if the document has no ArgumentPackage.
AddChildOutcome apply_add_top_goal(LibraryDocument& document, const std::string& element_id = {});

// The shapes a bare element create can take. A proposal builds an element FIRST
// and attaches it in a separate operation, so unlike `apply_add_child` this
// creates no relationship. Solution and Context are both ArtifactReference --
// they differ only in the relationship that later attaches them, which is a
// separate operation and therefore not this seam's business.
enum class NewElementKind {
    Claim,             // Claim
    Assumption,        // Claim, assertionDeclaration = assumed
    Justification,     // Claim, axiomatic + the gsn.role vendor tag
    ArgumentReasoning, // ArgumentReasoning (a GSN Strategy)
    ArtifactReference, // ArtifactReference (a GSN Solution or Context)
};

struct CreateElementFields {
    std::string element_id; // used verbatim when non-empty; empty lets the library generate
    std::string name;
    std::string text;     // the Claim's Description (clause 8.9); ignored by kinds with none
    std::string language; // language tag for name/text; may be empty
    // For an ArgumentReasoning only: the id of the element this strategy will
    // support once it has a sub-goal. Recorded as the `strategyTarget` vendor tag
    // instead of an AssertedInference, because a bare strategy's inference would
    // have no source and SACM's source [1..*] forbids that (clause 11.13). This is
    // the same deferral `apply_add_child` performs for a new Strategy; the
    // inference materializes when the first sub-goal gives it a source.
    std::string strategy_target;
};

// Creates ONE element inside `package_id`, with no relationship, mirroring what
// `ReviewProposalPatchService` puts in the model for a Create* operation. The
// assertion declaration and the GSN role tag follow the same mapping
// `apply_add_child` uses, so an element created either way projects identically.
//
// `package_id` must name an ArgumentPackage; the caller chooses it, because a
// flat POD model does not record which package an element belongs to and
// guessing would put a proposed claim in the wrong package of a multi-package
// case. Reports `supported == false` when it does not resolve.
AddChildOutcome apply_create_element(LibraryDocument& document,
                                     const std::string& package_id,
                                     NewElementKind kind,
                                     const CreateElementFields& fields);

// The id of the ArgumentPackage that owns `element_id`, or of the document's
// first ArgumentPackage when `element_id` is empty or does not resolve. Empty
// when the document has no ArgumentPackage at all.
//
// A read rather than an edit, exposed because the flat POD model records no
// package: a caller mirroring POD changes into the document has no other way to
// say WHERE a new element goes, and picking the first package unconditionally
// would put a proposed claim in the wrong package of a multi-package case.
std::string resolve_argument_package_id(const LibraryDocument& document, const std::string& element_id);

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
AddChildOutcome apply_challenge(LibraryDocument& document,
                                const std::string& target_id,
                                ChallengeSource source,
                                const std::string& element_id = {},
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
//
// When `requested_acp_id` is non-empty it is the `ACP<n>` id used verbatim (so an
// audited/replayed ACP add reproduces the id the legacy generator recorded);
// empty generates the next free one.
AcpOutcome
apply_add_acp(LibraryDocument& document, const std::string& target_id, const std::string& requested_acp_id = {});

// One Assurance Claim Point, as the tag set it is stored as. Mirrors the fields
// `core::acp::UpsertAcpTags` writes; empty fields are omitted exactly as it omits
// them, so the projection reads back what it wrote.
struct AcpTagFields {
    std::string id;
    std::string name;
    std::string resolution_kind; // "none" | "text" | "topGoalReference"
    std::string text;
    std::string confidence_claim_id;
    std::string argument_package_id;
    std::string top_goal_id;
};

// Replace the tag set for `fields.id` on `element_id`, mirroring
// `core::acp::UpsertAcpTags`: every `assuranceForge.acp` tag belonging to this
// ACP is dropped, then the new set is written. An ACP is a vendor extension
// (clause 8.12), not a SACM concept, so this is tags all the way down -- and
// because the library has `AddTaggedValue` and no update operation, "replace" is
// composed here the same way `apply_set_gsn_identifier` composes it.
//
// Knows nothing about which targets may carry an ACP. `core::acp`'s eligibility
// rules are Assurance Forge's, not SACM's, and stay with the command.
EditOutcome apply_upsert_acp_tags(LibraryDocument& document, const std::string& element_id, const AcpTagFields& fields);

// Drop every tag belonging to `acp_id` from `element_id`, mirroring
// `core::acp::RemoveAcpTags`. Reports `applied == false` with no diagnostics when
// the element carries none -- nothing to do is not a failure.
EditOutcome apply_remove_acp_tags(LibraryDocument& document, const std::string& element_id, const std::string& acp_id);

// Replace an Assertion's clause-11.6 meta-claims. An ACP on a RELATIONSHIP is
// carried partly by `metaClaim` and not only by vendor tags, so retracting or
// re-resolving one has to rewrite this list; `SetMetaClaims` is the library
// operation added for it. Every id must resolve to a Claim.
EditOutcome apply_set_meta_claims(LibraryDocument& document,
                                  const std::string& element_id,
                                  const std::vector<std::string>& meta_claim_ids);

// Create the ArgumentPackage and top goal a confidence argument tree consists
// of, mirroring the compound half of
// `core::acp::CreateConfidenceArgumentTreeForAcpWithIds`: a package carrying the
// `assuranceForge.argumentPackage.purpose = confidence` tag, holding one Claim
// with the given name and content. The caller then points the ACP at it with
// `apply_upsert_acp_tags`.
//
// Both ids are supplied rather than generated -- they are the compound
// operation's only non-deterministic outputs, and the audit payload records
// them, so the command plans them with the same generators the legacy path used.
AcpOutcome apply_create_confidence_argument_package(LibraryDocument& document,
                                                    const std::string& package_id,
                                                    const std::string& package_name,
                                                    const std::string& top_goal_id,
                                                    const std::string& top_goal_name,
                                                    const std::string& top_goal_content);

// Result of deleting an element.
struct DeleteOutcome {
    bool supported = true;
    bool applied = false;
    // The refusal was specifically "no element with that id". Distinguished
    // because for some callers absence is the postcondition they were after: a
    // proposal's deletion list is a before/after diff, and an earlier delete's
    // reference scrub can legitimately have taken a listed relationship with it.
    bool target_missing = false;
    std::vector<LoadDiagnostic> diagnostics;
};

// One consequence of a delete, flattened to strings for the UI.
//
// `deleted == false` means the element SURVIVES but is changed -- a
// relationship scrubbed of a deleted endpoint that still has a source and a
// target. That distinction is the point of showing a preview at all: "your
// strategy's inference loses one of its three sub-goals" and "your strategy's
// inference disappears" are different outcomes and the user has to be able to
// tell them apart before confirming.
struct DeleteEffect {
    std::string element_id;
    std::string kind; // SACM class name ("Claim", "AssertedInference", ...)
    std::string name; // display name; empty when the element has none
    bool is_relationship = false;
    bool deleted = true;
};

// What deleting a set of elements would do. `requested` are the effects on the
// elements the caller named; `consequential` are everything else the delete
// drags with it -- the part a user cannot predict and the reason this exists.
struct DeletePreview {
    bool supported = true;
    // False when the library would refuse the delete outright; `diagnostics`
    // say why. Advisory: the legacy removal path is still authoritative for
    // applying, so a caller shows this rather than blocking on it.
    bool can_apply = false;
    std::vector<DeleteEffect> requested;
    std::vector<DeleteEffect> consequential;
    std::vector<LoadDiagnostic> diagnostics;
};

// Computes what deleting `element_ids` would do, leaving `document` untouched
// (SACM23-INT-002).
//
// The deletes are modelled on a scratch copy of the document rather than
// element-by-element against the live one, because the consequences of deleting
// a SET are not the union of the consequences of deleting each member: under
// ScrubReferences an inference with three sources survives losing any one of
// them but dies when all three go. Previewing individually would understate the
// damage, which is the one direction a delete confirmation must never err in.
//
// Uses the same policy as `apply_delete_element` (ScrubReferences), so the
// preview describes the operation the caller will actually perform. Ids that do
// not resolve are skipped rather than reported: the caller's removal plan is
// computed over the app's GSN projection, which legitimately contains ids with
// no library counterpart.
DeletePreview preview_delete_elements(const LibraryDocument& document, const std::vector<std::string>& element_ids);

// Deletes a single element and scrubs it out of the relationships that
// reference it, the primitive `core::RemoveElement` composes. Uses the
// library's ScrubReferences policy: a referencing relationship is scrubbed of
// the deleted element and dropped only once that leaves it structurally invalid
// under SACM multiplicity -- matching the legacy helper's scrub-then-drop. It
// is NOT DeleteReferencingRelationships, which would cascade away a strategy's
// shared inference when one of several sub-goals is removed.
//
// This is the single-element building block. For a *leaf* it reproduces
// `RemoveElement` in either mode (they coincide when there are no children).
// Cascading a whole subtree (composing this over the removal plan) and the
// NodeOnly *reparent* case (which needs a relationship-retarget operation the
// library does not yet expose) are later slices; see
// docs/sacm/sacm-gsn-metamodel-gaps.md.
DeleteOutcome apply_delete_element(LibraryDocument& document, const std::string& element_id);

// Deletes an entire package (ArgumentPackage, ArtifactPackage, or
// TerminologyPackage) and everything it contains, plus the relationships that
// referenced the removed elements. Routes through the library's `DeleteElement`
// with `PackageDeletePolicy::DeleteRecursively` and the reference/cross-package
// policies that drop referencing relationships rather than reject. One seam
// covers all three package kinds because the library keys deletion on the
// package's `ElementId`.
//
// This is a SACM-clean recursive delete, which matches legacy
// `core::DeleteArgumentPackage` exactly but is intentionally *cleaner* than the
// other two legacy helpers: `core::DeleteArtifactPackage` is package-only and
// leaves referencing evidence relationships dangling (this seam drops them), and
// `core::DeleteTerminologyPackage` rejects a non-empty package (this seam deletes
// it recursively). Those divergences are pinned by the tests and are a flip-slice
// migration concern -- see Phase 1b.
DeleteOutcome apply_delete_package(LibraryDocument& document, const std::string& package_id);

// Sets an element's clause-8.2 gid, mirroring `core::SetElementGid`. One library
// operation, no composition: the app generates the value (a random UUID for an
// element, `gid-<id>` for terminology) and this writes it.
//
// Wider than the legacy mutator by construction, and deliberately so: the legacy
// one walks the projected POD and fails for anything the POD has no field for,
// while the library addresses any element it holds. An element the projection
// omits could not be given a gid before and can be now.
//
// Reports `supported == false` only when `element_id` is empty; a gid the library
// rejects (an unknown element, a duplicate) comes back as `applied == false` with
// diagnostics.
EditOutcome apply_set_gid(LibraryDocument& document, const std::string& element_id, const std::string& gid);

// Sets the GSN notation identifier ("G1", "Sn3") an element is displayed under,
// mirroring `core::SetGsnIdentifier`'s model write. GSN's identifier is not a
// SACM concept -- SACM elements have an `id` and an optional `gid` and nothing
// that means "the label the diagram shows" -- so it lives in the reserved
// `assuranceForge.gsn.identifier` TaggedValue (clause 8.12), which is what
// `case_projection` reads back.
//
// UPSERT, composed. The library has `AddTaggedValue` and no update operation, so
// a set is "drop the existing tag for this key, then add". The add is previewed
// before the old tag is removed, so a rejection leaves the element with the
// identifier it had rather than none at all.
//
// This does NOT enforce the app's editing rules -- non-empty, no surrounding
// whitespace, unique among the nodes -- which are Assurance Forge's, not SACM's.
// `UpdateGsnIdentifierCommand` applies them before calling here, exactly as the
// legacy mutator did.
EditOutcome
apply_set_gsn_identifier(LibraryDocument& document, const std::string& element_id, const std::string& identifier);

// Sets or clears the GSN undeveloped decorator, mirroring
// `core::SetElementUndeveloped`. GSN's `undeveloped` is SACM
// `assertionDeclaration = needsSupport` (docs/sacm/sacm-gsn-mapping.md), so this
// is one `SetAssertionDeclaration` and no vendor tag.
//
// REFUSES on an element whose declaration is anything other than `asserted` or
// `needsSupport`. SACM has a single `assertionDeclaration`, so `needsSupport`
// cannot coexist with `assumed`, `axiomatic`, `defeated` or `asCited` -- writing
// it would silently turn a GSN Assumption into an undeveloped Goal. That is not
// a limitation being worked around: the decorator means "requires support that
// has not yet been provided", and GSN reaches Assumptions and Justifications by
// `InContextOf`, never by `SupportedBy`, so there is no support for them to be
// missing. The refusal names the declaration in the way.
//
// The legacy path this replaces kept `undeveloped` as a POD boolean independent
// of the declaration, wrote both, and then lost the boolean on reload, because
// the reader honours the shorthand only when the declaration is still
// `asserted`. Marking an Assumption undeveloped therefore reported success and
// did nothing.
EditOutcome apply_set_undeveloped(LibraryDocument& document, const std::string& element_id, bool undeveloped);

// Replace an AssertedRelationship's endpoints, mirroring the model write in
// `core::DropRelationshipReference`. All three slots go together because the
// clause-11.13 multiplicity spans them: source[1..*], target[1] -- exactly one.
//
// This exists for the repair path: the quick fix for an `UnresolvedEndpoint`
// finding drops one endpoint that resolves to nothing. The library operation
// underneath accepts an unresolved id only where the relationship ALREADY
// carried it, so a relationship with two broken endpoints can be fixed one at a
// time while a new dangling reference still cannot be introduced.
//
// Emptying a slot is refused rather than silently deleting the relationship --
// the caller decides that, because withdrawing a claim of support is a different
// act from repairing a broken pointer.
EditOutcome apply_set_relationship_ends(LibraryDocument& document,
                                        const std::string& relationship_id,
                                        const std::vector<std::string>& sources,
                                        const std::vector<std::string>& targets,
                                        const std::string& reasoning);

// Set the document order of elements inside one argument package.
//
// `ordered_ids` names a subset; those elements take the positions they already
// occupy, in the order given, and everything unnamed stays put (see
// `sacm::commands::ReorderPackageElements`). That subset behaviour is what makes
// the seam usable from a projection: the projection does not list packages or
// clause-8.7 attachments, so a caller cannot enumerate a package's full contents.
//
// Order carries no SACM meaning and this seam claims none. It exists because the
// application's tree order is persisted as document order, and a sibling reorder
// that could not write it would be lost on save.
EditOutcome apply_reorder_package_elements(LibraryDocument& document,
                                           const std::string& package_id,
                                           const std::vector<std::string>& ordered_ids);

// Which AssertedRelationship to create. SACM's own vocabulary rather than GSN's:
// `AssertedInference` is what GSN draws as SupportedBy and `AssertedContext` what
// it draws as InContextOf, in the opposite direction (see
// docs/sacm/sacm-gsn-mapping.md). The seam takes the SACM kind so the direction
// swap stays in the caller that owns the GSN reading.
enum class RelationshipKind { AssertedInference, AssertedContext, AssertedEvidence };

// Create an AssertedRelationship between elements that ALREADY exist (clause
// 11.13), in the argument package that owns `target`.
//
// `apply_add_child` also creates a relationship, but only together with a new
// element; moving an existing subtree to a new parent needs the relationship
// alone. `relationship_id` is required rather than minted here, because the
// caller has to record it in the audit event for replay to reproduce the same
// document -- a seam that minted its own would put the two out of step.
//
// Refuses rather than creates a structurally invalid relationship. In particular
// an AssertedInference whose only end is `reasoning` violates source [1..*], the
// shape a bare-placed Strategy would produce; `apply_add_child` handles that case
// for a NEW strategy by deferring the inference behind a vendor tag, and this seam
// deliberately does not reproduce that -- it reports the refusal so the caller can
// decline the move instead of writing an argument the library would not load.
EditOutcome apply_add_relationship(LibraryDocument& document,
                                   const std::string& relationship_id,
                                   RelationshipKind kind,
                                   const std::vector<std::string>& sources,
                                   const std::vector<std::string>& targets,
                                   const std::string& reasoning);

// ---------------------------------------------------------------------------
// Terminology edit seams (Phase 0 part 2).
//
// These mirror the legacy `core::*Terminology*WithIds` mutators, routing each
// terminology edit through a library Operation instead of mutating the POD
// `sacm::AssuranceCasePackage`. Inputs and outputs are string-only so no
// `sacm::model` type crosses into `core` (same boundary rule as the element
// seams above). Where the legacy variant accepts a `forced_id`, the seam accepts
// an optional caller-supplied id used verbatim for id-deterministic replay.
//
// gid handling: the legacy `...WithIds` mutators accept a `forced_gid` and,
// absent one, generate `GenerateUniqueGid(package, id)` -- normally `gid-<id>`,
// but with a `-2`, `-3`, ... suffix when that base is already taken by an
// unrelated element. These seams mirror that: each create/associate seam takes
// an optional gid alongside the optional id and stamps it verbatim; an empty gid
// means the base `gid-<id>` form.
//
// Both halves matter. A LIVE flipped command plans (id, gid) with the legacy
// generators and hands both over, so a library-primary create is identical to
// the legacy one even on a document where the base gid collides. A REPLAY hands
// over the (id, gid) the audit payload recorded, so it reproduces the recorded
// identity rather than reconstructing a guess from the id.

// The app's terminology term draft, mirroring `core::TerminologyTermDraft`
// without depending on `core`. All fields are plain strings the seam translates
// into library operations; `category_refs` and `origin` are element ids (a
// leading '#' and surrounding whitespace are tolerated and normalized, matching
// the legacy `NormalizeCategoryRefs`/`NormalizeRef`).
struct TerminologyTermFields {
    std::string value;
    std::string name;
    std::string description;
    std::vector<std::string> category_refs;
    std::string external_reference;
    std::string origin;
};

// Caller-supplied identities for the ArtifactReference and AssertedContext a
// terminology association may create, mirroring `core::TerminologyContextForcedIds`.
// Each field is consulted only when the corresponding element is newly created;
// a reused or promoted one keeps the identity it already has. An empty id lets
// the library generate one; an empty gid means the base `gid-<id>` form.
struct TerminologyContextIdentities {
    std::string artifact_reference_id;
    std::string artifact_reference_gid;
    std::string asserted_context_id;
    std::string asserted_context_gid;
};

// Result of creating a terminology element (package/category/term). `element_id`
// is the id the library generated or the caller supplied; empty unless applied.
// The created element's gid is the one the caller asked for, or `gid-<id>`.
struct TerminologyCreateOutcome {
    bool supported = true;
    bool applied = false;
    std::string element_id;
    std::vector<LoadDiagnostic> diagnostics;
};

// Result of an update or delete terminology seam. `removed_ids` lists every
// element the operation deleted, the target included -- empty for an update. A
// cascading delete removes more than it was asked to, and an audit trail that
// only recorded the target would not say what else went.
struct TerminologyEditOutcome {
    bool supported = true;
    bool applied = false;
    std::vector<std::string> removed_ids;
    std::vector<LoadDiagnostic> diagnostics;
};

// Result of associating a term with an element / adding it as a visible context,
// mirroring `core::TerminologyContextAssociationResult` including its reuse and
// promote flags. `artifact_reference_id`/`asserted_context_id` are the created
// -- or reused/promoted -- ids.
struct TerminologyContextOutcome {
    bool supported = true;
    bool applied = false;
    bool already_associated = false;
    bool created_artifact_reference = false;
    bool created_asserted_context = false;
    std::string artifact_reference_id;
    std::string asserted_context_id;
    std::vector<LoadDiagnostic> diagnostics;
};

// (1) Create a TerminologyPackage under the document's root AssuranceCasePackage,
// mirroring `core::CreateTerminologyPackageWithIds`. `package_id`/`package_gid`
// when non-empty are used verbatim. Reports `supported == false` only if the
// document has no root AssuranceCasePackage.
TerminologyCreateOutcome apply_create_terminology_package(LibraryDocument& document,
                                                          const std::string& name,
                                                          const std::string& description,
                                                          const std::string& package_id = {},
                                                          const std::string& package_gid = {});

// (2) Create a Category inside terminology package `package_id`, mirroring
// `core::CreateTerminologyCategoryWithIds`. `category_id`/`category_gid` when
// non-empty are used verbatim.
TerminologyCreateOutcome apply_create_terminology_category(LibraryDocument& document,
                                                           const std::string& package_id,
                                                           const std::string& name,
                                                           const std::string& description,
                                                           const std::string& category_id = {},
                                                           const std::string& category_gid = {});

// (3) Create a Term inside terminology package `package_id`, mirroring
// `core::CreateTerminologyTermWithIds`. Composes CreateTerm (name/value/external
// reference/origin) + SetDescription + SetExpressionCategories; a partial failure
// rolls the created term back. Each category ref must resolve to a Category and
// a non-empty origin must resolve to an element (the library validates both,
// unlike the legacy POD which stored any string). `term_id`/`term_gid` when
// non-empty are used verbatim.
TerminologyCreateOutcome apply_create_terminology_term(LibraryDocument& document,
                                                       const std::string& package_id,
                                                       const TerminologyTermFields& fields,
                                                       const std::string& term_id = {},
                                                       const std::string& term_gid = {});

// (4) Update a TerminologyPackage's name/description, mirroring
// `core::UpdateTerminologyPackage` (SetName + SetDescription; empty description
// clears it).
TerminologyEditOutcome apply_update_terminology_package(LibraryDocument& document,
                                                        const std::string& package_id,
                                                        const std::string& name,
                                                        const std::string& description);

// (5) Update a Category's name/description, mirroring
// `core::UpdateTerminologyCategory`.
TerminologyEditOutcome apply_update_terminology_category(LibraryDocument& document,
                                                         const std::string& category_id,
                                                         const std::string& name,
                                                         const std::string& description);

// (6) Update a Term's fields, mirroring `core::UpdateTerminologyTerm`. Composes
// SetName + SetExpressionValue + SetDescription + SetExpressionCategories +
// SetTermExternalReference + SetTermOrigin, applied in sequence and stopping on
// the first library rejection (the composed sequence is not transactional, so a
// mid-sequence failure leaves the earlier sets applied -- unlike the atomic
// legacy mutator).
TerminologyEditOutcome apply_update_terminology_term(LibraryDocument& document,
                                                     const std::string& term_id,
                                                     const TerminologyTermFields& fields);

// (7) Delete a terminology package, category, or term, mirroring the primitive
// the legacy delete mutators compose: `DeleteElement` with the
// DeleteReferencingRelationships policy so no relationship is left dangling. This
// does NOT reproduce the legacy app-level guards (refuse to delete a non-empty
// package or an in-use category) -- those are UI safety checks, not model
// invariants; the library's own RejectIfNonEmpty package policy still rejects a
// non-empty package.
//
// `cascade_external_references` is the user's confirmed answer to "this also
// removes N things -- go ahead?", and nothing else may set it. A glossary term
// used as a visible context is referenced from an ArgumentPackage, which is a
// different package, so removing it crosses a package boundary; the library
// refuses that by default (SACM-CMD-007) precisely so a cascade cannot happen
// without someone asking for it. Pass true to opt in, having previewed the
// consequences with `preview_delete_terminology_element` and shown them.
//
// The flag has to be recorded in the audit payload rather than re-derived,
// because a replay has no user to ask: see DeleteTerminologyTermCommand.
TerminologyEditOutcome apply_delete_terminology_element(LibraryDocument& document,
                                                        const std::string& element_id,
                                                        bool cascade_external_references = false);

// What deleting `element_id` WITH the cascade opted in would do, leaving
// `document` untouched. `requested` is the effect on the element itself;
// `consequential` is everything the cascade drags with it -- the list the
// confirmation exists to show, including references the cascade merely CHANGES
// (`deleted == false`) rather than removes.
//
// `can_apply` means the target itself would be deleted -- deliberately stricter
// than `preview_delete_elements`, where it means only that some delete in the
// set applied. A caller must not offer a cascade when this is false: the
// consequential list would then describe removals leading to a refusal.
//
// An empty `consequential` means the plain, non-cascading delete is enough and
// the caller should not ask: there is nothing to consent to.
DeletePreview preview_delete_terminology_element(const LibraryDocument& document, const std::string& element_id);

// (8) Associate term `term_id` with element `target_element_id`, mirroring
// `core::AssociateTerminologyTermWithElementWithIds`: creates an ArtifactReference
// (referencedArtifact = term) and an AssertedContext {source = ref, target =
// element}, REUSING an existing term ArtifactReference in the target's argument
// package if one exists and reporting `already_associated` when a context already
// links them. The target must be a claim, strategy (ArgumentReasoning), or
// solution (ArtifactReference) in an argument package. Caller-supplied identities
// are consulted only when the corresponding entity is newly created.
TerminologyContextOutcome apply_associate_terminology_term(LibraryDocument& document,
                                                           const std::string& target_element_id,
                                                           const std::string& term_id,
                                                           const TerminologyContextIdentities& identities = {});

// (9) Add term `term_id` as a visible context on `target_element_id`, mirroring
// `core::AddTerminologyTermAsVisibleContextWithIds`: like (8) plus the visible-
// context marker on the AssertedContext's description, and it PROMOTES an existing
// non-visible context in place (marking it visible) rather than creating a
// duplicate when one is promotable. The target must be a claim or strategy. Caller
// -supplied identities are consulted only when a new reference/context is created.
TerminologyContextOutcome apply_add_terminology_visible_context(LibraryDocument& document,
                                                                const std::string& target_element_id,
                                                                const std::string& term_id,
                                                                const TerminologyContextIdentities& identities = {});

} // namespace sacm_adapter
