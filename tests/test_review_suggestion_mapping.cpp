#include "review/sccg/suggestion_mapping.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

// The mapping from a validated finding to the draft group a reviewer sees.
//
// These tests include no `app` header and construct no workspace, controller or
// provider, which is the point of the phase that moved this code: ADR 0013 says
// built-in review and review executed by an external client must produce the
// same groups from the same code, and code reachable only from `app` is
// reachable by exactly one of them.

namespace {

parser::SacmElement Claim(const std::string& id, const std::string& content) {
    parser::SacmElement element;
    element.id = id;
    element.type = "claim";
    element.name = id;
    element.content = content;
    return element;
}

parser::SacmElement Context(const std::string& id, const std::string& description) {
    parser::SacmElement element;
    element.id = id;
    element.type = "artifactreference";
    element.name = id;
    element.description = description;
    return element;
}

review::ReviewSuggestion
Suggestion(const std::string& item_id, const std::string& element_id, const std::string& text) {
    review::ReviewSuggestion suggestion;
    suggestion.review_item_id = item_id;
    suggestion.element_id = element_id;
    suggestion.suggested_text = text;
    suggestion.title = "CL.5 unbounded qualifier";
    suggestion.message = "\"safe\" is not bounded here.";
    suggestion.guideline_ids = {"CL.5"};
    return suggestion;
}

parser::AssuranceCase ModelWithClaim() {
    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "The vehicle is safe"));
    return model;
}

} // namespace

TEST(ReviewSuggestionMapping, MapsASuggestionWithoutAnApplicationOrAProvider) {
    const parser::AssuranceCase model = ModelWithClaim();
    const std::vector<review::SuggestedDraftGroup> groups = review::MapSuggestionsToDraftGroups(
        model,
        {Suggestion("item-1", "G1", "The vehicle is safe against collision hazards in its ODD")},
        {"Claim review", "run-7"});

    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].review_item_id, "item-1");
    ASSERT_EQ(groups[0].operations.size(), 1u);
    EXPECT_EQ(groups[0].operations[0].type, core::reviews::PatchOperationType::UpdateElementText);
    EXPECT_EQ(groups[0].operations[0].field, "content");
    EXPECT_EQ(groups[0].operations[0].old_value, "The vehicle is safe");
    EXPECT_EQ(groups[0].operations[0].new_value, "The vehicle is safe against collision hazards in its ODD");
}

// Provenance is what lets a reviewer take a staged change back to the finding
// and the guideline that asked for it, rather than trusting the tool.
TEST(ReviewSuggestionMapping, CarriesTheReviewProvenanceOntoTheGroup) {
    const parser::AssuranceCase model = ModelWithClaim();
    const std::vector<review::SuggestedDraftGroup> groups = review::MapSuggestionsToDraftGroups(
        model, {Suggestion("item-1", "G1", "A bounded claim")}, {"Claim review", "run-7"});

    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].request.source, core::drafts::DraftSource::SccgAiReview);
    EXPECT_EQ(groups[0].request.source_label, "Claim review");
    EXPECT_EQ(groups[0].request.source_session_id, "run-7");
    EXPECT_EQ(groups[0].request.title, "CL.5 unbounded qualifier");
    EXPECT_EQ(groups[0].request.guideline_ids, std::vector<std::string>{"CL.5"});
    EXPECT_EQ(groups[0].request.review_item_ids, std::vector<std::string>{"item-1"});
}

// An unnamed review still has to produce a group a reviewer can attribute.
TEST(ReviewSuggestionMapping, FallsBackToASourceLabelWhenTheProfileIsUnnamed) {
    const parser::AssuranceCase model = ModelWithClaim();
    const std::vector<review::SuggestedDraftGroup> groups =
        review::MapSuggestionsToDraftGroups(model, {Suggestion("item-1", "G1", "A bounded claim")}, {});

    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].request.source_label, "SCCG AI Review");
}

// A claim's text is its content; everything else carries it in description.
// Staging a suggestion against the wrong field would write text a reviewer
// never sees while leaving the sentence they objected to in place.
TEST(ReviewSuggestionMapping, WritesAContextElementThroughItsDescription) {
    parser::AssuranceCase model;
    model.elements.push_back(Context("C1", "Operating conditions"));

    const std::vector<review::SuggestedDraftGroup> groups = review::MapSuggestionsToDraftGroups(
        model, {Suggestion("item-1", "C1", "Operating conditions: dry, daylight")}, {});

    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].operations[0].field, "description");
    EXPECT_EQ(groups[0].operations[0].old_value, "Operating conditions");
}

// The three silent drops. None is an error worth a message: each is a review
// agreeing with a change that already happened, or naming something gone.
TEST(ReviewSuggestionMapping, DropsEmptySuggestions) {
    const parser::AssuranceCase model = ModelWithClaim();
    EXPECT_TRUE(review::MapSuggestionsToDraftGroups(model, {Suggestion("item-1", "G1", "   ")}, {}).empty());
}

TEST(ReviewSuggestionMapping, DropsASuggestionForAnElementNoLongerInTheModel) {
    const parser::AssuranceCase model = ModelWithClaim();
    EXPECT_TRUE(review::MapSuggestionsToDraftGroups(model, {Suggestion("item-1", "G-gone", "New text")}, {}).empty());
}

TEST(ReviewSuggestionMapping, DropsASuggestionThatMatchesTheTextAlready) {
    const parser::AssuranceCase model = ModelWithClaim();
    EXPECT_TRUE(review::MapSuggestionsToDraftGroups(model, {Suggestion("item-1", "G1", "  The vehicle is safe  ")}, {})
                    .empty());
}

// Each finding gets its own group, so a reviewer can accept one repair and
// reject another rather than being handed all of them as one decision.
TEST(ReviewSuggestionMapping, GivesEachFindingItsOwnGroup) {
    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "The vehicle is safe"));
    model.elements.push_back(Claim("G2", "Braking is effective"));

    const std::vector<review::SuggestedDraftGroup> groups = review::MapSuggestionsToDraftGroups(
        model,
        {Suggestion("item-1", "G1", "A bounded claim"), Suggestion("item-2", "G2", "A bounded braking claim")},
        {});

    ASSERT_EQ(groups.size(), 2u);
    EXPECT_EQ(groups[0].review_item_id, "item-1");
    EXPECT_EQ(groups[1].review_item_id, "item-2");
}
