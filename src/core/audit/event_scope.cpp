#include "core/audit/event_scope.h"

namespace core::audit {

namespace {

template <typename ElementT>
void InsertElementIdentity(const ElementT& element, ArgumentPackageScope& out) {
    if (!element.id.empty())
        out.element_ids.insert(element.id);
    if (!element.gid.empty())
        out.element_gids.insert(element.gid);
}

void AppendIfString(const nlohmann::ordered_json& payload, const char* key, std::vector<std::string>& out) {
    auto it = payload.find(key);
    if (it == payload.end() || !it->is_string())
        return;
    std::string value = it->get<std::string>();
    if (!value.empty())
        out.push_back(std::move(value));
}

void AppendStringArray(const nlohmann::ordered_json& payload, const char* key, std::vector<std::string>& out) {
    auto it = payload.find(key);
    if (it == payload.end() || !it->is_array())
        return;
    for (const auto& entry : *it) {
        if (!entry.is_string())
            continue;
        std::string value = entry.get<std::string>();
        if (!value.empty())
            out.push_back(std::move(value));
    }
}

} // namespace

ArgumentPackageScope CollectArgumentPackageScope(const sacm::ArgumentPackage& argument_package) {
    ArgumentPackageScope scope;
    for (const sacm::Claim& claim : argument_package.claims)
        InsertElementIdentity(claim, scope);
    for (const sacm::ArgumentReasoning& reasoning : argument_package.argumentReasonings)
        InsertElementIdentity(reasoning, scope);
    for (const sacm::ArtifactReference& reference : argument_package.artifactReferences)
        InsertElementIdentity(reference, scope);
    for (const sacm::AssertedInference& inference : argument_package.assertedInferences)
        InsertElementIdentity(inference, scope);
    for (const sacm::AssertedContext& context : argument_package.assertedContexts)
        InsertElementIdentity(context, scope);
    for (const sacm::AssertedEvidence& evidence : argument_package.assertedEvidences)
        InsertElementIdentity(evidence, scope);
    return scope;
}

std::vector<std::string> CollectEventElementIds(const AuditEvent& event) {
    std::vector<std::string> ids;
    if (!event.payload.is_object())
        return ids;

    AppendIfString(event.payload, "generated_id", ids);
    AppendIfString(event.payload, "generated_relationship_id", ids);
    AppendIfString(event.payload, "parent_id", ids);
    AppendIfString(event.payload, "element_id", ids);
    AppendIfString(event.payload, "target_id", ids);
    AppendIfString(event.payload, "source_id", ids);
    AppendStringArray(event.payload, "deleted_ids", ids);
    return ids;
}

bool TransactionTouchesScope(const AuditTransaction& transaction, const ArgumentPackageScope& scope) {
    for (const AuditEvent& event : transaction.events) {
        for (const std::string& id : CollectEventElementIds(event)) {
            if (scope.element_ids.count(id) > 0 || scope.element_gids.count(id) > 0)
                return true;
        }
    }
    return false;
}

} // namespace core::audit
