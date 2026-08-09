#include "sacm/validation/validate.h"

#include "model/traverse.h"

#include "sacm/model/assurance_case.h"
#include "sacm/model/document.h"
#include "sacm/validation/codes.h"

#include <algorithm>
#include <format>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sacm::validation {

namespace {

using model::ElementId;
using model::SACMElement;

Diagnostic make(std::string_view code,
                Severity severity,
                std::string_view requirement,
                std::vector<ElementId> affected,
                std::string message) {
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
bool reference_target_kind_ok(const SACMElement& referrer, std::string_view role, const SACMElement& target) {
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
            return role == "interface" ? target.kind() == ElementKind::AssuranceCasePackageInterface
                                       : dynamic_cast<const AssuranceCasePackage*>(&target) != nullptr;
        case ElementKind::ArgumentPackage:
        case ElementKind::ArgumentPackageInterface:
        case ElementKind::ArgumentPackageBinding:
            return role == "interface" ? target.kind() == ElementKind::ArgumentPackageInterface
                                       : dynamic_cast<const ArgumentPackage*>(&target) != nullptr;
        case ElementKind::ArtifactPackage:
        case ElementKind::ArtifactPackageInterface:
        case ElementKind::ArtifactPackageBinding:
            return role == "interface" ? target.kind() == ElementKind::ArtifactPackageInterface
                                       : dynamic_cast<const ArtifactPackage*>(&target) != nullptr;
        case ElementKind::TerminologyPackage:
        case ElementKind::TerminologyPackageInterface:
        case ElementKind::TerminologyPackageBinding:
            return role == "interface" ? target.kind() == ElementKind::TerminologyPackageInterface
                                       : dynamic_cast<const TerminologyPackage*>(&target) != nullptr;
        default:
            return false;
        }
    }
    return true;
}

// True when `element` sits anywhere inside `container`'s containment subtree.
bool is_contained_in(const SACMElement& element, const SACMElement& container) {
    for (const SACMElement* ancestor = element.parent(); ancestor != nullptr; ancestor = ancestor->parent()) {
        if (ancestor == &container) {
            return true;
        }
    }
    return false;
}

// Legal end typing for the relationship families whose own clause narrows the
// generic ArgumentAsset ends of 11.13. Kept separate from
// `reference_target_kind_ok` -- that one backs `validate_structure`, which is
// asserted after every command, and these are content constraints on documents
// rather than an invariant every mutation preserves.
//
// 11.14 (AssertedInference) and 11.16 (AssertedContext) state no such
// constraint and are deliberately absent: the GSN mapping emits
// `Solution -> ArtifactReference` with `SupportedBy -> AssertedInference`, so
// an inference legitimately carries ArtifactReference ends. A blanket
// tightening would reject conformant GSN-derived arguments.
void check_relationship_family_typing(const model::Document& document,
                                      const model::AssertedRelationship& relationship,
                                      std::vector<Diagnostic>& diagnostics) {
    const std::string_view family = metadata::kind_name(relationship.kind());
    // 11.15: "The source of AssertedEvidence relationships must be
    // ArtifactReference." 11.17/11.18: "The source and target of
    // AssertedArtifactSupport/AssertedArtifactContext must be of type
    // ArtifactReference."
    bool sources_must_be_references = false;
    bool targets_must_be_references = false;
    switch (relationship.kind()) {
    case model::ElementKind::AssertedEvidence:
        sources_must_be_references = true;
        break;
    case model::ElementKind::AssertedArtifactSupport:
    case model::ElementKind::AssertedArtifactContext:
        sources_must_be_references = true;
        targets_must_be_references = true;
        break;
    default:
        return;
    }

    const auto check_end = [&](const ElementId& end, std::string_view role) {
        const SACMElement* target = document.find(end);
        // A dangling or preserved-only end is already reported by
        // validate_structure; saying it twice in different words helps nobody.
        if (target == nullptr || target->kind() == model::ElementKind::ArtifactReference) {
            return;
        }
        diagnostics.push_back(make(codes::kRefWrongType,
                                   Severity::Error,
                                   "SACM23-ARG-002",
                                   {relationship.id(), end},
                                   std::format("the {} of {} '{}' is a {}; the clause requires ArtifactReference",
                                               role,
                                               family,
                                               relationship.id().value(),
                                               metadata::kind_name(target->kind()))));
    };
    if (sources_must_be_references) {
        for (const ElementId& source : relationship.sources()) {
            check_end(source, "source");
        }
    }
    if (targets_must_be_references) {
        for (const ElementId& target : relationship.targets()) {
            check_end(target, "target");
        }
    }
}

// Clauses 11.6/12.5 (interfaces) and 11.5/12.4 (bindings): everything an
// interface or a binding contains is a citation. The two clause families word
// it differently -- "only allowed with isCitation=true and +citedElement refer
// to ..." versus "must be ... citations to ..." -- but state the same rule, and
// both state it as a requirement rather than a recommendation.
void check_citation_only_content(const std::vector<const SACMElement*>& contents,
                                 const SACMElement& container,
                                 std::string_view clause,
                                 std::vector<Diagnostic>& diagnostics) {
    for (const SACMElement* child : contents) {
        if (child->is_citation() && child->cited_element().has_value()) {
            continue;
        }
        diagnostics.push_back(
            make(codes::kPackageContentInvalid,
                 Severity::Error,
                 "SACM23-PKG-002",
                 {container.id(), child->id()},
                 std::format("{} '{}' contains '{}', a {} that is not a citation; clause {} allows only "
                             "elements with isCitation=true and a citedElement",
                             metadata::kind_name(container.kind()),
                             container.id().value(),
                             child->id().value(),
                             metadata::kind_name(child->kind()),
                             clause)));
    }
}

// The other half of 11.6/12.5: an interface's citations point *into* the
// package it implements. Warning rather than error -- the locality rule is
// stated in prose with no OCL, and a citation whose target sits in a document
// this load never saw would otherwise be indistinguishable from a modelling
// mistake.
void check_interface_citation_locality(const model::Document& document,
                                       const std::vector<const SACMElement*>& contents,
                                       const SACMElement& interface_package,
                                       const std::optional<ElementId>& implements,
                                       std::string_view clause,
                                       std::vector<Diagnostic>& diagnostics) {
    if (!implements.has_value()) {
        return;
    }
    const SACMElement* implemented = document.find(*implements);
    if (implemented == nullptr) {
        return;
    }
    for (const SACMElement* child : contents) {
        if (!child->cited_element().has_value()) {
            continue;
        }
        const SACMElement* cited = document.find(*child->cited_element());
        if (cited == nullptr || is_contained_in(*cited, *implemented)) {
            continue;
        }
        diagnostics.push_back(
            make(codes::kPackageContentInvalid,
                 Severity::Warning,
                 "SACM23-PKG-002",
                 {interface_package.id(), child->id(), *child->cited_element()},
                 std::format("{} '{}' cites '{}', which is not inside the package '{}' it implements; clause "
                             "{} scopes an interface's citations to that package",
                             metadata::kind_name(interface_package.kind()),
                             interface_package.id().value(),
                             child->cited_element()->value(),
                             implements->value(),
                             clause)));
    }
}

// Clause 9.4's OCL admits a participant only if it is exactly an
// AssuranceCasePackage or an AssuranceCasePackageInterface, which excludes a
// binding; clause 10.5/10.6's parallel OCL uses `oclIsKindOf`, which admits
// one; clause 11.5's names the Interface subtype alone while its own
// Associations block declares the general package type. Three parallel clauses,
// three different rules for one concept.
//
// Resolution, applied uniformly and recorded in
// docs/sacm/sacm-decisions-and-questions.md: a binding is not a legal
// participant of a binding -- no clause gives a binding-of-bindings a meaning --
// but this is a warning, because one of the three clauses' own OCL permits it.
void check_binding_participants(const model::Document& document,
                                const SACMElement& binding,
                                const std::vector<ElementId>& participants,
                                std::vector<Diagnostic>& diagnostics) {
    for (const ElementId& participant : participants) {
        const SACMElement* target = document.find(participant);
        if (target == nullptr) {
            continue;
        }
        switch (target->kind()) {
        case model::ElementKind::AssuranceCasePackageBinding:
        case model::ElementKind::ArgumentPackageBinding:
        case model::ElementKind::ArtifactPackageBinding:
        case model::ElementKind::TerminologyPackageBinding:
            diagnostics.push_back(
                make(codes::kPackageContentInvalid,
                     Severity::Warning,
                     "SACM23-PKG-002",
                     {binding.id(), participant},
                     std::format("binding '{}' names '{}', itself a binding, as a participant package; the "
                                 "clause-9.4 OCL admits only a package or a package interface",
                                 binding.id().value(),
                                 participant.value())));
            break;
        default:
            break;
        }
    }
}

} // namespace

std::vector<Diagnostic> validate_structure(const model::Document& document) {
    std::vector<Diagnostic> diagnostics;

    // Unique ids (recomputed from the tree, independent of the index).
    std::unordered_set<ElementId> seen;
    document.for_each_element([&](const SACMElement& element) {
        if (element.id().empty()) {
            diagnostics.push_back(make(codes::kIdInvalid,
                                       Severity::Error,
                                       "SACM23-XMI-003",
                                       {element.id()},
                                       std::format("{} has an empty id", metadata::kind_name(element.kind()))));
            return;
        }
        if (!seen.insert(element.id()).second) {
            diagnostics.push_back(make(codes::kIdDuplicate,
                                       Severity::Error,
                                       "SACM23-XMI-003",
                                       {element.id()},
                                       std::format("duplicate element id '{}'", element.id().value())));
        }
    });

    // References resolve to elements of a legal kind.
    document.for_each_element([&](const SACMElement& element) {
        model::traverse::for_each_reference(element, [&](const model::traverse::ReferenceUse& use) {
            const SACMElement* target = document.find(*use.target);
            if (target == nullptr) {
                // A target held only as preserved compatibility content is
                // present in the document but absent from the index. It is
                // untyped, not missing, so it cannot be kind-checked -- but
                // calling it missing would mark an intact argument invalid
                // (SACM23-COMPAT-002).
                if (document.has_preserved_element(*use.target)) {
                    diagnostics.push_back(make(codes::kRefPreservedTarget,
                                               Severity::Warning,
                                               "SACM23-COMPAT-002",
                                               {element.id(), *use.target},
                                               std::format("'{}' references ({}) '{}', which was preserved as "
                                                           "compatibility content and therefore cannot be "
                                                           "type-checked",
                                                           element.id().value(),
                                                           use.role,
                                                           use.target->value())));
                    return;
                }
                diagnostics.push_back(make(codes::kRefDangling,
                                           Severity::Error,
                                           "SACM23-XMI-003",
                                           {element.id(), *use.target},
                                           std::format("'{}' references ({}) missing element '{}'",
                                                       element.id().value(),
                                                       use.role,
                                                       use.target->value())));
                return;
            }
            if (!reference_target_kind_ok(element, use.role, *target)) {
                diagnostics.push_back(make(codes::kRefWrongType,
                                           Severity::Error,
                                           "SACM23-ARG-002",
                                           {element.id(), *use.target},
                                           std::format("'{}' reference ({}) targets a {}, which is not a legal "
                                                       "target kind",
                                                       element.id().value(),
                                                       use.role,
                                                       metadata::kind_name(target->kind()))));
            }
        });
    });

    return diagnostics;
}

std::vector<Diagnostic> validate(const model::Document& document) {
    std::vector<Diagnostic> diagnostics = validate_structure(document);

    // gid uniqueness (clause 8.2: "a unique identifier that is unique within
    // the scope of the model instance"). Absent and empty gids are not
    // identities and are not compared -- clause 8.2 makes gid [0..1], and the
    // model keeps "absent" and "empty" distinct on purpose.
    std::unordered_map<std::string, ElementId> gid_owner;
    document.for_each_element([&](const SACMElement& element) {
        if (!element.gid().has_value() || element.gid()->empty()) {
            return;
        }
        const auto [existing, inserted] = gid_owner.emplace(*element.gid(), element.id());
        if (!inserted) {
            diagnostics.push_back(make(codes::kGidDuplicate,
                                       Severity::Error,
                                       "SACM23-BASE-002",
                                       {element.id(), existing->second},
                                       std::format("gid '{}' is used by both '{}' and '{}'; clause 8.2 requires it "
                                                   "to be unique within the model instance",
                                                   *element.gid(),
                                                   existing->second.value(),
                                                   element.id().value())));
        }
    });

    document.for_each_element([&](const SACMElement& element) {
        // Citation constraint (clause 8.2): citedElement implies isCitation.
        if (element.cited_element().has_value() && !element.is_citation()) {
            diagnostics.push_back(
                make(codes::kCitationInvalid,
                     Severity::Error,
                     "SACM23-BASE-002",
                     {element.id()},
                     std::format("'{}' has citedElement but isCitation is false", element.id().value())));
        }

        // abstractForm constraints (clause 8.2). The clause states the citing
        // element's own isAbstract flatly and the other two with "should", and
        // the severities follow that wording rather than flattening all three.
        if (element.abstract_form().has_value()) {
            if (element.is_abstract()) {
                diagnostics.push_back(make(codes::kAbstractnessInvalid,
                                           Severity::Error,
                                           "SACM23-BASE-002",
                                           {element.id()},
                                           std::format("'{}' uses abstractForm but is itself abstract; clause 8.2 "
                                                       "gives abstractForm to concrete elements",
                                                       element.id().value())));
            }
            if (const SACMElement* form = document.find(*element.abstract_form()); form != nullptr) {
                if (!form->is_abstract()) {
                    diagnostics.push_back(
                        make(codes::kAbstractnessInvalid,
                             Severity::Warning,
                             "SACM23-BASE-002",
                             {element.id(), form->id()},
                             std::format("'{}' names '{}' as its abstractForm, but '{}' is not abstract",
                                         element.id().value(),
                                         form->id().value(),
                                         form->id().value())));
                }
                if (form->kind() != element.kind()) {
                    diagnostics.push_back(
                        make(codes::kAbstractnessInvalid,
                             Severity::Warning,
                             "SACM23-BASE-002",
                             {element.id(), form->id()},
                             std::format("'{}' is a {} but names the {} '{}' as its abstractForm; clause 8.2 "
                                         "expects the same type",
                                         element.id().value(),
                                         metadata::kind_name(element.kind()),
                                         metadata::kind_name(form->kind()),
                                         form->id().value())));
                }
            }
        }

        if (const auto* model_element = dynamic_cast<const model::ModelElement*>(&element)) {
            // description is [0..1] per the specification text; the
            // machine-readable model allows more, so surplus is a warning.
            if (model_element->descriptions().size() > 1) {
                diagnostics.push_back(make(codes::kMultiplicityViolation,
                                           Severity::Warning,
                                           "SACM23-BASE-001",
                                           {element.id()},
                                           std::format("'{}' has {} descriptions; clause 8.6 allows at most one",
                                                       element.id().value(),
                                                       model_element->descriptions().size())));
            }
            // ImplementationConstraints only on abstract elements (clause 8.6).
            if (!model_element->implementation_constraints().empty() && !element.is_abstract()) {
                diagnostics.push_back(make(
                    codes::kCitationInvalid,
                    Severity::Error,
                    "SACM23-BASE-002",
                    {element.id()},
                    std::format("'{}' has implementation constraints but isAbstract is false", element.id().value())));
            }
        }

        // ExpressionLangString exclusivity (clause 8.4): "If expression is not
        // empty, then +content should be empty." A "should", so a warning --
        // and a real one, because the two carry the same meaning twice and a
        // reader has no rule for which wins.
        const auto check_expression_exclusivity = [&](const model::LangString& entry, std::string_view what) {
            if (entry.expression_ref.has_value() && !entry.content.empty()) {
                diagnostics.push_back(
                    make(codes::kExpressionContentConflict,
                         Severity::Warning,
                         "SACM23-BASE-001",
                         {element.id(), *entry.expression_ref},
                         std::format("'{}' {} references expression '{}' and also carries literal content; clause "
                                     "8.4 expects the content to be empty",
                                     element.id().value(),
                                     what,
                                     entry.expression_ref->value())));
            }
        };

        // MultiLangString language uniqueness (clause 8.5).
        const auto check_langs = [&](const model::MultiLangString& value, std::string_view what) {
            std::unordered_set<std::string> langs;
            for (const model::LangString& entry : value.values) {
                if (!langs.insert(entry.lang).second) {
                    diagnostics.push_back(
                        make(codes::kMultiplicityViolation,
                             Severity::Error,
                             "SACM23-BASE-001",
                             {element.id()},
                             std::format(
                                 "'{}' {} has duplicate language tag '{}'", element.id().value(), what, entry.lang)));
                }
                check_expression_exclusivity(entry, what);
            }
        };
        if (const auto* utility = dynamic_cast<const model::UtilityElement*>(&element)) {
            check_langs(utility->content(), "content");
        }
        if (const auto* tagged = dynamic_cast<const model::TaggedValue*>(&element)) {
            check_langs(tagged->key(), "key");
        }
        if (const auto* named = dynamic_cast<const model::ModelElement*>(&element)) {
            check_expression_exclusivity(named->name(), "name");
        }

        // Expression concreteness (clause 10.10 OCL:
        // self.isAbstract = false implies self.element->forall(e|e.isAbstract = false)).
        if (const auto* expression = dynamic_cast<const model::Expression*>(&element);
            expression != nullptr && !expression->is_abstract()) {
            for (const ElementId& part : expression->elements()) {
                const SACMElement* referenced = document.find(part);
                if (referenced == nullptr || !referenced->is_abstract()) {
                    continue;
                }
                diagnostics.push_back(
                    make(codes::kAbstractnessInvalid,
                         Severity::Error,
                         "SACM23-TERM-001",
                         {element.id(), part},
                         std::format("concrete Expression '{}' references the abstract element '{}'; clause 10.10 "
                                     "allows a concrete expression only concrete parts",
                                     element.id().value(),
                                     part.value())));
            }
        }

        // Asserted relationship multiplicities (clause 11.13:
        // source:ArgumentAsset[1..*], target:ArgumentAsset[1]).
        if (const auto* rel = dynamic_cast<const model::AssertedRelationship*>(&element)) {
            if (rel->sources().empty() || rel->targets().empty()) {
                diagnostics.push_back(
                    make(codes::kMultiplicityViolation,
                         Severity::Error,
                         "SACM23-ARG-002",
                         {element.id()},
                         std::format("'{}' must have at least one source and one target", element.id().value())));
            }
            // The upper bound is the half that costs something: a second target
            // round-trips, draws, and reads as a relationship with two
            // conclusions, which 11.13 does not define.
            if (rel->targets().size() > 1) {
                diagnostics.push_back(make(codes::kMultiplicityViolation,
                                           Severity::Error,
                                           "SACM23-ARG-002",
                                           {element.id()},
                                           std::format("'{}' has {} targets; clause 11.13 declares target[1]",
                                                       element.id().value(),
                                                       rel->targets().size())));
            }
            check_relationship_family_typing(document, *rel, diagnostics);
        }

        // ArgumentPackage content homogeneity (clause 11.4: "If an
        // ArgumentPackage has nested ArgumentPackages, then it is only allowed
        // to contain ArgumentPackages").
        //
        // A contained interface or binding does not trip it. Clause 11.6
        // requires an ArgumentPackageInterface to reside *inside* the package
        // it describes, so reading 11.4 literally would make every non-empty
        // package that declares an interface non-conformant by construction --
        // two clauses of the same specification cannot both be satisfied. The
        // resolution is recorded in docs/sacm/sacm-decisions-and-questions.md.
        if (element.kind() == model::ElementKind::ArgumentPackage) {
            const auto& package = static_cast<const model::ArgumentPackage&>(element);
            const auto& contents = package.argument_elements();
            const bool nests_a_plain_package =
                std::ranges::any_of(contents, [](const std::unique_ptr<model::ArgumentationElement>& child) {
                    return child->kind() == model::ElementKind::ArgumentPackage;
                });
            if (nests_a_plain_package) {
                for (const std::unique_ptr<model::ArgumentationElement>& child : contents) {
                    if (dynamic_cast<const model::ArgumentPackage*>(child.get()) != nullptr) {
                        continue;
                    }
                    diagnostics.push_back(
                        make(codes::kPackageContentInvalid,
                             Severity::Error,
                             "SACM23-ARG-002",
                             {element.id(), child->id()},
                             std::format("ArgumentPackage '{}' nests an ArgumentPackage, so clause 11.4 allows it "
                                         "to contain only ArgumentPackages; '{}' is a {}",
                                         element.id().value(),
                                         child->id().value(),
                                         metadata::kind_name(child->kind()))));
                }
            }
        }

        // Interface and binding content (clauses 9.3, 11.5, 11.6, 12.4, 12.5).
        const auto argument_contents = [](const model::ArgumentPackage& package) {
            std::vector<const SACMElement*> out;
            for (const std::unique_ptr<model::ArgumentationElement>& child : package.argument_elements()) {
                out.push_back(child.get());
            }
            return out;
        };
        const auto artifact_contents = [](const model::ArtifactPackage& package) {
            std::vector<const SACMElement*> out;
            for (const std::unique_ptr<model::ArtifactElement>& child : package.artifact_elements()) {
                out.push_back(child.get());
            }
            return out;
        };
        switch (element.kind()) {
        case model::ElementKind::ArgumentPackageInterface: {
            const auto& interface_package = static_cast<const model::ArgumentPackageInterface&>(element);
            const std::vector<const SACMElement*> contents = argument_contents(interface_package);
            check_citation_only_content(contents, element, "11.6", diagnostics);
            check_interface_citation_locality(
                document, contents, element, interface_package.implements(), "11.6", diagnostics);
            break;
        }
        case model::ElementKind::ArtifactPackageInterface: {
            const auto& interface_package = static_cast<const model::ArtifactPackageInterface&>(element);
            const std::vector<const SACMElement*> contents = artifact_contents(interface_package);
            check_citation_only_content(contents, element, "12.5", diagnostics);
            check_interface_citation_locality(
                document, contents, element, interface_package.implements(), "12.5", diagnostics);
            break;
        }
        case model::ElementKind::ArgumentPackageBinding:
            // The locality half ("citations to ArgumentElements contained
            // within the ArgumentPackages associated by participantPackage") is
            // deliberately not checked: a participant may be named as an
            // interface that itself resides inside the package the citation
            // targets, so the containment test has two legitimate answers.
            // Recorded rather than approximated.
            check_citation_only_content(argument_contents(static_cast<const model::ArgumentPackageBinding&>(element)),
                                        element,
                                        "11.5",
                                        diagnostics);
            break;
        case model::ElementKind::ArtifactPackageBinding:
            check_citation_only_content(artifact_contents(static_cast<const model::ArtifactPackageBinding&>(element)),
                                        element,
                                        "12.4",
                                        diagnostics);
            break;
        case model::ElementKind::AssuranceCasePackageInterface: {
            // Clause 9.3 OCL: an AssuranceCasePackageInterface contains only
            // interface-typed sub-packages. TerminologyPackage is excluded --
            // the clause's own prose says "TerminologyPackages" where its OCL
            // says the interface subtype, and the divergence is recorded rather
            // than resolved by guessing.
            const auto& interface_package = static_cast<const model::AssuranceCasePackageInterface&>(element);
            const auto require_interface = [&](const SACMElement& child, model::ElementKind expected) {
                if (child.kind() == expected) {
                    return;
                }
                diagnostics.push_back(
                    make(codes::kPackageContentInvalid,
                         Severity::Error,
                         "SACM23-PKG-002",
                         {element.id(), child.id()},
                         std::format("AssuranceCasePackageInterface '{}' contains the {} '{}'; clause 9.3 allows "
                                     "only {}",
                                     element.id().value(),
                                     metadata::kind_name(child.kind()),
                                     child.id().value(),
                                     metadata::kind_name(expected))));
            };
            for (const auto& child : interface_package.assurance_case_packages()) {
                require_interface(*child, model::ElementKind::AssuranceCasePackageInterface);
            }
            for (const auto& child : interface_package.argument_packages()) {
                require_interface(*child, model::ElementKind::ArgumentPackageInterface);
            }
            for (const auto& child : interface_package.artifact_packages()) {
                require_interface(*child, model::ElementKind::ArtifactPackageInterface);
            }
            break;
        }
        default:
            break;
        }
        if (const auto* rel = dynamic_cast<const model::ArtifactAssetRelationship*>(&element)) {
            if (rel->sources().empty() || rel->targets().empty()) {
                diagnostics.push_back(
                    make(codes::kMultiplicityViolation,
                         Severity::Error,
                         "SACM23-ART-001",
                         {element.id()},
                         std::format("'{}' must have at least one source and one target", element.id().value())));
            }
        }

        // Binding participant multiplicity [2..*] (clauses 9.4/10/11/12) and
        // participant typing (clause 9.4's OCL).
        const auto check_participants = [&](const std::vector<ElementId>& participants) {
            if (participants.size() < 2) {
                diagnostics.push_back(
                    make(codes::kMultiplicityViolation,
                         Severity::Error,
                         "SACM23-PKG-002",
                         {element.id()},
                         std::format("binding '{}' needs at least two participant packages", element.id().value())));
            }
            check_binding_participants(document, element, participants, diagnostics);
        };
        switch (element.kind()) {
        case model::ElementKind::AssuranceCasePackageBinding:
            check_participants(static_cast<const model::AssuranceCasePackageBinding&>(element).participant_packages());
            break;
        case model::ElementKind::ArgumentPackageBinding:
            check_participants(static_cast<const model::ArgumentPackageBinding&>(element).participant_packages());
            break;
        case model::ElementKind::ArtifactPackageBinding:
            check_participants(static_cast<const model::ArtifactPackageBinding&>(element).participant_packages());
            break;
        case model::ElementKind::TerminologyPackageBinding:
            check_participants(static_cast<const model::TerminologyPackageBinding&>(element).participant_packages());
            break;
        default:
            break;
        }
    });

    return diagnostics;
}

} // namespace sacm::validation
