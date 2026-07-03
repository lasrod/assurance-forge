#include "io/name_tables.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <utility>

namespace sacm::io::detail {

namespace {

constexpr std::array<std::pair<std::string_view, ElementKind>, 39> kClassNames{{
    {"Description", ElementKind::Description},
    {"ImplementationConstraint", ElementKind::ImplementationConstraint},
    {"Note", ElementKind::Note},
    {"TaggedValue", ElementKind::TaggedValue},
    {"AssuranceCasePackage", ElementKind::AssuranceCasePackage},
    {"AssuranceCasePackageInterface", ElementKind::AssuranceCasePackageInterface},
    {"AssuranceCasePackageBinding", ElementKind::AssuranceCasePackageBinding},
    {"TerminologyPackage", ElementKind::TerminologyPackage},
    {"TerminologyPackageInterface", ElementKind::TerminologyPackageInterface},
    {"TerminologyPackageBinding", ElementKind::TerminologyPackageBinding},
    {"TerminologyGroup", ElementKind::TerminologyGroup},
    {"Category", ElementKind::Category},
    {"Expression", ElementKind::Expression},
    {"Term", ElementKind::Term},
    {"ArgumentPackage", ElementKind::ArgumentPackage},
    {"ArgumentPackageInterface", ElementKind::ArgumentPackageInterface},
    {"ArgumentPackageBinding", ElementKind::ArgumentPackageBinding},
    {"ArgumentGroup", ElementKind::ArgumentGroup},
    {"Claim", ElementKind::Claim},
    {"ArgumentReasoning", ElementKind::ArgumentReasoning},
    {"ArtifactReference", ElementKind::ArtifactReference},
    {"AssertedInference", ElementKind::AssertedInference},
    {"AssertedEvidence", ElementKind::AssertedEvidence},
    {"AssertedContext", ElementKind::AssertedContext},
    {"AssertedArtifactSupport", ElementKind::AssertedArtifactSupport},
    {"AssertedArtifactContext", ElementKind::AssertedArtifactContext},
    {"ArtifactPackage", ElementKind::ArtifactPackage},
    {"ArtifactPackageInterface", ElementKind::ArtifactPackageInterface},
    {"ArtifactPackageBinding", ElementKind::ArtifactPackageBinding},
    {"ArtifactGroup", ElementKind::ArtifactGroup},
    {"Artifact", ElementKind::Artifact},
    {"ArtifactAssetRelationship", ElementKind::ArtifactAssetRelationship},
    // ptc/22-03-13 machine-readable alias for ArtifactAssetRelationship.
    {"ArtifactAssertedRelationship", ElementKind::ArtifactAssetRelationship},
    {"Activity", ElementKind::Activity},
    {"Event", ElementKind::Event},
    {"Participant", ElementKind::Participant},
    {"Technique", ElementKind::Technique},
    {"Resource", ElementKind::Resource},
    {"Property", ElementKind::Property},
}};

std::string to_lower(std::string_view text) {
    std::string lowered(text);
    std::ranges::transform(lowered, lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lowered;
}

}  // namespace

std::optional<ElementKind> kind_from_class_name(std::string_view name) {
    for (const auto& [class_name, kind] : kClassNames) {
        if (class_name == name) {
            return kind;
        }
    }
    return std::nullopt;
}

std::optional<ElementKind> kind_from_class_name_ci(std::string_view name) {
    const std::string lowered = to_lower(name);
    for (const auto& [class_name, kind] : kClassNames) {
        if (to_lower(class_name) == lowered) {
            return kind;
        }
    }
    return std::nullopt;
}

bool kind_is_argumentation_element(ElementKind kind) {
    switch (kind) {
        case ElementKind::ArgumentPackage:
        case ElementKind::ArgumentPackageInterface:
        case ElementKind::ArgumentPackageBinding:
        case ElementKind::ArgumentGroup:
        case ElementKind::Claim:
        case ElementKind::ArgumentReasoning:
        case ElementKind::ArtifactReference:
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

bool kind_is_artifact_element_in_artifact_package(ElementKind kind) {
    switch (kind) {
        case ElementKind::ArtifactPackage:
        case ElementKind::ArtifactPackageInterface:
        case ElementKind::ArtifactPackageBinding:
        case ElementKind::ArtifactGroup:
        case ElementKind::Artifact:
        case ElementKind::ArtifactAssetRelationship:
        case ElementKind::Activity:
        case ElementKind::Event:
        case ElementKind::Participant:
        case ElementKind::Technique:
        case ElementKind::Resource:
        case ElementKind::Property:
            return true;
        default:
            return false;
    }
}

bool kind_is_terminology_element(ElementKind kind) {
    switch (kind) {
        case ElementKind::TerminologyPackage:
        case ElementKind::TerminologyPackageInterface:
        case ElementKind::TerminologyPackageBinding:
        case ElementKind::TerminologyGroup:
        case ElementKind::Category:
        case ElementKind::Expression:
        case ElementKind::Term:
            return true;
        default:
            return false;
    }
}

bool kind_is_artifact_asset(ElementKind kind) {
    switch (kind) {
        case ElementKind::Artifact:
        case ElementKind::ArtifactAssetRelationship:
        case ElementKind::Activity:
        case ElementKind::Event:
        case ElementKind::Participant:
        case ElementKind::Technique:
        case ElementKind::Resource:
        case ElementKind::Property:
            return true;
        default:
            return false;
    }
}

bool kind_is_argument_asset(ElementKind kind) {
    switch (kind) {
        case ElementKind::Claim:
        case ElementKind::ArgumentReasoning:
        case ElementKind::ArtifactReference:
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

}  // namespace sacm::io::detail
