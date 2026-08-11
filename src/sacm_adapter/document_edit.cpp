#include "sacm_adapter/document_edit.h"

#include "sacm_adapter/gsn_role_tag.h"
#include "sacm_adapter/library_document_access.h"

#include "core/sacm_model.h"
#include "sacm/commands/mutation.h"
#include "sacm/commands/operations.h"
#include "sacm/io/xmi.h"
#include "sacm/metadata/element_kind.h"
#include "sacm/model/argumentation.h"
#include "sacm/model/assurance_case.h"
#include "sacm/model/document.h"
#include "sacm/model/element.h"
#include "sacm/model/element_id.h"
#include "sacm/model/lang_string.h"
#include "sacm/model/terminology.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

// An edit the seam UNDERSTANDS and REFUSES, as distinct from one it has no
// mapping for. The difference is load-bearing: `supported == false` tells the
// caller to keep the legacy edit authoritative, so refusing that way would route
// the very edit we are rejecting straight back into the legacy path. This says
// "the document is unchanged, and here is why" instead.
EditOutcome refused_outcome(std::string code, std::string message) {
    return EditOutcome{
        .supported = true,
        .applied = false,
        .diagnostics = {LoadDiagnostic{.code = std::move(code), .severity = "error", .message = std::move(message)}},
    };
}

// The app's `content` is a claim-like element's primary Description (clause
// 8.9). Other kinds carry their text elsewhere (Term/Expression `value`), where
// SetDescription would be wrong, so Content is only mapped for these kinds.
bool content_maps_to_description(sacm::metadata::ElementKind kind) {
    return kind == sacm::metadata::ElementKind::Claim || kind == sacm::metadata::ElementKind::ArgumentReasoning;
}

// The library language a primary-language edit must overwrite at Description
// slot `index`. A legacy `content=`/`description=` value is stored lang-less
// (set("", ...)); editing it under the primary language must overwrite that
// entry in place, not append a parallel "en" one that `primary()` would never
// return. So we target the existing slot's stored language, or the reader's
// lang-less convention for a slot that does not exist yet (so a later load
// round-trips identically).
std::string description_slot_language(const sacm::model::ModelElement& element, std::size_t index) {
    if (index < element.descriptions().size()) {
        const std::vector<sacm::model::LangString>& values = element.descriptions()[index]->content().values;
        if (!values.empty()) {
            return values.front().lang;
        }
    }
    return "";
}

std::string primary_description_language(const sacm::model::ModelElement& element) {
    return description_slot_language(element, 0);
}

} // namespace

EditOutcome apply_text_edit(LibraryDocument& document,
                            const std::string& element_id,
                            TextField field,
                            const std::string& language,
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
        // Content is the FIRST Description (the statement, clause 8.9). A primary
        // edit overwrites slot 0's stored (possibly lang-less) entry in place; a
        // non-primary edit adds/updates that language's LangString, which the
        // projection reads back into content_langs. SetDescription targets the
        // front Description (creating it when absent) = slot 0.
        const std::string target_language =
            language == kPrimaryLanguage ? description_slot_language(*element, 0) : language;
        return applied_outcome(doc.apply(sacm::commands::SetDescription{
            .element = id,
            .text = value,
            .language = target_language,
        }));
    }
    case TextField::Description: {
        const auto* element = doc.find_as<sacm::model::ModelElement>(id);
        if (element == nullptr) {
            return unsupported_outcome();
        }
        if (content_maps_to_description(element->kind())) {
            // A claim-like element has exactly ONE Description -- its statement,
            // reached through TextField::Content (ADR 0012). It has no note slot,
            // so there is nothing for this field to write. Writing slot 1 here is
            // what produced the redundant second Description that clause 8.9 does
            // not sanction.
            //
            // REFUSED, not unsupported. `supported == false` means "no mapping
            // yet, keep the legacy edit authoritative", which would hand this edit
            // back to the legacy path and reinstate the silent drop this change
            // exists to remove. The diagnostic names the field to use, because the
            // caller may be an agent with no other way to find out.
            return refused_outcome("AF-EDIT-CLAIM-HAS-NO-NOTE",
                                   "A Claim or ArgumentReasoning carries one description and that description is "
                                   "its statement; edit the content field instead.");
        }
        // For every other element the POD `description` IS the front Description,
        // so it maps directly (same language handling as Content).
        const std::string target_language =
            language == kPrimaryLanguage ? description_slot_language(*element, 0) : language;
        return applied_outcome(doc.apply(sacm::commands::SetDescription{
            .element = id,
            .text = value,
            .language = target_language,
        }));
    }
    }
    return unsupported_outcome();
}

namespace {

// Walks the containment chain from `element` to its owning ArgumentPackage,
// which is where a new claim/reasoning/reference and the linking relationship
// are created (mirroring core::FindOwningArgumentPackage). Null if the element
// is not inside one.
const sacm::model::ArgumentPackage* owning_argument_package(const sacm::model::SACMElement* element) {
    for (const sacm::model::SACMElement* cursor = element; cursor != nullptr; cursor = cursor->parent()) {
        if (const auto* package = dynamic_cast<const sacm::model::ArgumentPackage*>(cursor)) {
            return package;
        }
    }
    return nullptr;
}

// The argument package a top goal is created in: the root's first, mirroring
// core::FindOwningArgumentPackage's fallback for an element with no owning
// relationship. Null if the document has no argument package.
const sacm::model::ArgumentPackage* first_argument_package(const sacm::model::Document& doc) {
    if (doc.roots().empty()) {
        return nullptr;
    }
    const auto& packages = doc.roots().front()->argument_packages();
    return packages.empty() ? nullptr : packages.front().get();
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
    std::optional<sacm::model::AssertionDeclaration> assertion; // set for Assumption/Justification
    // GSN node role to record as a vendor TaggedValue (e.g. "Justification"),
    // preserving a GSN type SACM's AssertionDeclaration cannot express on its own.
    std::optional<std::string> gsn_role;
    sacm::metadata::ElementKind relationship_kind;
    bool attach_via_reasoning = false; // Strategy: the reasoning end, not a source
};

// Builds the create operations for `kind` under argument package `package_id`.
// Returns nullopt for kinds with no like-for-like library mapping yet.
// Converts a possibly-empty id string to the library's optional id: empty means
// "let the library generate one".
std::optional<sacm::model::ElementId> to_optional_id(const std::string& id) {
    if (id.empty()) {
        return std::nullopt;
    }
    return sacm::model::ElementId(id);
}

std::optional<ChildPlan> plan_child(ChildKind kind,
                                    const sacm::model::ElementId& package_id,
                                    const std::optional<sacm::model::ElementId>& element_id) {
    using sacm::metadata::ElementKind;
    switch (kind) {
    case ChildKind::Goal:
        return ChildPlan{.create_element = sacm::commands::CreateClaim{.parent = package_id, .id = element_id},
                         .assertion = std::nullopt,
                         .relationship_kind = ElementKind::AssertedInference};
    case ChildKind::Strategy:
        // Unreachable: apply_add_child intercepts Strategy before calling
        // plan_child, because a strategy is added before any sub-goal exists and
        // maps to only an ArgumentReasoning (no inference yet -- a sourceless
        // inference violates SACM source [1..*], clause 11.13). Kept for switch
        // exhaustiveness; see the Strategy branch in apply_add_child and
        // docs/sacm/sacm-gsn-metamodel-gaps.md.
        return std::nullopt;
    case ChildKind::Solution:
        return ChildPlan{.create_element =
                             sacm::commands::CreateArtifactReference{.parent = package_id, .id = element_id},
                         .assertion = std::nullopt,
                         .relationship_kind = ElementKind::AssertedEvidence};
    case ChildKind::Context:
        return ChildPlan{.create_element =
                             sacm::commands::CreateArtifactReference{.parent = package_id, .id = element_id},
                         .assertion = std::nullopt,
                         .relationship_kind = ElementKind::AssertedContext};
    case ChildKind::Assumption:
        return ChildPlan{.create_element = sacm::commands::CreateClaim{.parent = package_id, .id = element_id},
                         .assertion = sacm::model::AssertionDeclaration::Assumed,
                         .relationship_kind = ElementKind::AssertedContext};
    case ChildKind::Justification:
        // A GSN Justification maps to a Claim with assertionDeclaration =
        // axiomatic (the standards-correct mapping, docs/sacm/sacm-gsn-mapping.md
        // -- not the legacy non-standard "justification" literal). SACM cannot
        // distinguish a Justification from an axiomatically-asserted Goal, so the
        // GSN role is preserved in a vendor TaggedValue and translated back by
        // the projection.
        return ChildPlan{.create_element = sacm::commands::CreateClaim{.parent = package_id, .id = element_id},
                         .assertion = sacm::model::AssertionDeclaration::Axiomatic,
                         .gsn_role = std::string(kGsnRoleJustification),
                         .relationship_kind = ElementKind::AssertedContext};
    }
    return std::nullopt;
}

// Best-effort removal of an element created earlier in a compound operation
// that then failed, so a partial failure leaves no orphan behind. Any
// relationship already attached to it comes with it.
void rollback_element(sacm::model::Document& doc, const sacm::model::ElementId& element_id) {
    doc.apply(sacm::commands::DeleteElement{
        .target = element_id,
        .reference_policy = sacm::commands::ReferenceDeletePolicy::DeleteReferencingRelationships,
    });
}

// The strategy's single inference, if it has been materialized: the
// AssertedRelationship in `package` whose reasoning end is `strategy`.
const sacm::model::AssertedRelationship* find_strategy_inference(const sacm::model::ArgumentPackage& package,
                                                                 const sacm::model::ElementId& strategy) {
    for (const auto& element : package.argument_elements()) {
        const auto* relationship = dynamic_cast<const sacm::model::AssertedRelationship*>(element.get());
        if (relationship != nullptr && relationship->reasoning().has_value() &&
            *relationship->reasoning() == strategy) {
            return relationship;
        }
    }
    return nullptr;
}

// The goal a bare strategy will support, recorded in its strategyTarget vendor
// tag by apply_add_child(Strategy). Empty if absent.
std::string strategy_target_of(const sacm::model::ModelElement& strategy) {
    for (const auto& tag : strategy.tagged_values()) {
        if (tag->key().primary() == kGsnStrategyTargetTagKey) {
            return std::string(tag->content().primary());
        }
    }
    return {};
}

sacm::commands::MutationResult add_gsn_identifier_tag(sacm::model::Document& document,
                                                      const sacm::model::ElementId& element) {
    return document.apply(sacm::commands::AddTaggedValue{
        .element = element,
        .key = core::kGsnIdentifierTagKey,
        .value = element.value(),
    });
}

// A sub-goal added under a strategy is a source of the strategy's single
// inference (the standard one-inference GSN->SACM encoding, not the legacy
// per-sub-goal inference). Materialize `{target = the goal the strategy supports,
// reasoning = strategy, source = sub-goal}` on the first sub-goal (from the
// strategyTarget tag); extend that inference's sources on later ones. The tag is
// left in place -- once the inference exists it is ignored (only a *bare*
// strategy consults it), and leaving it keeps the live and replayed encodings
// identical.
AddChildOutcome apply_add_subgoal_under_strategy(sacm::model::Document& doc,
                                                 const sacm::model::ElementId& strategy,
                                                 const sacm::model::ElementId& package_id,
                                                 const std::string& element_id,
                                                 const std::string& relationship_id) {
    const sacm::commands::MutationResult created =
        doc.apply(sacm::commands::CreateClaim{.parent = package_id, .id = to_optional_id(element_id)});
    if (!created.applied || created.created_ids().empty()) {
        return failed_child(created);
    }
    const sacm::model::ElementId goal_id = created.created_ids().front();
    const sacm::commands::MutationResult identifier_tagged = add_gsn_identifier_tag(doc, goal_id);
    if (!identifier_tagged.applied) {
        rollback_element(doc, goal_id);
        return failed_child(identifier_tagged);
    }

    // Re-fetch the package after the create (the element vector may have grown).
    const auto* package = doc.find_as<sacm::model::ArgumentPackage>(package_id);
    if (package == nullptr) {
        rollback_element(doc, goal_id);
        return unsupported_child();
    }

    if (const sacm::model::AssertedRelationship* inference = find_strategy_inference(*package, strategy)) {
        // Later sub-goal: extend the existing inference's sources.
        const sacm::model::ElementId inference_id = inference->id();
        const sacm::commands::MutationResult extended =
            doc.apply(sacm::commands::AddRelationshipSource{.relationship = inference_id, .source = goal_id});
        if (!extended.applied) {
            rollback_element(doc, goal_id);
            return failed_child(extended);
        }
        AddChildOutcome outcome;
        outcome.supported = true;
        outcome.applied = true;
        outcome.new_element_id = goal_id.value();
        // No relationship is created (the existing inference is extended), so
        // new_relationship_id stays empty per the AddChildOutcome contract -- and
        // the audit event correspondingly records no generated relationship id
        // (replay finds the inference to extend itself).
        return outcome;
    }

    // First sub-goal: materialize the inference against the goal the strategy
    // supports (its strategyTarget tag).
    const auto* strategy_element = doc.find_as<sacm::model::ModelElement>(strategy);
    const std::string target = strategy_element ? strategy_target_of(*strategy_element) : std::string{};
    if (target.empty()) {
        rollback_element(doc, goal_id);
        return unsupported_child();
    }
    const sacm::commands::MutationResult materialized =
        doc.apply(sacm::commands::CreateAssertedRelationship{.parent = package_id,
                                                             .kind = sacm::metadata::ElementKind::AssertedInference,
                                                             .id = to_optional_id(relationship_id),
                                                             .sources = {goal_id},
                                                             .targets = {sacm::model::ElementId(target)},
                                                             .reasoning = strategy});
    if (!materialized.applied || materialized.created_ids().empty()) {
        rollback_element(doc, goal_id);
        return failed_child(materialized);
    }
    AddChildOutcome outcome;
    outcome.supported = true;
    outcome.applied = true;
    outcome.new_element_id = goal_id.value();
    outcome.new_relationship_id = materialized.created_ids().front().value();
    return outcome;
}

} // namespace

AddChildOutcome apply_add_child(LibraryDocument& document,
                                const std::string& parent_id,
                                ChildKind kind,
                                const std::string& element_id,
                                const std::string& relationship_id) {
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);
    const sacm::model::ElementId parent(parent_id);

    const sacm::model::ArgumentPackage* package = owning_argument_package(doc.find(parent));
    if (package == nullptr) {
        return unsupported_child();
    }
    const sacm::model::ElementId package_id = package->id();

    // A Strategy is added before it has any sub-goals; its AssertedInference
    // would have no source (SACM source [1..*]), so create only the
    // ArgumentReasoning and record the goal it will support in a vendor tag
    // (app-defer). The inference is materialized when the first sub-goal gives it
    // a source -- a following increment (extending an existing inference's
    // sources for the second and later sub-goals needs a new library op).
    if (kind == ChildKind::Strategy) {
        const sacm::commands::MutationResult created_strategy =
            doc.apply(sacm::commands::CreateArgumentReasoning{.parent = package_id, .id = to_optional_id(element_id)});
        if (!created_strategy.applied || created_strategy.created_ids().empty()) {
            return failed_child(created_strategy);
        }
        const sacm::model::ElementId strategy_id = created_strategy.created_ids().front();
        const sacm::commands::MutationResult tagged = doc.apply(sacm::commands::AddTaggedValue{
            .element = strategy_id, .key = kGsnStrategyTargetTagKey, .value = parent_id});
        if (!tagged.applied) {
            rollback_element(doc, strategy_id);
            return failed_child(tagged);
        }
        const sacm::commands::MutationResult identifier_tagged = add_gsn_identifier_tag(doc, strategy_id);
        if (!identifier_tagged.applied) {
            rollback_element(doc, strategy_id);
            return failed_child(identifier_tagged);
        }
        AddChildOutcome outcome;
        outcome.supported = true;
        outcome.applied = true;
        outcome.new_element_id = strategy_id.value();
        return outcome; // no relationship yet -- materialized on the first sub-goal
    }

    // A Goal added under a strategy (ArgumentReasoning) is a source of the
    // strategy's single inference, not a new inference targeting the strategy:
    // materialize that inference on the first sub-goal, extend it on later ones.
    // Other kinds under a strategy keep the normal path (they attach to the
    // strategy node itself, e.g. a context via AssertedContext).
    if (kind == ChildKind::Goal && doc.find(parent) != nullptr &&
        doc.find(parent)->kind() == sacm::metadata::ElementKind::ArgumentReasoning) {
        return apply_add_subgoal_under_strategy(doc, parent, package_id, element_id, relationship_id);
    }

    const std::optional<ChildPlan> plan = plan_child(kind, package_id, to_optional_id(element_id));
    if (!plan.has_value()) {
        return unsupported_child();
    }

    // 1. Create the element; take back the id (caller-supplied or generated).
    const sacm::commands::MutationResult created = doc.apply(plan->create_element);
    if (!created.applied || created.created_ids().empty()) {
        return failed_child(created);
    }
    const sacm::model::ElementId created_id = created.created_ids().front();
    const sacm::commands::MutationResult identifier_tagged = add_gsn_identifier_tag(doc, created_id);
    if (!identifier_tagged.applied) {
        rollback_element(doc, created_id);
        return failed_child(identifier_tagged);
    }

    // 2. Assumption/Justification set an assertion declaration.
    if (plan->assertion.has_value()) {
        const sacm::commands::MutationResult declared =
            doc.apply(sacm::commands::SetAssertionDeclaration{.element = created_id, .declaration = *plan->assertion});
        if (!declared.applied) {
            rollback_element(doc, created_id);
            return failed_child(declared);
        }
    }

    // 2b. Preserve a GSN role (Justification) SACM cannot express, as a vendor tag.
    if (plan->gsn_role.has_value()) {
        const sacm::commands::MutationResult tagged = doc.apply(
            sacm::commands::AddTaggedValue{.element = created_id, .key = kGsnRoleTagKey, .value = *plan->gsn_role});
        if (!tagged.applied) {
            rollback_element(doc, created_id);
            return failed_child(tagged);
        }
    }

    // 3. Link it to the parent: target is the parent (conclusion); the new
    //    element is the source (premise), except a Strategy's reasoning.
    sacm::commands::CreateAssertedRelationship relationship{
        .parent = package_id,
        .kind = plan->relationship_kind,
        .id = to_optional_id(relationship_id),
        .targets = {parent},
    };
    if (plan->attach_via_reasoning) {
        relationship.reasoning = created_id;
    } else {
        relationship.sources = {created_id};
    }
    const sacm::commands::MutationResult linked = doc.apply(relationship);
    if (!linked.applied || linked.created_ids().empty()) {
        rollback_element(doc, created_id);
        return failed_child(linked);
    }

    AddChildOutcome outcome;
    outcome.supported = true;
    outcome.applied = true;
    outcome.new_element_id = created_id.value();
    outcome.new_relationship_id = linked.created_ids().front().value();
    return outcome;
}

AddChildOutcome apply_add_top_goal(LibraryDocument& document, const std::string& element_id) {
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);
    const sacm::model::ArgumentPackage* package = first_argument_package(doc);
    if (package == nullptr) {
        return unsupported_child();
    }
    const sacm::commands::MutationResult created =
        doc.apply(sacm::commands::CreateClaim{.parent = package->id(), .id = to_optional_id(element_id)});
    if (!created.applied || created.created_ids().empty()) {
        return failed_child(created);
    }
    const sacm::model::ElementId created_id = created.created_ids().front();
    const sacm::commands::MutationResult identifier_tagged = add_gsn_identifier_tag(doc, created_id);
    if (!identifier_tagged.applied) {
        rollback_element(doc, created_id);
        return failed_child(identifier_tagged);
    }
    AddChildOutcome outcome;
    outcome.supported = true;
    outcome.applied = true;
    outcome.new_element_id = created_id.value();
    return outcome; // a top goal has no parent, so no relationship
}

std::string resolve_argument_package_id(const LibraryDocument& document, const std::string& element_id) {
    const sacm::model::Document& doc = LibraryDocumentAccess::document(document);
    if (!element_id.empty()) {
        if (const sacm::model::ArgumentPackage* owner =
                owning_argument_package(doc.find(sacm::model::ElementId(element_id)))) {
            return owner->id().value();
        }
    }
    if (const sacm::model::ArgumentPackage* first = first_argument_package(doc)) {
        return first->id().value();
    }
    return {};
}

AddChildOutcome apply_create_element(LibraryDocument& document,
                                     const std::string& package_id,
                                     NewElementKind kind,
                                     const CreateElementFields& fields) {
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);
    const sacm::model::ElementId package(package_id);
    const auto* argument_package = dynamic_cast<const sacm::model::ArgumentPackage*>(doc.find(package));
    if (argument_package == nullptr) {
        return unsupported_child();
    }

    const std::optional<sacm::model::ElementId> id = to_optional_id(fields.element_id);
    sacm::commands::Operation create = sacm::commands::CreateClaim{
        .parent = package,
        .id = id,
        .name = fields.name,
        .description = fields.text,
        .language = fields.language,
    };
    std::optional<sacm::model::AssertionDeclaration> assertion;
    std::optional<std::string> gsn_role;
    switch (kind) {
    case NewElementKind::Claim:
        break;
    case NewElementKind::Assumption:
        assertion = sacm::model::AssertionDeclaration::Assumed;
        break;
    case NewElementKind::Justification:
        // Axiomatic plus the vendor role tag, the same mapping `plan_child` uses:
        // SACM cannot tell a Justification from an axiomatically-asserted Goal, so
        // the GSN role rides in a TaggedValue the projection reads back.
        assertion = sacm::model::AssertionDeclaration::Axiomatic;
        gsn_role = std::string(kGsnRoleJustification);
        break;
    case NewElementKind::ArgumentReasoning:
        create = sacm::commands::CreateArgumentReasoning{.parent = package, .id = id, .name = fields.name};
        break;
    case NewElementKind::ArtifactReference:
        create = sacm::commands::CreateArtifactReference{.parent = package, .id = id, .name = fields.name};
        break;
    }

    const sacm::commands::MutationResult created = doc.apply(create);
    if (!created.applied || created.created_ids().empty()) {
        return failed_child(created);
    }
    const sacm::model::ElementId created_id = created.created_ids().front();

    // Everything past the create rolls the element back on failure, so a partial
    // create leaves no half-formed element for the projection to render.
    const sacm::commands::MutationResult identifier_tagged = add_gsn_identifier_tag(doc, created_id);
    if (!identifier_tagged.applied) {
        rollback_element(doc, created_id);
        return failed_child(identifier_tagged);
    }
    if (assertion.has_value()) {
        const sacm::commands::MutationResult declared =
            doc.apply(sacm::commands::SetAssertionDeclaration{.element = created_id, .declaration = *assertion});
        if (!declared.applied) {
            rollback_element(doc, created_id);
            return failed_child(declared);
        }
    }
    if (gsn_role.has_value()) {
        const sacm::commands::MutationResult tagged =
            doc.apply(sacm::commands::AddTaggedValue{.element = created_id, .key = kGsnRoleTagKey, .value = *gsn_role});
        if (!tagged.applied) {
            rollback_element(doc, created_id);
            return failed_child(tagged);
        }
    }
    // An ArgumentReasoning or ArtifactReference carries no Description, so text
    // given for one is dropped rather than written somewhere it would not project
    // back. The planner refuses that case before reaching here.

    AddChildOutcome outcome;
    outcome.supported = true;
    outcome.applied = true;
    outcome.new_element_id = created_id.value();
    return outcome; // no relationship: a proposal attaches separately
}

AddChildOutcome apply_challenge(LibraryDocument& document,
                                const std::string& target_id,
                                ChallengeSource source,
                                const std::string& element_id,
                                const std::string& relationship_id) {
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);
    const sacm::model::ElementId target(target_id);

    // The counter element joins the target's owning ArgumentPackage. Unlike the
    // legacy helper, which cannot index a relationship, the library gives every
    // contained element (relationships included) a parent, so this resolves
    // uniformly whether the target is an element or a relationship.
    const sacm::model::ArgumentPackage* package = owning_argument_package(doc.find(target));
    if (package == nullptr) {
        return unsupported_child();
    }
    const sacm::model::ElementId package_id = package->id();
    const std::optional<sacm::model::ElementId> counter_id_hint = to_optional_id(element_id);

    sacm::commands::Operation create_counter;
    sacm::metadata::ElementKind relationship_kind = sacm::metadata::ElementKind::AssertedInference;
    switch (source) {
    case ChallengeSource::CounterArgument:
        create_counter = sacm::commands::CreateClaim{.parent = package_id, .id = counter_id_hint};
        relationship_kind = sacm::metadata::ElementKind::AssertedInference;
        break;
    case ChallengeSource::CounterEvidence:
        create_counter = sacm::commands::CreateArtifactReference{.parent = package_id, .id = counter_id_hint};
        relationship_kind = sacm::metadata::ElementKind::AssertedEvidence;
        break;
    }

    const sacm::commands::MutationResult created = doc.apply(create_counter);
    if (!created.applied || created.created_ids().empty()) {
        return failed_child(created);
    }
    const sacm::model::ElementId counter_id = created.created_ids().front();
    const sacm::commands::MutationResult identifier_tagged = add_gsn_identifier_tag(doc, counter_id);
    if (!identifier_tagged.applied) {
        rollback_element(doc, counter_id);
        return failed_child(identifier_tagged);
    }

    const sacm::commands::MutationResult linked = doc.apply(sacm::commands::CreateAssertedRelationship{
        .parent = package_id,
        .kind = relationship_kind,
        .id = to_optional_id(relationship_id),
        .sources = {counter_id},
        .targets = {target},
        .is_counter = true,
    });
    if (!linked.applied || linked.created_ids().empty()) {
        rollback_element(doc, counter_id);
        return failed_child(linked);
    }

    AddChildOutcome outcome;
    outcome.supported = true;
    outcome.applied = true;
    outcome.new_element_id = counter_id.value();
    outcome.new_relationship_id = linked.created_ids().front().value();
    return outcome;
}

namespace {

constexpr const char* kAcpMarkerKey = "assuranceForge.acp";

// Collects every ACP id already present in the document, so the next id does
// not collide across packages -- mirroring core's CollectAcpsForIdGeneration.
std::unordered_set<std::string> existing_acp_ids(const sacm::model::Document& doc) {
    std::unordered_set<std::string> ids;
    doc.for_each_element([&](const sacm::model::SACMElement& element) {
        const auto* model_element = dynamic_cast<const sacm::model::ModelElement*>(&element);
        if (model_element == nullptr) {
            return;
        }
        for (const auto& tag : model_element->tagged_values()) {
            if (tag->key().primary() == kAcpMarkerKey) {
                const std::string value(tag->content().primary());
                if (!value.empty()) {
                    ids.insert(value);
                }
            }
        }
    });
    return ids;
}

// The next free `ACP<n>` id, matching core::acp::NextAcpId exactly.
std::string next_acp_id(const std::unordered_set<std::string>& existing) {
    for (int index = 1; index < 100000; ++index) {
        std::string candidate = "ACP" + std::to_string(index);
        if (!existing.contains(candidate)) {
            return candidate;
        }
    }
    return "ACPx";
}

AcpOutcome unsupported_acp() {
    return AcpOutcome{.supported = false};
}

AcpOutcome failed_acp(const sacm::commands::MutationResult& result) {
    AcpOutcome outcome;
    outcome.supported = true;
    outcome.applied = false;
    for (const sacm::validation::Diagnostic& diagnostic : result.diagnostics) {
        outcome.diagnostics.push_back(LoadDiagnostic{
            .code = diagnostic.code,
            .severity = std::string(sacm::validation::severity_name(diagnostic.severity)),
            .message = diagnostic.message,
        });
    }
    return outcome;
}

// Adds one vendor TaggedValue to `element`. Returns the library's result so the
// caller can stop on the first failure and leave the document unchanged beyond
// what already applied.
sacm::commands::MutationResult add_tag(sacm::model::Document& doc,
                                       const sacm::model::ElementId& element,
                                       const std::string& key,
                                       const std::string& value,
                                       std::optional<sacm::model::ElementId> tag_id = {}) {
    return doc.apply(
        sacm::commands::AddTaggedValue{.element = element, .id = std::move(tag_id), .key = key, .value = value});
}

} // namespace

AcpOutcome apply_add_acp(LibraryDocument& document, const std::string& target_id, const std::string& requested_acp_id) {
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);
    const sacm::model::ElementId target(target_id);

    const sacm::model::SACMElement* element = doc.find(target);
    // Element ACPs are eligible only on an ArtifactReference (core's
    // ElementEligibleForAcp); claims and relationships are later slices.
    if (element == nullptr || element->kind() != sacm::metadata::ElementKind::ArtifactReference) {
        return unsupported_acp();
    }

    // A caller-supplied id is used verbatim (so an audited/replayed ACP add
    // reproduces the exact `ACP<n>` the legacy core::acp::AddAcp generated);
    // empty means generate the next free one.
    const std::unordered_set<std::string> existing = existing_acp_ids(doc);
    const std::string acp_id = requested_acp_id.empty() ? next_acp_id(existing) : requested_acp_id;

    // A caller-supplied id must be unused: reusing one already present would write
    // duplicate marker tags and make the projection ambiguous. (A correct replay
    // supplies an id that was unique when first generated, so this only guards a
    // corrupt or double-applied event.)
    if (!requested_acp_id.empty() && existing.count(acp_id) != 0) {
        AcpOutcome outcome;
        outcome.supported = true;
        outcome.applied = false;
        outcome.diagnostics.push_back(LoadDiagnostic{
            .code = "SACM-CMD-004",
            .severity = "Error",
            .message = "requested ACP id '" + acp_id + "' already exists",
        });
        return outcome;
    }

    // Marker + name + resolutionKind = none, matching core::acp::UpsertAcpTags
    // for a freshly added (unresolved) ACP. The ACP id is the marker's *value*,
    // not the tag element's id: forcing the tag element id to it would collide
    // with the library's global element-id uniqueness if a real element already
    // uses that id, so let the library generate the tag id.
    const sacm::commands::MutationResult marker = add_tag(doc, target, kAcpMarkerKey, acp_id);
    if (!marker.applied) {
        return failed_acp(marker);
    }
    const sacm::commands::MutationResult name =
        add_tag(doc, target, std::string(kAcpMarkerKey) + "." + acp_id + ".name", acp_id);
    if (!name.applied) {
        return failed_acp(name);
    }
    const sacm::commands::MutationResult resolution =
        add_tag(doc, target, std::string(kAcpMarkerKey) + "." + acp_id + ".resolutionKind", "none");
    if (!resolution.applied) {
        return failed_acp(resolution);
    }

    AcpOutcome outcome;
    outcome.supported = true;
    outcome.applied = true;
    outcome.acp_id = acp_id;
    return outcome;
}

DeleteOutcome apply_delete_element(LibraryDocument& document, const std::string& element_id) {
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);

    const sacm::commands::MutationResult result = doc.apply(sacm::commands::DeleteElement{
        .target = sacm::model::ElementId(element_id),
        // Reproduce the legacy core::RemoveElement exactly: SCRUB the deleted
        // element out of referencing relationships and drop a relationship only
        // once it is left structurally empty -- not the whole-relationship
        // cascade. This keeps a strategy's shared inference alive when one of its
        // several sub-goals is removed (the cascade would detach the strategy).
        .reference_policy = sacm::commands::ReferenceDeletePolicy::ScrubReferences,
    });

    DeleteOutcome outcome;
    outcome.supported = true;
    outcome.applied = result.applied;
    for (const sacm::validation::Diagnostic& diagnostic : result.diagnostics) {
        outcome.diagnostics.push_back(LoadDiagnostic{
            .code = diagnostic.code,
            .severity = std::string(sacm::validation::severity_name(diagnostic.severity)),
            .message = diagnostic.message,
        });
    }
    return outcome;
}

namespace {

// Models a SEQUENCE of deletes on a throwaway copy and reports their combined
// effects. Both delete previews are this function with different policies --
// `preview_delete_elements` for the GSN element path, and the terminology
// cascade -- because a preview that describes a different operation than the one
// the caller will perform is worse than no preview.
DeletePreview preview_delete_sequence(const LibraryDocument& document,
                                      const std::vector<std::string>& element_ids,
                                      sacm::commands::ReferenceDeletePolicy reference_policy,
                                      sacm::commands::CrossPackageReferencePolicy cross_package_policy) {
    DeletePreview preview;
    preview.supported = true;

    const sacm::model::Document& live = LibraryDocumentAccess::document(document);

    // The deletes are modelled on a scratch copy so the live document is never
    // touched and the effects compose. Round-tripping through the serializer is
    // how a copy is made at all -- Document is move-only by design, and the
    // tolerant mode keeps preserved compatibility content so the scratch is a
    // faithful stand-in rather than a stripped one.
    const sacm::io::SaveResult serialized =
        sacm::io::save_xmi_string(live, sacm::io::SaveOptions{.mode = sacm::io::Mode::Tolerant});
    if (!serialized.ok) {
        preview.supported = false;
        preview.diagnostics.push_back(LoadDiagnostic{
            .code = "AF-PREVIEW-001",
            .severity = "Warning",
            .message = "could not build a preview: the document did not serialize",
        });
        return preview;
    }
    sacm::io::LoadResult scratch = sacm::io::load_xmi_string(serialized.xml);
    if (!scratch.document.has_value()) {
        preview.supported = false;
        preview.diagnostics.push_back(LoadDiagnostic{
            .code = "AF-PREVIEW-001",
            .severity = "Warning",
            .message = "could not build a preview: the document did not reload",
        });
        return preview;
    }

    // Names have to be captured before anything is deleted -- afterwards the
    // elements they belong to are gone.
    std::unordered_map<std::string, std::string> names;
    scratch.document->for_each_element([&names](const sacm::model::SACMElement& element) {
        if (const auto* named = dynamic_cast<const sacm::model::ModelElement*>(&element)) {
            names.emplace(element.id().value(), named->name().content);
        }
    });

    const std::unordered_set<std::string> requested_ids(element_ids.begin(), element_ids.end());
    std::unordered_set<std::string> reported;
    bool any_applied = false;

    // Utility elements (clause 8.7: Description/ImplementationConstraint/Note/
    // TaggedValue) are attachments, not things in their own right. They are
    // deleted with their owner, and the owner is already in the list -- listing
    // them separately turns "this goal and its inference" into a dozen rows of
    // internal bookkeeping and buries the consequence that matters.
    const auto is_attachment = [](sacm::metadata::ElementKind kind) {
        return kind == sacm::metadata::ElementKind::Description ||
               kind == sacm::metadata::ElementKind::ImplementationConstraint ||
               kind == sacm::metadata::ElementKind::Note || kind == sacm::metadata::ElementKind::TaggedValue;
    };

    const auto record = [&](const sacm::commands::ChangeRecord& change) {
        if (is_attachment(change.kind)) {
            return;
        }
        const std::string id = change.id.value();
        // A relationship can be touched by several deletes in the set; report
        // each element once, and let an outright deletion win over a scrub
        // recorded for the same element earlier in the sequence.
        const bool deleted = change.change == sacm::commands::ChangeRecord::Change::Deleted ||
                             change.change == sacm::commands::ChangeRecord::Change::RelationshipDeleted;
        if (!reported.insert(id).second) {
            std::vector<DeleteEffect>& bucket = requested_ids.contains(id) ? preview.requested : preview.consequential;
            for (DeleteEffect& existing : bucket) {
                if (existing.element_id == id && deleted) {
                    existing.deleted = true;
                }
            }
            return;
        }
        const auto name = names.find(id);
        DeleteEffect effect{
            .element_id = id,
            .kind = std::string(sacm::metadata::kind_name(change.kind)),
            .name = name != names.end() ? name->second : std::string{},
            .is_relationship = change.change == sacm::commands::ChangeRecord::Change::RelationshipDeleted ||
                               sacm::metadata::is_asserted_relationship_kind(change.kind) ||
                               change.kind == sacm::metadata::ElementKind::ArtifactAssetRelationship,
            .deleted = deleted,
        };
        if (requested_ids.contains(id)) {
            preview.requested.push_back(std::move(effect));
        } else {
            preview.consequential.push_back(std::move(effect));
        }
    };

    for (const std::string& element_id : element_ids) {
        const sacm::model::ElementId id{element_id};
        if (scratch.document->find(id) == nullptr) {
            // Not a library element (or already removed as a consequence of an
            // earlier delete in this set). Both are ordinary, not errors.
            continue;
        }
        const sacm::commands::MutationResult result = scratch.document->apply(sacm::commands::DeleteElement{
            .target = id,
            .reference_policy = reference_policy,
            .cross_package_policy = cross_package_policy,
        });
        any_applied = any_applied || result.applied;
        for (const sacm::commands::ChangeRecord& change : result.changes) {
            record(change);
        }
        for (const sacm::validation::Diagnostic& diagnostic : result.diagnostics) {
            preview.diagnostics.push_back(LoadDiagnostic{
                .code = diagnostic.code,
                .severity = std::string(sacm::validation::severity_name(diagnostic.severity)),
                .message = diagnostic.message,
            });
        }
    }

    preview.can_apply = any_applied;
    return preview;
}

} // namespace

DeletePreview preview_delete_elements(const LibraryDocument& document, const std::vector<std::string>& element_ids) {
    // Same policies as `apply_delete_element`, or the preview would describe an
    // operation the caller never performs: scrub-then-drop within the package,
    // and reject rather than reach across a package boundary.
    return preview_delete_sequence(document,
                                   element_ids,
                                   sacm::commands::ReferenceDeletePolicy::ScrubReferences,
                                   sacm::commands::CrossPackageReferencePolicy::RejectIfExternalReferencesExist);
}

DeleteOutcome apply_delete_package(LibraryDocument& document, const std::string& package_id) {
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);

    const sacm::commands::MutationResult result = doc.apply(sacm::commands::DeleteElement{
        .target = sacm::model::ElementId(package_id),
        // Drop referencing relationships rather than reject, so no relationship is
        // left dangling (SACM-clean; cleaner than legacy `DeleteArtifactPackage`,
        // which leaves them).
        .reference_policy = sacm::commands::ReferenceDeletePolicy::DeleteReferencingRelationships,
        // The target is a package: take its whole subtree.
        .package_policy = sacm::commands::PackageDeletePolicy::DeleteRecursively,
        // References from OTHER packages into the removed subtree are dropped too
        // (a removed argument package may be cited from elsewhere).
        .cross_package_policy = sacm::commands::CrossPackageReferencePolicy::DeleteExternalReferencingRelationships,
    });

    DeleteOutcome outcome;
    outcome.supported = true;
    outcome.applied = result.applied;
    for (const sacm::validation::Diagnostic& diagnostic : result.diagnostics) {
        outcome.diagnostics.push_back(LoadDiagnostic{
            .code = diagnostic.code,
            .severity = std::string(sacm::validation::severity_name(diagnostic.severity)),
            .message = diagnostic.message,
        });
    }
    return outcome;
}

EditOutcome apply_set_gid(LibraryDocument& document, const std::string& element_id, const std::string& gid) {
    if (element_id.empty()) {
        return EditOutcome{.supported = false};
    }
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);
    const sacm::commands::MutationResult result =
        doc.apply(sacm::commands::SetGid{.element = sacm::model::ElementId(element_id), .gid = gid});
    return applied_outcome(result);
}

EditOutcome
apply_set_gsn_identifier(LibraryDocument& document, const std::string& element_id, const std::string& identifier) {
    if (element_id.empty()) {
        return EditOutcome{.supported = false};
    }
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);
    const sacm::model::ElementId element(element_id);
    const auto* model_element = doc.find_as<sacm::model::ModelElement>(element);
    if (model_element == nullptr) {
        // Not a tag-carrying element. Reported as a rejection rather than as
        // unsupported: the caller named something specific and it was wrong.
        EditOutcome outcome;
        outcome.diagnostics.push_back(LoadDiagnostic{
            .code = "SACM-CMD-002",
            .severity = "error",
            .message = "'" + element_id + "' is not an element that can carry a GSN identifier",
        });
        return outcome;
    }

    // The tag this replaces, if the element already carries one.
    std::optional<sacm::model::ElementId> existing;
    for (const auto& tag : model_element->tagged_values()) {
        if (tag->key().primary() == core::kGsnIdentifierTagKey) {
            existing = tag->id();
            break;
        }
    }

    const sacm::commands::AddTaggedValue add{
        .element = element, .key = core::kGsnIdentifierTagKey, .value = identifier};
    // Previewed before the old tag goes, so a rejected add cannot leave the
    // element with no identifier at all.
    if (const sacm::commands::OperationPreview preview = doc.preview(add); !preview.can_apply) {
        EditOutcome outcome;
        for (const sacm::validation::Diagnostic& diagnostic : preview.diagnostics) {
            outcome.diagnostics.push_back(LoadDiagnostic{
                .code = diagnostic.code,
                .severity = std::string(sacm::validation::severity_name(diagnostic.severity)),
                .message = diagnostic.message,
            });
        }
        return outcome;
    }
    if (existing.has_value()) {
        const sacm::commands::MutationResult removed = doc.apply(sacm::commands::DeleteElement{.target = *existing});
        if (!removed.applied) {
            return applied_outcome(removed);
        }
    }
    return applied_outcome(doc.apply(add));
}

EditOutcome apply_set_undeveloped(LibraryDocument& document, const std::string& element_id, bool undeveloped) {
    if (element_id.empty()) {
        return EditOutcome{.supported = false};
    }
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);
    const sacm::model::ElementId element(element_id);
    const auto* assertion = doc.find_as<sacm::model::Assertion>(element);
    if (assertion == nullptr) {
        EditOutcome outcome;
        outcome.diagnostics.push_back(LoadDiagnostic{
            .code = "SACM-CMD-002",
            .severity = "error",
            .message = "'" + element_id + "' is not an element that can be marked undeveloped",
        });
        return outcome;
    }

    // The declaration slot has to be free. `asserted` is the ordinary case and
    // `needsSupport` is the decorator already being set, so both are ours to
    // write; anything else is a different GSN element wearing the same SACM
    // class, and overwriting it would change what the argument says.
    const sacm::model::AssertionDeclaration current = assertion->assertion_declaration();
    if (current != sacm::model::AssertionDeclaration::Asserted &&
        current != sacm::model::AssertionDeclaration::NeedsSupport) {
        EditOutcome outcome;
        outcome.diagnostics.push_back(LoadDiagnostic{
            .code = "SACM-CMD-002",
            .severity = "error",
            .message = "'" + element_id + "' is declared '" +
                       std::string(sacm::model::assertion_declaration_name(current)) +
                       "', and SACM carries the undeveloped decorator in that same assertionDeclaration, so the "
                       "two cannot both be recorded",
        });
        return outcome;
    }

    const sacm::model::AssertionDeclaration target =
        undeveloped ? sacm::model::AssertionDeclaration::NeedsSupport : sacm::model::AssertionDeclaration::Asserted;
    if (current == target) {
        return EditOutcome{.supported = true, .applied = true};
    }
    return applied_outcome(
        doc.apply(sacm::commands::SetAssertionDeclaration{.element = element, .declaration = target}));
}

namespace {

sacm::metadata::ElementKind to_element_kind(RelationshipKind kind) {
    switch (kind) {
    case RelationshipKind::AssertedInference:
        return sacm::metadata::ElementKind::AssertedInference;
    case RelationshipKind::AssertedContext:
        return sacm::metadata::ElementKind::AssertedContext;
    case RelationshipKind::AssertedEvidence:
        return sacm::metadata::ElementKind::AssertedEvidence;
    }
    return sacm::metadata::ElementKind::AssertedInference;
}

} // namespace

EditOutcome apply_reorder_package_elements(LibraryDocument& document,
                                           const std::string& package_id,
                                           const std::vector<std::string>& ordered_ids) {
    if (package_id.empty() || ordered_ids.empty()) {
        return EditOutcome{.supported = false};
    }
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);
    std::vector<sacm::model::ElementId> ordered;
    ordered.reserve(ordered_ids.size());
    for (const std::string& id : ordered_ids) {
        if (!id.empty()) {
            ordered.emplace_back(id);
        }
    }
    return applied_outcome(doc.apply(sacm::commands::ReorderPackageElements{
        .package = sacm::model::ElementId(package_id),
        .ordered = std::move(ordered),
    }));
}

EditOutcome apply_add_relationship(LibraryDocument& document,
                                   const std::string& relationship_id,
                                   RelationshipKind kind,
                                   const std::vector<std::string>& sources,
                                   const std::vector<std::string>& targets,
                                   const std::string& reasoning) {
    if (relationship_id.empty() || targets.empty()) {
        return EditOutcome{.supported = false};
    }
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);
    // The package that owns the TARGET, matching `apply_add_child`: a relationship
    // lives with the parent it points at, so a move across packages does not leave
    // it behind in the old one.
    const sacm::model::ArgumentPackage* package =
        owning_argument_package(doc.find(sacm::model::ElementId(targets.front())));
    if (package == nullptr) {
        return EditOutcome{.supported = false};
    }
    const auto to_ids = [](const std::vector<std::string>& values) {
        std::vector<sacm::model::ElementId> ids;
        ids.reserve(values.size());
        for (const std::string& value : values) {
            if (!value.empty()) {
                ids.emplace_back(value);
            }
        }
        return ids;
    };
    return applied_outcome(doc.apply(sacm::commands::CreateAssertedRelationship{
        .parent = package->id(),
        .kind = to_element_kind(kind),
        .id = to_optional_id(relationship_id),
        .sources = to_ids(sources),
        .targets = to_ids(targets),
        .reasoning =
            reasoning.empty() ? std::nullopt : std::optional<sacm::model::ElementId>(sacm::model::ElementId(reasoning)),
    }));
}

EditOutcome apply_set_relationship_ends(LibraryDocument& document,
                                        const std::string& relationship_id,
                                        const std::vector<std::string>& sources,
                                        const std::vector<std::string>& targets,
                                        const std::string& reasoning) {
    if (relationship_id.empty()) {
        return EditOutcome{.supported = false};
    }
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);
    const auto to_ids = [](const std::vector<std::string>& values) {
        std::vector<sacm::model::ElementId> ids;
        ids.reserve(values.size());
        for (const std::string& value : values) {
            if (!value.empty()) {
                ids.emplace_back(value);
            }
        }
        return ids;
    };
    return applied_outcome(doc.apply(sacm::commands::SetRelationshipEnds{
        .relationship = sacm::model::ElementId(relationship_id),
        .sources = to_ids(sources),
        .targets = to_ids(targets),
        .reasoning =
            reasoning.empty() ? std::nullopt : std::optional<sacm::model::ElementId>(sacm::model::ElementId(reasoning)),
    }));
}

// -------------------------------------------------------------- terminology

namespace {

// The AssertedContext description the application uses to mark a terminology
// context as *visible* in the GSN view. Duplicated from
// `core::kVisibleTerminologyContextMarker`: the adapter sits below `core` and
// must not include its headers, so keep the two literals in sync.
constexpr const char* kVisibleTerminologyContextMarker = "assurance-forge:visible-term-context";

// Mirrors core::TrimWhitespace (std::isspace on both ends).
std::string trim_whitespace(const std::string& value) {
    auto begin = value.begin();
    while (begin != value.end() && std::isspace(static_cast<unsigned char>(*begin)) != 0) {
        ++begin;
    }
    auto end = value.end();
    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1))) != 0) {
        --end;
    }
    return std::string(begin, end);
}

// Mirrors core::NormalizeRef (strip one leading '#', then trim).
std::string normalize_ref(const std::string& value) {
    std::string trimmed = trim_whitespace(value);
    if (!trimmed.empty() && trimmed.front() == '#') {
        trimmed.erase(trimmed.begin());
    }
    return trimmed;
}

// Mirrors core::NormalizeCategoryRefs: trim, strip a leading '#', drop empties
// and duplicates, preserve order.
std::vector<std::string> normalize_category_refs(const std::vector<std::string>& refs) {
    std::vector<std::string> normalized;
    for (const std::string& raw : refs) {
        std::string ref = trim_whitespace(raw);
        if (!ref.empty() && ref.front() == '#') {
            ref.erase(ref.begin());
        }
        if (ref.empty() || std::find(normalized.begin(), normalized.end(), ref) != normalized.end()) {
            continue;
        }
        normalized.push_back(std::move(ref));
    }
    return normalized;
}

// The root AssuranceCasePackage a top-level TerminologyPackage is created under
// (mirroring the legacy `CreateTerminologyPackage`, which appends to the case
// package's terminologyPackages). Null if the document has no root.
const sacm::model::AssuranceCasePackage* root_case_package(const sacm::model::Document& doc) {
    return doc.roots().empty() ? nullptr : doc.roots().front().get();
}

void fill_diagnostics(std::vector<LoadDiagnostic>& out, const std::vector<sacm::validation::Diagnostic>& diagnostics) {
    out.reserve(out.size() + diagnostics.size());
    for (const sacm::validation::Diagnostic& diagnostic : diagnostics) {
        out.push_back(LoadDiagnostic{
            .code = diagnostic.code,
            .severity = std::string(sacm::validation::severity_name(diagnostic.severity)),
            .message = diagnostic.message,
        });
    }
}

void fill_diagnostics(std::vector<LoadDiagnostic>& out, const sacm::commands::MutationResult& result) {
    fill_diagnostics(out, result.diagnostics);
}

// A terminology-edit failure carrying a preview's diagnostics (used to reject an
// update before any of its composed setters run, keeping the update atomic).
TerminologyEditOutcome preview_failed_terminology_edit(const sacm::commands::OperationPreview& preview) {
    TerminologyEditOutcome outcome;
    outcome.supported = true;
    outcome.applied = false;
    fill_diagnostics(outcome.diagnostics, preview.diagnostics);
    return outcome;
}

TerminologyCreateOutcome failed_terminology_create(const sacm::commands::MutationResult& result) {
    TerminologyCreateOutcome outcome;
    outcome.supported = true;
    outcome.applied = false;
    fill_diagnostics(outcome.diagnostics, result);
    return outcome;
}

TerminologyEditOutcome failed_terminology_edit(const sacm::commands::MutationResult& result) {
    TerminologyEditOutcome outcome;
    outcome.supported = true;
    outcome.applied = false;
    fill_diagnostics(outcome.diagnostics, result);
    return outcome;
}

TerminologyEditOutcome terminology_edit_error(const std::string& code, const std::string& message) {
    TerminologyEditOutcome outcome;
    outcome.supported = true;
    outcome.applied = false;
    outcome.diagnostics.push_back(LoadDiagnostic{.code = code, .severity = "Error", .message = message});
    return outcome;
}

TerminologyContextOutcome failed_terminology_context(const sacm::commands::MutationResult& result) {
    TerminologyContextOutcome outcome;
    outcome.supported = true;
    outcome.applied = false;
    fill_diagnostics(outcome.diagnostics, result);
    return outcome;
}

TerminologyContextOutcome terminology_context_error(const std::string& code, const std::string& message) {
    TerminologyContextOutcome outcome;
    outcome.supported = true;
    outcome.applied = false;
    outcome.diagnostics.push_back(LoadDiagnostic{.code = code, .severity = "Error", .message = message});
    return outcome;
}

// Sets/updates a terminology element's description under the front Description's
// existing language (or lang-less for a new one), matching the reader convention
// and `apply_text_edit`'s Content/Description handling. Empty text clears it.
// Returns the library result so the caller can stop on failure.
sacm::commands::MutationResult
set_terminology_description(sacm::model::Document& doc, const sacm::model::ElementId& id, const std::string& text) {
    std::string language;
    if (const auto* element = doc.find_as<sacm::model::ModelElement>(id)) {
        language = primary_description_language(*element);
    }
    return doc.apply(sacm::commands::SetDescription{.element = id, .text = text, .language = language});
}

// Stamp the legacy gid on a freshly created element so a library-seam create
// matches the legacy `core::*WithIds` mutators, which stamp
// `GenerateUniqueGid(package, id)` on the terminology package/category/term and
// the association's ArtifactReference/AssertedContext.
//
// `requested` is the caller's gid -- the one a live flipped command planned with
// the legacy generator, or the one an audit payload recorded. Empty falls back to
// the base `gid-<id>` form, which is what `GenerateUniqueGid` yields whenever
// that base is free. It is NOT always free: gid space is independent of id
// space, so a document can already carry `gid-TP1` on an unrelated element while
// `TP1` is a perfectly fresh id, and the legacy generator then disambiguates to
// `gid-TP1-2`. Reconstructing the gid from the id alone would silently diverge
// there, which is why the callers that know the real gid pass it.
sacm::commands::MutationResult
assign_legacy_gid(sacm::model::Document& doc, const sacm::model::ElementId& id, const std::string& requested = {}) {
    return doc.apply(sacm::commands::SetGid{.element = id, .gid = requested.empty() ? "gid-" + id.value() : requested});
}

// Reproduces core's TermContextLabel for the ArtifactReference/AssertedContext
// names a term association creates.
std::string term_context_label(const sacm::model::Term& term) {
    const std::string value = term.value();
    const std::string name = term.name().content;
    if (value.empty()) {
        return name.empty() ? term.id().value() : name;
    }
    if (name.empty() || name == value) {
        return value;
    }
    return value + ": " + name;
}

bool ids_contain(const std::vector<sacm::model::ElementId>& ids, const sacm::model::ElementId& id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

// An ArtifactReference whose referencedArtifact resolves to `term_id` (the term's
// id; library-created terminology references carry no gid).
bool reference_targets_term(const sacm::model::ArtifactReference& reference, const sacm::model::ElementId& term_id) {
    return ids_contain(reference.referenced_artifact_elements(), term_id);
}

// The front Description text of a relationship, for the visible-context marker
// check (mirrors core::IsVisibleTerminologyContext on `context.description`).
bool is_visible_terminology_context(const sacm::model::AssertedContext& context) {
    return trim_whitespace(context.description().primary()) == kVisibleTerminologyContextMarker;
}

// Collects the AssertedContexts directly contained in `package`.
std::vector<const sacm::model::AssertedContext*> package_contexts(const sacm::model::ArgumentPackage& package) {
    std::vector<const sacm::model::AssertedContext*> contexts;
    for (const auto& element : package.argument_elements()) {
        if (const auto* context = dynamic_cast<const sacm::model::AssertedContext*>(element.get())) {
            contexts.push_back(context);
        }
    }
    return contexts;
}

// Mirrors core::ArtifactReferenceUsedByOtherContexts: is `reference` a source of
// any context (other than `ignore_context_id`) that does NOT target
// `target_id`?
bool reference_used_by_other_contexts(const sacm::model::ArgumentPackage& package,
                                      const sacm::model::ElementId& reference_id,
                                      const sacm::model::ElementId& target_id,
                                      const sacm::model::ElementId& ignore_context_id) {
    for (const sacm::model::AssertedContext* context : package_contexts(package)) {
        if (context->id() == ignore_context_id) {
            continue;
        }
        if (!ids_contain(context->sources(), reference_id)) {
            continue;
        }
        if (!ids_contain(context->targets(), target_id)) {
            return true;
        }
    }
    return false;
}

// A target element eligible for a terminology *context* association (claim,
// strategy, or solution), mirroring core::ArgumentPackageContainsTargetElement.
bool is_association_target(const sacm::model::SACMElement* element) {
    if (element == nullptr) {
        return false;
    }
    switch (element->kind()) {
    case sacm::metadata::ElementKind::Claim:
    case sacm::metadata::ElementKind::ArgumentReasoning:
    case sacm::metadata::ElementKind::ArtifactReference:
        return true;
    default:
        return false;
    }
}

// A target eligible as a *visible* context (claim or strategy only), mirroring
// core::ArgumentPackageContainsVisibleContextTarget.
bool is_visible_context_target(const sacm::model::SACMElement* element) {
    if (element == nullptr) {
        return false;
    }
    return element->kind() == sacm::metadata::ElementKind::Claim ||
           element->kind() == sacm::metadata::ElementKind::ArgumentReasoning;
}

} // namespace

TerminologyCreateOutcome apply_create_terminology_package(LibraryDocument& document,
                                                          const std::string& name,
                                                          const std::string& description,
                                                          const std::string& package_id,
                                                          const std::string& package_gid) {
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);
    const sacm::model::AssuranceCasePackage* root = root_case_package(doc);
    if (root == nullptr) {
        return TerminologyCreateOutcome{.supported = false};
    }
    // Legacy CreateTerminologyPackage stores the name/description unmodified.
    const sacm::commands::MutationResult created = doc.apply(
        sacm::commands::CreateTerminologyPackage{.parent = root->id(), .id = to_optional_id(package_id), .name = name});
    if (!created.applied || created.created_ids().empty()) {
        return failed_terminology_create(created);
    }
    const sacm::model::ElementId new_id = created.created_ids().front();
    // Legacy CreateTerminologyPackageWithIds stamps GenerateUniqueGid on the
    // package; mint the same gid so the projection and canonical hash match.
    if (const sacm::commands::MutationResult gid = assign_legacy_gid(doc, new_id, package_gid); !gid.applied) {
        rollback_element(doc, new_id);
        return failed_terminology_create(gid);
    }
    if (!description.empty()) {
        const sacm::commands::MutationResult described = set_terminology_description(doc, new_id, description);
        if (!described.applied) {
            rollback_element(doc, new_id);
            return failed_terminology_create(described);
        }
    }
    TerminologyCreateOutcome outcome;
    outcome.applied = true;
    outcome.element_id = new_id.value();
    return outcome;
}

TerminologyCreateOutcome apply_create_terminology_category(LibraryDocument& document,
                                                           const std::string& package_id,
                                                           const std::string& name,
                                                           const std::string& description,
                                                           const std::string& category_id,
                                                           const std::string& category_gid) {
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);
    // Legacy ApplyCategoryDraft trims the name/description.
    const sacm::commands::MutationResult created =
        doc.apply(sacm::commands::CreateCategory{.parent = sacm::model::ElementId(package_id),
                                                 .id = to_optional_id(category_id),
                                                 .name = trim_whitespace(name)});
    if (!created.applied || created.created_ids().empty()) {
        return failed_terminology_create(created);
    }
    const sacm::model::ElementId new_id = created.created_ids().front();
    // Legacy CreateTerminologyCategoryWithIds stamps GenerateUniqueGid.
    if (const sacm::commands::MutationResult gid = assign_legacy_gid(doc, new_id, category_gid); !gid.applied) {
        rollback_element(doc, new_id);
        return failed_terminology_create(gid);
    }
    const std::string trimmed_description = trim_whitespace(description);
    if (!trimmed_description.empty()) {
        const sacm::commands::MutationResult described = set_terminology_description(doc, new_id, trimmed_description);
        if (!described.applied) {
            rollback_element(doc, new_id);
            return failed_terminology_create(described);
        }
    }
    TerminologyCreateOutcome outcome;
    outcome.applied = true;
    outcome.element_id = new_id.value();
    return outcome;
}

TerminologyCreateOutcome apply_create_terminology_term(LibraryDocument& document,
                                                       const std::string& package_id,
                                                       const TerminologyTermFields& fields,
                                                       const std::string& term_id,
                                                       const std::string& term_gid) {
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);
    // Legacy ApplyTermDraft trims value/name/externalReference/origin and
    // normalizes the category refs; reproduce that so the projection matches.
    const sacm::commands::MutationResult created = doc.apply(sacm::commands::CreateTerm{
        .parent = sacm::model::ElementId(package_id),
        .id = to_optional_id(term_id),
        .name = trim_whitespace(fields.name),
        .value = trim_whitespace(fields.value),
        .external_reference = trim_whitespace(fields.external_reference),
        .origin = to_optional_id(normalize_ref(fields.origin)),
    });
    if (!created.applied || created.created_ids().empty()) {
        return failed_terminology_create(created);
    }
    const sacm::model::ElementId new_id = created.created_ids().front();
    // Legacy CreateTerminologyTermWithIds stamps GenerateUniqueGid.
    if (const sacm::commands::MutationResult gid = assign_legacy_gid(doc, new_id, term_gid); !gid.applied) {
        rollback_element(doc, new_id);
        return failed_terminology_create(gid);
    }

    const std::string description = trim_whitespace(fields.description);
    if (!description.empty()) {
        const sacm::commands::MutationResult described = set_terminology_description(doc, new_id, description);
        if (!described.applied) {
            rollback_element(doc, new_id);
            return failed_terminology_create(described);
        }
    }

    const std::vector<std::string> categories = normalize_category_refs(fields.category_refs);
    if (!categories.empty()) {
        std::vector<sacm::model::ElementId> category_ids;
        category_ids.reserve(categories.size());
        for (const std::string& category : categories) {
            category_ids.emplace_back(category);
        }
        const sacm::commands::MutationResult classified = doc.apply(
            sacm::commands::SetExpressionCategories{.element = new_id, .categories = std::move(category_ids)});
        if (!classified.applied) {
            rollback_element(doc, new_id);
            return failed_terminology_create(classified);
        }
    }

    TerminologyCreateOutcome outcome;
    outcome.applied = true;
    outcome.element_id = new_id.value();
    return outcome;
}

TerminologyEditOutcome apply_update_terminology_package(LibraryDocument& document,
                                                        const std::string& package_id,
                                                        const std::string& name,
                                                        const std::string& description) {
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);
    const sacm::model::ElementId id(package_id);
    const sacm::commands::MutationResult named =
        doc.apply(sacm::commands::SetName{.element = id, .name = name, .language = {}});
    if (!named.applied) {
        return failed_terminology_edit(named);
    }
    const sacm::commands::MutationResult described = set_terminology_description(doc, id, description);
    if (!described.applied) {
        return failed_terminology_edit(described);
    }
    return TerminologyEditOutcome{.applied = true};
}

TerminologyEditOutcome apply_update_terminology_category(LibraryDocument& document,
                                                         const std::string& category_id,
                                                         const std::string& name,
                                                         const std::string& description) {
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);
    if (doc.find_as<sacm::model::Category>(sacm::model::ElementId(category_id)) == nullptr) {
        return terminology_edit_error("SACM-CMD-002", "'" + category_id + "' is not a Category");
    }
    const sacm::model::ElementId id(category_id);
    // Legacy ApplyCategoryDraft trims name/description.
    const sacm::commands::MutationResult named =
        doc.apply(sacm::commands::SetName{.element = id, .name = trim_whitespace(name), .language = {}});
    if (!named.applied) {
        return failed_terminology_edit(named);
    }
    const sacm::commands::MutationResult described = set_terminology_description(doc, id, trim_whitespace(description));
    if (!described.applied) {
        return failed_terminology_edit(described);
    }
    return TerminologyEditOutcome{.applied = true};
}

TerminologyEditOutcome apply_update_terminology_term(LibraryDocument& document,
                                                     const std::string& term_id,
                                                     const TerminologyTermFields& fields) {
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);
    const sacm::model::ElementId id(term_id);
    if (doc.find_as<sacm::model::Term>(id) == nullptr) {
        return terminology_edit_error("SACM-CMD-002", "'" + term_id + "' is not a Term");
    }

    // Pre-validate the two setters that can fail (an unresolvable category or
    // origin ref is the only realistic failure) so the composed update is atomic:
    // nothing is applied unless all of it will -- the ADR's success->changed /
    // failure->unchanged contract, which the legacy ApplyTermDraft also honours.
    std::vector<sacm::model::ElementId> pending_categories;
    for (const std::string& category : normalize_category_refs(fields.category_refs)) {
        pending_categories.emplace_back(category);
    }
    const sacm::commands::OperationPreview category_preview =
        doc.preview(sacm::commands::SetExpressionCategories{.element = id, .categories = pending_categories});
    if (!category_preview.can_apply) {
        return preview_failed_terminology_edit(category_preview);
    }
    const sacm::commands::OperationPreview origin_preview = doc.preview(
        sacm::commands::SetTermOrigin{.element = id, .origin = to_optional_id(normalize_ref(fields.origin))});
    if (!origin_preview.can_apply) {
        return preview_failed_terminology_edit(origin_preview);
    }

    // Compose the setters that reproduce ApplyTermDraft. Categories/origin are
    // pre-validated above, so on a found Term none of these can now fail.
    const sacm::commands::MutationResult named =
        doc.apply(sacm::commands::SetName{.element = id, .name = trim_whitespace(fields.name), .language = {}});
    if (!named.applied) {
        return failed_terminology_edit(named);
    }
    const sacm::commands::MutationResult valued =
        doc.apply(sacm::commands::SetExpressionValue{.element = id, .value = trim_whitespace(fields.value)});
    if (!valued.applied) {
        return failed_terminology_edit(valued);
    }
    const sacm::commands::MutationResult described =
        set_terminology_description(doc, id, trim_whitespace(fields.description));
    if (!described.applied) {
        return failed_terminology_edit(described);
    }
    std::vector<sacm::model::ElementId> category_ids;
    for (const std::string& category : normalize_category_refs(fields.category_refs)) {
        category_ids.emplace_back(category);
    }
    const sacm::commands::MutationResult classified =
        doc.apply(sacm::commands::SetExpressionCategories{.element = id, .categories = std::move(category_ids)});
    if (!classified.applied) {
        return failed_terminology_edit(classified);
    }
    const sacm::commands::MutationResult referenced = doc.apply(sacm::commands::SetTermExternalReference{
        .element = id, .external_reference = trim_whitespace(fields.external_reference)});
    if (!referenced.applied) {
        return failed_terminology_edit(referenced);
    }
    const sacm::commands::MutationResult origin =
        doc.apply(sacm::commands::SetTermOrigin{.element = id, .origin = to_optional_id(normalize_ref(fields.origin))});
    if (!origin.applied) {
        return failed_terminology_edit(origin);
    }
    return TerminologyEditOutcome{.applied = true};
}

namespace {

// The elements a CASCADING terminology delete removes, in application order:
// every ArtifactReference that exists solely to point at `element_id`, then
// `element_id` itself.
//
// The library's own cross-package cascade
// (CrossPackageReferencePolicy::DeleteExternalReferencingRelationships) is not
// what a user is asking for here. It removes the *entry* -- the term is dropped
// from the reference's referencedArtifact list -- and the ArtifactReference
// survives, now pointing at nothing, with its AssertedContext still drawing a
// context node on the canvas. That is a husk, not a removal: correct by SACM's
// letter (no dangling id) and wrong by every reading of "also remove the things
// that reference it".
//
// "Exists solely to point at it" is the conservative test: the reference names
// this element and nothing else. A reference that also points elsewhere is left
// alone and merely scrubbed, because deleting it would take a second artifact's
// evidence with it -- the preview reports that as a modification rather than a
// removal.
// Clause 8.7 utility elements (Description/ImplementationConstraint/Note/
// TaggedValue) are attachments carried ON their owner, deleted with it. The
// preview leaves them out, so the recorded removal has to as well -- otherwise a
// confirmation reading "also removes 2 elements" is followed by an audit entry
// naming four, and the extra two are internal bookkeeping nobody can act on.
bool is_utility_attachment(sacm::metadata::ElementKind kind) {
    return kind == sacm::metadata::ElementKind::Description ||
           kind == sacm::metadata::ElementKind::ImplementationConstraint || kind == sacm::metadata::ElementKind::Note ||
           kind == sacm::metadata::ElementKind::TaggedValue;
}

void collect_removed_ids(std::vector<std::string>& out, const sacm::commands::MutationResult& result) {
    for (const sacm::commands::ChangeRecord& change : result.changes) {
        const bool deleted = change.change == sacm::commands::ChangeRecord::Change::Deleted ||
                             change.change == sacm::commands::ChangeRecord::Change::RelationshipDeleted;
        if (deleted && !is_utility_attachment(change.kind)) {
            out.push_back(change.id.value());
        }
    }
}

// Scrub rather than DeleteReferencingRelationships, for the same reason
// `apply_delete_element` uses it: a relationship that still has a surviving
// source and target survives, scrubbed, instead of cascading further.
constexpr sacm::commands::ReferenceDeletePolicy kTerminologyCascadeReferencePolicy =
    sacm::commands::ReferenceDeletePolicy::ScrubReferences;

// The plan deliberately SPARES an ArtifactReference that names the term and
// something else, so by the time the term itself is deleted a cross-package
// referrer can still exist. Rejecting there (the default) would abort the
// sequence after its earlier deletes had already applied, reporting failure over
// a half-mutated document -- found in review of #360. Reaching across instead
// hands that referrer to the reference policy above, which SCRUBS it: the shared
// reference survives, minus the deleted term, which is exactly the "modified,
// not removed" outcome the confirmation already showed for it.
constexpr sacm::commands::CrossPackageReferencePolicy kTerminologyCascadeCrossPackagePolicy =
    sacm::commands::CrossPackageReferencePolicy::DeleteExternalReferencingRelationships;

std::vector<std::string> plan_terminology_delete_cascade(const sacm::model::Document& doc,
                                                         const std::string& element_id) {
    const sacm::model::ElementId target(element_id);
    std::vector<std::string> doomed;
    doc.for_each_element([&](const sacm::model::SACMElement& element) {
        const auto* reference = dynamic_cast<const sacm::model::ArtifactReference*>(&element);
        if (reference == nullptr) {
            return;
        }
        const std::vector<sacm::model::ElementId> referenced = reference->referenced_artifact_elements();
        if (referenced.size() == 1 && referenced.front() == target) {
            doomed.push_back(reference->id().value());
        }
    });
    doomed.push_back(element_id);
    return doomed;
}

} // namespace

TerminologyEditOutcome apply_delete_terminology_element(LibraryDocument& document,
                                                        const std::string& element_id,
                                                        bool cascade_external_references) {
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);
    TerminologyEditOutcome outcome;

    if (!cascade_external_references) {
        const sacm::commands::MutationResult result = doc.apply(sacm::commands::DeleteElement{
            .target = sacm::model::ElementId(element_id),
            .reference_policy = sacm::commands::ReferenceDeletePolicy::DeleteReferencingRelationships,
        });
        outcome.applied = result.applied;
        collect_removed_ids(outcome.removed_ids, result);
        fill_diagnostics(outcome.diagnostics, result);
        return outcome;
    }

    // The cascade is a SEQUENCE of ordinary deletes, applied in the same order
    // and under the same policies `preview_delete_terminology_element` models on
    // a scratch copy -- so what the confirmation showed and what happens here
    // cannot come apart.
    for (const std::string& id : plan_terminology_delete_cascade(doc, element_id)) {
        const sacm::commands::MutationResult result = doc.apply(sacm::commands::DeleteElement{
            .target = sacm::model::ElementId(id),
            .reference_policy = kTerminologyCascadeReferencePolicy,
            .cross_package_policy = kTerminologyCascadeCrossPackagePolicy,
        });
        fill_diagnostics(outcome.diagnostics, result);
        if (!result.applied) {
            // Partial: earlier deletes in the sequence stand. Report it rather
            // than pretending the whole cascade went through -- the caller
            // surfaces the diagnostics and `removed_ids` says how far it got.
            outcome.applied = false;
            return outcome;
        }
        collect_removed_ids(outcome.removed_ids, result);
    }
    outcome.applied = true;
    return outcome;
}

DeletePreview preview_delete_terminology_element(const LibraryDocument& document, const std::string& element_id) {
    const sacm::model::Document& doc = LibraryDocumentAccess::document(document);

    // Same plan, same order, same policies as the cascading apply above, run on a
    // throwaway copy -- the preview IS the apply rather than a second description
    // of it.
    DeletePreview preview = preview_delete_sequence(document,
                                                    plan_terminology_delete_cascade(doc, element_id),
                                                    kTerminologyCascadeReferencePolicy,
                                                    kTerminologyCascadeCrossPackagePolicy);

    // The user asked to delete ONE thing. `preview_delete_elements` buckets by
    // what the caller named, and the caller here named the whole doomed set, so
    // re-bucket: only the term is "requested"; everything the plan added on the
    // user's behalf is a consequence, which is the entire point of showing it.
    std::vector<DeleteEffect> requested;
    std::vector<DeleteEffect> consequential;
    for (std::vector<DeleteEffect>* bucket : {&preview.requested, &preview.consequential}) {
        for (DeleteEffect& effect : *bucket) {
            (effect.element_id == element_id ? requested : consequential).push_back(std::move(effect));
        }
    }
    preview.requested = std::move(requested);
    preview.consequential = std::move(consequential);

    // `can_apply` is STRICTER here than the sequence preview's "at least one
    // delete applied". The caller's question is not "does anything happen" but
    // "does the thing I asked for happen": a plan whose sub-deletes succeed while
    // the term itself is rejected reports any-applied == true, which is exactly
    // the reading that let a stranding cascade look previewable (review of #360).
    // So: the target has to come back as deleted.
    preview.can_apply = std::any_of(preview.requested.begin(), preview.requested.end(), [&](const DeleteEffect& e) {
        return e.element_id == element_id && e.deleted;
    });
    return preview;
}

TerminologyContextOutcome apply_associate_terminology_term(LibraryDocument& document,
                                                           const std::string& target_element_id,
                                                           const std::string& term_id,
                                                           const TerminologyContextIdentities& identities) {
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);
    const sacm::model::ElementId term_ref(term_id);
    const auto* term = doc.find_as<sacm::model::Term>(term_ref);
    if (term == nullptr) {
        return terminology_context_error("SACM-CMD-002", "Term not found.");
    }
    const sacm::model::ElementId target(target_element_id);
    const sacm::model::SACMElement* target_element = doc.find(target);
    const sacm::model::ArgumentPackage* package = owning_argument_package(target_element);
    if (package == nullptr || !is_association_target(target_element)) {
        return terminology_context_error(
            "SACM-CMD-002", "Selected element is not a claim, strategy, or solution in an argument package.");
    }
    const sacm::model::ElementId package_id = package->id();

    // Reuse: an existing terminology reference to the term in this package, and
    // an existing context that already links it to the target.
    const sacm::model::ArtifactReference* reusable_reference = nullptr;
    for (const auto& element : package->argument_elements()) {
        const auto* reference = dynamic_cast<const sacm::model::ArtifactReference*>(element.get());
        if (reference == nullptr || !reference_targets_term(*reference, term_ref)) {
            continue;
        }
        if (reusable_reference == nullptr) {
            reusable_reference = reference;
        }
        for (const sacm::model::AssertedContext* context : package_contexts(*package)) {
            if (ids_contain(context->sources(), reference->id()) && ids_contain(context->targets(), target)) {
                TerminologyContextOutcome associated;
                associated.applied = true;
                associated.already_associated = true;
                associated.artifact_reference_id = reference->id().value();
                associated.asserted_context_id = context->id().value();
                return associated;
            }
        }
    }

    TerminologyContextOutcome outcome;
    std::string reference_id;
    if (reusable_reference == nullptr) {
        const sacm::commands::MutationResult created_reference =
            doc.apply(sacm::commands::CreateArtifactReference{.parent = package_id,
                                                              .id = to_optional_id(identities.artifact_reference_id),
                                                              .name = term_context_label(*term),
                                                              .referenced_artifact_elements = {term_ref}});
        if (!created_reference.applied || created_reference.created_ids().empty()) {
            return failed_terminology_context(created_reference);
        }
        reference_id = created_reference.created_ids().front().value();
        outcome.created_artifact_reference = true;
        // Legacy AssociateTerminologyTermWithElementWithIds stamps
        // GenerateUniqueGid on a newly created reference (a reused one keeps its
        // gid); mint the same so replay converges on the raw hash.
        if (const sacm::commands::MutationResult gid =
                assign_legacy_gid(doc, sacm::model::ElementId(reference_id), identities.artifact_reference_gid);
            !gid.applied) {
            rollback_element(doc, sacm::model::ElementId(reference_id));
            return failed_terminology_context(gid);
        }
    } else {
        reference_id = reusable_reference->id().value();
    }

    const sacm::commands::MutationResult created_context =
        doc.apply(sacm::commands::CreateAssertedRelationship{.parent = package_id,
                                                             .kind = sacm::metadata::ElementKind::AssertedContext,
                                                             .id = to_optional_id(identities.asserted_context_id),
                                                             .name = "Context: " + term_context_label(*term),
                                                             .sources = {sacm::model::ElementId(reference_id)},
                                                             .targets = {target}});
    if (!created_context.applied || created_context.created_ids().empty()) {
        if (outcome.created_artifact_reference) {
            rollback_element(doc, sacm::model::ElementId(reference_id));
        }
        return failed_terminology_context(created_context);
    }
    const sacm::model::ElementId context_id = created_context.created_ids().front();
    // The AssertedContext also carries a legacy GenerateUniqueGid.
    if (const sacm::commands::MutationResult gid = assign_legacy_gid(doc, context_id, identities.asserted_context_gid);
        !gid.applied) {
        rollback_element(doc, context_id);
        if (outcome.created_artifact_reference) {
            rollback_element(doc, sacm::model::ElementId(reference_id));
        }
        return failed_terminology_context(gid);
    }

    outcome.applied = true;
    outcome.created_asserted_context = true;
    outcome.artifact_reference_id = reference_id;
    outcome.asserted_context_id = context_id.value();
    return outcome;
}

namespace {

// Candidates for the visible-context edit, mirroring core's
// VisibleContextSearchResult (ids captured by value so they survive the
// mutations the caller then applies).
struct VisibleContextCandidates {
    bool has_existing_visible = false;
    sacm::model::ElementId existing_visible_reference;
    sacm::model::ElementId existing_visible_context;
    bool has_promotable = false;
    sacm::model::ElementId promotable_reference;
    sacm::model::ElementId promotable_context;
    std::vector<sacm::model::ElementId> hidden_contexts_to_remove;
};

// Mirrors core::FindVisibleContextCandidates.
VisibleContextCandidates find_visible_context_candidates(const sacm::model::ArgumentPackage& package,
                                                         const sacm::model::ElementId& term_ref,
                                                         const sacm::model::ElementId& target) {
    VisibleContextCandidates result;
    for (const auto& element : package.argument_elements()) {
        const auto* reference = dynamic_cast<const sacm::model::ArtifactReference*>(element.get());
        if (reference == nullptr || !reference_targets_term(*reference, term_ref)) {
            continue;
        }
        for (const sacm::model::AssertedContext* context : package_contexts(package)) {
            if (!ids_contain(context->sources(), reference->id()) || !ids_contain(context->targets(), target)) {
                continue;
            }
            if (is_visible_terminology_context(*context)) {
                result.has_existing_visible = true;
                result.existing_visible_reference = reference->id();
                result.existing_visible_context = context->id();
                return result;
            }
            if (!reference_used_by_other_contexts(package, reference->id(), target, context->id()) &&
                !result.has_promotable) {
                result.has_promotable = true;
                result.promotable_reference = reference->id();
                result.promotable_context = context->id();
            } else {
                result.hidden_contexts_to_remove.push_back(context->id());
            }
        }
    }
    return result;
}

// Mirrors core::RemoveUnreferencedTerminologyArtifacts: delete any terminology
// reference to the term (other than `keep`) that no context still references.
void remove_unreferenced_term_artifacts(sacm::model::Document& doc,
                                        const sacm::model::ElementId& package_id,
                                        const sacm::model::ElementId& term_ref,
                                        const sacm::model::ElementId& keep) {
    const auto* package = doc.find_as<sacm::model::ArgumentPackage>(package_id);
    if (package == nullptr) {
        return;
    }
    std::vector<sacm::model::ElementId> doomed;
    for (const auto& element : package->argument_elements()) {
        const auto* reference = dynamic_cast<const sacm::model::ArtifactReference*>(element.get());
        if (reference == nullptr || reference->id() == keep || !reference_targets_term(*reference, term_ref)) {
            continue;
        }
        bool referenced = false;
        for (const sacm::model::AssertedContext* context : package_contexts(*package)) {
            if (ids_contain(context->sources(), reference->id())) {
                referenced = true;
                break;
            }
        }
        if (!referenced) {
            doomed.push_back(reference->id());
        }
    }
    for (const sacm::model::ElementId& id : doomed) {
        doc.apply(sacm::commands::DeleteElement{
            .target = id,
            .reference_policy = sacm::commands::ReferenceDeletePolicy::DeleteReferencingRelationships,
        });
    }
}

} // namespace

TerminologyContextOutcome apply_add_terminology_visible_context(LibraryDocument& document,
                                                                const std::string& target_element_id,
                                                                const std::string& term_id,
                                                                const TerminologyContextIdentities& identities) {
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);
    const sacm::model::ElementId term_ref(term_id);
    const auto* term = doc.find_as<sacm::model::Term>(term_ref);
    if (term == nullptr) {
        return terminology_context_error("SACM-CMD-002", "Term not found.");
    }
    const sacm::model::ElementId target(target_element_id);
    const sacm::model::SACMElement* target_element = doc.find(target);
    const sacm::model::ArgumentPackage* package = owning_argument_package(target_element);
    if (package == nullptr || !is_visible_context_target(target_element)) {
        return terminology_context_error("SACM-CMD-002",
                                         "Selected element is not a claim or strategy in an argument package.");
    }
    const sacm::model::ElementId package_id = package->id();

    const VisibleContextCandidates candidates = find_visible_context_candidates(*package, term_ref, target);

    // Already a visible context linking the term to the target.
    if (candidates.has_existing_visible) {
        TerminologyContextOutcome associated;
        associated.applied = true;
        associated.already_associated = true;
        associated.artifact_reference_id = candidates.existing_visible_reference.value();
        associated.asserted_context_id = candidates.existing_visible_context.value();
        return associated;
    }

    // Promote a non-visible context in place by marking it visible.
    if (candidates.has_promotable) {
        const sacm::commands::MutationResult promoted =
            set_terminology_description(doc, candidates.promotable_context, kVisibleTerminologyContextMarker);
        if (!promoted.applied) {
            return failed_terminology_context(promoted);
        }
        TerminologyContextOutcome outcome;
        outcome.applied = true;
        outcome.artifact_reference_id = candidates.promotable_reference.value();
        outcome.asserted_context_id = candidates.promotable_context.value();
        return outcome;
    }

    // Create a fresh terminology reference and a visible context.
    const sacm::commands::MutationResult created_reference =
        doc.apply(sacm::commands::CreateArtifactReference{.parent = package_id,
                                                          .id = to_optional_id(identities.artifact_reference_id),
                                                          .name = term_context_label(*term),
                                                          .referenced_artifact_elements = {term_ref}});
    if (!created_reference.applied || created_reference.created_ids().empty()) {
        return failed_terminology_context(created_reference);
    }
    const sacm::model::ElementId reference_id = created_reference.created_ids().front();
    // Legacy AddTerminologyTermAsVisibleContextWithIds stamps GenerateUniqueGid
    // on the created reference and context; mint the same for raw-hash parity.
    if (const sacm::commands::MutationResult gid =
            assign_legacy_gid(doc, reference_id, identities.artifact_reference_gid);
        !gid.applied) {
        rollback_element(doc, reference_id);
        return failed_terminology_context(gid);
    }

    const sacm::commands::MutationResult created_context =
        doc.apply(sacm::commands::CreateAssertedRelationship{.parent = package_id,
                                                             .kind = sacm::metadata::ElementKind::AssertedContext,
                                                             .id = to_optional_id(identities.asserted_context_id),
                                                             .name = "Context: " + term_context_label(*term),
                                                             .sources = {reference_id},
                                                             .targets = {target}});
    if (!created_context.applied || created_context.created_ids().empty()) {
        rollback_element(doc, reference_id);
        return failed_terminology_context(created_context);
    }
    const sacm::model::ElementId context_id = created_context.created_ids().front();
    if (const sacm::commands::MutationResult gid = assign_legacy_gid(doc, context_id, identities.asserted_context_gid);
        !gid.applied) {
        rollback_element(doc, context_id);
        rollback_element(doc, reference_id);
        return failed_terminology_context(gid);
    }

    const sacm::commands::MutationResult marked =
        set_terminology_description(doc, context_id, kVisibleTerminologyContextMarker);
    if (!marked.applied) {
        rollback_element(doc, context_id);
        rollback_element(doc, reference_id);
        return failed_terminology_context(marked);
    }

    // Clean up the contexts the search superseded and any now-unreferenced
    // terminology reference to the term (mirroring the legacy create branch).
    for (const sacm::model::ElementId& hidden : candidates.hidden_contexts_to_remove) {
        doc.apply(sacm::commands::DeleteElement{
            .target = hidden,
            .reference_policy = sacm::commands::ReferenceDeletePolicy::DeleteReferencingRelationships,
        });
    }
    remove_unreferenced_term_artifacts(doc, package_id, term_ref, reference_id);

    TerminologyContextOutcome outcome;
    outcome.applied = true;
    outcome.created_artifact_reference = true;
    outcome.created_asserted_context = true;
    outcome.artifact_reference_id = reference_id.value();
    outcome.asserted_context_id = context_id.value();
    return outcome;
}

namespace {

// Every tag key belonging to one ACP: the marker (whose VALUE is the id) and the
// `assuranceForge.acp.<id>.` field family. Mirrors core::acp::IsAcpTagForId.
bool is_acp_tag_for(const sacm::model::TaggedValue& tag, const std::string& acp_id) {
    const std::string key = tag.key().primary();
    if (key == core::kAcpMarkerTagKey) {
        return tag.content().primary() == acp_id;
    }
    return key.rfind(std::string(core::kAcpFieldTagPrefix) + acp_id + ".", 0) == 0;
}

// Ids of the tags on `element` belonging to `acp_id`, captured before any delete
// (the list being iterated is about to be mutated).
std::vector<sacm::model::ElementId> acp_tag_ids(const sacm::model::ModelElement& element, const std::string& acp_id) {
    std::vector<sacm::model::ElementId> ids;
    for (const auto& tag : element.tagged_values()) {
        if (is_acp_tag_for(*tag, acp_id)) {
            ids.push_back(tag->id());
        }
    }
    return ids;
}

EditOutcome acp_tag_target_error(const std::string& element_id) {
    EditOutcome outcome;
    outcome.diagnostics.push_back(LoadDiagnostic{
        .code = "SACM-CMD-002",
        .severity = "error",
        .message = "'" + element_id + "' is not an element that can carry an Assurance Claim Point",
    });
    return outcome;
}

} // namespace

EditOutcome
apply_upsert_acp_tags(LibraryDocument& document, const std::string& element_id, const AcpTagFields& fields) {
    if (element_id.empty() || fields.id.empty()) {
        return EditOutcome{.supported = false};
    }
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);
    const sacm::model::ElementId element(element_id);
    const auto* model_element = doc.find_as<sacm::model::ModelElement>(element);
    if (model_element == nullptr) {
        return acp_tag_target_error(element_id);
    }

    // The set to write, in the order core::acp::UpsertAcpTags writes it and with
    // the same omissions -- an empty optional field produces no tag at all, so
    // the projection reads back exactly what the legacy path would have left.
    std::vector<std::pair<std::string, std::string>> tags;
    tags.emplace_back(core::kAcpMarkerTagKey, fields.id);
    const std::string field_prefix = std::string(core::kAcpFieldTagPrefix) + fields.id;
    if (!fields.name.empty()) {
        tags.emplace_back(field_prefix + ".name", fields.name);
    }
    tags.emplace_back(field_prefix + ".resolutionKind", fields.resolution_kind);
    if (!fields.text.empty()) {
        tags.emplace_back(field_prefix + ".text", fields.text);
    }
    if (fields.resolution_kind == "text") {
        if (!fields.confidence_claim_id.empty()) {
            tags.emplace_back(field_prefix + ".confidenceClaimId", fields.confidence_claim_id);
        }
    } else if (fields.resolution_kind == "topGoalReference") {
        tags.emplace_back(field_prefix + ".argumentPackageId", fields.argument_package_id);
        tags.emplace_back(field_prefix + ".topGoalId", fields.top_goal_id);
    }

    for (const sacm::model::ElementId& id : acp_tag_ids(*model_element, fields.id)) {
        const sacm::commands::MutationResult removed = doc.apply(sacm::commands::DeleteElement{.target = id});
        if (!removed.applied) {
            return applied_outcome(removed);
        }
    }
    EditOutcome outcome;
    outcome.applied = true;
    for (const auto& [key, value] : tags) {
        const sacm::commands::MutationResult added =
            doc.apply(sacm::commands::AddTaggedValue{.element = element, .key = key, .value = value});
        fill_diagnostics(outcome.diagnostics, added);
        if (!added.applied) {
            outcome.applied = false;
            return outcome;
        }
    }
    return outcome;
}

EditOutcome apply_remove_acp_tags(LibraryDocument& document, const std::string& element_id, const std::string& acp_id) {
    if (element_id.empty() || acp_id.empty()) {
        return EditOutcome{.supported = false};
    }
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);
    const auto* model_element = doc.find_as<sacm::model::ModelElement>(sacm::model::ElementId(element_id));
    if (model_element == nullptr) {
        return acp_tag_target_error(element_id);
    }
    EditOutcome outcome;
    for (const sacm::model::ElementId& id : acp_tag_ids(*model_element, acp_id)) {
        const sacm::commands::MutationResult removed = doc.apply(sacm::commands::DeleteElement{.target = id});
        fill_diagnostics(outcome.diagnostics, removed);
        if (!removed.applied) {
            outcome.applied = false;
            return outcome;
        }
        outcome.applied = true;
    }
    return outcome;
}

EditOutcome apply_set_meta_claims(LibraryDocument& document,
                                  const std::string& element_id,
                                  const std::vector<std::string>& meta_claim_ids) {
    if (element_id.empty()) {
        return EditOutcome{.supported = false};
    }
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);
    std::vector<sacm::model::ElementId> ids;
    ids.reserve(meta_claim_ids.size());
    for (const std::string& id : meta_claim_ids) {
        ids.emplace_back(id);
    }
    return applied_outcome(doc.apply(
        sacm::commands::SetMetaClaims{.element = sacm::model::ElementId(element_id), .meta_claims = std::move(ids)}));
}

AcpOutcome apply_create_confidence_argument_package(LibraryDocument& document,
                                                    const std::string& package_id,
                                                    const std::string& package_name,
                                                    const std::string& top_goal_id,
                                                    const std::string& top_goal_name,
                                                    const std::string& top_goal_content) {
    if (package_id.empty() || top_goal_id.empty()) {
        return AcpOutcome{.supported = false};
    }
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);
    const sacm::model::AssuranceCasePackage* root = root_case_package(doc);
    if (root == nullptr) {
        return AcpOutcome{.supported = false};
    }

    const auto failed = [](const sacm::commands::MutationResult& result) {
        AcpOutcome outcome;
        fill_diagnostics(outcome.diagnostics, result);
        return outcome;
    };

    const sacm::commands::MutationResult created_package = doc.apply(sacm::commands::CreateArgumentPackage{
        .parent = root->id(), .id = sacm::model::ElementId(package_id), .name = package_name});
    if (!created_package.applied) {
        return failed(created_package);
    }
    const sacm::model::ElementId package(package_id);
    // The purpose tag is what makes this a CONFIDENCE package; without it the
    // tree is indistinguishable from an ordinary argument package.
    const sacm::commands::MutationResult purpose =
        doc.apply(sacm::commands::AddTaggedValue{.element = package,
                                                 .key = core::kArgumentPackagePurposeTagKey,
                                                 .value = core::kArgumentPackagePurposeConfidence});
    if (!purpose.applied) {
        rollback_element(doc, package);
        return failed(purpose);
    }

    const sacm::commands::MutationResult created_goal = doc.apply(sacm::commands::CreateClaim{
        .parent = package, .id = sacm::model::ElementId(top_goal_id), .name = top_goal_name});
    if (!created_goal.applied) {
        rollback_element(doc, package);
        return failed(created_goal);
    }
    const sacm::model::ElementId goal(top_goal_id);
    if (!top_goal_content.empty()) {
        const sacm::commands::MutationResult described = set_terminology_description(doc, goal, top_goal_content);
        if (!described.applied) {
            rollback_element(doc, package);
            return failed(described);
        }
    }
    if (const sacm::commands::MutationResult tagged = add_gsn_identifier_tag(doc, goal); !tagged.applied) {
        rollback_element(doc, package);
        return failed(tagged);
    }

    AcpOutcome outcome;
    outcome.applied = true;
    return outcome;
}

} // namespace sacm_adapter
