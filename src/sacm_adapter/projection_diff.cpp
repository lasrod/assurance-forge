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

void compare_field(std::vector<ProjectionDifference>& out,
                   const std::string& id,
                   std::string_view field,
                   const std::string& legacy,
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

void compare_lang_map(std::vector<ProjectionDifference>& out,
                      const std::string& id,
                      std::string_view field,
                      const std::map<std::string, std::string>& legacy,
                      const std::map<std::string, std::string>& projected) {
    if (legacy == projected) {
        return;
    }
    // Report the first concrete divergence -- an entry on one side only, or a
    // value mismatch -- so counts alone (which can be equal) do not make the
    // message uninformative in CI logs.
    std::string detail;
    for (const auto& [lang, text] : legacy) {
        const auto found = projected.find(lang);
        if (found == projected.end()) {
            detail = std::format("'{}' only in legacy", lang);
            break;
        }
        if (found->second != text) {
            detail = std::format("'{}': legacy '{}' vs projected '{}'", lang, text, found->second);
            break;
        }
    }
    if (detail.empty()) {
        for (const auto& [lang, text] : projected) {
            if (!legacy.contains(lang)) {
                detail = std::format("'{}' only in projected", lang);
                break;
            }
        }
    }
    out.push_back(ProjectionDifference{
        .category = "field",
        .path = std::format("{}.{}", id, field),
        .message = detail,
    });
}

// Reference lists are compared as sets: SACM does not order association ends,
// so a different order is not a difference.
void compare_refs(std::vector<ProjectionDifference>& out,
                  const std::string& id,
                  std::string_view field,
                  const std::vector<std::string>& legacy,
                  const std::vector<std::string>& projected) {
    const std::set<std::string> legacy_set(legacy.begin(), legacy.end());
    const std::set<std::string> projected_set(projected.begin(), projected.end());
    if (legacy_set == projected_set) {
        return;
    }
    out.push_back(ProjectionDifference{
        .category = "field",
        .path = std::format("{}.{}", id, field),
        .message = std::format("legacy {} refs vs projected {} refs", legacy_set.size(), projected_set.size()),
    });
}

} // namespace

std::vector<ProjectionDifference> diff_cases(const core::AssuranceCase& legacy, const core::AssuranceCase& projected) {
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

    // Assurance Claim Points, matched by id. A per-id difference is reported for
    // an ACP present on one side only or whose fields differ.
    const auto acp_index = [](const core::AssuranceCase& source) {
        std::map<std::string, const core::AcpRecord*> index;
        for (const core::AcpRecord& acp : source.acps) {
            index.emplace(acp.id, &acp);
        }
        return index;
    };
    const auto legacy_acps = acp_index(legacy);
    const auto projected_acps = acp_index(projected);
    const auto acp_fields_equal = [](const core::AcpRecord& a, const core::AcpRecord& b) {
        return a.name == b.name && a.target_kind == b.target_kind && a.target_id == b.target_id &&
               a.resolution_kind == b.resolution_kind && a.text == b.text &&
               a.confidence_claim_id == b.confidence_claim_id && a.argument_package_id == b.argument_package_id &&
               a.top_goal_id == b.top_goal_id;
    };
    for (const auto& [id, acp] : legacy_acps) {
        const auto found = projected_acps.find(id);
        if (found == projected_acps.end()) {
            differences.push_back(
                ProjectionDifference{.category = "acp-missing",
                                     .path = id,
                                     .message = std::format("legacy has ACP '{}'; projection does not", id)});
        } else if (!acp_fields_equal(*acp, *found->second)) {
            differences.push_back(
                ProjectionDifference{.category = "acp-field", .path = id, .message = "ACP fields differ"});
        }
    }
    for (const auto& [id, acp] : projected_acps) {
        if (!legacy_acps.contains(id)) {
            differences.push_back(
                ProjectionDifference{.category = "acp-extra",
                                     .path = id,
                                     .message = std::format("projection has ACP '{}'; legacy does not", id)});
        }
    }

    const auto legacy_index = index_by_id(legacy);
    const auto projected_index = index_by_id(projected);

    for (const auto& [id, element] : legacy_index) {
        if (!projected_index.contains(id)) {
            differences.push_back(ProjectionDifference{
                .category = "element-missing",
                .path = id,
                .message = std::format("legacy has {} '{}'; projection does not", element->type, element->name),
            });
        }
    }
    for (const auto& [id, element] : projected_index) {
        if (!legacy_index.contains(id)) {
            differences.push_back(ProjectionDifference{
                .category = "element-extra",
                .path = id,
                .message = std::format("projection has {} '{}'; legacy does not", element->type, element->name),
            });
        }
    }

    for (const auto& [id, legacy_element] : legacy_index) {
        const auto found = projected_index.find(id);
        if (found == projected_index.end()) {
            continue;
        }
        const core::SacmElement& other = *found->second;
        // Every field the POD model carries is compared: the projection must be
        // proven equivalent to the legacy parser before rendering depends on it,
        // and a partial comparison would let a gap ship silently.
        compare_field(differences, id, "type", legacy_element->type, other.type);
        compare_field(differences, id, "name", legacy_element->name, other.name);
        compare_field(differences, id, "content", legacy_element->content, other.content);
        // `description` on a claim-like element is EXCLUDED, not compared and
        // budgeted. The two models deliberately disagree there since ADR 0012: a
        // claim carries one clause-8.9 Description and that Description is its
        // statement, so the projection leaves the POD note empty while the legacy
        // parser mirrors the statement into it. Counting that as a fidelity
        // difference would inflate the recorded budget on every claim in every
        // fixture, and a budget that large stops being able to catch the drift it
        // exists to catch. Every other kind still compares -- their `<description>`
        // genuinely is a note, and a divergence there is still a defect.
        if (!core::ClaimLikeCarriesStatementAsDescription(*legacy_element)) {
            compare_field(differences, id, "description", legacy_element->description, other.description);
        }
        compare_field(differences, id, "gid", legacy_element->gid, other.gid);
        // The legacy parser leaves assertionDeclaration empty when the attribute
        // is absent; the library makes the clause-11.10 default ("asserted")
        // explicit. Same meaning, so an empty-vs-asserted pair is not a loss.
        const auto normalize_declaration = [](const std::string& value) {
            return value.empty() ? std::string("asserted") : value;
        };
        compare_field(differences,
                      id,
                      "assertion_declaration",
                      normalize_declaration(legacy_element->assertion_declaration),
                      normalize_declaration(other.assertion_declaration));
        compare_field(differences, id, "reasoning_ref", legacy_element->reasoning_ref, other.reasoning_ref);
        compare_refs(differences, id, "source_refs", legacy_element->source_refs, other.source_refs);
        compare_refs(differences, id, "target_refs", legacy_element->target_refs, other.target_refs);
        compare_refs(differences, id, "meta_claim_refs", legacy_element->meta_claim_refs, other.meta_claim_refs);
        compare_lang_map(differences, id, "name_langs", legacy_element->name_langs, other.name_langs);
        // Excluded for the same reason, and it has to be the same predicate: the
        // language map is the per-language half of the field above, so comparing
        // it while skipping the field would report the identical divergence under
        // a different category.
        if (!core::ClaimLikeCarriesStatementAsDescription(*legacy_element)) {
            compare_lang_map(
                differences, id, "description_langs", legacy_element->description_langs, other.description_langs);
        }
        compare_lang_map(differences, id, "content_langs", legacy_element->content_langs, other.content_langs);
        if (legacy_element->is_counter != other.is_counter) {
            differences.push_back(ProjectionDifference{
                .category = "field",
                .path = std::format("{}.is_counter", id),
                .message = std::format("legacy {} vs projected {}", legacy_element->is_counter, other.is_counter),
            });
        }
        if (legacy_element->undeveloped != other.undeveloped) {
            differences.push_back(ProjectionDifference{
                .category = "field",
                .path = std::format("{}.undeveloped", id),
                .message = std::format("legacy {} vs projected {}", legacy_element->undeveloped, other.undeveloped),
            });
        }
        if (legacy_element->is_abstract != other.is_abstract) {
            differences.push_back(ProjectionDifference{
                .category = "field",
                .path = std::format("{}.is_abstract", id),
                .message = std::format("legacy {} vs projected {}", legacy_element->is_abstract, other.is_abstract),
            });
        }
    }

    return differences;
}

} // namespace sacm_adapter
