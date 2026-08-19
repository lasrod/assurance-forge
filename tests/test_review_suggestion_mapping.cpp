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

// The mapper reports refusals alongside groups; these tests are about the
// groups, so the helper keeps them reading that way. The refusal path has its
// own tests below.
std::vector<review::SuggestedDraftGroup> MapGroups(const parser::AssuranceCase& working,
                                                   const std::vector<review::ReviewSuggestion>& suggestions,
                                                   const review::ReviewSuggestionContext& context) {
    return review::MapSuggestionsToDraftGroups(working, suggestions, context).groups;
}

} // namespace

TEST(ReviewSuggestionMapping, MapsASuggestionWithoutAnApplicationOrAProvider) {
    const parser::AssuranceCase model = ModelWithClaim();
    const std::vector<review::SuggestedDraftGroup> groups =
        MapGroups(model,
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
    const std::vector<review::SuggestedDraftGroup> groups =
        MapGroups(model, {Suggestion("item-1", "G1", "A bounded claim")}, {"Claim review", "run-7"});

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
        MapGroups(model, {Suggestion("item-1", "G1", "A bounded claim")}, {});

    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].request.source_label, "SCCG AI Review");
}

// A claim's text is its content; everything else carries it in description.
// Staging a suggestion against the wrong field would write text a reviewer
// never sees while leaving the sentence they objected to in place.
TEST(ReviewSuggestionMapping, WritesAContextElementThroughItsDescription) {
    parser::AssuranceCase model;
    model.elements.push_back(Context("C1", "Operating conditions"));

    const std::vector<review::SuggestedDraftGroup> groups =
        MapGroups(model, {Suggestion("item-1", "C1", "Operating conditions: dry, daylight")}, {});

    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].operations[0].field, "description");
    EXPECT_EQ(groups[0].operations[0].old_value, "Operating conditions");
}

// A Term carries its value in `content`, and the review prompt shows the model
// whichever field is populated. Choosing the field by element type instead
// staged the suggestion into `description`, which added text nobody reviewed
// and left the value they objected to standing.
TEST(ReviewSuggestionMapping, WritesATermThroughTheFieldItsValueLivesIn) {
    parser::AssuranceCase model;
    parser::SacmElement term;
    term.id = "T1";
    term.type = "term";
    term.name = "T1";
    term.content = "safe";
    model.elements.push_back(term);

    const std::vector<review::SuggestedDraftGroup> groups =
        MapGroups(model, {Suggestion("item-1", "T1", "safe within the stated ODD")}, {});

    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].operations[0].type, core::reviews::PatchOperationType::UpdateElementText);
    EXPECT_EQ(groups[0].operations[0].field, "content");
    EXPECT_EQ(groups[0].operations[0].old_value, "safe");
}

// With neither content nor description, the review reads the name -- so that is
// what the suggestion replaces. Staging it as text would write an empty field
// the reviewer never saw and leave the name they objected to in place.
TEST(ReviewSuggestionMapping, ReplacesTheNameWhenTheNameIsWhatTheReviewRead) {
    parser::AssuranceCase model;
    parser::SacmElement solution;
    solution.id = "Sn1";
    solution.type = "artifactreference";
    solution.name = "Test report";
    model.elements.push_back(solution);

    const std::vector<review::SuggestedDraftGroup> groups =
        MapGroups(model, {Suggestion("item-1", "Sn1", "Test report TR-1 rev C")}, {});

    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].operations[0].type, core::reviews::PatchOperationType::UpdateElementName);
    EXPECT_EQ(groups[0].operations[0].old_value, "Test report");
    EXPECT_EQ(groups[0].operations[0].new_value, "Test report TR-1 rev C");
}

// The three silent drops. None is an error worth a message: each is a review
// agreeing with a change that already happened, or naming something gone.
TEST(ReviewSuggestionMapping, DropsEmptySuggestions) {
    const parser::AssuranceCase model = ModelWithClaim();
    EXPECT_TRUE(MapGroups(model, {Suggestion("item-1", "G1", "   ")}, {}).empty());
}

TEST(ReviewSuggestionMapping, DropsASuggestionForAnElementNoLongerInTheModel) {
    const parser::AssuranceCase model = ModelWithClaim();
    EXPECT_TRUE(MapGroups(model, {Suggestion("item-1", "G-gone", "New text")}, {}).empty());
}

TEST(ReviewSuggestionMapping, DropsASuggestionThatMatchesTheTextAlready) {
    const parser::AssuranceCase model = ModelWithClaim();
    EXPECT_TRUE(MapGroups(model, {Suggestion("item-1", "G1", "  The vehicle is safe  ")}, {}).empty());
}

// Each finding gets its own group, so a reviewer can accept one repair and
// reject another rather than being handed all of them as one decision.
TEST(ReviewSuggestionMapping, GivesEachFindingItsOwnGroup) {
    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "The vehicle is safe"));
    model.elements.push_back(Claim("G2", "Braking is effective"));

    const std::vector<review::SuggestedDraftGroup> groups = MapGroups(
        model,
        {Suggestion("item-1", "G1", "A bounded claim"), Suggestion("item-2", "G2", "A bounded braking claim")},
        {});

    ASSERT_EQ(groups.size(), 2u);
    EXPECT_EQ(groups[0].review_item_id, "item-1");
    EXPECT_EQ(groups[1].review_item_id, "item-2");
}

// --- Structural repairs (S5) ---------------------------------------------
//
// The case SCCG describes for AR.2: a goal decomposed with no reasoning step.
// The repair is to add a strategy and re-hang the sub-claims beneath it, which
// no amount of rewording the goal can express.

namespace {

core::reviews::PatchOperation Op(core::reviews::PatchOperationType type) {
    core::reviews::PatchOperation operation;
    operation.type = type;
    return operation;
}

review::ReviewSuggestion StructuralSuggestion(std::vector<core::reviews::PatchOperation> operations) {
    review::ReviewSuggestion suggestion = Suggestion("item-1", "G1", "");
    suggestion.title = "AR.2 decomposition with no reasoning step";
    suggestion.guideline_ids = {"AR.2"};
    suggestion.proposed_operations = std::move(operations);
    return suggestion;
}

parser::AssuranceCase DecomposedGoal() {
    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "Autonomy function safety is acceptable"));
    model.elements.push_back(Claim("G2", "Perception safety is acceptable"));
    model.elements.push_back(Claim("G3", "Planning safety is acceptable"));
    return model;
}

} // namespace

TEST(ReviewSuggestionMapping, StagesAStrategyAndItsAttachmentsAsOneGroup) {
    core::reviews::PatchOperation create = Op(core::reviews::PatchOperationType::CreateStrategy);
    create.create_ref = "$strategy";
    create.text = "Break the claim down by UL 4600 autonomy topic";

    core::reviews::PatchOperation attach = Op(core::reviews::PatchOperationType::AddSupportedBy);
    attach.source = core::reviews::ElementRef{std::nullopt, "$strategy"};
    attach.target = core::reviews::ElementRef{"G1", std::nullopt};

    core::reviews::PatchOperation rehang = Op(core::reviews::PatchOperationType::AddSupportedBy);
    rehang.source = core::reviews::ElementRef{"G2", std::nullopt};
    rehang.target = core::reviews::ElementRef{std::nullopt, "$strategy"};

    const review::SuggestionMappingResult mapped =
        review::MapSuggestionsToDraftGroups(DecomposedGoal(),
                                            {StructuralSuggestion({create, attach, rehang})},
                                            {"Claim review", "run-1", {"G1", "G2", "G3"}});

    EXPECT_TRUE(mapped.refusals.empty());
    ASSERT_EQ(mapped.groups.size(), 1u);
    // One group, not three: a reviewer accepts the repair or none of it. Half a
    // restructuring leaves an orphan strategy on the canvas.
    ASSERT_EQ(mapped.groups[0].operations.size(), 3u);
    EXPECT_EQ(mapped.groups[0].operations[0].type, core::reviews::PatchOperationType::CreateStrategy);
    EXPECT_EQ(mapped.groups[0].request.guideline_ids, std::vector<std::string>{"AR.2"});
    EXPECT_EQ(mapped.groups[0].request.source, core::drafts::DraftSource::SccgAiReview);
}

// The scope rule, which is SCCG's own: a profile's data packages say what the
// review was entitled to reason about, so they bound how far its repair reaches.
TEST(ReviewSuggestionMapping, RefusesAnOperationTouchingAnElementTheReviewNeverSaw) {
    core::reviews::PatchOperation reach = Op(core::reviews::PatchOperationType::AddSupportedBy);
    reach.source = core::reviews::ElementRef{"G2", std::nullopt};
    reach.target = core::reviews::ElementRef{"SOMEWHERE_ELSE", std::nullopt};

    const review::SuggestionMappingResult mapped = review::MapSuggestionsToDraftGroups(
        DecomposedGoal(), {StructuralSuggestion({reach})}, {"Claim review", "run-1", {"G1", "G2", "G3"}});

    EXPECT_TRUE(mapped.groups.empty());
    ASSERT_EQ(mapped.refusals.size(), 1u);
    EXPECT_NE(mapped.refusals[0].find("SOMEWHERE_ELSE"), std::string::npos);
}

// Refusing the whole repair rather than the reachable part of it: a
// CreateStrategy whose attachment was dropped is an orphan, which is worse than
// the finding the review raised.
TEST(ReviewSuggestionMapping, RefusesTheWholeRepairWhenOnePartReachesOutOfScope) {
    core::reviews::PatchOperation create = Op(core::reviews::PatchOperationType::CreateStrategy);
    create.create_ref = "$strategy";
    create.text = "Break the claim down by topic";

    core::reviews::PatchOperation reach = Op(core::reviews::PatchOperationType::AddSupportedBy);
    reach.source = core::reviews::ElementRef{std::nullopt, "$strategy"};
    reach.target = core::reviews::ElementRef{"UNSEEN", std::nullopt};

    const review::SuggestionMappingResult mapped = review::MapSuggestionsToDraftGroups(
        DecomposedGoal(), {StructuralSuggestion({create, reach})}, {"Claim review", "run-1", {"G1"}});

    EXPECT_TRUE(mapped.groups.empty());
    EXPECT_EQ(mapped.refusals.size(), 1u);
}

// A reference to something no operation creates would stage an attachment to
// nothing.
TEST(ReviewSuggestionMapping, RefusesAReferenceToARefNothingCreates) {
    core::reviews::PatchOperation attach = Op(core::reviews::PatchOperationType::AddSupportedBy);
    attach.source = core::reviews::ElementRef{std::nullopt, "$never_created"};
    attach.target = core::reviews::ElementRef{"G1", std::nullopt};

    const review::SuggestionMappingResult mapped = review::MapSuggestionsToDraftGroups(
        DecomposedGoal(), {StructuralSuggestion({attach})}, {"Claim review", "run-1", {"G1"}});

    EXPECT_TRUE(mapped.groups.empty());
    ASSERT_EQ(mapped.refusals.size(), 1u);
    EXPECT_NE(mapped.refusals[0].find("$never_created"), std::string::npos);
}

TEST(ReviewSuggestionMapping, RefusesACreateThatNothingCanAttachTo) {
    const review::SuggestionMappingResult mapped = review::MapSuggestionsToDraftGroups(
        DecomposedGoal(),
        {StructuralSuggestion({Op(core::reviews::PatchOperationType::CreateStrategy)})},
        {"Claim review", "run-1", {"G1"}});

    EXPECT_TRUE(mapped.groups.empty());
    EXPECT_EQ(mapped.refusals.size(), 1u);
}

// A structural repair is the finding's answer where it has one; the suggested
// text stays the shorthand for the case a reword fixes.
TEST(ReviewSuggestionMapping, PrefersTheStructuralRepairOverTheTextShorthand) {
    core::reviews::PatchOperation create = Op(core::reviews::PatchOperationType::CreateStrategy);
    create.create_ref = "$strategy";
    create.text = "Break the claim down by topic";

    review::ReviewSuggestion suggestion = StructuralSuggestion({create});
    suggestion.suggested_text = "A reworded goal";

    const review::SuggestionMappingResult mapped =
        review::MapSuggestionsToDraftGroups(DecomposedGoal(), {suggestion}, {"Claim review", "run-1", {"G1"}});

    ASSERT_EQ(mapped.groups.size(), 1u);
    ASSERT_EQ(mapped.groups[0].operations.size(), 1u);
    EXPECT_EQ(mapped.groups[0].operations[0].type, core::reviews::PatchOperationType::CreateStrategy);
}
