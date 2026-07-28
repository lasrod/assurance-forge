#include "core/argument_package_projection.h"

#include "core/derived_views.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

void CollectPackageIdentity(const sacm::ArgumentPackage& argument_package,
                            std::unordered_set<std::string>& element_ids,
                            std::unordered_set<std::string>& element_gids) {
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
}

parser::AssuranceCase ProjectOntoPackage(const parser::AssuranceCase& source_model,
                                         const sacm::ArgumentPackage& argument_package,
                                         const std::unordered_set<std::string>& element_ids,
                                         const std::unordered_set<std::string>& element_gids,
                                         std::string_view fallback_name) {
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

    // A bare strategy (an ArgumentReasoning with a `strategyTarget` tag and no
    // inference yet) is placed under its goal by a RENDER-ONLY placeholder
    // inference, which by design is never in the SACM package -- so the filter
    // above always drops it and the strategy would render detached from the goal
    // it was just added to. Re-synthesize the placement on the projected view.
    SynthesizeBareStrategyPlacements(projection, argument_package);
    return projection;
}

// Which element ids each element names. Relationships name their ends; a
// challenged relationship names the claim challenging it; an inference names the
// reasoning that explains it.
void AppendReferences(const parser::SacmElement& element, std::vector<std::string>& out) {
    out.insert(out.end(), element.source_refs.begin(), element.source_refs.end());
    out.insert(out.end(), element.target_refs.begin(), element.target_refs.end());
    out.insert(out.end(), element.meta_claim_refs.begin(), element.meta_claim_refs.end());
    if (!element.reasoning_ref.empty())
        out.push_back(element.reasoning_ref);
}

// Undirected "mentions" adjacency over the model. Undirected because attachment
// runs both ways: the new relationship names the existing goal, and the new
// claim is named by that relationship.
std::unordered_map<std::string, std::vector<std::string>> BuildMentionGraph(
    const parser::AssuranceCase& model) {
    std::unordered_map<std::string, std::vector<std::string>> graph;
    for (const parser::SacmElement& element : model.elements) {
        if (element.id.empty())
            continue;
        std::vector<std::string> referenced;
        AppendReferences(element, referenced);
        for (const std::string& other : referenced) {
            if (other.empty())
                continue;
            graph[element.id].push_back(other);
            graph[other].push_back(element.id);
        }
    }
    return graph;
}

// The additions reachable from `seeds`, travelling only through additions. An
// addition two hops out -- a claim attached by a new relationship to a new
// strategy -- is reached; a committed element belonging to another package is
// never traversed, so one package's additions cannot leak into another's canvas.
std::unordered_set<std::string> AdditionsReachableFrom(
    const std::unordered_map<std::string, std::vector<std::string>>& graph,
    const std::unordered_set<std::string>&                           seeds,
    const std::unordered_set<std::string>&                           additions) {
    std::unordered_set<std::string> reached;
    std::vector<std::string>        frontier(seeds.begin(), seeds.end());
    while (!frontier.empty()) {
        const std::string current = frontier.back();
        frontier.pop_back();
        const std::unordered_map<std::string, std::vector<std::string>>::const_iterator neighbours =
            graph.find(current);
        if (neighbours == graph.end())
            continue;
        for (const std::string& neighbour : neighbours->second) {
            if (additions.count(neighbour) == 0 || reached.count(neighbour) > 0)
                continue;
            reached.insert(neighbour);
            frontier.push_back(neighbour);
        }
    }
    return reached;
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
    CollectPackageIdentity(argument_package, element_ids, element_gids);
    return ProjectOntoPackage(source_model, argument_package, element_ids, element_gids,
                              fallback_name);
}

parser::AssuranceCase BuildArgumentPackagePreviewProjection(
    const parser::AssuranceCase&      preview_model,
    const sacm::AssuranceCasePackage& package,
    const sacm::ArgumentPackage&      argument_package,
    const std::vector<std::string>&   added_element_ids,
    std::string_view                  fallback_name) {
    std::unordered_set<std::string> element_ids;
    std::unordered_set<std::string> element_gids;
    CollectPackageIdentity(argument_package, element_ids, element_gids);

    const std::unordered_set<std::string> additions(added_element_ids.begin(),
                                                    added_element_ids.end());
    if (additions.empty())
        return ProjectOntoPackage(preview_model, argument_package, element_ids, element_gids,
                                  fallback_name);

    const std::unordered_map<std::string, std::vector<std::string>> graph =
        BuildMentionGraph(preview_model);

    for (const std::string& reached : AdditionsReachableFrom(graph, element_ids, additions))
        element_ids.insert(reached);

    // An addition that hangs off nothing committed anywhere in the document --
    // the first goal of an empty argument -- has no package to be connected to.
    // It goes on the first argument package, which is where applying the change
    // set would put it, rather than being drawn nowhere.
    const bool is_first_package =
        !package.argumentPackages.empty() && &package.argumentPackages.front() == &argument_package;
    if (is_first_package) {
        std::unordered_set<std::string> committed;
        for (const parser::SacmElement& element : preview_model.elements) {
            if (!element.id.empty() && additions.count(element.id) == 0)
                committed.insert(element.id);
        }
        const std::unordered_set<std::string> attached_somewhere =
            AdditionsReachableFrom(graph, committed, additions);
        for (const std::string& addition : additions) {
            if (attached_somewhere.count(addition) == 0)
                element_ids.insert(addition);
        }
    }

    return ProjectOntoPackage(preview_model, argument_package, element_ids, element_gids,
                              fallback_name);
}

} // namespace core
