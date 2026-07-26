#include "io/name_tables.h"

#include <algorithm>
#include <array>
#include <span>
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

namespace {

// GSN (Goal Structuring Notation) as published alongside the SACM reference
// implementation. Every row below is the class's declared `eSuperTypes` in
// gsn.ecore, not a guess from the name.
//
// ChoiceNode, Context, and AwayContext specialize `ArgumentAsset`, which is
// abstract in SACM: there is no concrete SACM class they can become, so they
// carry no kind and must be preserved rather than coerced into a Claim.
// Beware the version numbering: `scsc.acwg.gsn/2.0` is the CURRENT metamodel
// (GSN Metamodel Specification v2.2, SCSC ACWG, July 2021, CC-BY-4.0), while
// `acwg.org/3.0/gsn` is the older one despite the higher number.
constexpr std::string_view kGsnNamespaces[] = {
    "http://scsc.acwg.gsn/2.0",
    "http://acwg.org/3.0/gsn",
    "http://org.eclipse.acme/1.0/gsn",
};

struct GsnType {
    std::string_view name;
    std::optional<ElementKind> kind;
    bool reverse_endpoints = false;
    // SACM AssertionDeclaration literal implied by the GSN type, per the ecore
    // and docs/sacm/sacm-gsn-mapping.md. Empty when the type implies none.
    std::string_view assertion_declaration = {};
};

constexpr GsnType kGsnTypes[] = {
    {"Module", ElementKind::ArgumentPackage},
    {"ContractModule", ElementKind::ArgumentPackageBinding},
    {"Strategy", ElementKind::ArgumentReasoning},
    {"Goal", ElementKind::Claim},
    // Goal, Justification and Assumption share one SACM supertype, so the
    // declaration is the only thing that keeps them apart once the GSN type is
    // resolved away. Both mappings come from the ecore, not from the names.
    {"Justification", ElementKind::Claim, /*reverse_endpoints=*/false, "axiomatic"},
    {"Assumption", ElementKind::Claim, /*reverse_endpoints=*/false, "assumed"},
    {"AwayGoal", ElementKind::Claim},
    // Present only in the current (scsc.acwg.gsn/2.0) metamodel.
    {"AwayAssumption", ElementKind::Claim, /*reverse_endpoints=*/false, "assumed"},
    {"AwayJustification", ElementKind::Claim, /*reverse_endpoints=*/false, "axiomatic"},
    {"Solution", ElementKind::ArtifactReference},
    {"AwaySolution", ElementKind::ArtifactReference},
    {"ModuleReference", ElementKind::ArtifactReference},
    {"ContractModuleReference", ElementKind::ArtifactReference},
    // The two relationship mappings run in the opposite direction to their
    // SACM supertypes -- see ExtensionType::reverse_endpoints. The ACWG's own
    // GSN-to-SACM transformation swaps the same two:
    // "the +source and +target attributes of GSN SupportedBy and SACM
    //  AssertedInference are reversed."
    {"SupportedBy", ElementKind::AssertedInference, /*reverse_endpoints=*/true},
    {"InContextOf", ElementKind::AssertedContext, /*reverse_endpoints=*/true},
    // Abstract SACM supertype (ArgumentAsset) -- no concrete equivalent, so
    // these are preserved rather than coerced.
    //
    // Context is deliberate, not an oversight. GSN Metamodel v2.2 section 2.1:
    // "context elements may be either of type Axiomatic Claim or of type
    // ArtifactReference. The type of the Context element is determined by the
    // nature of its content" -- a human judgement about meaning, with no
    // property to switch on. (The `refersToExternalMaterial` flag that older
    // tooling used for this was dropped from the current metamodel.) Choosing
    // one mechanically would be reinterpreting the argument.
    //
    // Choice carries no attributes at all; v2.2 models it as an ArgumentAsset
    // purely "to enable its connection to multiple SupportedBy relationships".
    // Its "m of n" cardinality is a GSN v3 concept with no metamodel yet.
    //
    // AwayContext is contested: both published ecores declare ArgumentAsset,
    // while the v2.2 prose and the ACWG transformation rules say
    // ArtifactReference. Preserved until the project settles it, because
    // guessing here would silently retype evidence.
    {"ChoiceNode", std::nullopt},  // spelled Choice in scsc.acwg.gsn/2.0
    {"Choice", std::nullopt},
    {"Context", std::nullopt},
    {"AwayContext", std::nullopt},
};

}  // namespace

namespace {

// Attributes and association ends from the normative model (asserted against
// docs/sacm/sacm-2.3-metamodel-inventory.md by the drift test), plus the
// serialization-level names XMI adds and the two misspellings the
// machine-readable model carries for Event.date.
constexpr std::string_view kKnownAttributes[] = {
    // Attributes from the normative model.
    "assertionDeclaration", "content", "date", "endTime", "externalReference", "gid",
    "isAbstract", "isCitation", "isCounter", "lang", "value", "version", "startTime",
    // Association ends, which serialize as idref-style attributes.
    "abstractForm", "argumentElement", "artifactElement", "category", "citedElement",
    "element", "expression", "implements", "interface", "metaClaim", "origin",
    "participantPackage", "reasoning", "referencedArtifactElement", "source", "structure",
    "target", "terminologyElement",
    // Tolerant-mode shorthands the reader accepts in place of a child element:
    // `name="x"` and `description="x"` for the LangString forms, `key` for a
    // TaggedValue key.
    "name", "description", "key",
    // Alias the reader accepts for referencedArtifactElement.
    "referencedArtifact",
    // Legacy GSN shorthand normalized to assertionDeclaration=needsSupport on
    // read; recognized rather than preserved as opaque vendor content.
    "undeveloped",
    // XMI serialization infrastructure.
    "id", "idref", "type", "href", "ref", "uuid", "label",
    // ptc/22-03-13 spells Event.date as `occurece`; accept both misspellings.
    "occurece", "occurence",
};

}  // namespace

std::span<const std::string_view> known_sacm_attributes() { return kKnownAttributes; }

bool is_known_sacm_attribute(std::string_view name) {
    return std::ranges::find(kKnownAttributes, name) != std::ranges::end(kKnownAttributes);
}

bool is_sacm_extension_namespace(std::string_view namespace_uri) {
    return std::ranges::any_of(kGsnNamespaces, [&](std::string_view known) {
        return namespace_uri == known;
    });
}

std::optional<ExtensionType> resolve_extension_type(std::string_view namespace_uri,
                                                    std::string_view type_name) {
    if (!is_sacm_extension_namespace(namespace_uri)) {
        return std::nullopt;
    }
    for (const GsnType& known : kGsnTypes) {
        if (known.name == type_name) {
            return ExtensionType{namespace_uri, known.name, known.kind, known.reverse_endpoints,
                                 known.assertion_declaration};
        }
    }
    return std::nullopt;
}

}  // namespace sacm::io::detail
