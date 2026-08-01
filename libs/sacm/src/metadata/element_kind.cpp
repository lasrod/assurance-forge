#include "sacm/metadata/element_kind.h"

namespace sacm::metadata {

std::string_view kind_name(ElementKind kind) {
    switch (kind) {
    case ElementKind::Description:
        return "Description";
    case ElementKind::ImplementationConstraint:
        return "ImplementationConstraint";
    case ElementKind::Note:
        return "Note";
    case ElementKind::TaggedValue:
        return "TaggedValue";
    case ElementKind::AssuranceCasePackage:
        return "AssuranceCasePackage";
    case ElementKind::AssuranceCasePackageInterface:
        return "AssuranceCasePackageInterface";
    case ElementKind::AssuranceCasePackageBinding:
        return "AssuranceCasePackageBinding";
    case ElementKind::TerminologyPackage:
        return "TerminologyPackage";
    case ElementKind::TerminologyPackageInterface:
        return "TerminologyPackageInterface";
    case ElementKind::TerminologyPackageBinding:
        return "TerminologyPackageBinding";
    case ElementKind::TerminologyGroup:
        return "TerminologyGroup";
    case ElementKind::Category:
        return "Category";
    case ElementKind::Expression:
        return "Expression";
    case ElementKind::Term:
        return "Term";
    case ElementKind::ArgumentPackage:
        return "ArgumentPackage";
    case ElementKind::ArgumentPackageInterface:
        return "ArgumentPackageInterface";
    case ElementKind::ArgumentPackageBinding:
        return "ArgumentPackageBinding";
    case ElementKind::ArgumentGroup:
        return "ArgumentGroup";
    case ElementKind::Claim:
        return "Claim";
    case ElementKind::ArgumentReasoning:
        return "ArgumentReasoning";
    case ElementKind::ArtifactReference:
        return "ArtifactReference";
    case ElementKind::AssertedInference:
        return "AssertedInference";
    case ElementKind::AssertedEvidence:
        return "AssertedEvidence";
    case ElementKind::AssertedContext:
        return "AssertedContext";
    case ElementKind::AssertedArtifactSupport:
        return "AssertedArtifactSupport";
    case ElementKind::AssertedArtifactContext:
        return "AssertedArtifactContext";
    case ElementKind::ArtifactPackage:
        return "ArtifactPackage";
    case ElementKind::ArtifactPackageInterface:
        return "ArtifactPackageInterface";
    case ElementKind::ArtifactPackageBinding:
        return "ArtifactPackageBinding";
    case ElementKind::ArtifactGroup:
        return "ArtifactGroup";
    case ElementKind::Artifact:
        return "Artifact";
    case ElementKind::ArtifactAssetRelationship:
        return "ArtifactAssetRelationship";
    case ElementKind::Activity:
        return "Activity";
    case ElementKind::Event:
        return "Event";
    case ElementKind::Participant:
        return "Participant";
    case ElementKind::Technique:
        return "Technique";
    case ElementKind::Resource:
        return "Resource";
    case ElementKind::Property:
        return "Property";
    }
    return "Unknown";
}

bool is_package_kind(ElementKind kind) {
    switch (kind) {
    case ElementKind::AssuranceCasePackage:
    case ElementKind::AssuranceCasePackageInterface:
    case ElementKind::AssuranceCasePackageBinding:
    case ElementKind::TerminologyPackage:
    case ElementKind::TerminologyPackageInterface:
    case ElementKind::TerminologyPackageBinding:
    case ElementKind::ArgumentPackage:
    case ElementKind::ArgumentPackageInterface:
    case ElementKind::ArgumentPackageBinding:
    case ElementKind::ArtifactPackage:
    case ElementKind::ArtifactPackageInterface:
    case ElementKind::ArtifactPackageBinding:
        return true;
    default:
        return false;
    }
}

bool is_asserted_relationship_kind(ElementKind kind) {
    switch (kind) {
    case ElementKind::AssertedInference:
    case ElementKind::AssertedEvidence:
    case ElementKind::AssertedContext:
    case ElementKind::AssertedArtifactSupport:
    case ElementKind::AssertedArtifactContext:
        return true;
    default:
        return false;
    }
}

} // namespace sacm::metadata
