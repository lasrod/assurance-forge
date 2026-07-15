#pragma once

#include "sacm/model/element.h"

namespace sacm::model {

class Property;

// Abstract base of the artifact assets (clause 12.5). Every asset may carry
// contained Properties.
class ArtifactAsset : public ArtifactElement {
  public:
    // Defined out of line in artifact.cpp so the vector of unique_ptr<Property>
    // is destroyed where Property is complete (Property is itself an
    // ArtifactAsset, so it can only be forward-declared here).
    ~ArtifactAsset() override;

    const std::vector<std::unique_ptr<Property>>& properties() const { return properties_; }

  protected:
    using ArtifactElement::ArtifactElement;

  private:
    friend struct sacm::detail::Access;

    std::vector<std::unique_ptr<Property>> properties_;
};

// A unit of data managed in the assurance case (clause 12.7), with
// provenance version/date.
class Artifact final : public ArtifactAsset {
  public:
    explicit Artifact(ElementId id) : ArtifactAsset(ElementKind::Artifact, std::move(id)) {}

    const std::string& version() const { return version_; }
    const std::string& date() const { return date_; }

  private:
    friend struct sacm::detail::Access;

    std::string version_;
    std::string date_;
};

// Activity performed during the lifecycle (clause 12.8).
class Activity final : public ArtifactAsset {
  public:
    explicit Activity(ElementId id) : ArtifactAsset(ElementKind::Activity, std::move(id)) {}

    const std::string& start_time() const { return start_time_; }
    const std::string& end_time() const { return end_time_; }

  private:
    friend struct sacm::detail::Access;

    std::string start_time_;
    std::string end_time_;
};

// Happening at a point in time (clause 12.9). The attribute is `date` per the
// specification text; the machine-readable model's `occurece` spelling is
// accepted on import.
class Event final : public ArtifactAsset {
  public:
    explicit Event(ElementId id) : ArtifactAsset(ElementKind::Event, std::move(id)) {}

    const std::string& date() const { return date_; }

  private:
    friend struct sacm::detail::Access;

    std::string date_;
};

// Person or organization involved (clause 12.10).
class Participant final : public ArtifactAsset {
  public:
    explicit Participant(ElementId id) : ArtifactAsset(ElementKind::Participant, std::move(id)) {}
};

// Method or procedure used (clause 12.11).
class Technique final : public ArtifactAsset {
  public:
    explicit Technique(ElementId id) : ArtifactAsset(ElementKind::Technique, std::move(id)) {}
};

// Resource used or produced (clause 12.12).
class Resource final : public ArtifactAsset {
  public:
    explicit Resource(ElementId id) : ArtifactAsset(ElementKind::Resource, std::move(id)) {}
};

// Named property of an ArtifactAsset (clause 12.13).
class Property final : public ArtifactAsset {
  public:
    explicit Property(ElementId id) : ArtifactAsset(ElementKind::Property, std::move(id)) {}
};

// Relationship linking ArtifactAssets (clause 12.14). Named
// ArtifactAssertedRelationship in the machine-readable model; the
// specification text name is used here.
class ArtifactAssetRelationship final : public ArtifactAsset {
  public:
    explicit ArtifactAssetRelationship(ElementId id)
        : ArtifactAsset(ElementKind::ArtifactAssetRelationship, std::move(id)) {}

    // source/target: ArtifactAsset[1..*]
    const std::vector<ElementId>& sources() const { return sources_; }
    const std::vector<ElementId>& targets() const { return targets_; }

  private:
    friend struct sacm::detail::Access;

    std::vector<ElementId> sources_;
    std::vector<ElementId> targets_;
};

// Group referencing member ArtifactElements (clause 12.3).
class ArtifactGroup final : public ArtifactElement {
  public:
    explicit ArtifactGroup(ElementId id) : ArtifactElement(ElementKind::ArtifactGroup, std::move(id)) {}

    const std::vector<ElementId>& artifact_elements() const { return artifact_elements_; }

  private:
    friend struct sacm::detail::Access;

    std::vector<ElementId> artifact_elements_;
};

// Artifact interchange package (clause 12.2).
class ArtifactPackage : public ArtifactElement {
  public:
    explicit ArtifactPackage(ElementId id)
        : ArtifactPackage(ElementKind::ArtifactPackage, std::move(id)) {}

    const std::vector<ElementId>& interfaces() const { return interfaces_; }
    const std::vector<std::unique_ptr<ArtifactElement>>& artifact_elements() const {
        return artifact_elements_;
    }

  protected:
    ArtifactPackage(ElementKind kind, ElementId id) : ArtifactElement(kind, std::move(id)) {}

  private:
    friend struct sacm::detail::Access;

    std::vector<ElementId> interfaces_;
    std::vector<std::unique_ptr<ArtifactElement>> artifact_elements_;
};

class ArtifactPackageInterface final : public ArtifactPackage {
  public:
    explicit ArtifactPackageInterface(ElementId id)
        : ArtifactPackage(ElementKind::ArtifactPackageInterface, std::move(id)) {}

    const std::optional<ElementId>& implements() const { return implements_; }

  private:
    friend struct sacm::detail::Access;

    std::optional<ElementId> implements_;
};

class ArtifactPackageBinding final : public ArtifactPackage {
  public:
    explicit ArtifactPackageBinding(ElementId id)
        : ArtifactPackage(ElementKind::ArtifactPackageBinding, std::move(id)) {}

    // participantPackage: ArtifactPackage[2..*]
    const std::vector<ElementId>& participant_packages() const { return participant_packages_; }

  private:
    friend struct sacm::detail::Access;

    std::vector<ElementId> participant_packages_;
};

}  // namespace sacm::model
