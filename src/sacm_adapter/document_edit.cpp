#include "sacm_adapter/document_edit.h"

#include "sacm_adapter/gsn_role_tag.h"
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
#include <unordered_set>

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

// The library language a primary-language content edit must overwrite. A legacy
// `content=` statement is stored lang-less (set("", ...)); editing it under the
// primary language must overwrite that entry in place, not append a parallel
// "en" one that `primary()` would never return. So we target the front
// Description's existing language.
std::string primary_description_language(const sacm::model::ModelElement& element) {
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
        // Non-primary languages accumulate a per-language content map the POD
        // keeps flat but the library stores as extra Description LangStrings;
        // that multi-language mapping is a later slice. Under a non-primary
        // language, leave it unsupported so the command bus re-derives from the
        // authoritative package instead (matching the legacy edit exactly).
        if (language != kPrimaryLanguage) {
            return unsupported_outcome();
        }
        const auto* element = doc.find_as<sacm::model::ModelElement>(id);
        if (element == nullptr || !content_maps_to_description(element->kind())) {
            return unsupported_outcome();
        }
        const sacm::commands::Operation operation = sacm::commands::SetDescription{
            .element = id,
            .text = value,
            .language = primary_description_language(*element),
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
    std::optional<sacm::model::AssertionDeclaration> assertion;  // set for Assumption/Justification
    // GSN node role to record as a vendor TaggedValue (e.g. "Justification"),
    // preserving a GSN type SACM's AssertionDeclaration cannot express on its own.
    std::optional<std::string> gsn_role;
    sacm::metadata::ElementKind relationship_kind;
    bool attach_via_reasoning = false;  // Strategy: the reasoning end, not a source
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

std::optional<ChildPlan> plan_child(ChildKind kind, const sacm::model::ElementId& package_id,
                                    const std::optional<sacm::model::ElementId>& element_id) {
    using sacm::metadata::ElementKind;
    switch (kind) {
    case ChildKind::Goal:
        return ChildPlan{
            .create_element = sacm::commands::CreateClaim{.parent = package_id, .id = element_id},
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
        return ChildPlan{
            .create_element =
                sacm::commands::CreateArtifactReference{.parent = package_id, .id = element_id},
            .assertion = std::nullopt,
            .relationship_kind = ElementKind::AssertedEvidence};
    case ChildKind::Context:
        return ChildPlan{
            .create_element =
                sacm::commands::CreateArtifactReference{.parent = package_id, .id = element_id},
            .assertion = std::nullopt,
            .relationship_kind = ElementKind::AssertedContext};
    case ChildKind::Assumption:
        return ChildPlan{
            .create_element = sacm::commands::CreateClaim{.parent = package_id, .id = element_id},
            .assertion = sacm::model::AssertionDeclaration::Assumed,
            .relationship_kind = ElementKind::AssertedContext};
    case ChildKind::Justification:
        // A GSN Justification maps to a Claim with assertionDeclaration =
        // axiomatic (the standards-correct mapping, docs/sacm/sacm-gsn-mapping.md
        // -- not the legacy non-standard "justification" literal). SACM cannot
        // distinguish a Justification from an axiomatically-asserted Goal, so the
        // GSN role is preserved in a vendor TaggedValue and translated back by
        // the projection.
        return ChildPlan{
            .create_element = sacm::commands::CreateClaim{.parent = package_id, .id = element_id},
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

} // namespace

AddChildOutcome apply_add_child(LibraryDocument& document, const std::string& parent_id,
                                ChildKind kind, const std::string& element_id,
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
            doc.apply(sacm::commands::CreateArgumentReasoning{.parent = package_id,
                                                              .id = to_optional_id(element_id)});
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
        AddChildOutcome outcome;
        outcome.supported = true;
        outcome.applied = true;
        outcome.new_element_id = strategy_id.value();
        return outcome;  // no relationship yet -- materialized on the first sub-goal
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

    // 2. Assumption/Justification set an assertion declaration.
    if (plan->assertion.has_value()) {
        const sacm::commands::MutationResult declared = doc.apply(sacm::commands::SetAssertionDeclaration{
            .element = created_id, .declaration = *plan->assertion});
        if (!declared.applied) {
            rollback_element(doc, created_id);
            return failed_child(declared);
        }
    }

    // 2b. Preserve a GSN role (Justification) SACM cannot express, as a vendor tag.
    if (plan->gsn_role.has_value()) {
        const sacm::commands::MutationResult tagged = doc.apply(sacm::commands::AddTaggedValue{
            .element = created_id, .key = kGsnRoleTagKey, .value = *plan->gsn_role});
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

AddChildOutcome apply_challenge(LibraryDocument& document, const std::string& target_id,
                                ChallengeSource source, const std::string& element_id,
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
        create_counter =
            sacm::commands::CreateArtifactReference{.parent = package_id, .id = counter_id_hint};
        relationship_kind = sacm::metadata::ElementKind::AssertedEvidence;
        break;
    }

    const sacm::commands::MutationResult created = doc.apply(create_counter);
    if (!created.applied || created.created_ids().empty()) {
        return failed_child(created);
    }
    const sacm::model::ElementId counter_id = created.created_ids().front();

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
                                       const sacm::model::ElementId& element, const std::string& key,
                                       const std::string& value,
                                       std::optional<sacm::model::ElementId> tag_id = {}) {
    return doc.apply(sacm::commands::AddTaggedValue{
        .element = element, .id = std::move(tag_id), .key = key, .value = value});
}

} // namespace

AcpOutcome apply_add_acp(LibraryDocument& document, const std::string& target_id) {
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);
    const sacm::model::ElementId target(target_id);

    const sacm::model::SACMElement* element = doc.find(target);
    // Element ACPs are eligible only on an ArtifactReference (core's
    // ElementEligibleForAcp); claims and relationships are later slices.
    if (element == nullptr ||
        element->kind() != sacm::metadata::ElementKind::ArtifactReference) {
        return unsupported_acp();
    }

    const std::string acp_id = next_acp_id(existing_acp_ids(doc));

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
    const sacm::commands::MutationResult resolution = add_tag(
        doc, target, std::string(kAcpMarkerKey) + "." + acp_id + ".resolutionKind", "none");
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
        // Match the legacy helper, which drops relationships that become empty
        // rather than rejecting a referenced target.
        .reference_policy = sacm::commands::ReferenceDeletePolicy::DeleteReferencingRelationships,
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

} // namespace sacm_adapter
