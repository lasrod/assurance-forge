#include "core/drafts/draft_provenance.h"

#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/document_edit.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <map>

namespace core::drafts {

namespace {

constexpr char kSourceField[] = "source";
constexpr char kLabelField[] = "label";
constexpr char kSessionField[] = "session";
constexpr char kTitleField[] = "title";
constexpr char kRationaleField[] = "rationale";

std::string TagKey(const std::string& contribution_id, const char* field) {
    return std::string(kDraftProvenanceTagPrefix) + contribution_id + "." + field;
}

// FNV-1a, because the id has to be the same on every run and on every platform,
// which `std::hash` does not promise.
std::uint32_t StableHash(const std::string& text) {
    std::uint32_t hash = 2166136261u;
    for (const char character : text) {
        hash ^= static_cast<unsigned char>(character);
        hash *= 16777619u;
    }
    return hash;
}

bool WriteField(sacm_adapter::LibraryDocument& document,
                const std::string& element_id,
                const std::string& contribution_id,
                const char* field,
                const std::string& value,
                std::string& error) {
    if (value.empty())
        return true;
    const sacm_adapter::EditOutcome outcome =
        sacm_adapter::apply_set_tagged_value(document, element_id, TagKey(contribution_id, field), value);
    if (outcome.supported && outcome.applied)
        return true;
    error = outcome.diagnostics.empty() ? std::format("The provenance of {} could not be recorded.", element_id)
                                        : std::format("The provenance of {} could not be recorded: {}",
                                                      element_id,
                                                      outcome.diagnostics.front().message);
    return false;
}

void AssignField(DraftProvenance& provenance, const std::string& field, const std::string& value) {
    if (field == kSourceField) {
        DraftSource source = DraftSource::Human;
        if (DraftSourceFromString(value, source))
            provenance.source = source;
    } else if (field == kLabelField) {
        provenance.label = value;
    } else if (field == kSessionField) {
        provenance.session_id = value;
    } else if (field == kTitleField) {
        provenance.title = value;
    } else if (field == kRationaleField) {
        provenance.rationale = value;
    }
}

} // namespace

std::string HumanContributionId(const std::string& author) {
    return std::format("human-{:08x}", StableHash(author));
}

bool WriteDraftProvenance(sacm_adapter::LibraryDocument& document,
                          const std::vector<std::string>& element_ids,
                          const DraftProvenance& provenance,
                          std::string& error) {
    const std::string& contribution = provenance.contribution_id;
    if (contribution.empty() || contribution.find('.') != std::string::npos) {
        error = std::format("\"{}\" cannot name a draft contribution: it must be non-empty and contain no '.'.",
                            contribution);
        return false;
    }
    for (const std::string& element_id : element_ids) {
        if (!WriteField(
                document, element_id, contribution, kSourceField, DraftSourceToString(provenance.source), error) ||
            !WriteField(document, element_id, contribution, kLabelField, provenance.label, error) ||
            !WriteField(document, element_id, contribution, kSessionField, provenance.session_id, error) ||
            !WriteField(document, element_id, contribution, kTitleField, provenance.title, error) ||
            !WriteField(document, element_id, contribution, kRationaleField, provenance.rationale, error)) {
            return false;
        }
    }
    return true;
}

std::vector<DraftContribution> ReadDraftProvenance(const sacm_adapter::LibraryDocument& document) {
    std::vector<DraftContribution> contributions;
    std::map<std::string, std::size_t> position_by_id;

    const std::string prefix(kDraftProvenanceTagPrefix);
    for (const sacm_adapter::ElementTaggedValue& tag : sacm_adapter::tagged_values_with_prefix(document, prefix)) {
        // `<contribution>.<field>`. The contribution cannot contain '.', so the
        // first one after the prefix is the separator.
        const std::string rest = tag.key.substr(prefix.size());
        const std::size_t separator = rest.find('.');
        if (separator == 0 || separator == std::string::npos || separator + 1 == rest.size())
            continue;
        const std::string contribution_id = rest.substr(0, separator);
        const std::string field = rest.substr(separator + 1);

        auto [found, inserted] = position_by_id.try_emplace(contribution_id, contributions.size());
        if (inserted) {
            DraftContribution contribution;
            contribution.provenance.contribution_id = contribution_id;
            contributions.push_back(std::move(contribution));
        }
        DraftContribution& contribution = contributions[found->second];
        AssignField(contribution.provenance, field, tag.value);
        contribution.element_ids.push_back(tag.element_id);
    }

    for (DraftContribution& contribution : contributions) {
        std::vector<std::string>& ids = contribution.element_ids;
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    }
    return contributions;
}

} // namespace core::drafts
