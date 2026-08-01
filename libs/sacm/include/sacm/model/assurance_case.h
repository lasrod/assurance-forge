#pragma once

#include "sacm/model/argumentation.h"
#include "sacm/model/artifact.h"
#include "sacm/model/element.h"
#include "sacm/model/terminology.h"

namespace sacm::model {

// The full assurance case interchange package (clause 9.2): the mandatory
// top interchange object of the Assurance Case Model compliance point.
// Contains argumentation, artifact, and terminology packages plus nested
// assurance case packages.
class AssuranceCasePackage : public ArtifactElement {
public:
    explicit AssuranceCasePackage(ElementId id)
        : AssuranceCasePackage(ElementKind::AssuranceCasePackage, std::move(id)) {}

    const std::vector<ElementId>& interfaces() const {
        return interfaces_;
    }
    const std::vector<std::unique_ptr<AssuranceCasePackage>>& assurance_case_packages() const {
        return assurance_case_packages_;
    }
    const std::vector<std::unique_ptr<ArgumentPackage>>& argument_packages() const {
        return argument_packages_;
    }
    const std::vector<std::unique_ptr<ArtifactPackage>>& artifact_packages() const {
        return artifact_packages_;
    }
    const std::vector<std::unique_ptr<TerminologyPackage>>& terminology_packages() const {
        return terminology_packages_;
    }

protected:
    AssuranceCasePackage(ElementKind kind, ElementId id) : ArtifactElement(kind, std::move(id)) {}

private:
    friend struct sacm::detail::Access;

    std::vector<ElementId> interfaces_;
    std::vector<std::unique_ptr<AssuranceCasePackage>> assurance_case_packages_;
    std::vector<std::unique_ptr<ArgumentPackage>> argument_packages_;
    std::vector<std::unique_ptr<ArtifactPackage>> artifact_packages_;
    std::vector<std::unique_ptr<TerminologyPackage>> terminology_packages_;
};

class AssuranceCasePackageInterface final : public AssuranceCasePackage {
public:
    explicit AssuranceCasePackageInterface(ElementId id)
        : AssuranceCasePackage(ElementKind::AssuranceCasePackageInterface, std::move(id)) {}

    const std::optional<ElementId>& implements() const {
        return implements_;
    }

private:
    friend struct sacm::detail::Access;

    std::optional<ElementId> implements_;
};

class AssuranceCasePackageBinding final : public AssuranceCasePackage {
public:
    explicit AssuranceCasePackageBinding(ElementId id)
        : AssuranceCasePackage(ElementKind::AssuranceCasePackageBinding, std::move(id)) {}

    // participantPackage: AssuranceCasePackage[2..*]
    const std::vector<ElementId>& participant_packages() const {
        return participant_packages_;
    }

private:
    friend struct sacm::detail::Access;

    std::vector<ElementId> participant_packages_;
};

} // namespace sacm::model
