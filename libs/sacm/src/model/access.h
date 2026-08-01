#pragma once

// Internal mutation gateway (not installed). Model fields are private; the
// command layer and the XMI reader mutate documents exclusively through
// this struct, keeping "mutation only via Document::apply or load"
// mechanically enforced at the public API.
//
// One further writer: `sacm::compat::adopt_preserved_content`. It is part of
// the load-side story -- it restores content a load produced onto a document a
// client rebuilt from a lossy intermediate -- and its contract is narrow:
// compatibility content only, never the typed model. See the note in
// `sacm/model/document.h`.

#include "sacm/model/assurance_case.h"
#include "sacm/model/document.h"
#include "sacm/model/element.h"

namespace sacm::detail {

struct Access {
    // --- SACMElement ---
    static void set_id(model::SACMElement& e, model::ElementId id) {
        e.id_ = std::move(id);
    }
    static std::optional<std::string>& gid(model::SACMElement& e) {
        return e.gid_;
    }
    static bool& is_citation(model::SACMElement& e) {
        return e.is_citation_;
    }
    static bool& is_abstract(model::SACMElement& e) {
        return e.is_abstract_;
    }
    static std::optional<model::ElementId>& cited_element(model::SACMElement& e) {
        return e.cited_element_;
    }
    static std::optional<model::ElementId>& abstract_form(model::SACMElement& e) {
        return e.abstract_form_;
    }
    static void set_parent(model::SACMElement& e, model::SACMElement* parent) {
        e.parent_ = parent;
    }
    static std::vector<std::string>& preserved_attributes(model::SACMElement& e) {
        return e.preserved_attributes_;
    }
    static std::vector<model::PreservedFragment>& preserved_content(model::SACMElement& e) {
        return e.preserved_content_;
    }

    // --- UtilityElement / TaggedValue ---
    static model::MultiLangString& content(model::UtilityElement& e) {
        return e.content_;
    }
    static model::MultiLangString& key(model::TaggedValue& e) {
        return e.key_;
    }

    // --- ModelElement ---
    static model::LangString& name(model::ModelElement& e) {
        return e.name_;
    }
    static std::vector<std::unique_ptr<model::Description>>& descriptions(model::ModelElement& e) {
        return e.descriptions_;
    }
    static std::vector<std::unique_ptr<model::ImplementationConstraint>>&
    implementation_constraints(model::ModelElement& e) {
        return e.implementation_constraints_;
    }
    static std::vector<std::unique_ptr<model::Note>>& notes(model::ModelElement& e) {
        return e.notes_;
    }
    static std::vector<std::unique_ptr<model::TaggedValue>>& tagged_values(model::ModelElement& e) {
        return e.tagged_values_;
    }

    // --- AssuranceCase ---
    static std::vector<model::ElementId>& interfaces(model::AssuranceCasePackage& e) {
        return e.interfaces_;
    }
    static std::vector<std::unique_ptr<model::AssuranceCasePackage>>&
    assurance_case_packages(model::AssuranceCasePackage& e) {
        return e.assurance_case_packages_;
    }
    static std::vector<std::unique_ptr<model::ArgumentPackage>>& argument_packages(model::AssuranceCasePackage& e) {
        return e.argument_packages_;
    }
    static std::vector<std::unique_ptr<model::ArtifactPackage>>& artifact_packages(model::AssuranceCasePackage& e) {
        return e.artifact_packages_;
    }
    static std::vector<std::unique_ptr<model::TerminologyPackage>>&
    terminology_packages(model::AssuranceCasePackage& e) {
        return e.terminology_packages_;
    }
    static std::optional<model::ElementId>& implements(model::AssuranceCasePackageInterface& e) {
        return e.implements_;
    }
    static std::vector<model::ElementId>& participant_packages(model::AssuranceCasePackageBinding& e) {
        return e.participant_packages_;
    }

    // --- Terminology ---
    static std::vector<model::ElementId>& categories(model::Category& e) {
        return e.categories_;
    }
    static std::string& value(model::ExpressionElement& e) {
        return e.value_;
    }
    static std::vector<model::ElementId>& categories(model::ExpressionElement& e) {
        return e.categories_;
    }
    static std::vector<model::ElementId>& elements(model::Expression& e) {
        return e.elements_;
    }
    static std::string& external_reference(model::Term& e) {
        return e.external_reference_;
    }
    static std::optional<model::ElementId>& origin(model::Term& e) {
        return e.origin_;
    }
    static std::vector<model::ElementId>& terminology_elements(model::TerminologyGroup& e) {
        return e.terminology_elements_;
    }
    static std::vector<model::ElementId>& interfaces(model::TerminologyPackage& e) {
        return e.interfaces_;
    }
    static std::vector<std::unique_ptr<model::TerminologyElement>>& terminology_elements(model::TerminologyPackage& e) {
        return e.terminology_elements_;
    }
    static std::optional<model::ElementId>& implements(model::TerminologyPackageInterface& e) {
        return e.implements_;
    }
    static std::vector<model::ElementId>& participant_packages(model::TerminologyPackageBinding& e) {
        return e.participant_packages_;
    }

    // --- Argumentation ---
    static std::vector<model::ElementId>& interfaces(model::ArgumentPackage& e) {
        return e.interfaces_;
    }
    static std::vector<std::unique_ptr<model::ArgumentationElement>>& argument_elements(model::ArgumentPackage& e) {
        return e.argument_elements_;
    }
    static std::optional<model::ElementId>& implements(model::ArgumentPackageInterface& e) {
        return e.implements_;
    }
    static std::vector<model::ElementId>& participant_packages(model::ArgumentPackageBinding& e) {
        return e.participant_packages_;
    }
    static std::vector<model::ElementId>& argument_elements(model::ArgumentGroup& e) {
        return e.argument_elements_;
    }
    static std::optional<model::ElementId>& structure(model::ArgumentReasoning& e) {
        return e.structure_;
    }
    static std::vector<model::ElementId>& referenced_artifact_elements(model::ArtifactReference& e) {
        return e.referenced_artifact_elements_;
    }
    static model::AssertionDeclaration& assertion_declaration(model::Assertion& e) {
        return e.assertion_declaration_;
    }
    static std::vector<model::ElementId>& meta_claims(model::Assertion& e) {
        return e.meta_claims_;
    }
    static bool& is_counter(model::AssertedRelationship& e) {
        return e.is_counter_;
    }
    static std::optional<model::ElementId>& reasoning(model::AssertedRelationship& e) {
        return e.reasoning_;
    }
    static std::vector<model::ElementId>& sources(model::AssertedRelationship& e) {
        return e.sources_;
    }
    static std::vector<model::ElementId>& targets(model::AssertedRelationship& e) {
        return e.targets_;
    }

    // --- Artifact ---
    static std::vector<std::unique_ptr<model::Property>>& properties(model::ArtifactAsset& e) {
        return e.properties_;
    }
    static std::string& version(model::Artifact& e) {
        return e.version_;
    }
    static std::string& date(model::Artifact& e) {
        return e.date_;
    }
    static std::string& start_time(model::Activity& e) {
        return e.start_time_;
    }
    static std::string& end_time(model::Activity& e) {
        return e.end_time_;
    }
    static std::string& date(model::Event& e) {
        return e.date_;
    }
    static std::vector<model::ElementId>& sources(model::ArtifactAssetRelationship& e) {
        return e.sources_;
    }
    static std::vector<model::ElementId>& targets(model::ArtifactAssetRelationship& e) {
        return e.targets_;
    }
    static std::vector<model::ElementId>& artifact_elements(model::ArtifactGroup& e) {
        return e.artifact_elements_;
    }
    static std::vector<model::ElementId>& interfaces(model::ArtifactPackage& e) {
        return e.interfaces_;
    }
    static std::vector<std::unique_ptr<model::ArtifactElement>>& artifact_elements(model::ArtifactPackage& e) {
        return e.artifact_elements_;
    }
    static std::optional<model::ElementId>& implements(model::ArtifactPackageInterface& e) {
        return e.implements_;
    }
    static std::vector<model::ElementId>& participant_packages(model::ArtifactPackageBinding& e) {
        return e.participant_packages_;
    }

    // --- Document ---
    static std::vector<std::unique_ptr<model::AssuranceCasePackage>>& roots(model::Document& d) {
        return d.roots_;
    }
    static std::vector<std::unique_ptr<model::SACMElement>>& other_roots(model::Document& d) {
        return d.other_roots_;
    }
    static std::unordered_map<model::ElementId, model::SACMElement*>& index(model::Document& d) {
        return d.index_;
    }
    static std::map<model::ElementKind, std::uint64_t>& id_counters(model::Document& d) {
        return d.id_counters_;
    }
    static const std::map<model::ElementKind, std::uint64_t>& id_counters(const model::Document& d) {
        return d.id_counters_;
    }
    static std::map<std::string, std::string>& foreign_namespaces(model::Document& d) {
        return d.foreign_namespaces_;
    }
    static std::unordered_set<model::ElementId>& preserved_element_ids(model::Document& d) {
        return d.preserved_element_ids_;
    }
    static std::uint64_t& revision(model::Document& d) {
        return d.revision_;
    }
};

} // namespace sacm::detail
