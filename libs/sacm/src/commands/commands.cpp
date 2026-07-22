#include "commands/commands.h"

#include "model/access.h"
#include "model/traverse.h"

#include "sacm/validation/codes.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <type_traits>
#include <unordered_set>

namespace sacm::commands::detail {

namespace {

using model::ElementId;
using model::SACMElement;
using sacm::detail::Access;
using validation::Diagnostic;
using validation::Severity;

void advance_id_counter(model::Document& document, model::ElementKind kind, const ElementId& id);
void index_subtree(model::Document& document, SACMElement& root);

Diagnostic make_error(std::string_view code, std::string_view requirement, const Operation& op,
                      std::vector<ElementId> affected, std::string message) {
    return Diagnostic{
        .code = std::string(code),
        .severity = Severity::Error,
        .requirement_id = std::string(requirement),
        .operation = std::string(operation_name(op)),
        .affected = std::move(affected),
        .location = std::nullopt,
        .message = std::move(message),
    };
}

// snake_case kind name used as generated-id prefix ("argument_package_3").
std::string id_prefix(model::ElementKind kind) {
    std::string prefix;
    for (const char c : metadata::kind_name(kind)) {
        if (std::isupper(static_cast<unsigned char>(c)) != 0) {
            if (!prefix.empty()) {
                prefix.push_back('_');
            }
            prefix.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        } else {
            prefix.push_back(c);
        }
    }
    return prefix;
}

// Deterministic generated id: first free "<prefix>_<n>" counting up from the
// document's per-kind counter. `claimed` covers ids planned earlier in the
// same operation. Pure: does not advance the document counter (perform
// advances it).
ElementId peek_generated_id(const model::Document& document, model::ElementKind kind,
                            std::unordered_set<ElementId>& claimed) {
    std::uint64_t counter = 0;
    const auto& counters = Access::id_counters(document);
    if (const auto it = counters.find(kind); it != counters.end()) {
        counter = it->second;
    }
    const std::string prefix = id_prefix(kind);
    while (true) {
        ++counter;
        ElementId candidate{std::format("{}_{}", prefix, counter)};
        if (!document.contains(candidate) && !claimed.contains(candidate)) {
            claimed.insert(candidate);
            return candidate;
        }
    }
}

// Validates a caller-provided id (or generates one) and records the
// Created effect. Returns an empty id when a diagnostic was produced.
ElementId plan_id(const model::Document& document, const std::optional<ElementId>& requested,
                  model::ElementKind kind, const Operation& op,
                  std::unordered_set<ElementId>& claimed, CheckOutcome& outcome) {
    if (!requested.has_value()) {
        return peek_generated_id(document, kind, claimed);
    }
    if (requested->empty() ||
        requested->value().find_first_of(" \t\r\n") != std::string::npos) {
        outcome.diagnostics.push_back(make_error(
            validation::codes::kIdInvalid, "SACM23-XMI-003", op, {*requested},
            std::format("invalid element id '{}'", requested->value())));
        return ElementId{};
    }
    if (document.contains(*requested) || claimed.contains(*requested)) {
        outcome.diagnostics.push_back(make_error(
            validation::codes::kCmdDuplicateId, "SACM23-CMD-002", op, {*requested},
            std::format("an element with id '{}' already exists", requested->value())));
        return ElementId{};
    }
    claimed.insert(*requested);
    return *requested;
}

std::string element_summary(const SACMElement& element) {
    std::string summary{metadata::kind_name(element.kind())};
    if (const auto* model_element = dynamic_cast<const model::ModelElement*>(&element)) {
        if (!model_element->name().content.empty()) {
            summary += std::format(" \"{}\"", model_element->name().content);
        }
    }
    return summary;
}

// ---------------------------------------------------------------- creates

CheckOutcome check_create_acp(const model::Document& document,
                              const CreateAssuranceCasePackage& create, const Operation& op) {
    CheckOutcome outcome;
    std::unordered_set<ElementId> claimed;
    std::optional<ElementId> parent_id;
    if (create.parent.has_value()) {
        const SACMElement* parent = document.find(*create.parent);
        if (parent == nullptr) {
            outcome.diagnostics.push_back(
                make_error(validation::codes::kCmdTargetNotFound, "SACM23-CMD-002", op,
                           {*create.parent},
                           std::format("parent '{}' not found", create.parent->value())));
            return outcome;
        }
        if (dynamic_cast<const model::AssuranceCasePackage*>(parent) == nullptr) {
            outcome.diagnostics.push_back(make_error(
                validation::codes::kCmdInvalidParent, "SACM23-CMD-002", op, {*create.parent},
                std::format("an AssuranceCasePackage cannot be created inside a {}",
                            metadata::kind_name(parent->kind()))));
            return outcome;
        }
        parent_id = *create.parent;
    }
    const ElementId id =
        plan_id(document, create.id, model::ElementKind::AssuranceCasePackage, op, claimed, outcome);
    if (!outcome.ok()) {
        return outcome;
    }
    outcome.effects.push_back(ChangeRecord{
        .id = id,
        .kind = model::ElementKind::AssuranceCasePackage,
        .change = ChangeRecord::Change::Created,
        .parent = parent_id,
        .property = std::nullopt,
        .before = std::nullopt,
        .after = std::format("AssuranceCasePackage \"{}\"", create.name),
    });
    return outcome;
}

CheckOutcome check_create_argument_package(const model::Document& document,
                                           const CreateArgumentPackage& create,
                                           const Operation& op) {
    CheckOutcome outcome;
    std::unordered_set<ElementId> claimed;
    const SACMElement* parent = document.find(create.parent);
    if (parent == nullptr) {
        outcome.diagnostics.push_back(
            make_error(validation::codes::kCmdTargetNotFound, "SACM23-CMD-003", op,
                       {create.parent},
                       std::format("parent '{}' not found", create.parent.value())));
        return outcome;
    }
    const bool parent_is_acp = dynamic_cast<const model::AssuranceCasePackage*>(parent) != nullptr;
    const bool parent_is_argument_package =
        dynamic_cast<const model::ArgumentPackage*>(parent) != nullptr;
    if (!parent_is_acp && !parent_is_argument_package) {
        outcome.diagnostics.push_back(make_error(
            validation::codes::kCmdInvalidParent, "SACM23-CMD-003", op, {create.parent},
            std::format("an ArgumentPackage cannot be created inside a {}",
                        metadata::kind_name(parent->kind()))));
        return outcome;
    }
    const ElementId id =
        plan_id(document, create.id, model::ElementKind::ArgumentPackage, op, claimed, outcome);
    if (!outcome.ok()) {
        return outcome;
    }
    outcome.effects.push_back(ChangeRecord{
        .id = id,
        .kind = model::ElementKind::ArgumentPackage,
        .change = ChangeRecord::Change::Created,
        .parent = create.parent,
        .property = std::nullopt,
        .before = std::nullopt,
        .after = std::format("ArgumentPackage \"{}\"", create.name),
    });
    return outcome;
}

CheckOutcome check_create_claim(const model::Document& document, const CreateClaim& create,
                                const Operation& op) {
    CheckOutcome outcome;
    std::unordered_set<ElementId> claimed;
    const SACMElement* parent = document.find(create.parent);
    if (parent == nullptr) {
        outcome.diagnostics.push_back(
            make_error(validation::codes::kCmdTargetNotFound, "SACM23-CMD-003", op,
                       {create.parent},
                       std::format("parent '{}' not found", create.parent.value())));
        return outcome;
    }
    if (dynamic_cast<const model::ArgumentPackage*>(parent) == nullptr) {
        outcome.diagnostics.push_back(make_error(
            validation::codes::kCmdInvalidParent, "SACM23-CMD-003", op, {create.parent},
            std::format("a Claim cannot be created inside a {}",
                        metadata::kind_name(parent->kind()))));
        return outcome;
    }
    const ElementId claim_id =
        plan_id(document, create.id, model::ElementKind::Claim, op, claimed, outcome);
    if (!outcome.ok()) {
        return outcome;
    }
    outcome.effects.push_back(ChangeRecord{
        .id = claim_id,
        .kind = model::ElementKind::Claim,
        .change = ChangeRecord::Change::Created,
        .parent = create.parent,
        .property = std::nullopt,
        .before = std::nullopt,
        .after = std::format("Claim \"{}\"", create.name),
    });
    if (!create.description.empty()) {
        const ElementId description_id =
            peek_generated_id(document, model::ElementKind::Description, claimed);
        outcome.effects.push_back(ChangeRecord{
            .id = description_id,
            .kind = model::ElementKind::Description,
            .change = ChangeRecord::Change::Created,
            .parent = claim_id,
            .property = std::nullopt,
            .before = std::nullopt,
            .after = std::format("Description \"{}\"", create.description),
        });
    }
    return outcome;
}

// ----------------------------------------------------------------- artifact

bool kind_is_simple_artifact_asset(model::ElementKind kind) {
    switch (kind) {
        case model::ElementKind::Artifact:
        case model::ElementKind::Activity:
        case model::ElementKind::Event:
        case model::ElementKind::Participant:
        case model::ElementKind::Technique:
        case model::ElementKind::Resource:
        case model::ElementKind::Property:
            return true;
        default:
            return false;
    }
}

CheckOutcome check_create_artifact(const model::Document& document, const ElementId& parent_id,
                                   const std::optional<ElementId>& requested_id,
                                   model::ElementKind kind, std::string_view name,
                                   const Operation& op) {
    CheckOutcome outcome;
    std::unordered_set<ElementId> claimed;
    const SACMElement* parent = document.find(parent_id);
    if (parent == nullptr) {
        outcome.diagnostics.push_back(
            make_error(validation::codes::kCmdTargetNotFound, "SACM23-ART-001", op, {parent_id},
                       std::format("parent '{}' not found", parent_id.value())));
        return outcome;
    }
    bool parent_ok = false;
    if (kind == model::ElementKind::ArtifactPackage) {
        parent_ok = dynamic_cast<const model::AssuranceCasePackage*>(parent) != nullptr ||
                    dynamic_cast<const model::ArtifactPackage*>(parent) != nullptr;
    } else if (kind == model::ElementKind::Property) {
        parent_ok = dynamic_cast<const model::ArtifactAsset*>(parent) != nullptr;
    } else {
        parent_ok = dynamic_cast<const model::ArtifactPackage*>(parent) != nullptr;
    }
    if (!parent_ok) {
        outcome.diagnostics.push_back(make_error(
            validation::codes::kCmdInvalidParent, "SACM23-ART-001", op, {parent_id},
            std::format("a {} cannot be created inside a {}", metadata::kind_name(kind),
                        metadata::kind_name(parent->kind()))));
        return outcome;
    }
    const ElementId id = plan_id(document, requested_id, kind, op, claimed, outcome);
    if (!outcome.ok()) {
        return outcome;
    }
    outcome.effects.push_back(ChangeRecord{
        .id = id,
        .kind = kind,
        .change = ChangeRecord::Change::Created,
        .parent = parent_id,
        .property = std::nullopt,
        .before = std::nullopt,
        .after = std::format("{} \"{}\"", metadata::kind_name(kind), name),
    });
    return outcome;
}

void attach_artifact(model::Document& document, const ElementId& parent_id,
                     std::unique_ptr<model::ArtifactElement> element) {
    model::ArtifactElement* raw = element.get();
    auto* parent = const_cast<SACMElement*>(document.find(parent_id));
    if (raw->kind() == model::ElementKind::Property) {
        auto* asset = dynamic_cast<model::ArtifactAsset*>(parent);
        Access::set_parent(*raw, asset);
        Access::properties(*asset).push_back(std::unique_ptr<model::Property>(
            static_cast<model::Property*>(element.release())));
    } else if (auto* pkg = dynamic_cast<model::ArtifactPackage*>(parent)) {
        Access::set_parent(*raw, pkg);
        Access::artifact_elements(*pkg).push_back(std::move(element));
    } else {
        auto* acp = static_cast<model::AssuranceCasePackage*>(parent);
        Access::set_parent(*raw, acp);
        Access::artifact_packages(*acp).push_back(std::unique_ptr<model::ArtifactPackage>(
            static_cast<model::ArtifactPackage*>(element.release())));
    }
    index_subtree(document, *raw);
}

void perform_create_artifact_package(model::Document& document,
                                     const CreateArtifactPackage& create,
                                     const std::vector<ChangeRecord>& effects) {
    const ChangeRecord& record = effects.front();
    auto package = std::make_unique<model::ArtifactPackage>(record.id);
    Access::name(*package) = model::LangString{.lang = "", .content = create.name};
    advance_id_counter(document, record.kind, record.id);
    attach_artifact(document, create.parent, std::move(package));
}

void perform_create_artifact_asset(model::Document& document, const CreateArtifactAsset& create,
                                   const std::vector<ChangeRecord>& effects) {
    const ChangeRecord& record = effects.front();
    std::unique_ptr<model::ArtifactAsset> asset;
    switch (create.kind) {
        case model::ElementKind::Artifact: {
            auto artifact = std::make_unique<model::Artifact>(record.id);
            Access::version(*artifact) = create.version;
            Access::date(*artifact) = create.date;
            asset = std::move(artifact);
            break;
        }
        case model::ElementKind::Activity: {
            auto activity = std::make_unique<model::Activity>(record.id);
            Access::start_time(*activity) = create.start_time;
            Access::end_time(*activity) = create.end_time;
            asset = std::move(activity);
            break;
        }
        case model::ElementKind::Event: {
            auto event = std::make_unique<model::Event>(record.id);
            Access::date(*event) = create.date;
            asset = std::move(event);
            break;
        }
        case model::ElementKind::Participant:
            asset = std::make_unique<model::Participant>(record.id);
            break;
        case model::ElementKind::Technique:
            asset = std::make_unique<model::Technique>(record.id);
            break;
        case model::ElementKind::Resource:
            asset = std::make_unique<model::Resource>(record.id);
            break;
        default:
            asset = std::make_unique<model::Property>(record.id);
            break;
    }
    Access::name(*asset) = model::LangString{.lang = "", .content = create.name};
    advance_id_counter(document, create.kind, record.id);
    attach_artifact(document, create.parent, std::move(asset));
}

void perform_create_artifact_relationship(model::Document& document,
                                          const CreateArtifactAssetRelationship& create,
                                          const std::vector<ChangeRecord>& effects) {
    const ChangeRecord& record = effects.front();
    auto relationship = std::make_unique<model::ArtifactAssetRelationship>(record.id);
    Access::name(*relationship) = model::LangString{.lang = "", .content = create.name};
    Access::sources(*relationship) = create.sources;
    Access::targets(*relationship) = create.targets;
    advance_id_counter(document, record.kind, record.id);
    attach_artifact(document, create.parent, std::move(relationship));
}

// ------------------------------------------------------------ argumentation

// Checks that `parent` is an ArgumentPackage; plans a single Created effect.
CheckOutcome check_create_argument_asset(const model::Document& document,
                                         const ElementId& parent_id,
                                         const std::optional<ElementId>& requested_id,
                                         model::ElementKind kind, std::string_view name,
                                         const Operation& op) {
    CheckOutcome outcome;
    std::unordered_set<ElementId> claimed;
    const SACMElement* parent = document.find(parent_id);
    if (parent == nullptr) {
        outcome.diagnostics.push_back(
            make_error(validation::codes::kCmdTargetNotFound, "SACM23-ARG-001", op, {parent_id},
                       std::format("parent '{}' not found", parent_id.value())));
        return outcome;
    }
    if (dynamic_cast<const model::ArgumentPackage*>(parent) == nullptr) {
        outcome.diagnostics.push_back(make_error(
            validation::codes::kCmdInvalidParent, "SACM23-ARG-001", op, {parent_id},
            std::format("a {} cannot be created inside a {}", metadata::kind_name(kind),
                        metadata::kind_name(parent->kind()))));
        return outcome;
    }
    const ElementId id = plan_id(document, requested_id, kind, op, claimed, outcome);
    if (!outcome.ok()) {
        return outcome;
    }
    outcome.effects.push_back(ChangeRecord{
        .id = id,
        .kind = kind,
        .change = ChangeRecord::Change::Created,
        .parent = parent_id,
        .property = std::nullopt,
        .before = std::nullopt,
        .after = std::format("{} \"{}\"", metadata::kind_name(kind), name),
    });
    return outcome;
}

// Requires `target` to exist and satisfy `predicate`; appends a diagnostic
// otherwise.
template <typename Predicate>
bool require_target(const model::Document& document, const ElementId& target,
                    std::string_view what, Predicate predicate, const Operation& op,
                    CheckOutcome& outcome) {
    const SACMElement* element = document.find(target);
    if (element == nullptr) {
        outcome.diagnostics.push_back(
            make_error(validation::codes::kCmdTargetNotFound, "SACM23-ARG-002", op, {target},
                       std::format("{} '{}' not found", what, target.value())));
        return false;
    }
    if (!predicate(*element)) {
        outcome.diagnostics.push_back(make_error(
            validation::codes::kRefWrongType, "SACM23-ARG-002", op, {target},
            std::format("{} '{}' is a {}, which is not a legal target kind", what, target.value(),
                        metadata::kind_name(element->kind()))));
        return false;
    }
    return true;
}

CheckOutcome check_create_asserted_relationship(const model::Document& document,
                                                const CreateAssertedRelationship& create,
                                                const Operation& op) {
    CheckOutcome outcome;
    if (!metadata::is_asserted_relationship_kind(create.kind)) {
        outcome.diagnostics.push_back(make_error(
            validation::codes::kCmdInvalidParent, "SACM23-ARG-001", op, {},
            std::format("{} is not an asserted relationship kind",
                        metadata::kind_name(create.kind))));
        return outcome;
    }
    outcome = check_create_argument_asset(document, create.parent, create.id, create.kind,
                                          create.name, op);
    if (!outcome.ok()) {
        return outcome;
    }
    if (create.sources.empty() || create.targets.empty()) {
        outcome.effects.clear();
        outcome.diagnostics.push_back(make_error(
            validation::codes::kMultiplicityViolation, "SACM23-ARG-002", op, {},
            "an asserted relationship needs at least one source and one target"));
        return outcome;
    }
    const auto is_argument_asset = [](const SACMElement& element) {
        return dynamic_cast<const model::ArgumentAsset*>(&element) != nullptr;
    };
    for (const ElementId& source : create.sources) {
        if (!require_target(document, source, "source", is_argument_asset, op, outcome)) {
            outcome.effects.clear();
            return outcome;
        }
    }
    for (const ElementId& target : create.targets) {
        if (!require_target(document, target, "target", is_argument_asset, op, outcome)) {
            outcome.effects.clear();
            return outcome;
        }
    }
    if (create.reasoning.has_value() &&
        !require_target(document, *create.reasoning, "reasoning",
                        [](const SACMElement& element) {
                            return element.kind() == model::ElementKind::ArgumentReasoning;
                        },
                        op, outcome)) {
        outcome.effects.clear();
        return outcome;
    }
    return outcome;
}

void perform_create_argument_reasoning(model::Document& document,
                                       const CreateArgumentReasoning& create,
                                       const std::vector<ChangeRecord>& effects) {
    const ChangeRecord& record = effects.front();
    auto reasoning = std::make_unique<model::ArgumentReasoning>(record.id);
    Access::name(*reasoning) = model::LangString{.lang = "", .content = create.name};
    Access::structure(*reasoning) = create.structure;
    advance_id_counter(document, record.kind, record.id);
    model::ArgumentReasoning* raw = reasoning.get();
    auto* parent =
        const_cast<model::ArgumentPackage*>(document.find_as<model::ArgumentPackage>(create.parent));
    Access::set_parent(*raw, parent);
    Access::argument_elements(*parent).push_back(std::move(reasoning));
    index_subtree(document, *raw);
}

void perform_create_artifact_reference(model::Document& document,
                                       const CreateArtifactReference& create,
                                       const std::vector<ChangeRecord>& effects) {
    const ChangeRecord& record = effects.front();
    auto reference = std::make_unique<model::ArtifactReference>(record.id);
    Access::name(*reference) = model::LangString{.lang = "", .content = create.name};
    Access::referenced_artifact_elements(*reference) = create.referenced_artifact_elements;
    advance_id_counter(document, record.kind, record.id);
    model::ArtifactReference* raw = reference.get();
    auto* parent =
        const_cast<model::ArgumentPackage*>(document.find_as<model::ArgumentPackage>(create.parent));
    Access::set_parent(*raw, parent);
    Access::argument_elements(*parent).push_back(std::move(reference));
    index_subtree(document, *raw);
}

void perform_create_asserted_relationship(model::Document& document,
                                          const CreateAssertedRelationship& create,
                                          const std::vector<ChangeRecord>& effects) {
    const ChangeRecord& record = effects.front();
    std::unique_ptr<model::AssertedRelationship> relationship;
    switch (create.kind) {
        case model::ElementKind::AssertedInference:
            relationship = std::make_unique<model::AssertedInference>(record.id);
            break;
        case model::ElementKind::AssertedEvidence:
            relationship = std::make_unique<model::AssertedEvidence>(record.id);
            break;
        case model::ElementKind::AssertedContext:
            relationship = std::make_unique<model::AssertedContext>(record.id);
            break;
        case model::ElementKind::AssertedArtifactSupport:
            relationship = std::make_unique<model::AssertedArtifactSupport>(record.id);
            break;
        default:
            relationship = std::make_unique<model::AssertedArtifactContext>(record.id);
            break;
    }
    Access::name(*relationship) = model::LangString{.lang = "", .content = create.name};
    Access::sources(*relationship) = create.sources;
    Access::targets(*relationship) = create.targets;
    Access::reasoning(*relationship) = create.reasoning;
    Access::is_counter(*relationship) = create.is_counter;
    advance_id_counter(document, create.kind, record.id);
    model::AssertedRelationship* raw = relationship.get();
    auto* parent =
        const_cast<model::ArgumentPackage*>(document.find_as<model::ArgumentPackage>(create.parent));
    Access::set_parent(*raw, parent);
    Access::argument_elements(*parent).push_back(std::move(relationship));
    index_subtree(document, *raw);
}

CheckOutcome check_set_assertion_declaration(const model::Document& document,
                                             const SetAssertionDeclaration& set,
                                             const Operation& op) {
    CheckOutcome outcome;
    const auto* assertion = document.find_as<model::Assertion>(set.element);
    if (assertion == nullptr) {
        outcome.diagnostics.push_back(make_error(
            validation::codes::kCmdTargetNotFound, "SACM23-ARG-001", op, {set.element},
            std::format("'{}' is not an Assertion", set.element.value())));
        return outcome;
    }
    outcome.effects.push_back(ChangeRecord{
        .id = set.element,
        .kind = assertion->kind(),
        .change = ChangeRecord::Change::Modified,
        .parent = std::nullopt,
        .property = "assertionDeclaration",
        .before = std::string(model::assertion_declaration_name(assertion->assertion_declaration())),
        .after = std::string(model::assertion_declaration_name(set.declaration)),
    });
    return outcome;
}

void perform_set_assertion_declaration(model::Document& document,
                                       const SetAssertionDeclaration& set) {
    auto* assertion = const_cast<model::Assertion*>(document.find_as<model::Assertion>(set.element));
    Access::assertion_declaration(*assertion) = set.declaration;
}

CheckOutcome check_add_meta_claim(const model::Document& document, const AddMetaClaim& add,
                                  const Operation& op) {
    CheckOutcome outcome;
    const auto* assertion = document.find_as<model::Assertion>(add.element);
    if (assertion == nullptr) {
        outcome.diagnostics.push_back(make_error(
            validation::codes::kCmdTargetNotFound, "SACM23-ARG-001", op, {add.element},
            std::format("'{}' is not an Assertion", add.element.value())));
        return outcome;
    }
    if (!require_target(document, add.meta_claim, "metaClaim",
                        [](const SACMElement& element) {
                            return element.kind() == model::ElementKind::Claim;
                        },
                        op, outcome)) {
        return outcome;
    }
    outcome.effects.push_back(ChangeRecord{
        .id = add.element,
        .kind = assertion->kind(),
        .change = ChangeRecord::Change::Modified,
        .parent = std::nullopt,
        .property = "metaClaim",
        .before = std::nullopt,
        .after = add.meta_claim.value(),
    });
    return outcome;
}

void perform_add_meta_claim(model::Document& document, const AddMetaClaim& add) {
    auto* assertion = const_cast<model::Assertion*>(document.find_as<model::Assertion>(add.element));
    Access::meta_claims(*assertion).push_back(add.meta_claim);
}

CheckOutcome check_add_relationship_source(const model::Document& document,
                                           const AddRelationshipSource& add, const Operation& op) {
    CheckOutcome outcome;
    const auto* relationship = document.find_as<model::AssertedRelationship>(add.relationship);
    if (relationship == nullptr) {
        outcome.diagnostics.push_back(make_error(
            validation::codes::kCmdTargetNotFound, "SACM23-ARG-002", op, {add.relationship},
            std::format("'{}' is not an AssertedRelationship", add.relationship.value())));
        return outcome;
    }
    // The source must be an ArgumentAsset, the same typing CreateAssertedRelationship enforces.
    const auto is_argument_asset = [](const SACMElement& element) {
        return dynamic_cast<const model::ArgumentAsset*>(&element) != nullptr;
    };
    if (!require_target(document, add.source, "source", is_argument_asset, op, outcome)) {
        return outcome;
    }
    const std::vector<ElementId>& sources = relationship->sources();
    if (std::find(sources.begin(), sources.end(), add.source) != sources.end()) {
        outcome.diagnostics.push_back(make_error(
            validation::codes::kMultiplicityViolation, "SACM23-ARG-002", op, {add.source},
            std::format("'{}' is already a source of '{}'", add.source.value(),
                        add.relationship.value())));
        return outcome;
    }
    outcome.effects.push_back(ChangeRecord{
        .id = add.relationship,
        .kind = relationship->kind(),
        .change = ChangeRecord::Change::Modified,
        .parent = std::nullopt,
        .property = "source",
        .before = std::nullopt,
        .after = add.source.value(),
    });
    return outcome;
}

void perform_add_relationship_source(model::Document& document, const AddRelationshipSource& add) {
    auto* relationship = const_cast<model::AssertedRelationship*>(
        document.find_as<model::AssertedRelationship>(add.relationship));
    Access::sources(*relationship).push_back(add.source);
}

// ------------------------------------------------------------- terminology

// Shared check for creating a terminology element under a parent.
CheckOutcome check_create_terminology(const model::Document& document, const ElementId& parent_id,
                                      const std::optional<ElementId>& requested_id,
                                      model::ElementKind kind, bool parent_may_be_acp,
                                      std::string_view name, const Operation& op) {
    CheckOutcome outcome;
    std::unordered_set<ElementId> claimed;
    const SACMElement* parent = document.find(parent_id);
    if (parent == nullptr) {
        outcome.diagnostics.push_back(
            make_error(validation::codes::kCmdTargetNotFound, "SACM23-TERM-001", op, {parent_id},
                       std::format("parent '{}' not found", parent_id.value())));
        return outcome;
    }
    const bool parent_is_terminology_package =
        dynamic_cast<const model::TerminologyPackage*>(parent) != nullptr;
    const bool parent_is_acp =
        parent_may_be_acp && dynamic_cast<const model::AssuranceCasePackage*>(parent) != nullptr;
    if (!parent_is_terminology_package && !parent_is_acp) {
        outcome.diagnostics.push_back(make_error(
            validation::codes::kCmdInvalidParent, "SACM23-TERM-001", op, {parent_id},
            std::format("a {} cannot be created inside a {}", metadata::kind_name(kind),
                        metadata::kind_name(parent->kind()))));
        return outcome;
    }
    const ElementId id = plan_id(document, requested_id, kind, op, claimed, outcome);
    if (!outcome.ok()) {
        return outcome;
    }
    outcome.effects.push_back(ChangeRecord{
        .id = id,
        .kind = kind,
        .change = ChangeRecord::Change::Created,
        .parent = parent_id,
        .property = std::nullopt,
        .before = std::nullopt,
        .after = std::format("{} \"{}\"", metadata::kind_name(kind), name),
    });
    return outcome;
}

CheckOutcome check_create_term(const model::Document& document, const CreateTerm& create,
                               const Operation& op) {
    CheckOutcome outcome = check_create_terminology(document, create.parent, create.id,
                                                    model::ElementKind::Term,
                                                    /*parent_may_be_acp=*/false, create.name, op);
    if (!outcome.ok()) {
        return outcome;
    }
    if (create.origin.has_value() && document.find(*create.origin) == nullptr) {
        outcome.effects.clear();
        outcome.diagnostics.push_back(
            make_error(validation::codes::kCmdTargetNotFound, "SACM23-TERM-001", op,
                       {*create.origin},
                       std::format("origin '{}' not found", create.origin->value())));
    }
    return outcome;
}

// Attaches a freshly built terminology element to its parent.
void attach_terminology(model::Document& document, const ElementId& parent_id,
                        std::unique_ptr<model::TerminologyElement> element) {
    model::TerminologyElement* raw = element.get();
    auto* parent = const_cast<SACMElement*>(document.find(parent_id));
    if (auto* pkg = dynamic_cast<model::TerminologyPackage*>(parent)) {
        Access::set_parent(*raw, pkg);
        Access::terminology_elements(*pkg).push_back(std::move(element));
    } else {
        auto* acp = static_cast<model::AssuranceCasePackage*>(parent);
        Access::set_parent(*raw, acp);
        Access::terminology_packages(*acp).push_back(std::unique_ptr<model::TerminologyPackage>(
            static_cast<model::TerminologyPackage*>(element.release())));
    }
    index_subtree(document, *raw);
}

void perform_create_terminology_package(model::Document& document,
                                        const CreateTerminologyPackage& create,
                                        const std::vector<ChangeRecord>& effects) {
    const ChangeRecord& record = effects.front();
    auto package = std::make_unique<model::TerminologyPackage>(record.id);
    Access::name(*package) = model::LangString{.lang = "", .content = create.name};
    advance_id_counter(document, record.kind, record.id);
    attach_terminology(document, create.parent, std::move(package));
}

void perform_create_category(model::Document& document, const CreateCategory& create,
                             const std::vector<ChangeRecord>& effects) {
    const ChangeRecord& record = effects.front();
    auto category = std::make_unique<model::Category>(record.id);
    Access::name(*category) = model::LangString{.lang = "", .content = create.name};
    advance_id_counter(document, record.kind, record.id);
    attach_terminology(document, create.parent, std::move(category));
}

void perform_create_term(model::Document& document, const CreateTerm& create,
                         const std::vector<ChangeRecord>& effects) {
    const ChangeRecord& record = effects.front();
    auto term = std::make_unique<model::Term>(record.id);
    Access::name(*term) = model::LangString{.lang = "", .content = create.name};
    Access::value(*term) = create.value;
    Access::external_reference(*term) = create.external_reference;
    Access::origin(*term) = create.origin;
    advance_id_counter(document, record.kind, record.id);
    attach_terminology(document, create.parent, std::move(term));
}

void perform_create_expression(model::Document& document, const CreateExpression& create,
                               const std::vector<ChangeRecord>& effects) {
    const ChangeRecord& record = effects.front();
    auto expression = std::make_unique<model::Expression>(record.id);
    Access::name(*expression) = model::LangString{.lang = "", .content = create.name};
    Access::value(*expression) = create.value;
    advance_id_counter(document, record.kind, record.id);
    attach_terminology(document, create.parent, std::move(expression));
}

// ---------------------------------------------------------------- setters

CheckOutcome check_set_citation(const model::Document& document, const SetCitation& set,
                                const Operation& op) {
    CheckOutcome outcome;
    const SACMElement* element = document.find(set.element);
    if (element == nullptr) {
        outcome.diagnostics.push_back(
            make_error(validation::codes::kCmdTargetNotFound, "SACM23-BASE-002", op,
                       {set.element}, std::format("element '{}' not found", set.element.value())));
        return outcome;
    }
    if (set.cited.has_value() && document.find(*set.cited) == nullptr) {
        outcome.diagnostics.push_back(
            make_error(validation::codes::kCmdTargetNotFound, "SACM23-BASE-002", op, {*set.cited},
                       std::format("cited element '{}' not found", set.cited->value())));
        return outcome;
    }
    outcome.effects.push_back(ChangeRecord{
        .id = set.element,
        .kind = element->kind(),
        .change = ChangeRecord::Change::Modified,
        .parent = std::nullopt,
        .property = "citedElement",
        .before = element->cited_element().has_value() ? std::optional(element->cited_element()->value())
                                                       : std::nullopt,
        .after = set.cited.has_value() ? std::optional(set.cited->value()) : std::nullopt,
    });
    return outcome;
}

void perform_set_citation(model::Document& document, const SetCitation& set) {
    auto* element = const_cast<SACMElement*>(document.find(set.element));
    Access::cited_element(*element) = set.cited;
    Access::is_citation(*element) = set.cited.has_value();
}

// Shared check for setters targeting a ModelElement.
CheckOutcome check_model_element_target(const model::Document& document,
                                        const model::ElementId& target,
                                        std::string_view requirement, const Operation& op,
                                        std::optional<std::string> property,
                                        std::optional<std::string> before,
                                        std::optional<std::string> after) {
    CheckOutcome outcome;
    const SACMElement* element = document.find(target);
    if (element == nullptr) {
        outcome.diagnostics.push_back(
            make_error(validation::codes::kCmdTargetNotFound, requirement, op, {target},
                       std::format("element '{}' not found", target.value())));
        return outcome;
    }
    if (dynamic_cast<const model::ModelElement*>(element) == nullptr) {
        outcome.diagnostics.push_back(make_error(
            validation::codes::kCmdInvalidParent, requirement, op, {target},
            std::format("a {} carries no name/description/taggedValue metadata",
                        metadata::kind_name(element->kind()))));
        return outcome;
    }
    outcome.effects.push_back(ChangeRecord{
        .id = target,
        .kind = element->kind(),
        .change = ChangeRecord::Change::Modified,
        .parent = std::nullopt,
        .property = std::move(property),
        .before = std::move(before),
        .after = std::move(after),
    });
    return outcome;
}

CheckOutcome check_set_name(const model::Document& document, const SetName& set,
                            const Operation& op) {
    std::optional<std::string> before;
    if (const auto* element = document.find_as<model::ModelElement>(set.element)) {
        before = element->name().content;
    }
    return check_model_element_target(document, set.element, "SACM23-BASE-001", op, "name",
                                      std::move(before), set.name);
}

void perform_set_name(model::Document& document, const SetName& set) {
    auto* element =
        const_cast<model::ModelElement*>(document.find_as<model::ModelElement>(set.element));
    Access::name(*element) =
        model::LangString{.lang = set.language, .content = set.name, .expression_ref = {}};
}

CheckOutcome check_set_description(const model::Document& document, const SetDescription& set,
                                   const Operation& op) {
    CheckOutcome outcome = check_model_element_target(document, set.element, "SACM23-BASE-001", op,
                                                      "description", std::nullopt, set.text);
    if (!outcome.ok()) {
        return outcome;
    }
    // Creating the Description element is an extra effect when absent.
    const auto* element = document.find_as<model::ModelElement>(set.element);
    if (element->descriptions().empty() && !set.text.empty()) {
        std::unordered_set<ElementId> claimed;
        outcome.effects.push_back(ChangeRecord{
            .id = peek_generated_id(document, model::ElementKind::Description, claimed),
            .kind = model::ElementKind::Description,
            .change = ChangeRecord::Change::Created,
            .parent = set.element,
            .property = std::nullopt,
            .before = std::nullopt,
            .after = set.text,
        });
    }
    return outcome;
}

void perform_set_description(model::Document& document, const SetDescription& set,
                             const std::vector<ChangeRecord>& effects) {
    auto* element =
        const_cast<model::ModelElement*>(document.find_as<model::ModelElement>(set.element));
    if (element->descriptions().empty()) {
        if (set.text.empty()) {
            return;
        }
        const ChangeRecord& created = effects.back();
        auto description = std::make_unique<model::Description>(created.id);
        Access::content(*description).set(set.language, set.text);
        Access::set_parent(*description, element);
        advance_id_counter(document, model::ElementKind::Description, created.id);
        model::Description* raw = description.get();
        Access::descriptions(*element).push_back(std::move(description));
        Access::index(document)[raw->id()] = raw;
        return;
    }
    model::MultiLangString& content = Access::content(*Access::descriptions(*element).front());
    if (set.text.empty()) {
        std::erase_if(content.values, [&set](const model::LangString& entry) {
            return entry.lang == set.language;
        });
    } else {
        content.set(set.language, set.text);
    }
}

CheckOutcome check_add_tagged_value(const model::Document& document, const AddTaggedValue& add,
                                    const Operation& op) {
    CheckOutcome outcome;
    const SACMElement* element = document.find(add.element);
    if (element == nullptr) {
        outcome.diagnostics.push_back(
            make_error(validation::codes::kCmdTargetNotFound, "SACM23-BASE-001", op,
                       {add.element}, std::format("element '{}' not found", add.element.value())));
        return outcome;
    }
    if (dynamic_cast<const model::ModelElement*>(element) == nullptr) {
        outcome.diagnostics.push_back(make_error(
            validation::codes::kCmdInvalidParent, "SACM23-BASE-001", op, {add.element},
            std::format("a {} cannot carry tagged values", metadata::kind_name(element->kind()))));
        return outcome;
    }
    std::unordered_set<ElementId> claimed;
    const ElementId id =
        plan_id(document, add.id, model::ElementKind::TaggedValue, op, claimed, outcome);
    if (!outcome.ok()) {
        return outcome;
    }
    outcome.effects.push_back(ChangeRecord{
        .id = id,
        .kind = model::ElementKind::TaggedValue,
        .change = ChangeRecord::Change::Created,
        .parent = add.element,
        .property = std::nullopt,
        .before = std::nullopt,
        .after = std::format("{}={}", add.key, add.value),
    });
    return outcome;
}

void perform_add_tagged_value(model::Document& document, const AddTaggedValue& add,
                              const std::vector<ChangeRecord>& effects) {
    const ChangeRecord& record = effects.front();
    auto* element =
        const_cast<model::ModelElement*>(document.find_as<model::ModelElement>(add.element));
    auto tagged = std::make_unique<model::TaggedValue>(record.id);
    Access::key(*tagged).set(add.language, add.key);
    Access::content(*tagged).set(add.language, add.value);
    Access::set_parent(*tagged, element);
    advance_id_counter(document, model::ElementKind::TaggedValue, record.id);
    model::TaggedValue* raw = tagged.get();
    Access::tagged_values(*element).push_back(std::move(tagged));
    Access::index(document)[raw->id()] = raw;
}

// ---------------------------------------------------------------- delete

CheckOutcome check_delete(const model::Document& document, const DeleteElement& erase,
                          const Operation& op) {
    CheckOutcome outcome;
    const SACMElement* target = document.find(erase.target);
    if (target == nullptr) {
        outcome.diagnostics.push_back(
            make_error(validation::codes::kCmdTargetNotFound, "SACM23-CMD-005", op,
                       {erase.target},
                       std::format("element '{}' not found", erase.target.value())));
        return outcome;
    }

    // The subtree that would disappear.
    std::unordered_set<ElementId> doomed;
    std::size_t non_utility_descendants = 0;
    model::traverse::for_each_descendant(*target, [&](const SACMElement& element) {
        doomed.insert(element.id());
        const bool is_utility = dynamic_cast<const model::UtilityElement*>(&element) != nullptr;
        if (&element != target && !is_utility) {
            ++non_utility_descendants;
        }
    });

    if (non_utility_descendants > 0 && erase.package_policy == PackageDeletePolicy::RejectIfNonEmpty) {
        outcome.diagnostics.push_back(make_error(
            validation::codes::kCmdPackageNotEmpty, "SACM23-PKG-003", op, {erase.target},
            std::format("'{}' contains {} element(s); deleting it requires "
                        "PackageDeletePolicy::DeleteRecursively",
                        erase.target.value(), non_utility_descendants)));
        return outcome;
    }

    // Inbound references from outside the doomed subtree, iterated to a
    // fixpoint because cascading relationship deletes can expose further
    // referrers.
    struct InboundRef {
        const SACMElement* referrer;
        std::string_view role;
        ElementId target_id;
    };
    std::vector<const SACMElement*> cascaded_relationships;
    std::vector<InboundRef> cleaned_references;
    const SACMElement* target_package = model::traverse::nearest_package(*target);

    bool changed = true;
    bool rejected = false;
    while (changed && !rejected) {
        changed = false;
        cleaned_references.clear();
        document.for_each_element([&](const SACMElement& element) {
            if (rejected || doomed.contains(element.id())) {
                return;
            }
            model::traverse::for_each_reference(
                element, [&](const model::traverse::ReferenceUse& use) {
                    if (rejected || !doomed.contains(*use.target)) {
                        return;
                    }
                    // Cross-package policy first: referrer in another package.
                    const SACMElement* referrer_package = model::traverse::nearest_package(element);
                    const bool cross_package = referrer_package != target_package;
                    if (cross_package &&
                        erase.cross_package_policy ==
                            CrossPackageReferencePolicy::RejectIfExternalReferencesExist) {
                        outcome.diagnostics.push_back(make_error(
                            validation::codes::kCmdExternalReferences, "SACM23-PKG-003", op,
                            {element.id(), *use.target},
                            std::format("'{}' is referenced ({}) from '{}' in another package",
                                        use.target->value(), use.role, element.id().value())));
                        rejected = true;
                        return;
                    }
                    if (erase.reference_policy == ReferenceDeletePolicy::RejectIfReferenced) {
                        outcome.diagnostics.push_back(make_error(
                            validation::codes::kCmdDeleteReferenced, "SACM23-CMD-005", op,
                            {element.id(), *use.target},
                            std::format("'{}' is referenced ({}) by '{}'; deleting it requires "
                                        "ReferenceDeletePolicy::DeleteReferencingRelationships",
                                        use.target->value(), use.role, element.id().value())));
                        rejected = true;
                        return;
                    }
                    // Cascade relationships; clean references elsewhere.
                    const bool is_relationship =
                        metadata::is_asserted_relationship_kind(element.kind()) ||
                        element.kind() == model::ElementKind::ArtifactAssetRelationship;
                    if (is_relationship) {
                        model::traverse::for_each_descendant(
                            element, [&](const SACMElement& descendant) {
                                doomed.insert(descendant.id());
                            });
                        cascaded_relationships.push_back(&element);
                        changed = true;
                    } else {
                        cleaned_references.push_back(InboundRef{&element, use.role, *use.target});
                    }
                });
        });
    }
    if (rejected) {
        return outcome;
    }

    // Effects in a stable order: cascaded relationships, reference cleanups,
    // then the target subtree (document order).
    for (const SACMElement* relationship : cascaded_relationships) {
        model::traverse::for_each_descendant(*relationship, [&](const SACMElement& element) {
            outcome.effects.push_back(ChangeRecord{
                .id = element.id(),
                .kind = element.kind(),
                .change = ChangeRecord::Change::RelationshipDeleted,
                .parent = element.parent() != nullptr
                              ? std::optional<ElementId>(element.parent()->id())
                              : std::nullopt,
                .property = std::nullopt,
                .before = element_summary(element),
                .after = std::nullopt,
            });
        });
    }
    for (const InboundRef& ref : cleaned_references) {
        outcome.effects.push_back(ChangeRecord{
            .id = ref.referrer->id(),
            .kind = ref.referrer->kind(),
            .change = ChangeRecord::Change::Modified,
            .parent = std::nullopt,
            .property = std::string(ref.role),
            .before = ref.target_id.value(),
            .after = std::nullopt,
        });
    }
    model::traverse::for_each_descendant(*target, [&](const SACMElement& element) {
        outcome.effects.push_back(ChangeRecord{
            .id = element.id(),
            .kind = element.kind(),
            .change = ChangeRecord::Change::Deleted,
            .parent = element.parent() != nullptr ? std::optional<ElementId>(element.parent()->id())
                                                  : std::nullopt,
            .property = std::nullopt,
            .before = element_summary(element),
            .after = std::nullopt,
        });
    });
    return outcome;
}

// ---------------------------------------------------------------- perform

void index_subtree(model::Document& document, SACMElement& root) {
    Access::index(document)[root.id()] = &root;
    model::traverse::for_each_child(root, [&document, &root](SACMElement& child) {
        Access::set_parent(child, &root);
        index_subtree(document, child);
    });
}

void advance_id_counter(model::Document& document, model::ElementKind kind, const ElementId& id) {
    const std::string prefix = id_prefix(kind) + "_";
    if (id.value().starts_with(prefix)) {
        std::uint64_t counter = 0;
        const std::string suffix = id.value().substr(prefix.size());
        if (!suffix.empty() && std::ranges::all_of(suffix, [](char c) {
                return std::isdigit(static_cast<unsigned char>(c)) != 0;
            })) {
            counter = std::stoull(suffix);
        }
        auto& counters = Access::id_counters(document);
        counters[kind] = std::max(counters[kind], counter);
    }
}

void perform_create_acp(model::Document& document, const CreateAssuranceCasePackage& create,
                        const std::vector<ChangeRecord>& effects) {
    const ChangeRecord& record = effects.front();
    auto package = std::make_unique<model::AssuranceCasePackage>(record.id);
    Access::name(*package) = model::LangString{.lang = "", .content = create.name};
    advance_id_counter(document, record.kind, record.id);
    model::AssuranceCasePackage* raw = package.get();
    if (create.parent.has_value()) {
        auto* parent = const_cast<model::AssuranceCasePackage*>(
            document.find_as<model::AssuranceCasePackage>(*create.parent));
        Access::set_parent(*raw, parent);
        Access::assurance_case_packages(*parent).push_back(std::move(package));
    } else {
        Access::roots(document).push_back(std::move(package));
    }
    index_subtree(document, *raw);
}

void perform_create_argument_package(model::Document& document,
                                     const CreateArgumentPackage& create,
                                     const std::vector<ChangeRecord>& effects) {
    const ChangeRecord& record = effects.front();
    auto package = std::make_unique<model::ArgumentPackage>(record.id);
    Access::name(*package) = model::LangString{.lang = "", .content = create.name};
    advance_id_counter(document, record.kind, record.id);
    model::ArgumentPackage* raw = package.get();
    auto* parent = const_cast<SACMElement*>(document.find(create.parent));
    if (auto* acp = dynamic_cast<model::AssuranceCasePackage*>(parent)) {
        Access::set_parent(*raw, acp);
        Access::argument_packages(*acp).push_back(std::move(package));
    } else {
        auto* argument_package = static_cast<model::ArgumentPackage*>(parent);
        Access::set_parent(*raw, argument_package);
        Access::argument_elements(*argument_package).push_back(std::move(package));
    }
    index_subtree(document, *raw);
}

void perform_create_claim(model::Document& document, const CreateClaim& create,
                          const std::vector<ChangeRecord>& effects) {
    const ChangeRecord& claim_record = effects.front();
    auto claim = std::make_unique<model::Claim>(claim_record.id);
    Access::name(*claim) = model::LangString{.lang = create.language, .content = create.name};
    advance_id_counter(document, claim_record.kind, claim_record.id);
    if (effects.size() > 1) {
        const ChangeRecord& description_record = effects[1];
        auto description = std::make_unique<model::Description>(description_record.id);
        Access::content(*description).set(create.language, create.description);
        advance_id_counter(document, description_record.kind, description_record.id);
        Access::descriptions(*claim).push_back(std::move(description));
    }
    model::Claim* raw = claim.get();
    auto* parent = const_cast<model::ArgumentPackage*>(
        document.find_as<model::ArgumentPackage>(create.parent));
    Access::set_parent(*raw, parent);
    Access::argument_elements(*parent).push_back(std::move(claim));
    index_subtree(document, *raw);
}

// Unlinks `element` from its owning container and destroys it.
void unlink_subtree(model::Document& document, SACMElement& element) {
    const ElementId id = element.id();
    SACMElement* parent = const_cast<SACMElement*>(element.parent());

    const auto erase_from = [&](auto& container) {
        return std::erase_if(container, [&element](const auto& owned) {
            return owned.get() == &element;
        }) > 0;
    };

    bool unlinked = false;
    if (parent == nullptr) {
        unlinked = erase_from(Access::roots(document)) || erase_from(Access::other_roots(document));
    } else {
        if (auto* model_element = dynamic_cast<model::ModelElement*>(parent); !unlinked && model_element != nullptr) {
            unlinked = erase_from(Access::descriptions(*model_element)) ||
                       erase_from(Access::implementation_constraints(*model_element)) ||
                       erase_from(Access::notes(*model_element)) ||
                       erase_from(Access::tagged_values(*model_element));
        }
        if (auto* asset = dynamic_cast<model::ArtifactAsset*>(parent); !unlinked && asset != nullptr) {
            unlinked = erase_from(Access::properties(*asset));
        }
        if (auto* acp = dynamic_cast<model::AssuranceCasePackage*>(parent); !unlinked && acp != nullptr) {
            unlinked = erase_from(Access::assurance_case_packages(*acp)) ||
                       erase_from(Access::argument_packages(*acp)) ||
                       erase_from(Access::artifact_packages(*acp)) ||
                       erase_from(Access::terminology_packages(*acp));
        }
        if (auto* pkg = dynamic_cast<model::ArgumentPackage*>(parent); !unlinked && pkg != nullptr) {
            unlinked = erase_from(Access::argument_elements(*pkg));
        }
        if (auto* pkg = dynamic_cast<model::ArtifactPackage*>(parent); !unlinked && pkg != nullptr) {
            unlinked = erase_from(Access::artifact_elements(*pkg));
        }
        if (auto* pkg = dynamic_cast<model::TerminologyPackage*>(parent); !unlinked && pkg != nullptr) {
            unlinked = erase_from(Access::terminology_elements(*pkg));
        }
    }
    (void)unlinked;
    (void)id;
}

void perform_delete(model::Document& document, const DeleteElement&,
                    const std::vector<ChangeRecord>& effects) {
    // Doomed ids and subtree roots (a doomed element whose parent is not
    // doomed) from the planned effects.
    std::unordered_set<ElementId> doomed;
    for (const ChangeRecord& record : effects) {
        if (record.change == ChangeRecord::Change::Deleted ||
            record.change == ChangeRecord::Change::RelationshipDeleted) {
            doomed.insert(record.id);
        }
    }

    // Clean references from surviving referrers.
    for (const ChangeRecord& record : effects) {
        if (record.change != ChangeRecord::Change::Modified) {
            continue;
        }
        auto* referrer = const_cast<SACMElement*>(document.find(record.id));
        if (referrer != nullptr) {
            model::traverse::remove_references_to(*referrer, doomed);
        }
    }

    // Unlink subtree roots; children die with their owners.
    std::vector<SACMElement*> subtree_roots;
    for (const ChangeRecord& record : effects) {
        if (record.change != ChangeRecord::Change::Deleted &&
            record.change != ChangeRecord::Change::RelationshipDeleted) {
            continue;
        }
        auto* element = const_cast<SACMElement*>(document.find(record.id));
        if (element == nullptr) {
            continue;  // already unlinked as part of an earlier subtree
        }
        const SACMElement* parent = element->parent();
        if (parent == nullptr || !doomed.contains(parent->id())) {
            subtree_roots.push_back(element);
        }
    }
    for (const ElementId& id : doomed) {
        Access::index(document).erase(id);
    }
    for (SACMElement* root : subtree_roots) {
        unlink_subtree(document, *root);
    }
}

}  // namespace

bool CheckOutcome::ok() const { return !validation::has_errors(diagnostics); }

CheckOutcome check(const model::Document& document, const Operation& operation) {
    return std::visit(
        [&document, &operation](const auto& op) -> CheckOutcome {
            using T = std::decay_t<decltype(op)>;
            if constexpr (std::is_same_v<T, CreateAssuranceCasePackage>) {
                return check_create_acp(document, op, operation);
            } else if constexpr (std::is_same_v<T, CreateArgumentPackage>) {
                return check_create_argument_package(document, op, operation);
            } else if constexpr (std::is_same_v<T, CreateClaim>) {
                return check_create_claim(document, op, operation);
            } else if constexpr (std::is_same_v<T, CreateArtifactPackage>) {
                return check_create_artifact(document, op.parent, op.id,
                                             model::ElementKind::ArtifactPackage, op.name,
                                             operation);
            } else if constexpr (std::is_same_v<T, CreateArtifactAsset>) {
                CheckOutcome outcome;
                if (!kind_is_simple_artifact_asset(op.kind)) {
                    outcome.diagnostics.push_back(make_error(
                        validation::codes::kCmdInvalidParent, "SACM23-ART-001", operation, {},
                        std::format("{} is not an artifact asset kind",
                                    metadata::kind_name(op.kind))));
                    return outcome;
                }
                return check_create_artifact(document, op.parent, op.id, op.kind, op.name,
                                             operation);
            } else if constexpr (std::is_same_v<T, CreateArtifactAssetRelationship>) {
                CheckOutcome outcome = check_create_artifact(
                    document, op.parent, op.id, model::ElementKind::ArtifactAssetRelationship,
                    op.name, operation);
                if (!outcome.ok()) {
                    return outcome;
                }
                if (op.sources.empty() || op.targets.empty()) {
                    outcome.effects.clear();
                    outcome.diagnostics.push_back(make_error(
                        validation::codes::kMultiplicityViolation, "SACM23-ART-001", operation, {},
                        "an artifact asset relationship needs at least one source and one "
                        "target"));
                    return outcome;
                }
                const auto is_artifact_asset = [](const SACMElement& element) {
                    return dynamic_cast<const model::ArtifactAsset*>(&element) != nullptr;
                };
                for (const ElementId& source : op.sources) {
                    if (!require_target(document, source, "source", is_artifact_asset, operation,
                                        outcome)) {
                        outcome.effects.clear();
                        return outcome;
                    }
                }
                for (const ElementId& target : op.targets) {
                    if (!require_target(document, target, "target", is_artifact_asset, operation,
                                        outcome)) {
                        outcome.effects.clear();
                        return outcome;
                    }
                }
                return outcome;
            } else if constexpr (std::is_same_v<T, CreateArgumentReasoning>) {
                CheckOutcome outcome = check_create_argument_asset(
                    document, op.parent, op.id, model::ElementKind::ArgumentReasoning, op.name,
                    operation);
                if (outcome.ok() && op.structure.has_value() &&
                    !require_target(document, *op.structure, "structure",
                                    [](const SACMElement& element) {
                                        return dynamic_cast<const model::ArgumentPackage*>(
                                                   &element) != nullptr;
                                    },
                                    operation, outcome)) {
                    outcome.effects.clear();
                }
                return outcome;
            } else if constexpr (std::is_same_v<T, CreateArtifactReference>) {
                CheckOutcome outcome = check_create_argument_asset(
                    document, op.parent, op.id, model::ElementKind::ArtifactReference, op.name,
                    operation);
                if (outcome.ok()) {
                    for (const ElementId& referenced : op.referenced_artifact_elements) {
                        if (!require_target(document, referenced, "referencedArtifactElement",
                                            [](const SACMElement& element) {
                                                return dynamic_cast<const model::ArtifactElement*>(
                                                           &element) != nullptr;
                                            },
                                            operation, outcome)) {
                            outcome.effects.clear();
                            break;
                        }
                    }
                }
                return outcome;
            } else if constexpr (std::is_same_v<T, CreateAssertedRelationship>) {
                return check_create_asserted_relationship(document, op, operation);
            } else if constexpr (std::is_same_v<T, SetAssertionDeclaration>) {
                return check_set_assertion_declaration(document, op, operation);
            } else if constexpr (std::is_same_v<T, AddMetaClaim>) {
                return check_add_meta_claim(document, op, operation);
            } else if constexpr (std::is_same_v<T, AddRelationshipSource>) {
                return check_add_relationship_source(document, op, operation);
            } else if constexpr (std::is_same_v<T, CreateTerminologyPackage>) {
                return check_create_terminology(document, op.parent, op.id,
                                                model::ElementKind::TerminologyPackage,
                                                /*parent_may_be_acp=*/true, op.name, operation);
            } else if constexpr (std::is_same_v<T, CreateCategory>) {
                return check_create_terminology(document, op.parent, op.id,
                                                model::ElementKind::Category,
                                                /*parent_may_be_acp=*/false, op.name, operation);
            } else if constexpr (std::is_same_v<T, CreateTerm>) {
                return check_create_term(document, op, operation);
            } else if constexpr (std::is_same_v<T, CreateExpression>) {
                return check_create_terminology(document, op.parent, op.id,
                                                model::ElementKind::Expression,
                                                /*parent_may_be_acp=*/false, op.name, operation);
            } else if constexpr (std::is_same_v<T, SetCitation>) {
                return check_set_citation(document, op, operation);
            } else if constexpr (std::is_same_v<T, SetName>) {
                return check_set_name(document, op, operation);
            } else if constexpr (std::is_same_v<T, SetDescription>) {
                return check_set_description(document, op, operation);
            } else if constexpr (std::is_same_v<T, AddTaggedValue>) {
                return check_add_tagged_value(document, op, operation);
            } else {
                return check_delete(document, op, operation);
            }
        },
        operation);
}

void perform(model::Document& document, const Operation& operation,
             const std::vector<ChangeRecord>& effects) {
    std::visit(
        [&document, &effects](const auto& op) {
            using T = std::decay_t<decltype(op)>;
            if constexpr (std::is_same_v<T, CreateAssuranceCasePackage>) {
                perform_create_acp(document, op, effects);
            } else if constexpr (std::is_same_v<T, CreateArgumentPackage>) {
                perform_create_argument_package(document, op, effects);
            } else if constexpr (std::is_same_v<T, CreateClaim>) {
                perform_create_claim(document, op, effects);
            } else if constexpr (std::is_same_v<T, CreateArtifactPackage>) {
                perform_create_artifact_package(document, op, effects);
            } else if constexpr (std::is_same_v<T, CreateArtifactAsset>) {
                perform_create_artifact_asset(document, op, effects);
            } else if constexpr (std::is_same_v<T, CreateArtifactAssetRelationship>) {
                perform_create_artifact_relationship(document, op, effects);
            } else if constexpr (std::is_same_v<T, CreateArgumentReasoning>) {
                perform_create_argument_reasoning(document, op, effects);
            } else if constexpr (std::is_same_v<T, CreateArtifactReference>) {
                perform_create_artifact_reference(document, op, effects);
            } else if constexpr (std::is_same_v<T, CreateAssertedRelationship>) {
                perform_create_asserted_relationship(document, op, effects);
            } else if constexpr (std::is_same_v<T, SetAssertionDeclaration>) {
                perform_set_assertion_declaration(document, op);
            } else if constexpr (std::is_same_v<T, AddMetaClaim>) {
                perform_add_meta_claim(document, op);
            } else if constexpr (std::is_same_v<T, AddRelationshipSource>) {
                perform_add_relationship_source(document, op);
            } else if constexpr (std::is_same_v<T, CreateTerminologyPackage>) {
                perform_create_terminology_package(document, op, effects);
            } else if constexpr (std::is_same_v<T, CreateCategory>) {
                perform_create_category(document, op, effects);
            } else if constexpr (std::is_same_v<T, CreateTerm>) {
                perform_create_term(document, op, effects);
            } else if constexpr (std::is_same_v<T, CreateExpression>) {
                perform_create_expression(document, op, effects);
            } else if constexpr (std::is_same_v<T, SetCitation>) {
                perform_set_citation(document, op);
            } else if constexpr (std::is_same_v<T, SetName>) {
                perform_set_name(document, op);
            } else if constexpr (std::is_same_v<T, SetDescription>) {
                perform_set_description(document, op, effects);
            } else if constexpr (std::is_same_v<T, AddTaggedValue>) {
                perform_add_tagged_value(document, op, effects);
            } else {
                perform_delete(document, op, effects);
            }
        },
        operation);
}

}  // namespace sacm::commands::detail
