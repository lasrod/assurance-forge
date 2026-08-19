#include "review/sccg/suggestion_mapping.h"

#include "core/string_utils.h"

#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace review {

namespace {

// A reference is in bounds if it names an element the review read, or one this
// same finding creates. Anything else is a reach outside the review's evidence.
bool RefIsInScope(const std::optional<core::reviews::ElementRef>& ref,
                  const std::set<std::string>& reviewed,
                  const std::set<std::string>& created_refs,
                  std::string& out_offender) {
    if (!ref.has_value())
        return true;
    if (ref->create_ref.has_value()) {
        if (created_refs.count(ref->create_ref.value()) > 0)
            return true;
        out_offender = "\"" + ref->create_ref.value() + "\", which no operation in this finding creates";
        return false;
    }
    if (!ref->existing_id.has_value())
        return true;
    if (reviewed.count(ref->existing_id.value()) > 0)
        return true;
    out_offender = ref->existing_id.value() + ", which this review was not shown";
    return false;
}

// Refuses the whole finding's repair rather than part of it. Half a
// restructuring is worse than none: a CreateStrategy whose attachment was
// dropped leaves an orphan on the reviewer's canvas.
bool OperationsAreInScope(const std::vector<core::reviews::PatchOperation>& operations,
                          const std::vector<std::string>& reviewed_element_ids,
                          std::string& out_reason) {
    const std::set<std::string> reviewed(reviewed_element_ids.begin(), reviewed_element_ids.end());
    std::set<std::string> created_refs;
    for (const core::reviews::PatchOperation& operation : operations) {
        if (core::reviews::IsCreateOperation(operation.type) && operation.create_ref.has_value())
            created_refs.insert(operation.create_ref.value());
    }

    for (const core::reviews::PatchOperation& operation : operations) {
        if (core::reviews::IsCreateOperation(operation.type) && !operation.create_ref.has_value()) {
            out_reason = "a create operation names no create_ref, so nothing can attach to it";
            return false;
        }
        std::string offender;
        if (!RefIsInScope(operation.element, reviewed, created_refs, offender) ||
            !RefIsInScope(operation.source, reviewed, created_refs, offender) ||
            !RefIsInScope(operation.target, reviewed, created_refs, offender)) {
            out_reason = "it touches " + offender;
            return false;
        }
    }
    return true;
}

} // namespace

ElementTextTarget TextTargetFor(const parser::SacmElement& element) {
    if (!element.content.empty())
        return {"content", element.content};
    if (!element.description.empty())
        return {"description", element.description};
    return {"name", element.name};
}

SuggestionMappingResult MapSuggestionsToDraftGroups(const parser::AssuranceCase& working,
                                                    const std::vector<ReviewSuggestion>& suggestions,
                                                    const ReviewSuggestionContext& context) {
    SuggestionMappingResult result;

    for (const ReviewSuggestion& suggestion : suggestions) {
        const parser::SacmElement* anchor = parser::FindElementByIdOrGidValue(working, suggestion.element_id);
        if (!anchor)
            continue;

        SuggestedDraftGroup group;
        group.review_item_id = suggestion.review_item_id;
        group.request.title = suggestion.title.empty() ? "AI suggested change" : suggestion.title;
        group.request.rationale = suggestion.message;
        group.request.source = core::drafts::DraftSource::SccgAiReview;
        group.request.source_label =
            context.review_profile_name.empty() ? "SCCG AI Review" : context.review_profile_name;
        group.request.source_session_id = context.review_run_id;
        group.request.guideline_ids = suggestion.guideline_ids;
        group.request.review_item_ids = {suggestion.review_item_id};

        // A structural repair is the finding's answer where it has one; the
        // suggested text is the shorthand for the case a reword fixes.
        if (!suggestion.proposed_operations.empty()) {
            std::string reason;
            if (!OperationsAreInScope(suggestion.proposed_operations, context.reviewed_element_ids, reason)) {
                result.refusals.push_back("A suggested change for " + anchor->id + " was refused because " + reason +
                                          ".");
                continue;
            }
            group.request.summary = "AI proposed a structural change around " + anchor->id + ".";
            group.operations = suggestion.proposed_operations;
            result.groups.push_back(std::move(group));
            continue;
        }

        const std::string suggested_text = core::TrimWhitespace(suggestion.suggested_text);
        if (suggested_text.empty())
            continue;

        const ElementTextTarget text_target = TextTargetFor(*anchor);
        if (core::TrimWhitespace(text_target.current_text) == suggested_text)
            continue;

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

        group.request.summary = "AI suggested replacement text for " + anchor->id + ".";
        group.operations.push_back(std::move(operation));
        result.groups.push_back(std::move(group));
    }

    return result;
}

} // namespace review
