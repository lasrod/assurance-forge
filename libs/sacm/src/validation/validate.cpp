#include "sacm/validation/validate.h"

#include "model/traverse.h"

#include "sacm/model/assurance_case.h"
#include "sacm/model/document.h"
#include "sacm/validation/codes.h"

#include <format>
#include <unordered_map>
#include <unordered_set>

namespace sacm::validation {

namespace {

using model::ElementId;
using model::SACMElement;

Diagnostic make(std::string_view code, Severity severity, std::string_view requirement,
                std::vector<ElementId> affected, std::string message) {
    return Diagnostic{
        .code = std::string(code),
        .severity = severity,
        .requirement_id = std::string(requirement),
        .operation = "",
        .affected = std::move(affected),
        .location = std::nullopt,
        .message = std::move(message),
    };
}

// Legal target class per reference role. Roles that accept any SACMElement
// (citedElement/abstractForm) return true directly.
bool reference_target_kind_ok(const SACMElement& referrer, std::string_view role,
                              const SACMElement& target) {
    using namespace model;
    if (role == "citedElement" || role == "abstractForm") {
        return true;
    }
    if (role == "expression" || role == "element") {
        return dynamic_cast<const ExpressionElement*>(&target) != nullptr;
    }
    if (role == "category") {
        return target.kind() == ElementKind::Category;
    }
    if (role == "origin") {
        return dynamic_cast<const ModelElement*>(&target) != nullptr;
    }
    if (role == "terminologyElement") {
        return dynamic_cast<const TerminologyElement*>(&target) != nullptr;
    }
    if (role == "argumentElement") {
        return dynamic_cast<const ArgumentationElement*>(&target) != nullptr;
    }
    if (role == "artifactElement") {
        return dynamic_cast<const ArtifactElement*>(&target) != nullptr;
    }
    if (role == "structure") {
        return dynamic_cast<const ArgumentPackage*>(&target) != nullptr;
    }
    if (role == "referencedArtifactElement") {
        return dynamic_cast<const ArtifactElement*>(&target) != nullptr;
    }
    if (role == "metaClaim") {
        return target.kind() == ElementKind::Claim;
    }
    if (role == "reasoning") {
        return target.kind() == ElementKind::ArgumentReasoning;
    }
    if (role == "source" || role == "target") {
        if (referrer.kind() == ElementKind::ArtifactAssetRelationship) {
            return dynamic_cast<const ArtifactAsset*>(&target) != nullptr;
        }
        return dynamic_cast<const ArgumentAsset*>(&target) != nullptr;
    }
    if (role == "interface" || role == "implements" || role == "participantPackage") {
        switch (referrer.kind()) {
            case ElementKind::AssuranceCasePackage:
            case ElementKind::AssuranceCasePackageInterface:
            case ElementKind::AssuranceCasePackageBinding:
                return role == "interface"
                           ? target.kind() == ElementKind::AssuranceCasePackageInterface
                           : dynamic_cast<const AssuranceCasePackage*>(&target) != nullptr;
            case ElementKind::ArgumentPackage:
            case ElementKind::ArgumentPackageInterface:
            case ElementKind::ArgumentPackageBinding:
                return role == "interface"
                           ? target.kind() == ElementKind::ArgumentPackageInterface
                           : dynamic_cast<const ArgumentPackage*>(&target) != nullptr;
            case ElementKind::ArtifactPackage:
            case ElementKind::ArtifactPackageInterface:
            case ElementKind::ArtifactPackageBinding:
                return role == "interface"
                           ? target.kind() == ElementKind::ArtifactPackageInterface
                           : dynamic_cast<const ArtifactPackage*>(&target) != nullptr;
            case ElementKind::TerminologyPackage:
            case ElementKind::TerminologyPackageInterface:
            case ElementKind::TerminologyPackageBinding:
                return role == "interface"
                           ? target.kind() == ElementKind::TerminologyPackageInterface
                           : dynamic_cast<const TerminologyPackage*>(&target) != nullptr;
            default:
                return false;
        }
    }
    return true;
}

}  // namespace

std::vector<Diagnostic> validate_structure(const model::Document& document) {
    std::vector<Diagnostic> diagnostics;

    // Unique ids (recomputed from the tree, independent of the index).
    std::unordered_set<ElementId> seen;
    document.for_each_element([&](const SACMElement& element) {
        if (element.id().empty()) {
            diagnostics.push_back(make(codes::kIdInvalid, Severity::Error, "SACM23-XMI-003",
                                       {element.id()},
                                       std::format("{} has an empty id",
                                                   metadata::kind_name(element.kind()))));
            return;
        }
        if (!seen.insert(element.id()).second) {
            diagnostics.push_back(make(codes::kIdDuplicate, Severity::Error, "SACM23-XMI-003",
                                       {element.id()},
                                       std::format("duplicate element id '{}'",
                                                   element.id().value())));
        }
    });

    // References resolve to elements of a legal kind.
    document.for_each_element([&](const SACMElement& element) {
        model::traverse::for_each_reference(
            element, [&](const model::traverse::ReferenceUse& use) {
                const SACMElement* target = document.find(*use.target);
                if (target == nullptr) {
                    // A target held only as preserved compatibility content is
                    // present in the document but absent from the index. It is
                    // untyped, not missing, so it cannot be kind-checked -- but
                    // calling it missing would mark an intact argument invalid
                    // (SACM23-COMPAT-002).
                    if (document.has_preserved_element(*use.target)) {
                        diagnostics.push_back(make(
                            codes::kRefPreservedTarget, Severity::Warning, "SACM23-COMPAT-002",
                            {element.id(), *use.target},
                            std::format("'{}' references ({}) '{}', which was preserved as "
                                        "compatibility content and therefore cannot be "
                                        "type-checked",
                                        element.id().value(), use.role, use.target->value())));
                        return;
                    }
                    diagnostics.push_back(make(
                        codes::kRefDangling, Severity::Error, "SACM23-XMI-003",
                        {element.id(), *use.target},
                        std::format("'{}' references ({}) missing element '{}'",
                                    element.id().value(), use.role, use.target->value())));
                    return;
                }
                if (!reference_target_kind_ok(element, use.role, *target)) {
                    diagnostics.push_back(make(
                        codes::kRefWrongType, Severity::Error, "SACM23-ARG-002",
                        {element.id(), *use.target},
                        std::format("'{}' reference ({}) targets a {}, which is not a legal "
                                    "target kind",
                                    element.id().value(), use.role,
                                    metadata::kind_name(target->kind()))));
                }
            });
    });

    return diagnostics;
}

std::vector<Diagnostic> validate(const model::Document& document) {
    std::vector<Diagnostic> diagnostics = validate_structure(document);

    document.for_each_element([&](const SACMElement& element) {
        // Citation constraint (clause 8.2): citedElement implies isCitation.
        if (element.cited_element().has_value() && !element.is_citation()) {
            diagnostics.push_back(make(
                codes::kCitationInvalid, Severity::Error, "SACM23-BASE-002", {element.id()},
                std::format("'{}' has citedElement but isCitation is false",
                            element.id().value())));
        }

        if (const auto* model_element = dynamic_cast<const model::ModelElement*>(&element)) {
            // description is [0..1] per the specification text; the
            // machine-readable model allows more, so surplus is a warning.
            if (model_element->descriptions().size() > 1) {
                diagnostics.push_back(make(
                    codes::kMultiplicityViolation, Severity::Warning, "SACM23-BASE-001",
                    {element.id()},
                    std::format("'{}' has {} descriptions; clause 8.6 allows at most one",
                                element.id().value(), model_element->descriptions().size())));
            }
            // ImplementationConstraints only on abstract elements (clause 8.6).
            if (!model_element->implementation_constraints().empty() && !element.is_abstract()) {
                diagnostics.push_back(make(
                    codes::kCitationInvalid, Severity::Error, "SACM23-BASE-002", {element.id()},
                    std::format("'{}' has implementation constraints but isAbstract is false",
                                element.id().value())));
            }
        }

        // MultiLangString language uniqueness (clause 8.5).
        const auto check_langs = [&](const model::MultiLangString& value, std::string_view what) {
            std::unordered_set<std::string> langs;
            for (const model::LangString& entry : value.values) {
                if (!langs.insert(entry.lang).second) {
                    diagnostics.push_back(make(
                        codes::kMultiplicityViolation, Severity::Error, "SACM23-BASE-001",
                        {element.id()},
                        std::format("'{}' {} has duplicate language tag '{}'",
                                    element.id().value(), what, entry.lang)));
                }
            }
        };
        if (const auto* utility = dynamic_cast<const model::UtilityElement*>(&element)) {
            check_langs(utility->content(), "content");
        }
        if (const auto* tagged = dynamic_cast<const model::TaggedValue*>(&element)) {
            check_langs(tagged->key(), "key");
        }

        // Asserted relationship multiplicities (clause 11.13).
        if (const auto* rel = dynamic_cast<const model::AssertedRelationship*>(&element)) {
            if (rel->sources().empty() || rel->targets().empty()) {
                diagnostics.push_back(make(
                    codes::kMultiplicityViolation, Severity::Error, "SACM23-ARG-002",
                    {element.id()},
                    std::format("'{}' must have at least one source and one target",
                                element.id().value())));
            }
        }
        if (const auto* rel = dynamic_cast<const model::ArtifactAssetRelationship*>(&element)) {
            if (rel->sources().empty() || rel->targets().empty()) {
                diagnostics.push_back(make(
                    codes::kMultiplicityViolation, Severity::Error, "SACM23-ART-001",
                    {element.id()},
                    std::format("'{}' must have at least one source and one target",
                                element.id().value())));
            }
        }

        // Binding participant multiplicity [2..*] (clauses 9.4/10/11/12).
        const auto check_participants = [&](const std::vector<ElementId>& participants) {
            if (participants.size() < 2) {
                diagnostics.push_back(make(
                    codes::kMultiplicityViolation, Severity::Error, "SACM23-PKG-002",
                    {element.id()},
                    std::format("binding '{}' needs at least two participant packages",
                                element.id().value())));
            }
        };
        switch (element.kind()) {
            case model::ElementKind::AssuranceCasePackageBinding:
                check_participants(
                    static_cast<const model::AssuranceCasePackageBinding&>(element)
                        .participant_packages());
                break;
            case model::ElementKind::ArgumentPackageBinding:
                check_participants(static_cast<const model::ArgumentPackageBinding&>(element)
                                       .participant_packages());
                break;
            case model::ElementKind::ArtifactPackageBinding:
                check_participants(static_cast<const model::ArtifactPackageBinding&>(element)
                                       .participant_packages());
                break;
            case model::ElementKind::TerminologyPackageBinding:
                check_participants(static_cast<const model::TerminologyPackageBinding&>(element)
                                       .participant_packages());
                break;
            default:
                break;
        }
    });

    return diagnostics;
}

}  // namespace sacm::validation
