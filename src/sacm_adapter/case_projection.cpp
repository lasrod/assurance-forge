#include "sacm_adapter/case_projection.h"

#include "sacm_adapter/gsn_role_tag.h"
#include "sacm_adapter/library_document_access.h"

#include "sacm/model/argumentation.h"
#include "sacm/model/artifact.h"
#include "sacm/model/assurance_case.h"
#include "sacm/model/element.h"
#include "sacm/model/terminology.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace sacm_adapter {

namespace {

// The POD model spells types as the lowercased SACM class name
// ("claim", "assertedinference"), so mirror that rather than inventing a
// second vocabulary the comparison would then have to reconcile.
std::string lowercase_kind_name(sacm::metadata::ElementKind kind) {
    std::string name(sacm::metadata::kind_name(kind));
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return name;
}

// The legacy POD keys language maps by tag, defaulting an absent tag to "en"
// (xml_parser.cpp extract_name/extract_description). Mirror that so the language
// toggle sees the same keys.
void fill_lang_map(std::map<std::string, std::string>& out,
                   const sacm::model::MultiLangString& value) {
    for (const sacm::model::LangString& entry : value.values) {
        if (entry.content.empty()) {
            continue;
        }
        out[entry.lang.empty() ? "en" : entry.lang] = entry.content;
    }
}

std::vector<std::string> to_id_strings(const std::vector<sacm::model::ElementId>& ids) {
    std::vector<std::string> out;
    out.reserve(ids.size());
    for (const sacm::model::ElementId& id : ids) {
        out.push_back(id.value());
    }
    return out;
}

// Assurance Claim Points are not a SACM concept: the application encodes them as
// vendor TaggedValues keyed "assuranceForge.acp[.<id>.<field>]" (clause 8.12
// extension mechanism), which the library preserves. Synthesize the POD records
// from them exactly as the legacy parser's extract_acps does, so ACP support
// survives the projection. Keyed by TaggedValue key -> value.
std::string tagged_value_for(const sacm::model::ModelElement& element, const std::string& key) {
    for (const auto& tag : element.tagged_values()) {
        if (tag->key().primary() == key) {
            return std::string(tag->content().primary());
        }
    }
    return {};
}

void collect_acps(const sacm::model::ModelElement& element, std::string_view target_kind,
                  std::vector<core::AcpRecord>& out) {
    std::unordered_set<std::string> seen;
    for (const auto& tag : element.tagged_values()) {
        if (tag->key().primary() != "assuranceForge.acp") {
            continue;
        }
        const std::string acp_id(tag->content().primary());
        if (acp_id.empty() || !seen.insert(acp_id).second) {
            continue;
        }
        const auto field = [&](const char* name) {
            return tagged_value_for(element, "assuranceForge.acp." + acp_id + "." + name);
        };
        core::AcpRecord acp;
        acp.id = acp_id;
        acp.name = field("name");
        acp.target_kind = std::string(target_kind);
        acp.target_id = element.id().value();
        acp.resolution_kind = field("resolutionKind");
        if (acp.resolution_kind.empty()) {
            acp.resolution_kind = "none";
        }
        acp.text = field("text");
        acp.confidence_claim_id = field("confidenceClaimId");
        acp.argument_package_id = field("argumentPackageId");
        acp.top_goal_id = field("topGoalId");
        out.push_back(std::move(acp));
    }
}

core::SacmElement project_element(const sacm::model::SACMElement& element) {
    core::SacmElement projected;
    projected.id = element.id().value();
    projected.gid = element.gid().value_or("");
    projected.type = lowercase_kind_name(element.kind());

    if (const auto* model_element = dynamic_cast<const sacm::model::ModelElement*>(&element)) {
        projected.name = model_element->name().content;
        if (!projected.name.empty()) {
            const std::string& lang = model_element->name().lang;
            projected.name_langs[lang.empty() ? "en" : lang] = projected.name;
        }
        // A multi-language name does not fit clause 8.6 (name is one LangString),
        // so import parks the extra languages in a reserved "sacm.import.name"
        // TaggedValue. Reconstruct the full language map from both, or the
        // language toggle would lose every translated name.
        for (const auto& tagged_value : model_element->tagged_values()) {
            if (tagged_value->key().primary() == "sacm.import.name") {
                fill_lang_map(projected.name_langs, tagged_value->content());
            }
        }

        const sacm::model::MultiLangString& description = model_element->description();
        if (!description.empty()) {
            projected.description = description.primary();
            fill_lang_map(projected.description_langs, description);
        }
    }

    // Statement = Description (clause 8.9). Only claim-like elements carry a
    // statement; the POD's `content` field holds it (the GSN node's fallback
    // label and the inspector's statement view). Artifacts, references and
    // relationships legitimately have a `<description>` that is a note, not a
    // statement, so they must keep `content` empty as the legacy parser does.
    if (element.kind() == sacm::metadata::ElementKind::Claim ||
        element.kind() == sacm::metadata::ElementKind::ArgumentReasoning) {
        const auto* model_element = static_cast<const sacm::model::ModelElement*>(&element);
        const auto& descriptions = model_element->descriptions();
        if (!descriptions.empty()) {
            const sacm::model::MultiLangString& statement = descriptions.front()->content();
            projected.content = statement.primary();
            fill_lang_map(projected.content_langs, statement);
            // The statement is the primary Description; a second Description is
            // the secondary note the POD keeps in its `description` field.
            const sacm::model::MultiLangString& note =
                descriptions.size() > 1 ? descriptions[1]->content() : statement;
            projected.description = note.primary();
            projected.description_langs.clear();
            fill_lang_map(projected.description_langs, note);
        }
    }

    // Terms and Expressions carry their text in `value`, which the POD renders
    // as `content`.
    if (const auto* expression = dynamic_cast<const sacm::model::Expression*>(&element)) {
        projected.content = expression->value();
        projected.content_langs["en"] = projected.content;
    } else if (const auto* term = dynamic_cast<const sacm::model::Term*>(&element)) {
        projected.content = term->value();
        projected.content_langs["en"] = projected.content;
    }

    if (const auto* assertion = dynamic_cast<const sacm::model::Assertion*>(&element)) {
        projected.assertion_declaration =
            sacm::model::assertion_declaration_name(assertion->assertion_declaration());
        // A GSN Justification is stored as `axiomatic` plus a vendor gsn.role
        // tag (SACM cannot express the node type; see gsn_role_tag.h). Translate
        // it back to the app's internal "justification" so the tree classifier
        // (core::classify_role) renders it correctly. A file that predates the
        // gsn.role encoding used a non-standard assertionDeclaration="justification";
        // the library normalizes that to axiomatic and preserves the role in the
        // reserved kImportAssertionDeclarationKey tag, which we also honour so old
        // files render (and re-save via gsn.role) as Justifications.
        if (assertion->assertion_declaration() == sacm::model::AssertionDeclaration::Axiomatic) {
            if (const auto* model_element =
                    dynamic_cast<const sacm::model::ModelElement*>(&element)) {
                const bool via_gsn_role =
                    tagged_value_for(*model_element, kGsnRoleTagKey) == kGsnRoleJustification;
                const bool via_import = tagged_value_for(*model_element,
                                                         kImportAssertionDeclarationKey) ==
                                        kImportAssertionDeclarationJustification;
                if (via_gsn_role || via_import) {
                    projected.assertion_declaration = "justification";
                }
            }
        }
        projected.meta_claim_refs = to_id_strings(assertion->meta_claims());
        // GSN "undeveloped" is `needsSupport` in SACM terms; see
        // docs/sacm/sacm-gsn-mapping.md.
        projected.undeveloped =
            assertion->assertion_declaration() == sacm::model::AssertionDeclaration::NeedsSupport;
    }

    if (const auto* relationship =
            dynamic_cast<const sacm::model::AssertedRelationship*>(&element)) {
        projected.source_refs = to_id_strings(relationship->sources());
        projected.target_refs = to_id_strings(relationship->targets());
        projected.is_counter = relationship->is_counter();
        if (relationship->reasoning().has_value()) {
            projected.reasoning_ref = relationship->reasoning()->value();
        }
    }

    return projected;
}

} // namespace

core::AssuranceCase project_case(const LibraryDocument& document) {
    const sacm::model::Document& source = LibraryDocumentAccess::document(document);

    core::AssuranceCase projected;
    if (!source.roots().empty()) {
        const sacm::model::AssuranceCasePackage& root = *source.roots().front();
        projected.id = root.id().value();
        projected.name = root.name().content;
        if (!root.description().empty()) {
            projected.description = root.description().primary();
        }
    }

    source.for_each_element([&projected](const sacm::model::SACMElement& element) {
        // The POD model's `elements` are the things the application draws.
        // Packages are containers, not drawn nodes, and utility elements
        // (Description, Note, TaggedValue, ImplementationConstraint) are
        // metadata carried *on* elements rather than elements in their own
        // right. The legacy parser lists neither, so including them here would
        // be a projection artefact, not a finding.
        if (sacm::metadata::is_package_kind(element.kind())) {
            return;
        }
        if (dynamic_cast<const sacm::model::UtilityElement*>(&element) != nullptr) {
            return;
        }
        projected.elements.push_back(project_element(element));

        // Synthesize any Assurance Claim Points this element carries. Target
        // kind mirrors the legacy parser: relationships are "relationship",
        // everything else "element".
        if (const auto* model_element =
                dynamic_cast<const sacm::model::ModelElement*>(&element)) {
            const bool is_relationship =
                dynamic_cast<const sacm::model::AssertedRelationship*>(&element) != nullptr;
            collect_acps(*model_element, is_relationship ? "relationship" : "element",
                         projected.acps);
        }
    });

    return projected;
}

namespace {

// Copies the identity/name/description a legacy SacmElement shares with a
// library element, for the terminology/artifact projection.
void copy_common_legacy_fields(sacm::SacmElement& target, const sacm::model::SACMElement& source) {
    target.id = source.id().value();
    target.gid = source.gid().value_or("");
    const auto* model_element = dynamic_cast<const sacm::model::ModelElement*>(&source);
    if (model_element == nullptr) {
        return;
    }
    target.name = model_element->name().content;
    if (!target.name.empty()) {
        const std::string& lang = model_element->name().lang;
        target.name_ml.texts[lang.empty() ? "en" : lang] = target.name;
    }
    const sacm::model::MultiLangString& description = model_element->description();
    if (!description.empty()) {
        target.description = description.primary();
        for (const sacm::model::LangString& entry : description.values) {
            if (!entry.content.empty()) {
                target.description_ml.texts[entry.lang.empty() ? "en" : entry.lang] = entry.content;
            }
        }
    }
}

const sacm::model::AssuranceCasePackage* root_package(const LibraryDocument& document) {
    const sacm::model::Document& source = LibraryDocumentAccess::document(document);
    return source.roots().empty() ? nullptr : source.roots().front().get();
}

void collect_argument_package_shells(const sacm::model::AssuranceCasePackage& case_package,
                                     std::vector<ArgumentPackageShell>& out) {
    for (const auto& argument_package : case_package.argument_packages()) {
        ArgumentPackageShell shell;
        copy_common_legacy_fields(shell.identity, *argument_package);
        for (const auto& element : argument_package->argument_elements()) {
            shell.element_ids.push_back(element->id().value());
        }
        out.push_back(std::move(shell));
    }
    for (const auto& nested : case_package.assurance_case_packages()) {
        collect_argument_package_shells(*nested, out);
    }
}

} // namespace

std::vector<sacm::TerminologyPackage> project_terminology_packages(const LibraryDocument& document) {
    std::vector<sacm::TerminologyPackage> result;
    const sacm::model::AssuranceCasePackage* root = root_package(document);
    if (root == nullptr) {
        return result;
    }
    for (const auto& terminology_package : root->terminology_packages()) {
        sacm::TerminologyPackage projected;
        copy_common_legacy_fields(projected, *terminology_package);
        for (const auto& element : terminology_package->terminology_elements()) {
            // Term is-a Expression, so test it first.
            if (const auto* term = dynamic_cast<const sacm::model::Term*>(element.get())) {
                sacm::Term projected_term;
                copy_common_legacy_fields(projected_term, *term);
                projected_term.value = term->value();
                projected_term.externalReference = term->external_reference();
                if (term->origin().has_value()) {
                    projected_term.origin = term->origin()->value();
                }
                for (const sacm::model::ElementId& category : term->categories()) {
                    projected_term.category_refs.push_back(category.value());
                }
                projected.terms.push_back(std::move(projected_term));
            } else if (const auto* expression =
                           dynamic_cast<const sacm::model::Expression*>(element.get())) {
                sacm::Expression projected_expression;
                copy_common_legacy_fields(projected_expression, *expression);
                projected_expression.value = expression->value();
                projected.expressions.push_back(std::move(projected_expression));
            } else if (const auto* category =
                           dynamic_cast<const sacm::model::Category*>(element.get())) {
                sacm::Category projected_category;
                copy_common_legacy_fields(projected_category, *category);
                projected.categories.push_back(std::move(projected_category));
            }
            // TerminologyGroup and other terminology elements have no legacy
            // struct; the canonical hash does not cover them.
        }
        result.push_back(std::move(projected));
    }
    return result;
}

namespace {

// Indexes every legacy element in the package by id so a library element's tags
// can be attached to its counterpart in one pass.
void index_by_id(sacm::SacmElement& element,
                 std::unordered_map<std::string, sacm::SacmElement*>& by_id) {
    if (!element.id.empty()) {
        by_id[element.id] = &element;
    }
}

void index_terminology_package(sacm::TerminologyPackage& tp,
                               std::unordered_map<std::string, sacm::SacmElement*>& by_id) {
    index_by_id(tp, by_id);
    for (sacm::Category& c : tp.categories) index_by_id(c, by_id);
    for (sacm::Expression& e : tp.expressions) index_by_id(e, by_id);
    for (sacm::Term& t : tp.terms) index_by_id(t, by_id);
}

} // namespace

void copy_library_tags_onto_package(const LibraryDocument& document,
                                    sacm::AssuranceCasePackage& package) {
    std::unordered_map<std::string, sacm::SacmElement*> by_id;
    // ArtifactReference is indexed separately (by concrete type) so the
    // referencedArtifact the POD drops can be restored -- terminology detection
    // relies on it.
    std::unordered_map<std::string, sacm::ArtifactReference*> artifact_refs_by_id;
    index_by_id(package, by_id);
    for (sacm::TerminologyPackage& tp : package.terminologyPackages) index_terminology_package(tp, by_id);
    for (sacm::ArtifactPackage& ap : package.artifactPackages) {
        index_by_id(ap, by_id);
        for (sacm::Artifact& a : ap.artifacts) index_by_id(a, by_id);
    }
    for (sacm::ArgumentPackage& ap : package.argumentPackages) {
        index_by_id(ap, by_id);
        for (sacm::Claim& c : ap.claims) index_by_id(c, by_id);
        for (sacm::ArgumentReasoning& r : ap.argumentReasonings) index_by_id(r, by_id);
        for (sacm::ArtifactReference& r : ap.artifactReferences) {
            index_by_id(r, by_id);
            if (!r.id.empty()) artifact_refs_by_id[r.id] = &r;
        }
        for (sacm::AssertedInference& r : ap.assertedInferences) index_by_id(r, by_id);
        for (sacm::AssertedContext& r : ap.assertedContexts) index_by_id(r, by_id);
        for (sacm::AssertedEvidence& r : ap.assertedEvidences) index_by_id(r, by_id);
        for (sacm::TerminologyPackage& tp : ap.terminologyPackages) index_terminology_package(tp, by_id);
    }

    const sacm::model::Document& source = LibraryDocumentAccess::document(document);
    source.for_each_element([&](const sacm::model::SACMElement& element) {
        const std::string id = element.id().value();
        // Restore referencedArtifact on artifact references (dropped by the POD).
        if (const auto* library_ref =
                dynamic_cast<const sacm::model::ArtifactReference*>(&element)) {
            const auto it = artifact_refs_by_id.find(id);
            if (it != artifact_refs_by_id.end() && !library_ref->referenced_artifact_elements().empty()) {
                it->second->referencedArtifact =
                    library_ref->referenced_artifact_elements().front().value();
            }
        }
        // Restore vendor TaggedValues (ACP, confidence-package, GSN-role).
        const auto* model_element = dynamic_cast<const sacm::model::ModelElement*>(&element);
        if (model_element == nullptr || model_element->tagged_values().empty()) {
            return;
        }
        const auto it = by_id.find(id);
        if (it == by_id.end()) {
            return;
        }
        for (const auto& tag : model_element->tagged_values()) {
            it->second->taggedValues.push_back(sacm::TaggedValue{
                .id = tag->id().value(),
                .key = std::string(tag->key().primary()),
                .value = std::string(tag->content().primary()),
            });
        }
    });
}

std::vector<sacm::ArtifactPackage> project_artifact_packages(const LibraryDocument& document) {
    std::vector<sacm::ArtifactPackage> result;
    const sacm::model::AssuranceCasePackage* root = root_package(document);
    if (root == nullptr) {
        return result;
    }
    for (const auto& artifact_package : root->artifact_packages()) {
        sacm::ArtifactPackage projected;
        copy_common_legacy_fields(projected, *artifact_package);
        for (const auto& element : artifact_package->artifact_elements()) {
            if (const auto* artifact = dynamic_cast<const sacm::model::Artifact*>(element.get())) {
                sacm::Artifact projected_artifact;
                copy_common_legacy_fields(projected_artifact, *artifact);
                projected_artifact.version = artifact->version();
                projected_artifact.date = artifact->date();
                projected.artifacts.push_back(std::move(projected_artifact));
            }
        }
        result.push_back(std::move(projected));
    }
    return result;
}

std::vector<ArgumentPackageShell> project_argument_package_shells(const LibraryDocument& document) {
    std::vector<ArgumentPackageShell> result;
    const sacm::model::AssuranceCasePackage* root = root_package(document);
    if (root != nullptr) {
        collect_argument_package_shells(*root, result);
    }
    return result;
}

} // namespace sacm_adapter
