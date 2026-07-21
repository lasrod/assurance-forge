#include "sacm_adapter/case_projection.h"

#include "sacm_adapter/library_document_access.h"

#include "sacm/model/argumentation.h"
#include "sacm/model/assurance_case.h"
#include "sacm/model/element.h"

#include <algorithm>
#include <cctype>
#include <set>
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
    std::set<std::string> seen;
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

} // namespace sacm_adapter
