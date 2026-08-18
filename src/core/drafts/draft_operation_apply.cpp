#include "core/drafts/draft_operation_apply.h"

#include "core/sacm_model.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/document_edit.h"

#include <algorithm>
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

// Resolves an operand to a real element id, following `create_ref` into what
// this batch has already created.
bool ResolveRef(const std::optional<reviews::ElementRef>& ref,
                const std::map<std::string, std::string>& created,
                const char* role,
                std::string& out,
                std::string& error) {
    if (!ref.has_value()) {
        error = std::string("This operation needs a ") + role + ".";
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
    error = std::string("The ") + role + " must name an existing element by id, or a create_ref this batch creates.";
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
        error = Describe(written, std::string("The ") + what + " of " + element_id + " could not be set");
        return false;
    }
    for (const auto& [language, value] : translations) {
        if (language == reviews::kPatchPrimaryLanguage)
            continue;
        const sacm_adapter::EditOutcome translated =
            sacm_adapter::apply_text_edit(document, element_id, field, language, value);
        if (!translated.supported || !translated.applied) {
            error = Describe(translated, "The '" + language + "' " + what + " of " + element_id + " could not be set");
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

    bool ApplyCreate(const PatchOperation& operation, std::string& error);
    bool ApplyUpdateText(const PatchOperation& operation, std::string& error);
    bool ApplyUpdateName(const PatchOperation& operation, std::string& error);
    bool ApplyUndeveloped(const PatchOperation& operation, bool undeveloped, std::string& error);
    bool ApplyAddRelationship(const PatchOperation& operation, sacm_adapter::RelationshipKind kind, std::string& error);
    bool ApplyRemoveRelationship(const PatchOperation& operation, const std::string& type, std::string& error);
    bool ApplyRemoveElement(const PatchOperation& operation, std::string& error);
    bool Apply(const PatchOperation& operation, std::string& error);
};

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

bool Applier::ApplyRemoveRelationship(const PatchOperation& operation, const std::string& type, std::string& error) {
    std::string source_id;
    std::string target_id;
    if (!ResolveRef(operation.source, created, "source", source_id, error))
        return false;
    if (!ResolveRef(operation.target, created, "target", target_id, error))
        return false;

    const core::AssuranceCase model = sacm_adapter::project_case(document);
    for (const core::SacmElement& element : model.elements) {
        if (element.type != type)
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
            error = "The link from " + source_id + " to " + target_id + " is part of a relationship that also " +
                    "supports other elements, and removing one of its ends is not expressible yet.";
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
        return ApplyAddRelationship(operation, sacm_adapter::RelationshipKind::AssertedInference, error);
    case PatchOperationType::AddInContextOf:
        return ApplyAddRelationship(operation, sacm_adapter::RelationshipKind::AssertedContext, error);
    case PatchOperationType::RemoveSupportedBy:
        return ApplyRemoveRelationship(operation, "assertedinference", error);
    case PatchOperationType::RemoveInContextOf:
        return ApplyRemoveRelationship(operation, "assertedcontext", error);
    case PatchOperationType::RemoveElement:
        return ApplyRemoveElement(operation, error);
    case PatchOperationType::CreateTerm:
    case PatchOperationType::UpdateTerm:
    case PatchOperationType::RemoveTerm:
    case PatchOperationType::CreateCategory:
    case PatchOperationType::UpdateCategory:
        // Deliberately refused rather than approximated. The terminology seams
        // exist and are a direct mapping, but routing them is its own slice and
        // a half-routed glossary would put terms in the draft that acceptance
        // could not carry -- the failure this redesign removes.
        error = "Terminology operations are not yet routed onto the working draft document.";
        return false;
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
    Applier applier{
        scratch, ids, sacm_adapter::resolve_argument_package_id(scratch, anchor_element_id), result.created_ids};

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
