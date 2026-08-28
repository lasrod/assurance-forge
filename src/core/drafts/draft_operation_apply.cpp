#include "core/drafts/draft_operation_apply.h"

#include "core/cse_attributes.h"
#include "core/evidence_attributes.h"

#include "core/sacm_model.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/document_edit.h"

#include <algorithm>
#include <format>
#include <optional>
#include <unordered_set>
#include <utility>

namespace core::drafts {

namespace {

using reviews::PatchOperation;
using reviews::PatchOperationType;

// The GSN identifier prefixes the application has always used, so an element an
// agent creates is named the way one the user creates is.
//
// ACP-scoped prefixes (`ACP1_G3`) are not reproduced here: they are derived from
// the legacy package type this path no longer touches, and a confidence
// argument is not yet authorable over MCP. An element created in one gets a
// plain prefix, which is a naming difference rather than a broken document.
const char* PrefixForCreate(PatchOperationType type) {
    switch (type) {
    case PatchOperationType::CreateClaim:
        return "G";
    case PatchOperationType::CreateStrategy:
        return "S";
    case PatchOperationType::CreateSolution:
        return "Sn";
    case PatchOperationType::CreateContext:
        return "C";
    case PatchOperationType::CreateAssumption:
        return "A";
    case PatchOperationType::CreateJustification:
        return "J";
    default:
        return "N";
    }
}

bool SeamKindForCreate(PatchOperationType type, sacm_adapter::NewElementKind& out) {
    switch (type) {
    case PatchOperationType::CreateClaim:
        out = sacm_adapter::NewElementKind::Claim;
        return true;
    case PatchOperationType::CreateStrategy:
        out = sacm_adapter::NewElementKind::ArgumentReasoning;
        return true;
    case PatchOperationType::CreateSolution:
    case PatchOperationType::CreateContext:
        out = sacm_adapter::NewElementKind::ArtifactReference;
        return true;
    case PatchOperationType::CreateAssumption:
        out = sacm_adapter::NewElementKind::Assumption;
        return true;
    case PatchOperationType::CreateJustification:
        out = sacm_adapter::NewElementKind::Justification;
        return true;
    default:
        return false;
    }
}

// Allocates over the ids the DOCUMENT holds, not the ids the projection shows.
// The projection deliberately omits packages and utility elements, and an
// allocator blind to those would hand out an id something already answers to.
class IdAllocator {
public:
    explicit IdAllocator(const sacm_adapter::LibraryDocument& document) {
        for (const sacm_adapter::DocumentElement& element : sacm_adapter::list_document_elements(document)) {
            if (!element.id.empty())
                taken_.insert(element.id);
        }
    }

    std::string Next(const std::string& prefix) {
        for (int index = 1; index < 100000; ++index) {
            std::string candidate = prefix + std::to_string(index);
            if (taken_.insert(candidate).second)
                return candidate;
        }
        return prefix + "x";
    }

private:
    std::unordered_set<std::string> taken_;
};

const core::SacmElement* FindProjected(const core::AssuranceCase& model, const std::string& id) {
    for (const core::SacmElement& element : model.elements) {
        if (element.id == id)
            return &element;
    }
    return nullptr;
}

std::string Describe(const sacm_adapter::EditOutcome& outcome, const std::string& what) {
    if (!outcome.diagnostics.empty())
        return what + ": " + outcome.diagnostics.front().message;
    if (!outcome.supported)
        return what + ", and this model has no way to express it.";
    return what + ".";
}

// The same wording for the seams that report an AddChildOutcome. Two structs
// rather than one is a `sacm_adapter` shape, not a difference an agent reading
// the refusal should have to notice.
std::string DescribeChild(const sacm_adapter::AddChildOutcome& outcome, const std::string& what) {
    if (!outcome.diagnostics.empty())
        return what + ": " + outcome.diagnostics.front().message;
    if (!outcome.supported)
        return what + ", and this model has no way to express it.";
    return what + ".";
}

// Resolves an operand to a real element id, following `create_ref` into what
// this batch has already created.
bool ResolveRef(const std::optional<reviews::ElementRef>& ref,
                const std::map<std::string, std::string>& created,
                const char* role,
                std::string& out,
                std::string& error) {
    if (!ref.has_value()) {
        error = std::format("This operation needs a {}.", role);
        return false;
    }
    if (ref->existing_id.has_value() && !ref->existing_id->empty()) {
        out = ref->existing_id.value();
        return true;
    }
    if (ref->create_ref.has_value() && !ref->create_ref->empty()) {
        const auto found = created.find(ref->create_ref.value());
        if (found == created.end()) {
            error = "This operation refers to \"" + ref->create_ref.value() +
                    "\", which nothing in this batch has created yet. Create it first, or name an existing element "
                    "by id.";
            return false;
        }
        out = found->second;
        return true;
    }
    error = std::format("The {} must name an existing element by id, or a create_ref this batch creates.", role);
    return false;
}

// Which text field this element's text lives in, decided by its kind.
//
// The same rule the patch service and the seams follow, asked here so the answer
// is resolved rather than guessed: an operation that names `content` on a kind
// that has none is the defect this whole redesign began with.
sacm_adapter::TextField TextFieldFor(const core::SacmElement& element) {
    return core::ElementCarriesContent(element) ? sacm_adapter::TextField::Content
                                                : sacm_adapter::TextField::Description;
}

// Writes one text value plus every translation attached to the operation.
bool WriteTextAndTranslations(sacm_adapter::LibraryDocument& document,
                              const std::string& element_id,
                              sacm_adapter::TextField field,
                              const std::string& primary,
                              const std::map<std::string, std::string>& translations,
                              const char* what,
                              std::string& error) {
    const sacm_adapter::EditOutcome written =
        sacm_adapter::apply_text_edit(document, element_id, field, reviews::kPatchPrimaryLanguage, primary);
    if (!written.supported || !written.applied) {
        error = Describe(written, std::format("The {} of {} could not be set", what, element_id));
        return false;
    }
    for (const auto& [language, value] : translations) {
        if (language == reviews::kPatchPrimaryLanguage)
            continue;
        const sacm_adapter::EditOutcome translated =
            sacm_adapter::apply_text_edit(document, element_id, field, language, value);
        if (!translated.supported || !translated.applied) {
            error = Describe(translated, std::format("The '{}' {} of {} could not be set", language, what, element_id));
            return false;
        }
    }
    return true;
}

struct Applier {
    sacm_adapter::LibraryDocument& document;
    IdAllocator& ids;
    std::string package_id;
    std::map<std::string, std::string>& created;
    // The TerminologyPackage created for this batch, if it had to create one.
    // Remembered so a batch defining three terms in a case with no glossary
    // makes one package rather than three.
    std::string created_terminology_package_id;

    bool TerminologyPackage(std::string& out, std::string& error);
    bool ApplyCreate(const PatchOperation& operation, std::string& error);
    bool ApplyCreateTerm(const PatchOperation& operation, std::string& error);
    bool ApplyCreateCategory(const PatchOperation& operation, std::string& error);
    bool ApplyUpdateTerm(const PatchOperation& operation, std::string& error);
    bool ApplyUpdateCategory(const PatchOperation& operation, std::string& error);
    bool ApplyRemoveTerm(const PatchOperation& operation, std::string& error);
    bool ApplyUpdateText(const PatchOperation& operation, std::string& error);
    bool ApplyUpdateName(const PatchOperation& operation, std::string& error);
    bool ApplyUndeveloped(const PatchOperation& operation, bool undeveloped, std::string& error);
    bool ApplyAddSupport(const PatchOperation& operation, std::string& error);
    bool ApplyAddRelationship(const PatchOperation& operation, sacm_adapter::RelationshipKind kind, std::string& error);
    bool
    ApplyRemoveRelationship(const PatchOperation& operation, const std::vector<std::string>& types, std::string& error);
    bool ApplyRemoveElement(const PatchOperation& operation, std::string& error);
    bool ApplySetEvidenceLocation(const PatchOperation& operation, std::string& error);
    bool ApplySetEvidenceAttribute(const PatchOperation& operation, std::string& error);
    bool ApplySetCseAttribute(const PatchOperation& operation, std::string& error);
    bool Apply(const PatchOperation& operation, std::string& error);
};

// The glossary this batch's terms go in, created on demand.
//
// A case with no TerminologyPackage grows its first one here rather than
// refusing: a project could otherwise never gain a glossary over MCP, and
// refusing would be a worse answer than filing the term somewhere obvious.
bool Applier::TerminologyPackage(std::string& out, std::string& error) {
    if (!created_terminology_package_id.empty()) {
        out = created_terminology_package_id;
        return true;
    }
    const std::string existing = sacm_adapter::resolve_terminology_package_id(document);
    if (!existing.empty()) {
        out = existing;
        return true;
    }
    const sacm_adapter::TerminologyCreateOutcome outcome =
        sacm_adapter::apply_create_terminology_package(document, "Terminology", "");
    if (!outcome.supported || !outcome.applied) {
        error = outcome.diagnostics.empty()
                    ? "This case has no glossary and one could not be created."
                    : "This case has no glossary and one could not be created: " + outcome.diagnostics.front().message;
        return false;
    }
    created_terminology_package_id = outcome.element_id;
    out = created_terminology_package_id;
    return true;
}

bool Applier::ApplyCreateTerm(const PatchOperation& operation, std::string& error) {
    if (!operation.create_ref.has_value() || operation.create_ref->empty()) {
        error = "A create operation needs a create_ref so later operations can refer to what it made.";
        return false;
    }
    if (operation.text.empty()) {
        error = "A term needs the word or phrase it defines, in \"text\".";
        return false;
    }
    // SACM gives an ExpressionElement one value (clause 10.11), so a term cannot
    // be stated in two languages. Refused here rather than at acceptance, with
    // the field that CAN carry translations named.
    if (!operation.translations.empty() && operation.new_value.empty()) {
        error = "A term's value is a single string and cannot be translated. Translate its definition instead.";
        return false;
    }
    // A term is a word paired with what it means. Without the definition it is
    // the word alone, and the glossary then shows the reader a term that looks
    // defined and is not -- which is worse for a safety argument than no
    // glossary entry. Twice reported from real sessions: an agent staged a whole
    // glossary, set the category and the external reference the guidance names,
    // and left every definition empty, because nothing here asked for one.
    if (operation.new_value.empty()) {
        error = "CreateTerm for " + operation.create_ref.value() + " has no definition. Put what \"" + operation.text +
                "\" means in \"new_value\": a term with no definition reads as defined and "
                "is not. Revise it later with UpdateTerm field \"definition\".";
        return false;
    }

    std::string terminology_package;
    if (!TerminologyPackage(terminology_package, error))
        return false;

    const std::string id = ids.Next("T");
    sacm_adapter::TerminologyTermFields fields;
    fields.value = operation.text;
    fields.description = operation.new_value;
    const sacm_adapter::TerminologyCreateOutcome outcome =
        sacm_adapter::apply_create_terminology_term(document, terminology_package, fields, id);
    if (!outcome.supported || !outcome.applied) {
        error = outcome.diagnostics.empty() ? "The term could not be created."
                                            : "The term could not be created: " + outcome.diagnostics.front().message;
        return false;
    }
    const std::string new_id = outcome.element_id.empty() ? id : outcome.element_id;
    created.emplace(operation.create_ref.value(), new_id);

    // Definition translations land after the term exists, on the field that
    // holds the definition.
    for (const auto& [language, value] : operation.translations) {
        if (language == reviews::kPatchPrimaryLanguage)
            continue;
        const sacm_adapter::EditOutcome translated =
            sacm_adapter::apply_text_edit(document, new_id, sacm_adapter::TextField::Description, language, value);
        if (!translated.supported || !translated.applied) {
            error = Describe(translated, std::format("The '{}' definition of {} could not be set", language, new_id));
            return false;
        }
    }
    return true;
}

bool Applier::ApplyCreateCategory(const PatchOperation& operation, std::string& error) {
    if (!operation.create_ref.has_value() || operation.create_ref->empty()) {
        error = "A create operation needs a create_ref so later operations can refer to what it made.";
        return false;
    }
    if (operation.text.empty()) {
        error = "A category needs a name, in \"text\".";
        return false;
    }
    std::string terminology_package;
    if (!TerminologyPackage(terminology_package, error))
        return false;

    const std::string id = ids.Next("CAT");
    const sacm_adapter::TerminologyCreateOutcome outcome = sacm_adapter::apply_create_terminology_category(
        document, terminology_package, operation.text, operation.new_value, id);
    if (!outcome.supported || !outcome.applied) {
        error = outcome.diagnostics.empty()
                    ? "The category could not be created."
                    : "The category could not be created: " + outcome.diagnostics.front().message;
        return false;
    }
    created.emplace(operation.create_ref.value(), outcome.element_id.empty() ? id : outcome.element_id);
    return true;
}

bool Applier::ApplyUpdateTerm(const PatchOperation& operation, std::string& error) {
    std::string term_id;
    if (!ResolveRef(operation.element, created, "element", term_id, error))
        return false;

    const core::AssuranceCase model = sacm_adapter::project_case(document);
    const core::SacmElement* term = FindProjected(model, term_id);
    if (term == nullptr) {
        error = "There is no element " + term_id + " in this argument.";
        return false;
    }
    if (term->type != "term") {
        error = "UpdateTerm targets " + term_id + ", which is a " + term->type +
                ", not a term. Use UpdateElementText for argument elements.";
        return false;
    }

    if (operation.field == reviews::kTermFieldValue) {
        if (!operation.translations.empty()) {
            error = "A term's value is a single string; translations apply to its definition. "
                    "Use field \"definition\" to translate what the term means.";
            return false;
        }
        if (operation.new_value.empty()) {
            error = "This would blank the term's value. Use RemoveTerm to remove the term instead.";
            return false;
        }
        return WriteTextAndTranslations(
            document, term_id, sacm_adapter::TextField::Content, operation.new_value, {}, "value", error);
    }
    if (operation.field == reviews::kTermFieldDefinition) {
        return WriteTextAndTranslations(document,
                                        term_id,
                                        sacm_adapter::TextField::Description,
                                        operation.new_value,
                                        operation.translations,
                                        "definition",
                                        error);
    }
    if (operation.field == reviews::kTermFieldName) {
        if (!operation.translations.empty()) {
            error = "A term's name is one SACM LangString; state it in the primary language only.";
            return false;
        }
        return WriteTextAndTranslations(
            document, term_id, sacm_adapter::TextField::Name, operation.new_value, {}, "name", error);
    }
    if (operation.field == reviews::kTermFieldCategory) {
        // Space separated, matching the XMI convention for an idref list, so a
        // term can carry the several categories clause 10.8 allows. Empty clears
        // them: an uncategorized term is a reportable state, not an impossible
        // one, so removing a category has to be expressible.
        std::vector<std::string> categories;
        std::string current;
        for (const char character : operation.new_value) {
            if (character == ' ' || character == '\t' || character == '\n') {
                if (!current.empty())
                    categories.push_back(std::exchange(current, {}));
                continue;
            }
            current += character;
        }
        if (!current.empty())
            categories.push_back(current);

        const sacm_adapter::EditOutcome outcome =
            sacm_adapter::apply_set_term_categories(document, term_id, categories);
        if (!outcome.supported || !outcome.applied) {
            error = Describe(outcome, "The categories of " + term_id + " could not be set");
            return false;
        }
        return true;
    }
    if (operation.field == reviews::kTermFieldExternalReference) {
        const sacm_adapter::EditOutcome outcome =
            sacm_adapter::apply_set_term_external_reference(document, term_id, operation.new_value);
        if (!outcome.supported || !outcome.applied) {
            error = Describe(outcome, "The external reference of " + term_id + " could not be set");
            return false;
        }
        return true;
    }
    if (operation.field == reviews::kTermFieldOrigin) {
        const sacm_adapter::EditOutcome outcome =
            sacm_adapter::apply_set_term_origin(document, term_id, operation.new_value);
        if (!outcome.supported || !outcome.applied) {
            error = Describe(outcome, "The origin of " + term_id + " could not be set");
            return false;
        }
        return true;
    }
    error = "Unsupported UpdateTerm field \"" + operation.field +
            "\". Use value, definition, name, category, external_reference or origin.";
    return false;
}

bool Applier::ApplyUpdateCategory(const PatchOperation& operation, std::string& error) {
    std::string category_id;
    if (!ResolveRef(operation.element, created, "element", category_id, error))
        return false;

    const core::AssuranceCase model = sacm_adapter::project_case(document);
    const core::SacmElement* category = FindProjected(model, category_id);
    if (category == nullptr) {
        error = "There is no element " + category_id + " in this argument.";
        return false;
    }
    if (category->type != "category") {
        error = "UpdateCategory targets " + category_id + ", which is a " + category->type + ", not a category.";
        return false;
    }

    if (operation.field == reviews::kCategoryFieldName) {
        return WriteTextAndTranslations(
            document, category_id, sacm_adapter::TextField::Name, operation.new_value, {}, "name", error);
    }
    if (operation.field == reviews::kCategoryFieldDescription) {
        return WriteTextAndTranslations(document,
                                        category_id,
                                        sacm_adapter::TextField::Description,
                                        operation.new_value,
                                        operation.translations,
                                        "description",
                                        error);
    }
    error = "Unsupported UpdateCategory field \"" + operation.field + "\". Use name or description.";
    return false;
}

bool Applier::ApplyRemoveTerm(const PatchOperation& operation, std::string& error) {
    std::string term_id;
    if (!ResolveRef(operation.element, created, "element", term_id, error))
        return false;
    // No cascade. Removing a term an argument package references crosses a
    // package boundary, which the library refuses by default precisely so it
    // cannot happen without someone being asked -- and this surface has no way
    // to ask. The refusal names the situation instead of widening the removal.
    const sacm_adapter::TerminologyEditOutcome outcome =
        sacm_adapter::apply_delete_terminology_element(document, term_id, /*cascade_external_references=*/false);
    if (!outcome.supported || !outcome.applied) {
        error = outcome.diagnostics.empty()
                    ? "Term " + term_id + " could not be removed."
                    : "Term " + term_id + " could not be removed: " + outcome.diagnostics.front().message;
        return false;
    }
    return true;
}

bool Applier::ApplyCreate(const PatchOperation& operation, std::string& error) {
    sacm_adapter::NewElementKind kind = sacm_adapter::NewElementKind::Claim;
    if (!SeamKindForCreate(operation.type, kind)) {
        error = "This element kind cannot be created.";
        return false;
    }
    if (!operation.create_ref.has_value() || operation.create_ref->empty()) {
        error = "A create operation needs a create_ref so later operations can refer to what it made.";
        return false;
    }
    if (package_id.empty()) {
        error = "This argument has no ArgumentPackage to create the element in.";
        return false;
    }

    const std::string id = ids.Next(PrefixForCreate(operation.type));
    sacm_adapter::CreateElementFields fields;
    fields.element_id = id;
    fields.name = operation.new_value;
    fields.language = reviews::kPatchPrimaryLanguage;
    // An ArtifactReference has no statement, so its text is written as a
    // description after it exists rather than handed to the create.
    const bool carries_statement = kind != sacm_adapter::NewElementKind::ArtifactReference;
    if (carries_statement)
        fields.text = operation.text;

    const sacm_adapter::AddChildOutcome created_outcome =
        sacm_adapter::apply_create_element(document, package_id, kind, fields);
    if (!created_outcome.supported || !created_outcome.applied) {
        error = created_outcome.diagnostics.empty()
                    ? "The element could not be created."
                    : "The element could not be created: " + created_outcome.diagnostics.front().message;
        return false;
    }
    const std::string new_id = created_outcome.new_element_id.empty() ? id : created_outcome.new_element_id;
    created.emplace(operation.create_ref.value(), new_id);

    if (!operation.text.empty() || !operation.translations.empty()) {
        const sacm_adapter::TextField field =
            carries_statement ? sacm_adapter::TextField::Content : sacm_adapter::TextField::Description;
        if (!WriteTextAndTranslations(document, new_id, field, operation.text, operation.translations, "text", error))
            return false;
    }
    return true;
}

bool Applier::ApplyUpdateText(const PatchOperation& operation, std::string& error) {
    std::string element_id;
    if (!ResolveRef(operation.element, created, "element", element_id, error))
        return false;

    const core::AssuranceCase model = sacm_adapter::project_case(document);
    const core::SacmElement* element = FindProjected(model, element_id);
    if (element == nullptr) {
        error = "There is no element " + element_id + " in this argument.";
        return false;
    }

    // The field is resolved from the element's kind. A caller that named one is
    // held to it only when it agrees, because naming the other field is exactly
    // the request this design exists to make impossible.
    const sacm_adapter::TextField resolved = TextFieldFor(*element);
    if (!operation.field.empty() && operation.field != "name") {
        const bool wants_content = operation.field == "content";
        const bool wants_description = operation.field == "description";
        if (!wants_content && !wants_description) {
            error = "Unsupported text field \"" + operation.field + "\".";
            return false;
        }
        const bool agrees = (wants_content && resolved == sacm_adapter::TextField::Content) ||
                            (wants_description && resolved == sacm_adapter::TextField::Description);
        if (!agrees) {
            const char* correct = resolved == sacm_adapter::TextField::Content ? "content" : "description";
            error = "Element " + element_id + " is a " + element->type + ", whose text is its '" + correct +
                    "'. Use field \"" + correct + "\", or omit the field and it is resolved for you.";
            return false;
        }
    }
    const sacm_adapter::TextField field = operation.field == "name" ? sacm_adapter::TextField::Name : resolved;
    return WriteTextAndTranslations(
        document, element_id, field, operation.new_value, operation.translations, "text", error);
}

bool Applier::ApplyUpdateName(const PatchOperation& operation, std::string& error) {
    std::string element_id;
    if (!ResolveRef(operation.element, created, "element", element_id, error))
        return false;
    return WriteTextAndTranslations(document,
                                    element_id,
                                    sacm_adapter::TextField::Name,
                                    operation.new_value,
                                    operation.translations,
                                    "name",
                                    error);
}

bool Applier::ApplyUndeveloped(const PatchOperation& operation, bool undeveloped, std::string& error) {
    std::string element_id;
    if (!ResolveRef(operation.element, created, "element", element_id, error))
        return false;
    const sacm_adapter::EditOutcome outcome = sacm_adapter::apply_set_undeveloped(document, element_id, undeveloped);
    if (!outcome.supported || !outcome.applied) {
        error = Describe(outcome, "The undeveloped marker on " + element_id + " could not be changed");
        return false;
    }
    return true;
}

// Support attaches by the relationship the child's GSN role requires, which is
// not always an AssertedInference and is sometimes no relationship at all.
//
// Delegated rather than decided here. A GSN Solution and a GSN Context are the
// same SACM type and differ only in how they attach, and a GSN Strategy is the
// reasoning of an inference rather than one of its ends -- so an operation that
// always built an AssertedInference produced a Solution indistinguishable from a
// Context (it then rendered with Context notation) and a Strategy wired as an
// end, which the GSN well-formedness check reports as an error against an
// argument that looks perfectly ordinary on the canvas.
bool Applier::ApplyAddSupport(const PatchOperation& operation, std::string& error) {
    std::string source_id;
    std::string target_id;
    if (!ResolveRef(operation.source, created, "source", source_id, error))
        return false;
    if (!ResolveRef(operation.target, created, "target", target_id, error))
        return false;

    // `source` is the premise and `target` the conclusion, which is SACM's
    // direction; GSN's SupportedBy runs the other way and the reader swaps them
    // (docs/sacm/sacm-gsn-mapping.md). So the element being attached UNDER the
    // parent is the source.
    const sacm_adapter::AddChildOutcome outcome =
        sacm_adapter::apply_attach_child(document, target_id, source_id, ids.Next("R"));
    if (!outcome.supported || !outcome.applied) {
        error = DescribeChild(outcome, "The support from " + source_id + " to " + target_id + " could not be created");
        return false;
    }
    return true;
}

bool Applier::ApplyAddRelationship(const PatchOperation& operation,
                                   sacm_adapter::RelationshipKind kind,
                                   std::string& error) {
    std::string source_id;
    std::string target_id;
    if (!ResolveRef(operation.source, created, "source", source_id, error))
        return false;
    if (!ResolveRef(operation.target, created, "target", target_id, error))
        return false;

    const std::string relationship_id = ids.Next("R");
    const sacm_adapter::EditOutcome outcome =
        sacm_adapter::apply_add_relationship(document, relationship_id, kind, {source_id}, {target_id}, {});
    if (!outcome.supported || !outcome.applied) {
        error = Describe(outcome, "The link from " + source_id + " to " + target_id + " could not be created");
        return false;
    }
    return true;
}

bool Applier::ApplyRemoveRelationship(const PatchOperation& operation,
                                      const std::vector<std::string>& types,
                                      std::string& error) {
    std::string source_id;
    std::string target_id;
    if (!ResolveRef(operation.source, created, "source", source_id, error))
        return false;
    if (!ResolveRef(operation.target, created, "target", target_id, error))
        return false;

    const core::AssuranceCase model = sacm_adapter::project_case(document);
    for (const core::SacmElement& element : model.elements) {
        if (std::find(types.begin(), types.end(), element.type) == types.end())
            continue;
        const bool has_source =
            std::find(element.source_refs.begin(), element.source_refs.end(), source_id) != element.source_refs.end();
        const bool has_target =
            std::find(element.target_refs.begin(), element.target_refs.end(), target_id) != element.target_refs.end();
        if (!has_source || !has_target)
            continue;
        // Only when this relationship is exactly the one named. A shared
        // inference carrying several sub-goals means something different from a
        // link between two elements, and deleting it would withdraw support the
        // caller never mentioned.
        if (element.source_refs.size() > 1) {
            error = std::format("The link from {} to {} is part of a relationship that also supports other "
                                "elements, and removing one of its ends is not expressible yet.",
                                source_id,
                                target_id);
            return false;
        }
        const sacm_adapter::DeleteOutcome removed = sacm_adapter::apply_delete_element(document, element.id);
        if (!removed.supported || !removed.applied) {
            error = removed.diagnostics.empty()
                        ? "The link could not be removed."
                        : "The link could not be removed: " + removed.diagnostics.front().message;
            return false;
        }
        return true;
    }
    error = "There is no link from " + source_id + " to " + target_id + " to remove.";
    return false;
}

bool Applier::ApplyRemoveElement(const PatchOperation& operation, std::string& error) {
    std::string element_id;
    if (!ResolveRef(operation.element, created, "element", element_id, error))
        return false;
    const sacm_adapter::DeleteOutcome removed = sacm_adapter::apply_delete_element(document, element_id);
    if (!removed.supported || !removed.applied) {
        if (removed.target_missing) {
            error = "There is no element " + element_id + " to remove.";
            return false;
        }
        error = removed.diagnostics.empty()
                    ? "Element " + element_id + " could not be removed."
                    : "Element " + element_id + " could not be removed: " + removed.diagnostics.front().message;
        return false;
    }
    return true;
}

// Where a piece of evidence is. The seam decides what that means in SACM (the
// cited Resource's location, created when the reference cites none), so a
// batch that names a Claim here is refused by it rather than accepted into a
// field the document does not have.
bool Applier::ApplySetEvidenceLocation(const PatchOperation& operation, std::string& error) {
    std::string element_id;
    if (!ResolveRef(operation.element, created, "element", element_id, error))
        return false;
    const sacm_adapter::EditOutcome outcome =
        sacm_adapter::apply_set_evidence_location(document, element_id, operation.new_value);
    if (!outcome.supported || !outcome.applied) {
        error = Describe(outcome, "The location of " + element_id + " could not be recorded");
        return false;
    }
    return true;
}

// One register column. The column is named by its token so a client can
// discover the vocabulary from the schema; a token that names no column is
// refused here, in the call that made it.
bool Applier::ApplySetEvidenceAttribute(const PatchOperation& operation, std::string& error) {
    std::string element_id;
    if (!ResolveRef(operation.element, created, "element", element_id, error))
        return false;
    EvidenceAttribute attribute = EvidenceAttribute::Owner;
    if (!ParseEvidenceAttribute(operation.field, attribute)) {
        error = "SetEvidenceAttribute names no evidence column: \"" + operation.field +
                "\". Use owner, type, version, date, maturity, controlled_environment or notes.";
        return false;
    }
    const sacm_adapter::EditOutcome outcome =
        sacm_adapter::apply_set_evidence_attribute(document, element_id, attribute, operation.new_value);
    if (!outcome.supported || !outcome.applied) {
        error = Describe(outcome,
                         std::string("The ") + EvidenceAttributeToken(attribute) + " of " + element_id +
                             " could not be recorded");
        return false;
    }
    return true;
}

// One CSE register column, on the relationship that carries the support.
bool Applier::ApplySetCseAttribute(const PatchOperation& operation, std::string& error) {
    std::string relationship_id;
    if (!ResolveRef(operation.element, created, "element", relationship_id, error))
        return false;
    CseAttribute attribute = CseAttribute::AssessmentStatus;
    if (!ParseCseAttribute(operation.field, attribute)) {
        error = "SetCseAttribute names no CSE column: \"" + operation.field +
                "\". Use claim_owner, evidence_owner, safety_case_owner, claim_criteria, evidence_criteria, "
                "assessment_status or notes.";
        return false;
    }
    const sacm_adapter::EditOutcome outcome =
        sacm_adapter::apply_set_cse_attribute(document, relationship_id, attribute, operation.new_value);
    if (!outcome.supported || !outcome.applied) {
        error = Describe(outcome,
                         std::string("The ") + CseAttributeToken(attribute) + " of " + relationship_id +
                             " could not be recorded");
        return false;
    }
    return true;
}

bool Applier::Apply(const PatchOperation& operation, std::string& error) {
    switch (operation.type) {
    case PatchOperationType::CreateClaim:
    case PatchOperationType::CreateStrategy:
    case PatchOperationType::CreateSolution:
    case PatchOperationType::CreateContext:
    case PatchOperationType::CreateAssumption:
    case PatchOperationType::CreateJustification:
        return ApplyCreate(operation, error);
    case PatchOperationType::UpdateElementText:
        return ApplyUpdateText(operation, error);
    case PatchOperationType::UpdateElementName:
        return ApplyUpdateName(operation, error);
    case PatchOperationType::SetUndeveloped:
        return ApplyUndeveloped(operation, true, error);
    case PatchOperationType::ClearUndeveloped:
        return ApplyUndeveloped(operation, false, error);
    case PatchOperationType::AddSupportedBy:
        return ApplyAddSupport(operation, error);
    case PatchOperationType::AddInContextOf:
        return ApplyAddRelationship(operation, sacm_adapter::RelationshipKind::AssertedContext, error);
    case PatchOperationType::RemoveSupportedBy:
        // Support is an AssertedInference for a claim or strategy and an
        // AssertedEvidence for a solution -- the mapping apply_attach_child
        // applies -- so withdrawing it looks for either.
        return ApplyRemoveRelationship(operation, {"assertedinference", "assertedevidence"}, error);
    case PatchOperationType::RemoveInContextOf:
        return ApplyRemoveRelationship(operation, {"assertedcontext"}, error);
    case PatchOperationType::RemoveElement:
        return ApplyRemoveElement(operation, error);
    case PatchOperationType::SetEvidenceLocation:
        return ApplySetEvidenceLocation(operation, error);
    case PatchOperationType::SetEvidenceAttribute:
        return ApplySetEvidenceAttribute(operation, error);
    case PatchOperationType::SetCseAttribute:
        return ApplySetCseAttribute(operation, error);
    case PatchOperationType::CreateTerm:
        return ApplyCreateTerm(operation, error);
    case PatchOperationType::UpdateTerm:
        return ApplyUpdateTerm(operation, error);
    case PatchOperationType::RemoveTerm:
        return ApplyRemoveTerm(operation, error);
    case PatchOperationType::CreateCategory:
        return ApplyCreateCategory(operation, error);
    case PatchOperationType::UpdateCategory:
        return ApplyUpdateCategory(operation, error);
    }
    error = "Unsupported operation.";
    return false;
}

} // namespace

DraftOperationResult ApplyOperationsToDraftDocument(sacm_adapter::LibraryDocument& document,
                                                    const std::vector<reviews::PatchOperation>& operations,
                                                    const std::string& anchor_element_id) {
    DraftOperationResult result;
    if (operations.empty()) {
        result.error = "There are no operations to apply.";
        return result;
    }

    // Applied to a copy, which replaces the draft only once every operation has
    // succeeded. A client whose third operation is refused must not have to
    // reason about whether its first two survived.
    const sacm_adapter::SaveOutcome serialized = sacm_adapter::save_document(document);
    if (!serialized.ok) {
        result.error = "The working draft could not be prepared for editing.";
        return result;
    }
    sacm_adapter::LibraryDocument scratch;
    if (!sacm_adapter::reload_document(scratch, serialized.xml)) {
        result.error = "The working draft could not be prepared for editing.";
        return result;
    }

    IdAllocator ids(scratch);
    Applier applier{scratch,
                    ids,
                    sacm_adapter::resolve_argument_package_id(scratch, anchor_element_id),
                    result.created_ids,
                    /*created_terminology_package_id=*/{}};

    for (std::size_t index = 0; index < operations.size(); ++index) {
        std::string error;
        if (!applier.Apply(operations[index], error)) {
            result.error = error;
            result.failed_operation = index + 1;
            result.created_ids.clear();
            return result;
        }
    }

    const sacm_adapter::SaveOutcome edited = sacm_adapter::save_document(scratch);
    if (!edited.ok || !sacm_adapter::reload_document(document, edited.xml)) {
        result.error = "The edited working draft could not be stored.";
        result.created_ids.clear();
        return result;
    }
    result.applied = true;
    return result;
}

} // namespace core::drafts
