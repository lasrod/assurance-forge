#include "sacm_adapter/projection_diff.h"

#include <algorithm>
#include <format>
#include <map>
#include <set>
#include <string_view>

namespace sacm_adapter {

namespace {

std::map<std::string, const core::SacmElement*> index_by_id(const core::AssuranceCase& source) {
    std::map<std::string, const core::SacmElement*> index;
    for (const core::SacmElement& element : source.elements) {
        index.emplace(element.id, &element);
    }
    return index;
}

void compare_field(std::vector<ProjectionDifference>& out, const std::string& id,
                   std::string_view field, const std::string& legacy,
                   const std::string& projected) {
    if (legacy == projected) {
        return;
    }
    out.push_back(ProjectionDifference{
        .category = "field",
        .path = std::format("{}.{}", id, field),
        .message = std::format("legacy '{}' vs projected '{}'", legacy, projected),
    });
}

// Reference lists are compared as sets: SACM does not order association ends,
// so a different order is not a difference.
void compare_refs(std::vector<ProjectionDifference>& out, const std::string& id,
                  std::string_view field, const std::vector<std::string>& legacy,
                  const std::vector<std::string>& projected) {
    const std::set<std::string> legacy_set(legacy.begin(), legacy.end());
    const std::set<std::string> projected_set(projected.begin(), projected.end());
    if (legacy_set == projected_set) {
        return;
    }
    out.push_back(ProjectionDifference{
        .category = "field",
        .path = std::format("{}.{}", id, field),
        .message = std::format("legacy {} refs vs projected {} refs", legacy_set.size(),
                               projected_set.size()),
    });
}

} // namespace

std::vector<ProjectionDifference> diff_cases(const core::AssuranceCase& legacy,
                                             const core::AssuranceCase& projected) {
    std::vector<ProjectionDifference> differences;

    if (legacy.id != projected.id) {
        differences.push_back(ProjectionDifference{
            .category = "case",
            .path = "id",
            .message = std::format("legacy '{}' vs projected '{}'", legacy.id, projected.id),
        });
    }
    if (legacy.name != projected.name) {
        differences.push_back(ProjectionDifference{
            .category = "case",
            .path = "name",
            .message = std::format("legacy '{}' vs projected '{}'", legacy.name, projected.name),
        });
    }

    const auto legacy_index = index_by_id(legacy);
    const auto projected_index = index_by_id(projected);

    for (const auto& [id, element] : legacy_index) {
        if (!projected_index.contains(id)) {
            differences.push_back(ProjectionDifference{
                .category = "element-missing",
                .path = id,
                .message = std::format("legacy has {} '{}'; projection does not", element->type,
                                       element->name),
            });
        }
    }
    for (const auto& [id, element] : projected_index) {
        if (!legacy_index.contains(id)) {
            differences.push_back(ProjectionDifference{
                .category = "element-extra",
                .path = id,
                .message = std::format("projection has {} '{}'; legacy does not", element->type,
                                       element->name),
            });
        }
    }

    for (const auto& [id, legacy_element] : legacy_index) {
        const auto found = projected_index.find(id);
        if (found == projected_index.end()) {
            continue;
        }
        const core::SacmElement& other = *found->second;
        compare_field(differences, id, "type", legacy_element->type, other.type);
        compare_field(differences, id, "name", legacy_element->name, other.name);
        compare_field(differences, id, "description", legacy_element->description,
                      other.description);
        compare_refs(differences, id, "source_refs", legacy_element->source_refs,
                     other.source_refs);
        compare_refs(differences, id, "target_refs", legacy_element->target_refs,
                     other.target_refs);
        if (legacy_element->is_counter != other.is_counter) {
            differences.push_back(ProjectionDifference{
                .category = "field",
                .path = std::format("{}.is_counter", id),
                .message = std::format("legacy {} vs projected {}", legacy_element->is_counter,
                                       other.is_counter),
            });
        }
        if (legacy_element->undeveloped != other.undeveloped) {
            differences.push_back(ProjectionDifference{
                .category = "field",
                .path = std::format("{}.undeveloped", id),
                .message = std::format("legacy {} vs projected {}", legacy_element->undeveloped,
                                       other.undeveloped),
            });
        }
    }

    return differences;
}

} // namespace sacm_adapter
