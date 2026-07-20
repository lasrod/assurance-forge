#include "sacm_adapter/case_projection.h"

#include "sacm_adapter/library_document_access.h"

#include "sacm/model/argumentation.h"
#include "sacm/model/assurance_case.h"
#include "sacm/model/element.h"

#include <algorithm>
#include <cctype>
#include <string>

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

void fill_lang_map(std::map<std::string, std::string>& out,
                   const sacm::model::MultiLangString& value) {
    for (const sacm::model::LangString& entry : value.values) {
        out[entry.lang] = entry.content;
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

core::SacmElement project_element(const sacm::model::SACMElement& element) {
    core::SacmElement projected;
    projected.id = element.id().value();
    projected.gid = element.gid().value_or("");
    projected.type = lowercase_kind_name(element.kind());

    if (const auto* model_element = dynamic_cast<const sacm::model::ModelElement*>(&element)) {
        projected.name = model_element->name().content;
        if (!model_element->name().lang.empty()) {
            projected.name_langs[model_element->name().lang] = model_element->name().content;
        }
        const sacm::model::MultiLangString& description = model_element->description();
        if (!description.empty()) {
            projected.description = description.primary();
            fill_lang_map(projected.description_langs, description);
        }
    }

    if (const auto* assertion = dynamic_cast<const sacm::model::Assertion*>(&element)) {
        projected.assertion_declaration =
            sacm::model::assertion_declaration_name(assertion->assertion_declaration());
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
    });

    // Assurance Claim Points are synthesized by the application from vendor
    // TaggedValues, not read from SACM, so they are out of scope for this
    // projection and excluded from the Stage 3 comparison rather than silently
    // reported as missing. See docs/sacm/sacm-gsn-metamodel-gaps.md.
    return projected;
}

} // namespace sacm_adapter
