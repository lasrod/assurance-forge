#pragma once

#include "sacm/commands/policies.h"
#include "sacm/metadata/element_kind.h"
#include "sacm/model/argumentation.h"
#include "sacm/model/element_id.h"
#include "sacm/model/lang_string.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace sacm::commands {

// SACM-native edit operations (aggregate structs; designated initializers
// encouraged). Omitted `id` fields are generated deterministically by the
// document. The Operation variant grows slice by slice.

// Create an AssuranceCasePackage: a document root when `parent` is empty,
// or a nested package inside another AssuranceCasePackage.
struct CreateAssuranceCasePackage {
    std::optional<model::ElementId> id;
    std::string name;
    std::optional<model::ElementId> parent;
};

// Create an ArgumentPackage inside an AssuranceCasePackage or (nested)
// inside another ArgumentPackage.
struct CreateArgumentPackage {
    model::ElementId parent;
    std::optional<model::ElementId> id;
    std::string name;
};

// Create a Claim inside an ArgumentPackage. `description` becomes the
// Claim's Description content (SACM stores claim text there).
struct CreateClaim {
    model::ElementId parent;
    std::optional<model::ElementId> id;
    std::string name;
    std::string description;
    std::string language; // language tag for name/description; may be empty
};

// Create an ArtifactPackage inside an AssuranceCasePackage or (nested)
// inside another ArtifactPackage (clause 12.2).
struct CreateArtifactPackage {
    model::ElementId parent;
    std::optional<model::ElementId> id;
    std::string name;
};

// Create an artifact asset (clauses 12.7-12.13): Artifact, Activity, Event,
// Participant, Technique, Resource, or Property. Provenance fields apply
// where the kind defines them and are ignored otherwise. A Property is
// created inside another ArtifactAsset; the rest inside an ArtifactPackage.
struct CreateArtifactAsset {
    model::ElementId parent;
    metadata::ElementKind kind = metadata::ElementKind::Artifact;
    std::optional<model::ElementId> id;
    std::string name;
    std::string version;
    std::string date;
    std::string start_time;
    std::string end_time;
};

// Create an ArtifactAssetRelationship linking artifact assets (clause 12.14).
struct CreateArtifactAssetRelationship {
    model::ElementId parent;
    std::optional<model::ElementId> id;
    std::string name;
    std::vector<model::ElementId> sources;
    std::vector<model::ElementId> targets;
};

// Create an ArgumentReasoning inside an ArgumentPackage (clause 11.12).
struct CreateArgumentReasoning {
    model::ElementId parent;
    std::optional<model::ElementId> id;
    std::string name;
    std::optional<model::ElementId> structure; // detailing ArgumentPackage
};

// Create an ArtifactReference inside an ArgumentPackage (clause 11.9).
struct CreateArtifactReference {
    model::ElementId parent;
    std::optional<model::ElementId> id;
    std::string name;
    std::vector<model::ElementId> referenced_artifact_elements;
};

// Create one of the five asserted relationships (clauses 11.14-11.18).
// `kind` must be AssertedInference/Evidence/Context/ArtifactSupport/
// ArtifactContext; sources and targets reference ArgumentAssets.
struct CreateAssertedRelationship {
    model::ElementId parent;
    metadata::ElementKind kind;
    std::optional<model::ElementId> id;
    std::string name;
    std::vector<model::ElementId> sources;
    std::vector<model::ElementId> targets;
    std::optional<model::ElementId> reasoning;
    bool is_counter = false;
};

// Set an Assertion's assertionDeclaration (clause 11.10).
struct SetAssertionDeclaration {
    model::ElementId element;
    model::AssertionDeclaration declaration = model::AssertionDeclaration::Asserted;
};

// Replace an Assertion's meta-claims wholesale (clause 11.6), the way
// SetExpressionCategories replaces an ExpressionElement's categories.
//
// `AddMetaClaim` alone left the association one-way: a meta-claim could be
// attached and never detached, so a client that had to retract one -- an
// Assurance Claim Point whose resolution changes or is removed -- had no
// operation for it and would have had to reach past the API. Every id must
// resolve to a Claim; an empty list clears them.
struct SetMetaClaims {
    model::ElementId element;
    std::vector<model::ElementId> meta_claims;
};

// Replace an AssertedRelationship's endpoints wholesale (clause 11.13):
// source[1..*], target[1..*], and the optional reasoning an AssertedInference
// may carry. `AddRelationshipSource` could only extend, so an endpoint could be
// attached and never withdrawn; all three slots are replaced together because
// the multiplicity invariant spans them and writing one at a time would have to
// pass through states SACM forbids.
//
// UNRESOLVED IDS: a reference that resolves to nothing is accepted ONLY where
// the relationship already carried it. The reader admits such documents (it
// stores endpoint ids verbatim and reports the dangling ones separately), and a
// tool that repairs them has to be able to drop one broken endpoint while
// leaving another -- a strict check would make a two-fault relationship
// unrepairable. What stays forbidden is INTRODUCING one: an id that neither
// resolves nor was already there is rejected. So this operation can leave a
// document no worse than it found it, and cannot make it worse.
struct SetRelationshipEnds {
    model::ElementId relationship;
    std::vector<model::ElementId> sources;
    std::vector<model::ElementId> targets;
    std::optional<model::ElementId> reasoning;
};

// Attach a meta-claim (a Claim about an Assertion, clause 11.6).
struct AddMetaClaim {
    model::ElementId element;
    model::ElementId meta_claim;
};

// Add a source to an existing AssertedRelationship (clause 11.13, source[1..*]).
// Supports GSN strategy editing: a strategy's inference is materialized when its
// first sub-goal is added, and this extends that inference's sources for each
// later sub-goal. The source must be an ArgumentAsset (same typing as
// CreateAssertedRelationship); adding one already present is rejected.
struct AddRelationshipSource {
    model::ElementId relationship;
    model::ElementId source;
};

// Create a TerminologyPackage inside an AssuranceCasePackage or (nested)
// inside another TerminologyPackage.
struct CreateTerminologyPackage {
    model::ElementId parent;
    std::optional<model::ElementId> id;
    std::string name;
};

// Create a Category inside a TerminologyPackage (clause 10.8).
struct CreateCategory {
    model::ElementId parent;
    std::optional<model::ElementId> id;
    std::string name;
};

// Create a Term inside a TerminologyPackage (clause 10.7).
struct CreateTerm {
    model::ElementId parent;
    std::optional<model::ElementId> id;
    std::string name;
    std::string value;
    std::string external_reference;
    std::optional<model::ElementId> origin;
};

// Create an Expression inside a TerminologyPackage (clause 10.10).
struct CreateExpression {
    model::ElementId parent;
    std::optional<model::ElementId> id;
    std::string name;
    std::string value;
};

// Set or clear an element's citation (clause 8.2): `cited` present sets
// citedElement and isCitation=true; absent clears both.
struct SetCitation {
    model::ElementId element;
    std::optional<model::ElementId> cited;
};

// Set (or clear) a SACMElement's gid (clause 8.2: gid is String[0..1]). Applies
// to any SACMElement, not only ModelElements. A non-empty `gid` sets it; an
// empty `gid` clears it to absent. The model keeps an explicit gid="" distinct
// from an absent gid for round-trip fidelity, but this operation cannot set the
// empty-string form (no client needs it) -- empty means clear.
struct SetGid {
    model::ElementId element;
    std::string gid;
};

// Set a ModelElement's name (clause 8.6: LangString[1]).
struct SetName {
    model::ElementId element;
    std::string name;
    std::string language; // may be empty
};

// Set the description text for one language, creating the Description when
// absent (clause 8.9). Empty text removes that language entry.
struct SetDescription {
    model::ElementId element;
    std::string text;
    std::string language; // may be empty
};

// Set the description text for one language at ordinal slot `index` in a
// ModelElement's Description list. The SACM machine model stores descriptions
// as Description[0..*] (clause 8.9); a client that models more than one (e.g. a
// primary statement plus a secondary note) addresses them by index. `index`
// must be <= the current count -- no gaps: an in-range index edits that
// Description, an index equal to the count appends a new one. Empty text removes
// that language's entry from an existing slot and never creates a slot.
// SetDescription is the index==0 special case.
struct SetDescriptionAt {
    model::ElementId element;
    std::size_t index = 0;
    std::string text;
    std::string language; // may be empty
};

// Set the value text of a Term or Expression (clause 10.7 / 10.10).
struct SetExpressionValue {
    model::ElementId element;
    std::string value;
};

// Set a Term's external reference URI/text (clause 10.7).
struct SetTermExternalReference {
    model::ElementId element;
    std::string external_reference;
};

// Set or clear a Term's origin reference (clause 10.7); absent origin clears it.
struct SetTermOrigin {
    model::ElementId element;
    std::optional<model::ElementId> origin;
};

// Replace the set of Categories a Term or Expression belongs to (clause 10.8);
// each id must resolve to a Category.
struct SetExpressionCategories {
    model::ElementId element;
    std::vector<model::ElementId> categories;
};

// Attach a TaggedValue key/value pair (clause 8.12 extension mechanism).
struct AddTaggedValue {
    model::ElementId element;
    std::optional<model::ElementId> id;
    std::string key;
    std::string value;
    std::string language; // may be empty
};

// Delete any element by ID. Destructive consequences are governed by the
// explicit policies; defaults reject rather than cascade.
struct DeleteElement {
    model::ElementId target;
    ReferenceDeletePolicy reference_policy = ReferenceDeletePolicy::RejectIfReferenced;
    PackageDeletePolicy package_policy = PackageDeletePolicy::RejectIfNonEmpty;
    CrossPackageReferencePolicy cross_package_policy = CrossPackageReferencePolicy::RejectIfExternalReferencesExist;
};

using Operation = std::variant<CreateAssuranceCasePackage,
                               CreateArgumentPackage,
                               CreateClaim,
                               CreateArgumentReasoning,
                               CreateArtifactReference,
                               CreateAssertedRelationship,
                               CreateTerminologyPackage,
                               CreateCategory,
                               CreateTerm,
                               CreateExpression,
                               CreateArtifactPackage,
                               CreateArtifactAsset,
                               CreateArtifactAssetRelationship,
                               SetCitation,
                               SetGid,
                               SetName,
                               SetDescription,
                               SetDescriptionAt,
                               SetAssertionDeclaration,
                               AddMetaClaim,
                               SetMetaClaims,
                               AddRelationshipSource,
                               SetRelationshipEnds,
                               SetExpressionValue,
                               SetTermExternalReference,
                               SetTermOrigin,
                               SetExpressionCategories,
                               AddTaggedValue,
                               DeleteElement>;

// Stable operation name ("CreateClaim", "DeleteElement", ...) used in
// previews, results, and diagnostics.
std::string_view operation_name(const Operation& operation);

} // namespace sacm::commands
