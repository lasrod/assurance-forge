#pragma once

// Turning validated review findings into the draft groups a reviewer will see.
//
// This is the last stage of the review method, and it belongs here rather than
// in `app` for the reason ADR 0013 gives: built-in review and review executed
// by an external client must produce the same groups, from the same code, with
// the same constraints. A mapper living in `app` is reachable by exactly one of
// those two callers, which is how the two paths drift.
//
// What this does **not** do is touch a workspace. It returns what should be
// staged; beginning a group, staging it, marking it ready and linking it back
// to its finding stay with the caller, because those are writes to application
// state and the review method has no business performing them.

#include "core/drafts/draft_workspace_store.h"
#include "core/reviews/review_proposal.h"
#include "parser/model_utils.h"
#include "parser/xml_parser.h"

#include <string>
#include <vector>

namespace review {

// One finding's suggested repair, with the provenance the resulting draft group
// has to carry. Deliberately not `core::reviews::ReviewItem`: the review item is
// application state that also records status, authorship and links, none of
// which the mapping needs, and depending on it would make this callable only
// from where that store lives.
struct ReviewSuggestion {
    std::string review_item_id;
    std::string element_id;
    std::string suggested_text;
    // Carried onto the group so a reviewer reading the draft sees the finding
    // that asked for it without following a link back.
    std::string title;
    std::string message;
    std::vector<std::string> guideline_ids;
    // The structural repair the finding asks for, when SCCG's answer is to add
    // or re-attach an element rather than reword one. Takes precedence over
    // `suggested_text`, which stays the shorthand for the text-only case.
    std::vector<core::reviews::PatchOperation> proposed_operations;
};

// Which review run these suggestions came from, and what it was allowed to see.
struct ReviewSuggestionContext {
    std::string review_profile_name;
    std::string review_run_id;
    // The elements the review read -- `review::ReviewedElementIds`. A proposal
    // may touch these and elements it creates itself, and nothing else.
    //
    // The bound is SCCG's own: a profile's required data packages say what a
    // review of this element is entitled to reason about, so they also say how
    // far its repair may reach. A review shown one claim and its children has no
    // business rewiring a branch it never saw, and a reviewer should not have to
    // check whether it did.
    std::vector<std::string> reviewed_element_ids;
};

// A group the review is asking for, and the operations that make it. The caller
// stages `operations` under `request` and links the result back to
// `review_item_id`.
struct SuggestedDraftGroup {
    core::drafts::DraftGroupRequest request;
    std::vector<core::reviews::PatchOperation> operations;
    std::string review_item_id;
};

// Which field of an element carries the text a review reads, and therefore the
// field a suggested replacement belongs in.
//
// This is one function because the review request builder and the mapper have
// to agree, and when they were two they disagreed: the request showed the model
// whichever of content/description was populated (falling back to the name),
// while the mapper chose by element type. A Term or Expression carries its
// value in `content`, so a suggestion the model made against the text it was
// shown was staged into `description` -- adding text nobody reviewed and
// leaving the sentence they objected to untouched.
//
// `field` is "content", "description" or "name", in that order of preference.
struct ElementTextTarget {
    std::string field;
    std::string current_text;
};

ElementTextTarget TextTargetFor(const parser::SacmElement& element);

// Maps suggestions against `working` -- the model the review actually read, not
// the accepted document, because a suggestion is a response to what the
// reviewer saw.
//
// Silently drops a suggestion whose text is empty, whose element is no longer
// in the model, or whose text already matches what the element says. None of
// those is an error worth telling a user about: they are a review agreeing with
// a change that already happened.
// Groups that should be staged, and what was refused on the way. Refusals are
// carried rather than dropped: a finding whose repair silently vanished reads
// to a reviewer as a finding nobody could fix.
struct SuggestionMappingResult {
    std::vector<SuggestedDraftGroup> groups;
    std::vector<std::string> refusals;
};

SuggestionMappingResult MapSuggestionsToDraftGroups(const parser::AssuranceCase& working,
                                                    const std::vector<ReviewSuggestion>& suggestions,
                                                    const ReviewSuggestionContext& context);

} // namespace review
