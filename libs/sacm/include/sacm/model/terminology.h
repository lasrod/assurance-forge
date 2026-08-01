#pragma once

#include "sacm/model/element.h"

namespace sacm::model {

// Abstract base of all Terminology package elements (clause 10.2).
class TerminologyElement : public ArtifactElement {
protected:
    using ArtifactElement::ArtifactElement;
};

// Abstract base of the terminology assets contained in packages (clause 10.5).
class TerminologyAsset : public TerminologyElement {
protected:
    using TerminologyElement::TerminologyElement;
};

// Category for organizing terminology concepts (clause 10.8). Categories may
// reference sub-categories (SACM 2.3 addition).
class Category final : public TerminologyAsset {
public:
    explicit Category(ElementId id) : TerminologyAsset(ElementKind::Category, std::move(id)) {}

    const std::vector<ElementId>& categories() const {
        return categories_;
    }

private:
    friend struct sacm::detail::Access;

    std::vector<ElementId> categories_;
};

// Abstract base of Term and Expression (clause 10.6): a value string plus
// category references.
class ExpressionElement : public TerminologyAsset {
public:
    const std::string& value() const {
        return value_;
    }
    const std::vector<ElementId>& categories() const {
        return categories_;
    }

protected:
    using TerminologyAsset::TerminologyAsset;

private:
    friend struct sacm::detail::Access;

    std::string value_;
    std::vector<ElementId> categories_;
};

// Structured expression composed of other ExpressionElements (clause 10.10).
class Expression final : public ExpressionElement {
public:
    explicit Expression(ElementId id) : ExpressionElement(ElementKind::Expression, std::move(id)) {}

    const std::vector<ElementId>& elements() const {
        return elements_;
    }

private:
    friend struct sacm::detail::Access;

    std::vector<ElementId> elements_;
};

// Terminology term (clause 10.7) with an optional external reference and an
// origin ModelElement reference.
class Term final : public ExpressionElement {
public:
    explicit Term(ElementId id) : ExpressionElement(ElementKind::Term, std::move(id)) {}

    const std::string& external_reference() const {
        return external_reference_;
    }
    const std::optional<ElementId>& origin() const {
        return origin_;
    }

private:
    friend struct sacm::detail::Access;

    std::string external_reference_;
    std::optional<ElementId> origin_;
};

// Group referencing member TerminologyElements (clause 10.3); membership is
// by reference, containment stays with the package.
class TerminologyGroup final : public TerminologyElement {
public:
    explicit TerminologyGroup(ElementId id) : TerminologyElement(ElementKind::TerminologyGroup, std::move(id)) {}

    const std::vector<ElementId>& terminology_elements() const {
        return terminology_elements_;
    }

private:
    friend struct sacm::detail::Access;

    std::vector<ElementId> terminology_elements_;
};

// Terminology interchange package (clause 10.4).
class TerminologyPackage : public TerminologyElement {
public:
    explicit TerminologyPackage(ElementId id) : TerminologyPackage(ElementKind::TerminologyPackage, std::move(id)) {}

    const std::vector<ElementId>& interfaces() const {
        return interfaces_;
    }
    const std::vector<std::unique_ptr<TerminologyElement>>& terminology_elements() const {
        return terminology_elements_;
    }

protected:
    TerminologyPackage(ElementKind kind, ElementId id) : TerminologyElement(kind, std::move(id)) {}

private:
    friend struct sacm::detail::Access;

    std::vector<ElementId> interfaces_;
    std::vector<std::unique_ptr<TerminologyElement>> terminology_elements_;
};

class TerminologyPackageInterface final : public TerminologyPackage {
public:
    explicit TerminologyPackageInterface(ElementId id)
        : TerminologyPackage(ElementKind::TerminologyPackageInterface, std::move(id)) {}

    const std::optional<ElementId>& implements() const {
        return implements_;
    }

private:
    friend struct sacm::detail::Access;

    std::optional<ElementId> implements_;
};

class TerminologyPackageBinding final : public TerminologyPackage {
public:
    explicit TerminologyPackageBinding(ElementId id)
        : TerminologyPackage(ElementKind::TerminologyPackageBinding, std::move(id)) {}

    // participantPackage: TerminologyPackage[2..*]
    const std::vector<ElementId>& participant_packages() const {
        return participant_packages_;
    }

private:
    friend struct sacm::detail::Access;

    std::vector<ElementId> participant_packages_;
};

} // namespace sacm::model
