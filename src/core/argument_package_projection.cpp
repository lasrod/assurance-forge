#include "core/argument_package_projection.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_set>

namespace core {

namespace {

template <typename ElementT>
void AddElementIdentity(const ElementT& element,
                        std::unordered_set<std::string>& ids,
                        std::unordered_set<std::string>& gids) {
    if (!element.id.empty())
        ids.insert(element.id);
    if (!element.gid.empty())
        gids.insert(element.gid);
}

} // namespace

const sacm::ArgumentPackage* FindArgumentPackageByIdentity(const sacm::AssuranceCasePackage& package,
                                                           std::string_view package_id,
                                                           std::string_view package_gid) {
    auto found = std::find_if(package.argumentPackages.begin(), package.argumentPackages.end(),
                              [&](const sacm::ArgumentPackage& pkg) {
                                  const bool id_matches = !package_id.empty() && pkg.id == package_id;
                                  const bool gid_matches = !package_gid.empty() && pkg.gid == package_gid;
                                  return id_matches || gid_matches;
                              });
    return found == package.argumentPackages.end() ? nullptr : &*found;
}

parser::AssuranceCase BuildArgumentPackageProjection(const parser::AssuranceCase& source_model,
                                                     const sacm::ArgumentPackage& argument_package,
                                                     std::string_view fallback_name) {
    std::unordered_set<std::string> element_ids;
    std::unordered_set<std::string> element_gids;
    for (const sacm::Claim& claim : argument_package.claims)
        AddElementIdentity(claim, element_ids, element_gids);
    for (const sacm::ArgumentReasoning& reasoning : argument_package.argumentReasonings)
        AddElementIdentity(reasoning, element_ids, element_gids);
    for (const sacm::ArtifactReference& artifact_reference : argument_package.artifactReferences)
        AddElementIdentity(artifact_reference, element_ids, element_gids);
    for (const sacm::AssertedInference& inference : argument_package.assertedInferences)
        AddElementIdentity(inference, element_ids, element_gids);
    for (const sacm::AssertedContext& context : argument_package.assertedContexts)
        AddElementIdentity(context, element_ids, element_gids);
    for (const sacm::AssertedEvidence& evidence : argument_package.assertedEvidences)
        AddElementIdentity(evidence, element_ids, element_gids);

    parser::AssuranceCase projection;
    projection.id = argument_package.id.empty() ? source_model.id : argument_package.id;
    projection.name = argument_package.name.empty() ? std::string(fallback_name) : argument_package.name;
    projection.description = argument_package.description;
    for (const parser::SacmElement& element : source_model.elements) {
        if (element_ids.count(element.id) > 0 || element_gids.count(element.gid) > 0)
            projection.elements.push_back(element);
    }
    for (const parser::AcpRecord& acp : source_model.acps) {
        if (element_ids.count(acp.target_id) > 0 || element_gids.count(acp.target_id) > 0)
            projection.acps.push_back(acp);
    }
    return projection;
}

} // namespace core
