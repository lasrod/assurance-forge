#pragma once

#include <string_view>

namespace sacm::metadata {

// Every concrete SACMElement class of the SACM 2.3 metamodel
// (docs/sacm/sacm-2.3-metamodel-inventory.md). LangString and
// MultiLangString are value types, not SACMElements, and have no kind.
enum class ElementKind {
    // Base (clause 8)
    Description,
    ImplementationConstraint,
    Note,
    TaggedValue,
    // AssuranceCase (clause 9)
    AssuranceCasePackage,
    AssuranceCasePackageInterface,
    AssuranceCasePackageBinding,
    // Terminology (clause 10)
    TerminologyPackage,
    TerminologyPackageInterface,
    TerminologyPackageBinding,
    TerminologyGroup,
    Category,
    Expression,
    Term,
    // Argumentation (clause 11)
    ArgumentPackage,
    ArgumentPackageInterface,
    ArgumentPackageBinding,
    ArgumentGroup,
    Claim,
    ArgumentReasoning,
    ArtifactReference,
    AssertedInference,
    AssertedEvidence,
    AssertedContext,
    AssertedArtifactSupport,
    AssertedArtifactContext,
    // Artifact (clause 12)
    ArtifactPackage,
    ArtifactPackageInterface,
    ArtifactPackageBinding,
    ArtifactGroup,
    Artifact,
    ArtifactAssetRelationship,
    Activity,
    Event,
    Participant,
    Technique,
    Resource,
    Property,
};

// SACM 2.3 class name for the kind (exact metamodel spelling).
std::string_view kind_name(ElementKind kind);

// True for the package kinds that act as interchange containers
// (AssuranceCasePackage/ArgumentPackage/ArtifactPackage/TerminologyPackage
// and their interface/binding subtypes).
bool is_package_kind(ElementKind kind);

// True for AssertedInference/Evidence/Context/ArtifactSupport/ArtifactContext.
bool is_asserted_relationship_kind(ElementKind kind);

} // namespace sacm::metadata
