#include "model/traverse.h"

#include "model/access.h"

#include "sacm/model/assurance_case.h"

#include <algorithm>

namespace sacm::model::traverse {

void for_each_child(const SACMElement& element, const std::function<void(const SACMElement&)>& fn) {
    // ModelElement utility containment (name is a value type, not a child).
    if (const auto* model_element = dynamic_cast<const ModelElement*>(&element)) {
        for (const auto& child : model_element->descriptions()) fn(*child);
        for (const auto& child : model_element->implementation_constraints()) fn(*child);
        for (const auto& child : model_element->notes()) fn(*child);
        for (const auto& child : model_element->tagged_values()) fn(*child);
    }
    if (const auto* asset = dynamic_cast<const ArtifactAsset*>(&element)) {
        for (const auto& child : asset->properties()) fn(*child);
    }
    if (const auto* acp = dynamic_cast<const AssuranceCasePackage*>(&element)) {
        for (const auto& child : acp->assurance_case_packages()) fn(*child);
        for (const auto& child : acp->argument_packages()) fn(*child);
        for (const auto& child : acp->artifact_packages()) fn(*child);
        for (const auto& child : acp->terminology_packages()) fn(*child);
    }
    if (const auto* pkg = dynamic_cast<const ArgumentPackage*>(&element)) {
        for (const auto& child : pkg->argument_elements()) fn(*child);
    }
    if (const auto* pkg = dynamic_cast<const ArtifactPackage*>(&element)) {
        for (const auto& child : pkg->artifact_elements()) fn(*child);
    }
    if (const auto* pkg = dynamic_cast<const TerminologyPackage*>(&element)) {
        for (const auto& child : pkg->terminology_elements()) fn(*child);
    }
}

void for_each_child(SACMElement& element, const std::function<void(SACMElement&)>& fn) {
    // Children are owned by `element`; the const walk visits the same
    // objects, so casting constness away here is sound.
    for_each_child(std::as_const(element), [&fn](const SACMElement& child) {
        fn(const_cast<SACMElement&>(child));
    });
}

void for_each_descendant(const SACMElement& element,
                         const std::function<void(const SACMElement&)>& fn) {
    fn(element);
    for_each_child(element, [&fn](const SACMElement& child) { for_each_descendant(child, fn); });
}

namespace {

void visit_lang_string_refs(const LangString& value, std::string_view role,
                            const std::function<void(const ReferenceUse&)>& fn) {
    if (value.expression_ref.has_value()) {
        fn(ReferenceUse{role, &*value.expression_ref});
    }
}

void visit_multi_lang_refs(const MultiLangString& value, std::string_view role,
                           const std::function<void(const ReferenceUse&)>& fn) {
    for (const LangString& entry : value.values) {
        visit_lang_string_refs(entry, role, fn);
    }
}

void emit(const std::optional<ElementId>& target, std::string_view role,
          const std::function<void(const ReferenceUse&)>& fn) {
    if (target.has_value()) {
        fn(ReferenceUse{role, &*target});
    }
}

void emit_all(const std::vector<ElementId>& targets, std::string_view role,
              const std::function<void(const ReferenceUse&)>& fn) {
    for (const ElementId& target : targets) {
        fn(ReferenceUse{role, &target});
    }
}

}  // namespace

void for_each_reference(const SACMElement& element,
                        const std::function<void(const ReferenceUse&)>& fn) {
    emit(element.cited_element(), "citedElement", fn);
    emit(element.abstract_form(), "abstractForm", fn);

    if (const auto* utility = dynamic_cast<const UtilityElement*>(&element)) {
        visit_multi_lang_refs(utility->content(), "expression", fn);
    }
    if (const auto* tagged = dynamic_cast<const TaggedValue*>(&element)) {
        visit_multi_lang_refs(tagged->key(), "expression", fn);
    }
    if (const auto* model_element = dynamic_cast<const ModelElement*>(&element)) {
        visit_lang_string_refs(model_element->name(), "expression", fn);
    }

    switch (element.kind()) {
        case ElementKind::AssuranceCasePackage:
        case ElementKind::AssuranceCasePackageInterface:
        case ElementKind::AssuranceCasePackageBinding: {
            const auto& acp = static_cast<const AssuranceCasePackage&>(element);
            emit_all(acp.interfaces(), "interface", fn);
            if (element.kind() == ElementKind::AssuranceCasePackageInterface) {
                emit(static_cast<const AssuranceCasePackageInterface&>(element).implements(),
                     "implements", fn);
            }
            if (element.kind() == ElementKind::AssuranceCasePackageBinding) {
                emit_all(
                    static_cast<const AssuranceCasePackageBinding&>(element).participant_packages(),
                    "participantPackage", fn);
            }
            break;
        }
        case ElementKind::TerminologyPackage:
        case ElementKind::TerminologyPackageInterface:
        case ElementKind::TerminologyPackageBinding: {
            const auto& pkg = static_cast<const TerminologyPackage&>(element);
            emit_all(pkg.interfaces(), "interface", fn);
            if (element.kind() == ElementKind::TerminologyPackageInterface) {
                emit(static_cast<const TerminologyPackageInterface&>(element).implements(),
                     "implements", fn);
            }
            if (element.kind() == ElementKind::TerminologyPackageBinding) {
                emit_all(
                    static_cast<const TerminologyPackageBinding&>(element).participant_packages(),
                    "participantPackage", fn);
            }
            break;
        }
        case ElementKind::TerminologyGroup:
            emit_all(static_cast<const TerminologyGroup&>(element).terminology_elements(),
                     "terminologyElement", fn);
            break;
        case ElementKind::Category:
            emit_all(static_cast<const Category&>(element).categories(), "category", fn);
            break;
        case ElementKind::Expression: {
            const auto& expression = static_cast<const Expression&>(element);
            emit_all(expression.categories(), "category", fn);
            emit_all(expression.elements(), "element", fn);
            break;
        }
        case ElementKind::Term: {
            const auto& term = static_cast<const Term&>(element);
            emit_all(term.categories(), "category", fn);
            emit(term.origin(), "origin", fn);
            break;
        }
        case ElementKind::ArgumentPackage:
        case ElementKind::ArgumentPackageInterface:
        case ElementKind::ArgumentPackageBinding: {
            const auto& pkg = static_cast<const ArgumentPackage&>(element);
            emit_all(pkg.interfaces(), "interface", fn);
            if (element.kind() == ElementKind::ArgumentPackageInterface) {
                emit(static_cast<const ArgumentPackageInterface&>(element).implements(),
                     "implements", fn);
            }
            if (element.kind() == ElementKind::ArgumentPackageBinding) {
                emit_all(static_cast<const ArgumentPackageBinding&>(element).participant_packages(),
                         "participantPackage", fn);
            }
            break;
        }
        case ElementKind::ArgumentGroup:
            emit_all(static_cast<const ArgumentGroup&>(element).argument_elements(),
                     "argumentElement", fn);
            break;
        case ElementKind::ArgumentReasoning:
            emit(static_cast<const ArgumentReasoning&>(element).structure(), "structure", fn);
            break;
        case ElementKind::ArtifactReference:
            emit_all(static_cast<const ArtifactReference&>(element).referenced_artifact_elements(),
                     "referencedArtifactElement", fn);
            break;
        case ElementKind::Claim:
        case ElementKind::AssertedInference:
        case ElementKind::AssertedEvidence:
        case ElementKind::AssertedContext:
        case ElementKind::AssertedArtifactSupport:
        case ElementKind::AssertedArtifactContext: {
            const auto& assertion = static_cast<const Assertion&>(element);
            emit_all(assertion.meta_claims(), "metaClaim", fn);
            if (const auto* rel = dynamic_cast<const AssertedRelationship*>(&element)) {
                emit(rel->reasoning(), "reasoning", fn);
                emit_all(rel->sources(), "source", fn);
                emit_all(rel->targets(), "target", fn);
            }
            break;
        }
        case ElementKind::ArtifactAssetRelationship: {
            const auto& rel = static_cast<const ArtifactAssetRelationship&>(element);
            emit_all(rel.sources(), "source", fn);
            emit_all(rel.targets(), "target", fn);
            break;
        }
        case ElementKind::ArtifactGroup:
            emit_all(static_cast<const ArtifactGroup&>(element).artifact_elements(),
                     "artifactElement", fn);
            break;
        case ElementKind::ArtifactPackage:
        case ElementKind::ArtifactPackageInterface:
        case ElementKind::ArtifactPackageBinding: {
            const auto& pkg = static_cast<const ArtifactPackage&>(element);
            emit_all(pkg.interfaces(), "interface", fn);
            if (element.kind() == ElementKind::ArtifactPackageInterface) {
                emit(static_cast<const ArtifactPackageInterface&>(element).implements(),
                     "implements", fn);
            }
            if (element.kind() == ElementKind::ArtifactPackageBinding) {
                emit_all(static_cast<const ArtifactPackageBinding&>(element).participant_packages(),
                         "participantPackage", fn);
            }
            break;
        }
        default:
            break;
    }
}

namespace {

std::size_t erase_doomed(std::vector<ElementId>& ids, const std::unordered_set<ElementId>& doomed) {
    const std::size_t before = ids.size();
    std::erase_if(ids, [&doomed](const ElementId& id) { return doomed.contains(id); });
    return before - ids.size();
}

std::size_t clear_doomed(std::optional<ElementId>& id, const std::unordered_set<ElementId>& doomed) {
    if (id.has_value() && doomed.contains(*id)) {
        id.reset();
        return 1;
    }
    return 0;
}

std::size_t clear_lang_string(LangString& value, const std::unordered_set<ElementId>& doomed) {
    if (value.expression_ref.has_value() && doomed.contains(*value.expression_ref)) {
        value.expression_ref.reset();
        return 1;
    }
    return 0;
}

std::size_t clear_multi_lang(MultiLangString& value, const std::unordered_set<ElementId>& doomed) {
    std::size_t removed = 0;
    for (LangString& entry : value.values) {
        removed += clear_lang_string(entry, doomed);
    }
    return removed;
}

}  // namespace

std::size_t remove_references_to(SACMElement& referrer,
                                 const std::unordered_set<ElementId>& doomed) {
    using detail::Access;
    std::size_t removed = 0;
    removed += clear_doomed(Access::cited_element(referrer), doomed);
    removed += clear_doomed(Access::abstract_form(referrer), doomed);

    if (auto* utility = dynamic_cast<UtilityElement*>(&referrer)) {
        removed += clear_multi_lang(Access::content(*utility), doomed);
    }
    if (auto* tagged = dynamic_cast<TaggedValue*>(&referrer)) {
        removed += clear_multi_lang(Access::key(*tagged), doomed);
    }
    if (auto* model_element = dynamic_cast<ModelElement*>(&referrer)) {
        removed += clear_lang_string(Access::name(*model_element), doomed);
    }

    if (auto* acp = dynamic_cast<AssuranceCasePackage*>(&referrer)) {
        removed += erase_doomed(Access::interfaces(*acp), doomed);
    }
    if (auto* iface = dynamic_cast<AssuranceCasePackageInterface*>(&referrer)) {
        removed += clear_doomed(Access::implements(*iface), doomed);
    }
    if (auto* binding = dynamic_cast<AssuranceCasePackageBinding*>(&referrer)) {
        removed += erase_doomed(Access::participant_packages(*binding), doomed);
    }
    if (auto* pkg = dynamic_cast<TerminologyPackage*>(&referrer)) {
        removed += erase_doomed(Access::interfaces(*pkg), doomed);
    }
    if (auto* iface = dynamic_cast<TerminologyPackageInterface*>(&referrer)) {
        removed += clear_doomed(Access::implements(*iface), doomed);
    }
    if (auto* binding = dynamic_cast<TerminologyPackageBinding*>(&referrer)) {
        removed += erase_doomed(Access::participant_packages(*binding), doomed);
    }
    if (auto* group = dynamic_cast<TerminologyGroup*>(&referrer)) {
        removed += erase_doomed(Access::terminology_elements(*group), doomed);
    }
    if (auto* category = dynamic_cast<Category*>(&referrer)) {
        removed += erase_doomed(Access::categories(*category), doomed);
    }
    if (auto* expr_element = dynamic_cast<ExpressionElement*>(&referrer)) {
        removed += erase_doomed(Access::categories(*expr_element), doomed);
    }
    if (auto* expression = dynamic_cast<Expression*>(&referrer)) {
        removed += erase_doomed(Access::elements(*expression), doomed);
    }
    if (auto* term = dynamic_cast<Term*>(&referrer)) {
        removed += clear_doomed(Access::origin(*term), doomed);
    }
    if (auto* pkg = dynamic_cast<ArgumentPackage*>(&referrer)) {
        removed += erase_doomed(Access::interfaces(*pkg), doomed);
    }
    if (auto* iface = dynamic_cast<ArgumentPackageInterface*>(&referrer)) {
        removed += clear_doomed(Access::implements(*iface), doomed);
    }
    if (auto* binding = dynamic_cast<ArgumentPackageBinding*>(&referrer)) {
        removed += erase_doomed(Access::participant_packages(*binding), doomed);
    }
    if (auto* group = dynamic_cast<ArgumentGroup*>(&referrer)) {
        removed += erase_doomed(Access::argument_elements(*group), doomed);
    }
    if (auto* reasoning = dynamic_cast<ArgumentReasoning*>(&referrer)) {
        removed += clear_doomed(Access::structure(*reasoning), doomed);
    }
    if (auto* reference = dynamic_cast<ArtifactReference*>(&referrer)) {
        removed += erase_doomed(Access::referenced_artifact_elements(*reference), doomed);
    }
    if (auto* assertion = dynamic_cast<Assertion*>(&referrer)) {
        removed += erase_doomed(Access::meta_claims(*assertion), doomed);
    }
    if (auto* rel = dynamic_cast<AssertedRelationship*>(&referrer)) {
        removed += clear_doomed(Access::reasoning(*rel), doomed);
        removed += erase_doomed(Access::sources(*rel), doomed);
        removed += erase_doomed(Access::targets(*rel), doomed);
    }
    if (auto* rel = dynamic_cast<ArtifactAssetRelationship*>(&referrer)) {
        removed += erase_doomed(Access::sources(*rel), doomed);
        removed += erase_doomed(Access::targets(*rel), doomed);
    }
    if (auto* group = dynamic_cast<ArtifactGroup*>(&referrer)) {
        removed += erase_doomed(Access::artifact_elements(*group), doomed);
    }
    if (auto* pkg = dynamic_cast<ArtifactPackage*>(&referrer)) {
        removed += erase_doomed(Access::interfaces(*pkg), doomed);
    }
    if (auto* iface = dynamic_cast<ArtifactPackageInterface*>(&referrer)) {
        removed += clear_doomed(Access::implements(*iface), doomed);
    }
    if (auto* binding = dynamic_cast<ArtifactPackageBinding*>(&referrer)) {
        removed += erase_doomed(Access::participant_packages(*binding), doomed);
    }
    return removed;
}

const SACMElement* nearest_package(const SACMElement& element) {
    for (const SACMElement* current = &element; current != nullptr; current = current->parent()) {
        if (metadata::is_package_kind(current->kind())) {
            return current;
        }
    }
    return nullptr;
}

}  // namespace sacm::model::traverse
