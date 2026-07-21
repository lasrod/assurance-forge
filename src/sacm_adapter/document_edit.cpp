#include "sacm_adapter/document_edit.h"

#include "sacm_adapter/library_document_access.h"

#include "sacm/commands/mutation.h"
#include "sacm/commands/operations.h"
#include "sacm/metadata/element_kind.h"
#include "sacm/model/argumentation.h"
#include "sacm/model/document.h"
#include "sacm/model/element.h"
#include "sacm/model/element_id.h"
#include "sacm/model/lang_string.h"

#include <optional>
#include <string>

namespace sacm_adapter {

namespace {

// The app's primary editor language. `core::SetElementTextField` writes the
// canonical `name`/`content` scalar only for this language, so it is the code
// the app treats as an element's primary text (mirrored by the projection,
// which maps a lang-less library entry to "en").
constexpr const char* kPrimaryLanguage = "en";

// Flatten a library MutationResult into the adapter's string-only EditOutcome.
EditOutcome applied_outcome(const sacm::commands::MutationResult& result) {
    EditOutcome outcome;
    outcome.supported = true;
    outcome.applied = result.applied;
    outcome.diagnostics.reserve(result.diagnostics.size());
    for (const sacm::validation::Diagnostic& diagnostic : result.diagnostics) {
        outcome.diagnostics.push_back(LoadDiagnostic{
            .code = diagnostic.code,
            .severity = std::string(sacm::validation::severity_name(diagnostic.severity)),
            .message = diagnostic.message,
        });
    }
    return outcome;
}

EditOutcome unsupported_outcome() {
    return EditOutcome{.supported = false, .applied = false, .diagnostics = {}};
}

// The app's `content` is a claim-like element's primary Description (clause
// 8.9). Other kinds carry their text elsewhere (Term/Expression `value`), where
// SetDescription would be wrong, so Content is only mapped for these kinds.
bool content_maps_to_description(sacm::metadata::ElementKind kind) {
    return kind == sacm::metadata::ElementKind::Claim ||
           kind == sacm::metadata::ElementKind::ArgumentReasoning;
}

// The library language whose entry a primary-language edit must overwrite.
// A legacy `content=` statement is stored lang-less (set("", ...)); editing it
// under "en" must overwrite that entry in place, not append a parallel "en"
// one that `primary()` would never return. So for a primary edit we target the
// front Description's existing language; other languages target themselves.
std::string effective_description_language(const sacm::model::ModelElement& element,
                                           const std::string& app_language) {
    if (app_language != kPrimaryLanguage) {
        return app_language;
    }
    if (!element.descriptions().empty()) {
        const std::vector<sacm::model::LangString>& values =
            element.descriptions().front()->content().values;
        if (!values.empty()) {
            return values.front().lang;
        }
    }
    // No statement yet: match the reader's lang-less convention for a created
    // Description so a later load round-trips identically.
    return "";
}

} // namespace

EditOutcome apply_text_edit(LibraryDocument& document, const std::string& element_id,
                            TextField field, const std::string& language,
                            const std::string& value) {
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);
    const sacm::model::ElementId id(element_id);

    switch (field) {
    case TextField::Name: {
        // SetName replaces the element's single name LangString (clause 8.6).
        // Under a non-primary language that would drop the primary name, so
        // multi-language name edits wait for the slice that handles the
        // reserved "sacm.import.name" TaggedValue.
        if (language != kPrimaryLanguage) {
            return unsupported_outcome();
        }
        const sacm::commands::Operation operation = sacm::commands::SetName{
            .element = id,
            .name = value,
            .language = language,
        };
        return applied_outcome(doc.apply(operation));
    }
    case TextField::Content: {
        const auto* element = doc.find_as<sacm::model::ModelElement>(id);
        if (element == nullptr || !content_maps_to_description(element->kind())) {
            return unsupported_outcome();
        }
        const sacm::commands::Operation operation = sacm::commands::SetDescription{
            .element = id,
            .text = value,
            .language = effective_description_language(*element, language),
        };
        return applied_outcome(doc.apply(operation));
    }
    case TextField::Description:
        // The secondary-note Description (claim) and non-claim Descriptions are
        // not wired yet; see the header. Report unsupported so the caller keeps
        // the legacy edit authoritative.
        return unsupported_outcome();
    }
    return unsupported_outcome();
}

namespace {

// Walks the containment chain from `element` to its owning ArgumentPackage,
// which is where a new claim/reasoning/reference and the linking relationship
// are created (mirroring core::FindOwningArgumentPackage). Null if the element
// is not inside one.
const sacm::model::ArgumentPackage* owning_argument_package(
    const sacm::model::SACMElement* element) {
    for (const sacm::model::SACMElement* cursor = element; cursor != nullptr;
         cursor = cursor->parent()) {
        if (const auto* package = dynamic_cast<const sacm::model::ArgumentPackage*>(cursor)) {
            return package;
        }
    }
    return nullptr;
}

AddChildOutcome unsupported_child() {
    return AddChildOutcome{.supported = false};
}

// Flatten a failed MutationResult into an AddChildOutcome (supported, not
// applied).
AddChildOutcome failed_child(const sacm::commands::MutationResult& result) {
    AddChildOutcome outcome;
    outcome.supported = true;
    outcome.applied = false;
    outcome.diagnostics.reserve(result.diagnostics.size());
    for (const sacm::validation::Diagnostic& diagnostic : result.diagnostics) {
        outcome.diagnostics.push_back(LoadDiagnostic{
            .code = diagnostic.code,
            .severity = std::string(sacm::validation::severity_name(diagnostic.severity)),
            .message = diagnostic.message,
        });
    }
    return outcome;
}

// The new element and the relationship linking it to the parent, per kind.
struct ChildPlan {
    sacm::commands::Operation create_element;
    std::optional<sacm::model::AssertionDeclaration> assertion;  // set for Assumption
    sacm::metadata::ElementKind relationship_kind;
    bool attach_via_reasoning = false;  // Strategy: the reasoning end, not a source
};

// Builds the create operations for `kind` under argument package `package_id`.
// Returns nullopt for kinds with no like-for-like library mapping yet.
std::optional<ChildPlan> plan_child(ChildKind kind, const sacm::model::ElementId& package_id) {
    using sacm::metadata::ElementKind;
    switch (kind) {
    case ChildKind::Goal:
        return ChildPlan{.create_element = sacm::commands::CreateClaim{.parent = package_id},
                         .assertion = std::nullopt,
                         .relationship_kind = ElementKind::AssertedInference};
    case ChildKind::Strategy:
        // Not wired: a strategy is added before any sub-goal exists, so its
        // AssertedInference would have a `reasoning` and a `target` but no
        // source -- which SACM's source [1..*] (clause 11.13) forbids. The
        // library correctly rejects that transient state the legacy app
        // tolerated; representing a bare strategy in valid SACM is an open
        // decision (docs/sacm/sacm-gsn-metamodel-gaps.md).
        return std::nullopt;
    case ChildKind::Solution:
        return ChildPlan{
            .create_element = sacm::commands::CreateArtifactReference{.parent = package_id},
            .assertion = std::nullopt,
            .relationship_kind = ElementKind::AssertedEvidence};
    case ChildKind::Context:
        return ChildPlan{
            .create_element = sacm::commands::CreateArtifactReference{.parent = package_id},
            .assertion = std::nullopt,
            .relationship_kind = ElementKind::AssertedContext};
    case ChildKind::Assumption:
        return ChildPlan{.create_element = sacm::commands::CreateClaim{.parent = package_id},
                         .assertion = sacm::model::AssertionDeclaration::Assumed,
                         .relationship_kind = ElementKind::AssertedContext};
    case ChildKind::Justification:
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace

AddChildOutcome apply_add_child(LibraryDocument& document, const std::string& parent_id,
                                ChildKind kind) {
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);
    const sacm::model::ElementId parent(parent_id);

    const sacm::model::ArgumentPackage* package = owning_argument_package(doc.find(parent));
    if (package == nullptr) {
        return unsupported_child();
    }
    const sacm::model::ElementId package_id = package->id();

    const std::optional<ChildPlan> plan = plan_child(kind, package_id);
    if (!plan.has_value()) {
        return unsupported_child();
    }

    // 1. Create the element and take the id the library generated for it.
    const sacm::commands::MutationResult created = doc.apply(plan->create_element);
    if (!created.applied || created.created_ids().empty()) {
        return failed_child(created);
    }
    const sacm::model::ElementId element_id = created.created_ids().front();

    // 2. Assumption is a claim marked `assumed`.
    if (plan->assertion.has_value()) {
        const sacm::commands::MutationResult declared = doc.apply(sacm::commands::SetAssertionDeclaration{
            .element = element_id, .declaration = *plan->assertion});
        if (!declared.applied) {
            return failed_child(declared);
        }
    }

    // 3. Link it to the parent: target is the parent (conclusion); the new
    //    element is the source (premise), except a Strategy's reasoning.
    sacm::commands::CreateAssertedRelationship relationship{
        .parent = package_id,
        .kind = plan->relationship_kind,
        .targets = {parent},
    };
    if (plan->attach_via_reasoning) {
        relationship.reasoning = element_id;
    } else {
        relationship.sources = {element_id};
    }
    const sacm::commands::MutationResult linked = doc.apply(relationship);
    if (!linked.applied || linked.created_ids().empty()) {
        return failed_child(linked);
    }

    AddChildOutcome outcome;
    outcome.supported = true;
    outcome.applied = true;
    outcome.new_element_id = element_id.value();
    outcome.new_relationship_id = linked.created_ids().front().value();
    return outcome;
}

} // namespace sacm_adapter
