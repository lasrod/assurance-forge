#include "core/reviews/review_proposal_patch_service.h"

#include "core/cse_attributes.h"
#include "core/evidence_attributes.h"

#include "core/element_factory.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_set>

namespace core::reviews {
namespace {

bool IsRelationshipType(const std::string& type) {
    return type == "assertedinference" || type == "assertedcontext" || type == "assertedevidence";
}

const char* PrefixFor(PatchOperationType type) {
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
    case PatchOperationType::CreateTerm:
        return "T";
    case PatchOperationType::CreateCategory:
        return "CAT";
    default:
        return "N";
    }
}

const char* DefaultNameFor(PatchOperationType type) {
    switch (type) {
    case PatchOperationType::CreateClaim:
        return "";
    case PatchOperationType::CreateStrategy:
        return "";
    case PatchOperationType::CreateSolution:
        return "";
    case PatchOperationType::CreateContext:
        return "";
    case PatchOperationType::CreateAssumption:
        return "";
    case PatchOperationType::CreateJustification:
        return "";
    default:
        return "";
    }
}

std::string ElementTypeFor(PatchOperationType type) {
    switch (type) {
    case PatchOperationType::CreateClaim:
    case PatchOperationType::CreateAssumption:
    case PatchOperationType::CreateJustification:
        return "claim";
    case PatchOperationType::CreateStrategy:
        return "argumentreasoning";
    case PatchOperationType::CreateSolution:
    case PatchOperationType::CreateContext:
        return "artifactreference";
    case PatchOperationType::CreateTerm:
        return "term";
    case PatchOperationType::CreateCategory:
        return "category";
    default:
        return {};
    }
}

std::string AssertionDeclarationFor(PatchOperationType type) {
    switch (type) {
    case PatchOperationType::CreateAssumption:
        return "assumed";
    case PatchOperationType::CreateJustification:
        return "justification";
    default:
        return {};
    }
}

// A whitespace-separated id list, the way XMI writes an idref list. Used for a
// term's categories, where SACM 10.8 permits several.
std::vector<std::string> SplitRefList(const std::string& value) {
    std::vector<std::string> refs;
    std::istringstream stream(value);
    std::string ref;
    while (stream >> ref) {
        // A leading '#' is the XML-fragment spelling of a reference; tolerated
        // and normalized so an agent copying an id out of a document works.
        if (ref.front() == '#')
            ref.erase(0, 1);
        if (!ref.empty())
            refs.push_back(ref);
    }
    return refs;
}

parser::SacmElement* FindElement(parser::AssuranceCase& model, const std::string& id) {
    for (parser::SacmElement& element : model.elements) {
        if (element.id == id)
            return &element;
    }
    return nullptr;
}

const parser::SacmElement* FindElement(const parser::AssuranceCase& model, const std::string& id) {
    for (const parser::SacmElement& element : model.elements) {
        if (element.id == id)
            return &element;
    }
    return nullptr;
}

std::unordered_set<std::string> CollectIds(const parser::AssuranceCase& model) {
    std::unordered_set<std::string> ids;
    ids.reserve(model.elements.size() * 2);
    for (const parser::SacmElement& element : model.elements) {
        if (!element.id.empty())
            ids.insert(element.id);
    }
    return ids;
}

std::string GenerateUniqueId(std::unordered_set<std::string>& ids, const std::string& prefix) {
    for (int i = 1; i < 100000; ++i) {
        std::string candidate = prefix + std::to_string(i);
        if (ids.insert(candidate).second)
            return candidate;
    }
    std::string fallback = prefix + "x" + std::to_string(ids.size() + 1);
    ids.insert(fallback);
    return fallback;
}

std::string GenerateRelationshipId(std::unordered_set<std::string>& ids) {
    return GenerateUniqueId(ids, "R");
}

bool ValidateCreateRefName(const std::string& create_ref, std::string& error) {
    if (create_ref.empty()) {
        error = "Create operation is missing create_ref.";
        return false;
    }
    if (create_ref.front() != '$') {
        error = "Patch-local create_ref values must start with '$'.";
        return false;
    }
    return true;
}

bool CollectGeneratedIds(const ReviewProposal& proposal,
                         const parser::AssuranceCase& current_model,
                         std::map<std::string, std::string>& generated_ids,
                         std::string& error) {
    generated_ids.clear();
    return AllocateProposalIds(proposal, current_model, {}, generated_ids, error);
}

bool ResolveRef(const ElementRef& ref,
                const parser::AssuranceCase& model,
                const std::map<std::string, std::string>& generated_ids,
                std::string& resolved_id,
                std::string& error) {
    resolved_id.clear();
    const bool has_existing = ref.existing_id.has_value() && !ref.existing_id->empty();
    const bool has_create = ref.create_ref.has_value() && !ref.create_ref->empty();
    if (has_existing == has_create) {
        error = "Element references must contain exactly one existing id or create_ref.";
        return false;
    }
    if (has_existing) {
        if (!FindElement(model, ref.existing_id.value())) {
            error = "Operation references missing existing element " + ref.existing_id.value() + ".";
            return false;
        }
        resolved_id = ref.existing_id.value();
        return true;
    }

    auto found = generated_ids.find(ref.create_ref.value());
    if (found == generated_ids.end()) {
        error = "Operation references unknown created element " + ref.create_ref.value() + ".";
        return false;
    }
    resolved_id = found->second;
    return true;
}

bool ResolveOptionalElementRef(const std::optional<ElementRef>& ref,
                               const parser::AssuranceCase& model,
                               const std::map<std::string, std::string>& generated_ids,
                               const std::string& role,
                               std::string& resolved_id,
                               std::string& error) {
    if (!ref.has_value()) {
        error = "Operation is missing " + role + " reference.";
        return false;
    }
    return ResolveRef(ref.value(), model, generated_ids, resolved_id, error);
}

// Writes one language of one field. The scalar mirrors the primary language
// only: it is what the rest of the model treats as "what this element says", and
// letting a translation land there would change the argument's meaning for every
// reader who has not switched languages.
void WriteLanguage(std::string& scalar,
                   std::map<std::string, std::string>& texts,
                   const std::string& language,
                   const std::string& value) {
    if (value.empty() && language != kPatchPrimaryLanguage) {
        // Clearing a translation is a real edit -- an author who removes a stale
        // Japanese sentence must not have it left behind by the merge.
        texts.erase(language);
        return;
    }
    texts[language] = value;
    if (language == kPatchPrimaryLanguage)
        scalar = value;
}

// Applies the primary text, when the operation states one, plus every
// translation it carries. `primary` is absent for an update that only revises a
// translation -- the common case of translating argument that already exists,
// where touching the English would be a change nobody asked for.
void WriteField(std::string& scalar,
                std::map<std::string, std::string>& texts,
                const std::optional<std::string>& primary,
                const std::map<std::string, std::string>& translations) {
    if (primary.has_value())
        WriteLanguage(scalar, texts, kPatchPrimaryLanguage, primary.value());
    for (const std::pair<const std::string, std::string>& entry : translations) {
        if (entry.first != kPatchPrimaryLanguage)
            WriteLanguage(scalar, texts, entry.first, entry.second);
    }
}

void SetElementText(parser::SacmElement& element,
                    const std::optional<std::string>& primary,
                    const std::map<std::string, std::string>& translations) {
    const bool uses_content = element.type == "claim" || element.type == "argumentreasoning";
    if (uses_content) {
        WriteField(element.content, element.content_langs, primary, translations);
    } else {
        WriteField(element.description, element.description_langs, primary, translations);
    }
}

bool ApplyCreateOperation(const PatchOperation& operation,
                          parser::AssuranceCase& model,
                          const std::map<std::string, std::string>& generated_ids,
                          std::string& error) {
    if (!operation.create_ref.has_value()) {
        error = "Create operation is missing create_ref.";
        return false;
    }
    auto id_it = generated_ids.find(operation.create_ref.value());
    if (id_it == generated_ids.end()) {
        error = "Create operation references unknown create_ref " + operation.create_ref.value() + ".";
        return false;
    }
    if (FindElement(model, id_it->second)) {
        error = "Generated element id already exists: " + id_it->second;
        return false;
    }

    // A term's fields differ from an argument element's: `text` is the term
    // itself (the value that term detection matches in claim text) and
    // `new_value` is its definition. Routed apart from SetElementText because
    // the value maps to `content` where the generic non-claim path would write
    // `description` -- which for a term is the definition, not the term.
    if (operation.type == PatchOperationType::CreateTerm) {
        if (operation.text.empty()) {
            error = "CreateTerm for " + operation.create_ref.value() +
                    " needs \"text\": the term itself, the word or phrase being defined.";
            return false;
        }
        if (!operation.translations.empty() && operation.new_value.empty()) {
            error = "CreateTerm for " + operation.create_ref.value() +
                    " supplies translations but no definition; give the " + std::string(kPatchPrimaryLanguage) +
                    " definition in \"new_value\". Translations apply to the definition -- a term's value is a "
                    "single string.";
            return false;
        }
        // Same rule as the staging path: a word with no meaning beside it is not
        // a term, and a glossary of them reads as defined.
        if (operation.new_value.empty()) {
            error = "CreateTerm for " + operation.create_ref.value() + " has no definition. Put what \"" +
                    operation.text +
                    "\" means in \"new_value\": a term with no definition reads as defined and "
                    "is not. Revise it later with UpdateTerm field \"definition\".";
            return false;
        }
        parser::SacmElement element;
        element.id = id_it->second;
        element.type = ElementTypeFor(operation.type);
        WriteField(element.content, element.content_langs, operation.text, {});
        if (!operation.new_value.empty() || !operation.translations.empty()) {
            WriteField(element.description, element.description_langs, operation.new_value, operation.translations);
        }
        model.elements.push_back(std::move(element));
        return true;
    }

    // A Category has a name and a description and nothing else: no value, so
    // `text` is its name rather than a value the way a term's is.
    if (operation.type == PatchOperationType::CreateCategory) {
        if (operation.text.empty()) {
            error = "CreateCategory for " + operation.create_ref.value() +
                    " needs \"text\": the category's name, such as \"Domain terms\".";
            return false;
        }
        parser::SacmElement element;
        element.id = id_it->second;
        element.type = ElementTypeFor(operation.type);
        WriteField(element.name, element.name_langs, operation.text, {});
        if (!operation.new_value.empty() || !operation.translations.empty()) {
            WriteField(element.description, element.description_langs, operation.new_value, operation.translations);
        }
        model.elements.push_back(std::move(element));
        return true;
    }

    // A new element has no primary text to fall back on, so translations
    // without it would create a node that renders empty for every reader who
    // has not switched languages -- an invisible claim in a safety argument.
    if (operation.text.empty() && !operation.translations.empty()) {
        error = "Create operation for " + operation.create_ref.value() + " supplies translations but no \"text\"; " +
                "give the " + std::string(kPatchPrimaryLanguage) + " statement in \"text\".";
        return false;
    }

    parser::SacmElement element;
    element.id = id_it->second;
    element.type = ElementTypeFor(operation.type);
    element.name = DefaultNameFor(operation.type);
    element.name_langs[kPatchPrimaryLanguage] = element.name;
    element.assertion_declaration = AssertionDeclarationFor(operation.type);
    if (!operation.text.empty()) {
        SetElementText(element, operation.text, operation.translations);
    }
    model.elements.push_back(std::move(element));
    return true;
}

bool RemoveValue(std::vector<std::string>& values, const std::string& value) {
    const auto old_size = values.size();
    values.erase(std::remove(values.begin(), values.end(), value), values.end());
    return values.size() != old_size;
}

std::optional<RemoveMode> RemoveModeFromField(const std::string& field, std::string& error) {
    if (field.empty())
        return std::nullopt;
    if (field == kReviewProposalRemoveModeNodeOnly)
        return RemoveMode::NodeOnly;
    if (field == kReviewProposalRemoveModeNodeAndDescendants)
        return RemoveMode::NodeAndDescendants;
    error = "Unsupported RemoveElement mode: " + field;
    return std::nullopt;
}

bool IsEvidenceLikeElement(const parser::SacmElement& element) {
    return element.type == "artifact" || element.type == "artifactreference" || element.type == "expression";
}

bool ApplyUpdateOperation(const PatchOperation& operation,
                          parser::AssuranceCase& model,
                          const std::map<std::string, std::string>& generated_ids,
                          std::string& error) {
    std::string element_id;
    if (!ResolveOptionalElementRef(operation.element, model, generated_ids, "element", element_id, error))
        return false;
    parser::SacmElement* element = FindElement(model, element_id);
    if (!element) {
        error = "Operation references missing element " + element_id + ".";
        return false;
    }

    // An update that carries only translations revises just those languages.
    // Translating an argument that already exists is the ordinary case, and it
    // must not blank the English on the way through. With no translations the
    // primary is always written, so clearing a field keeps working.
    const std::optional<std::string> primary = (!operation.new_value.empty() || operation.translations.empty())
                                                   ? std::optional<std::string>(operation.new_value)
                                                   : std::nullopt;

    switch (operation.type) {
    case PatchOperationType::UpdateTerm:
        if (element->type != "term") {
            error = "UpdateTerm targets " + element_id + ", which is a " + element->type +
                    ", not a term. Use UpdateElementText for argument elements.";
            return false;
        }
        if (operation.field == kTermFieldValue) {
            // The value is what term detection matches in claim text; a term
            // whose value is blank can never resolve, so blanking it is refused
            // rather than applied. It is also ONE string -- SACM 10.11 gives an
            // ExpressionElement a single value -- so translations cannot ride
            // on this field.
            if (!operation.translations.empty()) {
                error = "A term's value is a single string; translations apply to its definition. "
                        "Use field \"definition\" to translate what the term means.";
                return false;
            }
            if (operation.new_value.empty()) {
                error = "UpdateTerm on " + element_id +
                        " would blank the term's value. Use RemoveTerm to "
                        "remove the term instead.";
                return false;
            }
            WriteField(element->content, element->content_langs, operation.new_value, {});
            return true;
        }
        if (operation.field == kTermFieldDefinition) {
            WriteField(element->description, element->description_langs, primary, operation.translations);
            return true;
        }
        if (operation.field == kTermFieldName) {
            if (!operation.translations.empty()) {
                error = "A term's name is one SACM LangString; state it in the primary language only.";
                return false;
            }
            WriteField(element->name, element->name_langs, operation.new_value, {});
            return true;
        }
        if (operation.field == kTermFieldCategory) {
            // Space separated, matching the XMI convention for an idref list, so
            // a term can carry the several categories SACM 10.8 allows. Empty
            // clears them -- an uncategorized term is a reportable state, not an
            // impossible one, so removing a category has to be expressible.
            std::vector<std::string> categories;
            for (const std::string& candidate : SplitRefList(operation.new_value)) {
                const parser::SacmElement* category = FindElement(model, candidate);
                if (category == nullptr) {
                    error = "UpdateTerm names category " + candidate +
                            ", which does not exist. Create it with a "
                            "CreateCategory operation, or use an id from list_terms.";
                    return false;
                }
                if (category->type != "category") {
                    error = "UpdateTerm names " + candidate + " as a category, but it is a " + category->type + ".";
                    return false;
                }
                // A term belongs to a category once. Repeating an id -- easy in
                // a space-separated list -- would otherwise store a duplicate
                // reference that shows up twice in `list_terms` and changes the
                // element's hash without changing what it classifies.
                if (std::find(categories.begin(), categories.end(), candidate) == categories.end())
                    categories.push_back(candidate);
            }
            element->category_refs = std::move(categories);
            return true;
        }
        if (operation.field == kTermFieldExternalReference) {
            element->external_reference = operation.new_value;
            return true;
        }
        if (operation.field == kTermFieldOrigin) {
            // An origin is a reference to the element the definition came from,
            // not free text -- the library refuses one that does not resolve, so
            // catching it here turns an acceptance-time failure into a staging
            // message the agent can act on.
            if (!operation.new_value.empty() && FindElement(model, operation.new_value) == nullptr) {
                error = "UpdateTerm names origin " + operation.new_value +
                        ", which does not exist. An origin is the id of the element the definition comes from; "
                        "use \"external_reference\" for a citation string such as a URL or standard clause.";
                return false;
            }
            element->origin_ref = operation.new_value;
            return true;
        }
        error = "UpdateTerm field must be \"value\", \"definition\", \"name\", \"category\", "
                "\"external_reference\", or \"origin\", not \"" +
                operation.field + "\".";
        return false;
    case PatchOperationType::UpdateCategory:
        if (element->type != "category") {
            error = "UpdateCategory targets " + element_id + ", which is a " + element->type + ", not a category.";
            return false;
        }
        if (operation.field == kCategoryFieldName) {
            if (operation.new_value.empty()) {
                error = "UpdateCategory on " + element_id + " would blank the category's name.";
                return false;
            }
            WriteField(element->name, element->name_langs, operation.new_value, {});
            return true;
        }
        if (operation.field == kCategoryFieldDescription) {
            WriteField(element->description, element->description_langs, primary, operation.translations);
            return true;
        }
        error = "UpdateCategory field must be \"name\" or \"description\", not \"" + operation.field + "\".";
        return false;
    case PatchOperationType::UpdateElementName:
        WriteField(element->name, element->name_langs, primary, operation.translations);
        return true;
    case PatchOperationType::UpdateElementText:
        if (operation.field == "name") {
            WriteField(element->name, element->name_langs, primary, operation.translations);
        } else if (operation.field == "content" || operation.field.empty()) {
            // Which field holds an element's text is decided by its kind, not by
            // the caller, and the two refusals here are one rule seen from both
            // sides -- the mirror of the `description` guard below.
            //
            // An omitted field is resolved rather than refused: the caller
            // asserted nothing, so there is nothing to contradict. A field that
            // NAMES `content` on a kind that has none is refused, because the
            // promotion seam cannot write it either; letting it through produced
            // a draft that rendered correctly and then failed acceptance with
            // "writing text on C2 failed" and no way forward.
            if (!core::ElementCarriesContent(*element)) {
                if (!operation.field.empty()) {
                    error = "Element " + element_id + " is a " + element->type +
                            ", which has no content; its text is its description. "
                            "Update the 'description' field instead.";
                    return false;
                }
                WriteField(element->description, element->description_langs, primary, operation.translations);
                return true;
            }
            WriteField(element->content, element->content_langs, primary, operation.translations);
        } else if (operation.field == "description") {
            // Refused rather than ignored. Before ADR 0012 this landed in a
            // second Description slot that no longer exists; applying it as a
            // silent no-op would let a proposal report success while changing
            // nothing the reviewer approved -- the worst outcome for an operation
            // that edits a safety argument. An agent gets told which field to use.
            if (core::ClaimLikeCarriesStatementAsDescription(*element)) {
                error = "Element " + element_id + " is a " + element->type +
                        ", which carries one description and that description is its statement. "
                        "Update the 'content' field instead.";
                return false;
            }
            WriteField(element->description, element->description_langs, primary, operation.translations);
        } else {
            error = "Unsupported UpdateElementText field: " + operation.field;
            return false;
        }
        return true;
    case PatchOperationType::SetUndeveloped:
        element->undeveloped = true;
        return true;
    case PatchOperationType::ClearUndeveloped:
        element->undeveloped = false;
        return true;
    case PatchOperationType::SetEvidenceLocation:
        if (element->type != "artifactreference") {
            error = "SetEvidenceLocation targets " + element->id + ", which is not evidence (an ArtifactReference).";
            return false;
        }
        element->artifact_location = operation.new_value;
        return true;
    case PatchOperationType::SetCseAttribute: {
        if (element->type != "assertedevidence") {
            error = "SetCseAttribute targets " + element->id + ", which does not carry claim-evidence support.";
            return false;
        }
        CseAttribute cse_attribute = CseAttribute::AssessmentStatus;
        if (!ParseCseAttribute(operation.field, cse_attribute)) {
            error = "SetCseAttribute names no CSE column: \"" + operation.field +
                    "\". Use claim_owner, evidence_owner, safety_case_owner, claim_criteria, evidence_criteria, "
                    "assessment_status or notes.";
            return false;
        }
        CseRecordField(element->cse_assessment, cse_attribute) = operation.new_value;
        return true;
    }
    case PatchOperationType::SetEvidenceAttribute: {
        if (element->type != "artifactreference") {
            error = "SetEvidenceAttribute targets " + element->id + ", which is not evidence (an ArtifactReference).";
            return false;
        }
        EvidenceAttribute attribute = EvidenceAttribute::Owner;
        if (!ParseEvidenceAttribute(operation.field, attribute)) {
            error = "SetEvidenceAttribute names no evidence column: \"" + operation.field +
                    "\". Use owner, type, version, date, maturity, controlled_environment or notes.";
            return false;
        }
        EvidenceRecordField(element->evidence, attribute) = operation.new_value;
        return true;
    }
    default:
        error = "Unsupported update operation.";
        return false;
    }
}

bool ApplyAddRelationshipOperation(const PatchOperation& operation,
                                   parser::AssuranceCase& model,
                                   const std::map<std::string, std::string>& generated_ids,
                                   std::unordered_set<std::string>& ids,
                                   std::string& error) {
    std::string source_id;
    std::string target_id;
    if (!ResolveOptionalElementRef(operation.source, model, generated_ids, "source", source_id, error) ||
        !ResolveOptionalElementRef(operation.target, model, generated_ids, "target", target_id, error)) {
        return false;
    }

    const parser::SacmElement* source = FindElement(model, source_id);
    const parser::SacmElement* target = FindElement(model, target_id);
    if (!source || !target) {
        error = "Relationship operation references an element that does not exist.";
        return false;
    }

    parser::SacmElement relationship;
    relationship.id = GenerateRelationshipId(ids);
    relationship.target_refs.push_back(target_id);

    if (operation.type == PatchOperationType::AddInContextOf) {
        relationship.type = "assertedcontext";
        relationship.source_refs.push_back(source_id);
    } else if (IsEvidenceLikeElement(*source)) {
        relationship.type = "assertedevidence";
        relationship.source_refs.push_back(source_id);
    } else {
        relationship.type = "assertedinference";
        if (source->type == "argumentreasoning") {
            relationship.reasoning_ref = source_id;
        } else {
            relationship.source_refs.push_back(source_id);
        }
    }

    model.elements.push_back(std::move(relationship));
    return true;
}

bool RelationshipMatches(const parser::SacmElement& relationship,
                         PatchOperationType operation_type,
                         const std::string& source_id,
                         const std::string& target_id) {
    const bool target_matches =
        std::find(relationship.target_refs.begin(), relationship.target_refs.end(), target_id) !=
        relationship.target_refs.end();
    if (!target_matches)
        return false;

    if (operation_type == PatchOperationType::RemoveInContextOf) {
        return relationship.type == "assertedcontext" &&
               std::find(relationship.source_refs.begin(), relationship.source_refs.end(), source_id) !=
                   relationship.source_refs.end();
    }

    if (relationship.type == "assertedevidence") {
        return std::find(relationship.source_refs.begin(), relationship.source_refs.end(), source_id) !=
               relationship.source_refs.end();
    }

    if (relationship.type != "assertedinference")
        return false;
    if (relationship.reasoning_ref == source_id)
        return true;
    return std::find(relationship.source_refs.begin(), relationship.source_refs.end(), source_id) !=
           relationship.source_refs.end();
}

bool IsDanglingRelationship(const parser::SacmElement& relationship) {
    if (!IsRelationshipType(relationship.type))
        return false;
    if (relationship.target_refs.empty())
        return true;
    if (relationship.type == "assertedinference") {
        return relationship.source_refs.empty() && relationship.reasoning_ref.empty();
    }
    return relationship.source_refs.empty();
}

bool ApplyRemoveRelationshipOperation(const PatchOperation& operation,
                                      parser::AssuranceCase& model,
                                      const std::map<std::string, std::string>& generated_ids,
                                      std::string& error) {
    std::string source_id;
    std::string target_id;
    if (!ResolveOptionalElementRef(operation.source, model, generated_ids, "source", source_id, error) ||
        !ResolveOptionalElementRef(operation.target, model, generated_ids, "target", target_id, error)) {
        return false;
    }

    bool removed = false;
    for (parser::SacmElement& relationship : model.elements) {
        if (!RelationshipMatches(relationship, operation.type, source_id, target_id))
            continue;
        if (relationship.type == "assertedinference" && relationship.reasoning_ref == source_id) {
            relationship.reasoning_ref.clear();
            removed = true;
        }
        removed = RemoveValue(relationship.source_refs, source_id) || removed;
    }

    model.elements.erase(
        std::remove_if(model.elements.begin(),
                       model.elements.end(),
                       [](const parser::SacmElement& element) { return IsDanglingRelationship(element); }),
        model.elements.end());

    if (!removed) {
        error = "No matching relationship was found to remove.";
        return false;
    }
    return true;
}

// Removing a term takes only the term: nothing in the flat model references a
// term directly (a visible term context references its ArtifactReference), so
// there is no relationship sweep. Whether the term is referenced from an
// argument package in the authoritative document is the library's call at
// acceptance -- an in-use term's removal is refused there rather than silently
// cascading (SACM-CMD-007).
bool ApplyRemoveTermOperation(const PatchOperation& operation,
                              parser::AssuranceCase& model,
                              const std::map<std::string, std::string>& generated_ids,
                              std::string& error) {
    std::string element_id;
    if (!ResolveOptionalElementRef(operation.element, model, generated_ids, "element", element_id, error))
        return false;
    const parser::SacmElement* element = FindElement(model, element_id);
    if (element == nullptr) {
        error = "RemoveTerm references missing element " + element_id + ".";
        return false;
    }
    if (element->type != "term") {
        error = "RemoveTerm targets " + element_id + ", which is a " + element->type +
                ", not a term. Use RemoveElement for argument elements.";
        return false;
    }
    model.elements.erase(std::remove_if(model.elements.begin(),
                                        model.elements.end(),
                                        [&](const parser::SacmElement& entry) { return entry.id == element_id; }),
                         model.elements.end());
    return true;
}

bool ApplyRemoveElementOperation(const PatchOperation& operation,
                                 parser::AssuranceCase& model,
                                 const std::map<std::string, std::string>& generated_ids,
                                 std::string& error) {
    std::string element_id;
    if (!ResolveOptionalElementRef(operation.element, model, generated_ids, "element", element_id, error))
        return false;
    if (!FindElement(model, element_id)) {
        error = "RemoveElement references missing element " + element_id + ".";
        return false;
    }

    if (!operation.field.empty()) {
        std::optional<RemoveMode> mode = RemoveModeFromField(operation.field, error);
        if (!mode.has_value())
            return false;
        return RemoveElement(model, nullptr, element_id, mode.value(), error);
    }

    model.elements.erase(
        std::remove_if(model.elements.begin(),
                       model.elements.end(),
                       [&](const parser::SacmElement& element) {
                           if (element.id == element_id)
                               return true;
                           if (!IsRelationshipType(element.type))
                               return false;
                           return std::find(element.source_refs.begin(), element.source_refs.end(), element_id) !=
                                      element.source_refs.end() ||
                                  std::find(element.target_refs.begin(), element.target_refs.end(), element_id) !=
                                      element.target_refs.end() ||
                                  element.reasoning_ref == element_id;
                       }),
        model.elements.end());
    return true;
}

bool ApplyOperation(const PatchOperation& operation,
                    parser::AssuranceCase& model,
                    const std::map<std::string, std::string>& generated_ids,
                    std::unordered_set<std::string>& ids,
                    std::string& error) {
    if (IsCreateOperation(operation.type)) {
        return ApplyCreateOperation(operation, model, generated_ids, error);
    }

    switch (operation.type) {
    case PatchOperationType::UpdateElementText:
    case PatchOperationType::UpdateElementName:
    case PatchOperationType::SetUndeveloped:
    case PatchOperationType::ClearUndeveloped:
    case PatchOperationType::SetEvidenceLocation:
    case PatchOperationType::SetEvidenceAttribute:
    case PatchOperationType::SetCseAttribute:
    case PatchOperationType::UpdateTerm:
    case PatchOperationType::UpdateCategory:
        return ApplyUpdateOperation(operation, model, generated_ids, error);
    case PatchOperationType::AddSupportedBy:
    case PatchOperationType::AddInContextOf:
        return ApplyAddRelationshipOperation(operation, model, generated_ids, ids, error);
    case PatchOperationType::RemoveSupportedBy:
    case PatchOperationType::RemoveInContextOf:
        return ApplyRemoveRelationshipOperation(operation, model, generated_ids, error);
    case PatchOperationType::RemoveElement:
        return ApplyRemoveElementOperation(operation, model, generated_ids, error);
    case PatchOperationType::RemoveTerm:
        return ApplyRemoveTermOperation(operation, model, generated_ids, error);
    default:
        error = "Unsupported proposal operation.";
        return false;
    }
}

} // namespace

bool AllocateProposalIds(const ReviewProposal& proposal,
                         const parser::AssuranceCase& current_model,
                         const std::unordered_set<std::string>& reserved,
                         std::map<std::string, std::string>& ids,
                         std::string& error) {
    std::unordered_set<std::string> taken = CollectIds(current_model);
    taken.insert(reserved.begin(), reserved.end());
    // Identities this map already pins are off limits too, or extending a patch
    // would hand a new create operation an id an earlier one is already using.
    for (const auto& [create_ref, id] : ids)
        taken.insert(id);

    std::unordered_set<std::string> seen_refs;
    for (const PatchOperation& operation : proposal.operations) {
        if (!IsCreateOperation(operation.type))
            continue;
        if (!operation.create_ref.has_value() || !ValidateCreateRefName(operation.create_ref.value(), error))
            return false;
        const std::string& create_ref = operation.create_ref.value();
        if (!seen_refs.insert(create_ref).second) {
            error = "Duplicate patch-local create_ref: " + create_ref;
            return false;
        }
        if (ids.count(create_ref) > 0)
            continue;
        ids[create_ref] = GenerateUniqueId(taken, PrefixFor(operation.type));
    }
    return true;
}

ProposalPreviewResult ReviewProposalPatchService::BuildPreviewModel(const ReviewProposal& proposal,
                                                                    const parser::AssuranceCase& current_model) const {
    ProposalPreviewResult result;
    result.preview_model = current_model;

    if (!CollectGeneratedIds(proposal, current_model, result.generated_ids, result.error)) {
        return result;
    }

    std::unordered_set<std::string> ids = CollectIds(result.preview_model);
    for (size_t i = 0; i < proposal.operations.size(); ++i) {
        std::string error;
        if (!ApplyOperation(proposal.operations[i], result.preview_model, result.generated_ids, ids, error)) {
            std::ostringstream message;
            message << "Operation " << (i + 1) << " failed: " << error;
            result.error = message.str();
            return result;
        }
    }

    result.success = true;
    result.error.clear();
    return result;
}

ApplyProposalResult ReviewProposalPatchService::ApplyProposal(const ReviewProposal& proposal,
                                                              parser::AssuranceCase& current_model) const {
    ProposalPreviewResult preview = BuildPreviewModel(proposal, current_model);
    ApplyProposalResult result;
    result.generated_ids = preview.generated_ids;
    if (!preview.success) {
        result.error = preview.error;
        return result;
    }

    current_model = std::move(preview.preview_model);
    result.success = true;
    result.error.clear();
    return result;
}

ApplyProposalResult
ReviewProposalPatchService::ApplyProposalWithIds(const ReviewProposal& proposal,
                                                 parser::AssuranceCase& current_model,
                                                 const std::map<std::string, std::string>& predetermined_ids) const {
    ApplyProposalResult result;
    result.generated_ids = predetermined_ids;

    // Validate that every create operation has a matching predetermined id.
    for (const PatchOperation& operation : proposal.operations) {
        if (!IsCreateOperation(operation.type))
            continue;
        if (!operation.create_ref.has_value() || operation.create_ref->empty()) {
            result.error = "Create operation is missing create_ref during replay.";
            return result;
        }
        if (predetermined_ids.find(operation.create_ref.value()) == predetermined_ids.end()) {
            result.error = "Missing predetermined id for create_ref '" + operation.create_ref.value() + "'.";
            return result;
        }
    }

    parser::AssuranceCase preview = current_model;
    std::unordered_set<std::string> ids = CollectIds(preview);
    for (size_t i = 0; i < proposal.operations.size(); ++i) {
        std::string error;
        if (!ApplyOperation(proposal.operations[i], preview, predetermined_ids, ids, error)) {
            std::ostringstream message;
            message << "Operation " << (i + 1) << " failed: " << error;
            result.error = message.str();
            return result;
        }
    }

    current_model = std::move(preview);
    result.success = true;
    result.error.clear();
    return result;
}

} // namespace core::reviews
