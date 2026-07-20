#pragma once

#include "sacm/model/element.h"

namespace sacm::model {

// assertionDeclaration enumeration (clause 11.10).
enum class AssertionDeclaration {
    Asserted,
    NeedsSupport,
    Assumed,
    Axiomatic,
    Defeated,
    AsCited,
};

std::string_view assertion_declaration_name(AssertionDeclaration declaration);
std::optional<AssertionDeclaration> parse_assertion_declaration(std::string_view literal);

// Abstract base of all Argumentation package elements (clause 11.2).
class ArgumentationElement : public ArtifactElement {
  protected:
    using ArtifactElement::ArtifactElement;
};

// Argumentation interchange package (clause 11.4). Contains heterogeneous
// ArgumentationElements (claims, reasoning, relationships, nested packages,
// groups) in document order via the `argumentElement` role.
class ArgumentPackage : public ArgumentationElement {
  public:
    explicit ArgumentPackage(ElementId id)
        : ArgumentPackage(ElementKind::ArgumentPackage, std::move(id)) {}

    const std::vector<ElementId>& interfaces() const { return interfaces_; }
    const std::vector<std::unique_ptr<ArgumentationElement>>& argument_elements() const {
        return argument_elements_;
    }

  protected:
    ArgumentPackage(ElementKind kind, ElementId id) : ArgumentationElement(kind, std::move(id)) {}

  private:
    friend struct sacm::detail::Access;

    std::vector<ElementId> interfaces_;
    std::vector<std::unique_ptr<ArgumentationElement>> argument_elements_;
};

class ArgumentPackageInterface final : public ArgumentPackage {
  public:
    explicit ArgumentPackageInterface(ElementId id)
        : ArgumentPackage(ElementKind::ArgumentPackageInterface, std::move(id)) {}

    const std::optional<ElementId>& implements() const { return implements_; }

  private:
    friend struct sacm::detail::Access;

    std::optional<ElementId> implements_;
};

class ArgumentPackageBinding final : public ArgumentPackage {
  public:
    explicit ArgumentPackageBinding(ElementId id)
        : ArgumentPackage(ElementKind::ArgumentPackageBinding, std::move(id)) {}

    // participantPackage: ArgumentPackage[2..*]
    const std::vector<ElementId>& participant_packages() const { return participant_packages_; }

  private:
    friend struct sacm::detail::Access;

    std::vector<ElementId> participant_packages_;
};

// Group referencing member ArgumentationElements (clause 11.3).
class ArgumentGroup final : public ArgumentationElement {
  public:
    explicit ArgumentGroup(ElementId id)
        : ArgumentationElement(ElementKind::ArgumentGroup, std::move(id)) {}

    const std::vector<ElementId>& argument_elements() const { return argument_elements_; }

  private:
    friend struct sacm::detail::Access;

    std::vector<ElementId> argument_elements_;
};

// Abstract base of the argumentation assets (clause 11.5).
class ArgumentAsset : public ArgumentationElement {
  protected:
    using ArgumentationElement::ArgumentationElement;
};

// Description of the reasoning of an argument step (clause 11.12); may point
// to an ArgumentPackage detailing the reasoning structure.
class ArgumentReasoning final : public ArgumentAsset {
  public:
    explicit ArgumentReasoning(ElementId id)
        : ArgumentAsset(ElementKind::ArgumentReasoning, std::move(id)) {}

    const std::optional<ElementId>& structure() const { return structure_; }

  private:
    friend struct sacm::detail::Access;

    std::optional<ElementId> structure_;
};

// Reference from the argumentation to ArtifactElements (clause 11.9), e.g.
// evidence artifacts.
class ArtifactReference final : public ArgumentAsset {
  public:
    explicit ArtifactReference(ElementId id)
        : ArgumentAsset(ElementKind::ArtifactReference, std::move(id)) {}

    const std::vector<ElementId>& referenced_artifact_elements() const {
        return referenced_artifact_elements_;
    }

  private:
    friend struct sacm::detail::Access;

    std::vector<ElementId> referenced_artifact_elements_;
};

// Abstract base of Claim and the asserted relationships (clause 11.6):
// carries the assertion declaration and meta-claims.
class Assertion : public ArgumentAsset {
  public:
    AssertionDeclaration assertion_declaration() const { return assertion_declaration_; }
    const std::vector<ElementId>& meta_claims() const { return meta_claims_; }

  protected:
    using ArgumentAsset::ArgumentAsset;

  private:
    friend struct sacm::detail::Access;

    AssertionDeclaration assertion_declaration_ = AssertionDeclaration::Asserted;
    std::vector<ElementId> meta_claims_;
};

// A proposition of the argumentation (clause 11.11); the SACM representation
// of what GSN notations render as a goal. Content lives in the inherited
// description.
class Claim final : public Assertion {
  public:
    explicit Claim(ElementId id) : Assertion(ElementKind::Claim, std::move(id)) {}
};

// Abstract base of the asserted relationships (clause 11.13). source and
// target reference ArgumentAssets; isCounter marks counter-argumentation.
class AssertedRelationship : public Assertion {
  public:
    bool is_counter() const { return is_counter_; }
    const std::optional<ElementId>& reasoning() const { return reasoning_; }
    const std::vector<ElementId>& sources() const { return sources_; }
    const std::vector<ElementId>& targets() const { return targets_; }

  protected:
    using Assertion::Assertion;

  private:
    friend struct sacm::detail::Access;

    bool is_counter_ = false;
    std::optional<ElementId> reasoning_;
    std::vector<ElementId> sources_;
    std::vector<ElementId> targets_;
};

// Inference between claims (clause 11.14).
class AssertedInference final : public AssertedRelationship {
  public:
    explicit AssertedInference(ElementId id)
        : AssertedRelationship(ElementKind::AssertedInference, std::move(id)) {}
};

// Evidence relationship from ArtifactReferences to claims (clause 11.15).
class AssertedEvidence final : public AssertedRelationship {
  public:
    explicit AssertedEvidence(ElementId id)
        : AssertedRelationship(ElementKind::AssertedEvidence, std::move(id)) {}
};

// Contextual relationship (clause 11.16).
class AssertedContext final : public AssertedRelationship {
  public:
    explicit AssertedContext(ElementId id)
        : AssertedRelationship(ElementKind::AssertedContext, std::move(id)) {}
};

// Artifact support relationship (clause 11.17).
class AssertedArtifactSupport final : public AssertedRelationship {
  public:
    explicit AssertedArtifactSupport(ElementId id)
        : AssertedRelationship(ElementKind::AssertedArtifactSupport, std::move(id)) {}
};

// Artifact context relationship (clause 11.18).
class AssertedArtifactContext final : public AssertedRelationship {
  public:
    explicit AssertedArtifactContext(ElementId id)
        : AssertedRelationship(ElementKind::AssertedArtifactContext, std::move(id)) {}
};

}  // namespace sacm::model
