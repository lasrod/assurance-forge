#include "review/sccg/suggestion_mapping.h"

#include "core/string_utils.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace review {

ElementTextTarget TextTargetFor(const parser::SacmElement& element) {
    if (!element.content.empty())
        return {"content", element.content};
    if (!element.description.empty())
        return {"description", element.description};
    return {"name", element.name};
}

std::vector<SuggestedDraftGroup> MapSuggestionsToDraftGroups(const parser::AssuranceCase& working,
                                                             const std::vector<ReviewSuggestion>& suggestions,
                                                             const ReviewSuggestionContext& context) {
    std::vector<SuggestedDraftGroup> groups;

    for (const ReviewSuggestion& suggestion : suggestions) {
        const std::string suggested_text = core::TrimWhitespace(suggestion.suggested_text);
        if (suggested_text.empty())
            continue;

        const parser::SacmElement* anchor = parser::FindElementByIdOrGidValue(working, suggestion.element_id);
        if (!anchor)
            continue;

        const ElementTextTarget text_target = TextTargetFor(*anchor);
        if (core::TrimWhitespace(text_target.current_text) == suggested_text)
            continue;

        SuggestedDraftGroup group;
        group.review_item_id = suggestion.review_item_id;
        group.request.title = suggestion.title.empty() ? "AI suggested change" : suggestion.title;
        group.request.summary = "AI suggested replacement text for " + anchor->id + ".";
        group.request.rationale = suggestion.message;
        group.request.source = core::drafts::DraftSource::SccgAiReview;
        group.request.source_label =
            context.review_profile_name.empty() ? "SCCG AI Review" : context.review_profile_name;
        group.request.source_session_id = context.review_run_id;
        group.request.guideline_ids = suggestion.guideline_ids;
        group.request.review_item_ids = {suggestion.review_item_id};

        // An element with neither content nor description is read by its name,
        // so that is what the suggestion replaces. Staging it as text would
        // write into an empty field the reviewer never saw and leave the name
        // they objected to standing.
        const bool replaces_the_name = text_target.field == "name";

        core::reviews::PatchOperation operation;
        operation.type = replaces_the_name ? core::reviews::PatchOperationType::UpdateElementName
                                           : core::reviews::PatchOperationType::UpdateElementText;
        operation.element = core::reviews::ElementRef{anchor->id, std::nullopt};
        if (!replaces_the_name)
            operation.field = text_target.field;
        operation.old_value = text_target.current_text;
        operation.new_value = suggested_text;
        group.operations.push_back(std::move(operation));

        groups.push_back(std::move(group));
    }

    return groups;
}

} // namespace review
