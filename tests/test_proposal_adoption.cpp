#include "app/proposal_adoption.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

// A proposal file dropped in by another process is invisible until this
// application claims it: the Review panel walks review items and renders a
// proposal only for those carrying a `proposal_id`.
//
// Adoption exists because the alternative -- letting the other process write the
// review item itself -- made two processes writers of the same file, and the
// application's next whole-file save reverted the other one's addition. Reported
// from real use: a proposal landed on disk and never appeared.

namespace {

core::reviews::ReviewProposal MakeProposal(const std::string& id, const std::string& anchor) {
    core::reviews::ReviewProposal proposal;
    proposal.id                = id;
    proposal.title             = "Decompose the top goal";
    proposal.summary           = "Adds a supporting sub-goal.";
    proposal.author_name       = "MCP: claude-ai 0.1.0";
    proposal.created_utc       = "2026-07-27T12:00:00Z";
    proposal.anchor_element_id = anchor;
    return proposal;
}

core::reviews::ReviewItem LinkedItem(const std::string& proposal_id) {
    core::reviews::ReviewItem item;
    item.id          = "external-proposal:" + proposal_id;
    item.proposal_id = proposal_id;
    return item;
}

} // namespace

TEST(ProposalAdoption, ClaimsAProposalWithNoReviewItem) {
    const std::vector<core::reviews::ReviewProposal> proposals = {MakeProposal("p1", "G1")};

    const std::vector<app::ProposalAdoption> adoptions = app::PlanProposalAdoption(proposals, {});

    ASSERT_EQ(adoptions.size(), 1u);
    EXPECT_EQ(adoptions[0].proposal_id, "p1");
    // The item is what makes it visible and applyable, so the link is the point.
    ASSERT_TRUE(adoptions[0].item.proposal_id.has_value());
    EXPECT_EQ(adoptions[0].item.proposal_id.value(), "p1");
    EXPECT_EQ(adoptions[0].item.element_id, "G1");
    EXPECT_EQ(adoptions[0].item.title, "Decompose the top goal");
    EXPECT_EQ(adoptions[0].item.reviewer_name, "MCP: claude-ai 0.1.0");
    EXPECT_EQ(adoptions[0].item.status, core::reviews::ReviewItemStatus::Open);
}

// A proposal this application created already has an item. Claiming it again
// would put a duplicate comment in front of the user every poll.
TEST(ProposalAdoption, LeavesAnAlreadyLinkedProposalAlone) {
    const std::vector<core::reviews::ReviewProposal> proposals = {MakeProposal("p1", "G1")};

    EXPECT_TRUE(app::PlanProposalAdoption(proposals, {LinkedItem("p1")}).empty());
}

// The poll runs every frame; a second pass over the same file must produce the
// same item id so it updates in place rather than accumulating.
TEST(ProposalAdoption, DerivesAStableItemIdFromTheProposal) {
    const std::vector<core::reviews::ReviewProposal> proposals = {MakeProposal("p1", "G1")};

    const auto first  = app::PlanProposalAdoption(proposals, {});
    const auto second = app::PlanProposalAdoption(proposals, {});

    ASSERT_EQ(first.size(), 1u);
    ASSERT_EQ(second.size(), 1u);
    EXPECT_EQ(first[0].item.id, second[0].item.id);
}

TEST(ProposalAdoption, ClaimsOnlyTheUnlinkedOnesInAMixedSet) {
    const std::vector<core::reviews::ReviewProposal> proposals = {
        MakeProposal("linked", "G1"), MakeProposal("fresh", "G2")};

    const std::vector<app::ProposalAdoption> adoptions =
        app::PlanProposalAdoption(proposals, {LinkedItem("linked")});

    ASSERT_EQ(adoptions.size(), 1u);
    EXPECT_EQ(adoptions[0].proposal_id, "fresh");
}

// Falls back rather than showing the user a blank comment from a draft that
// happens to carry no title or author.
TEST(ProposalAdoption, FallsBackWhenTheProposalNamesNoTitleOrAuthor) {
    core::reviews::ReviewProposal bare;
    bare.id = "p1";

    const std::vector<app::ProposalAdoption> adoptions = app::PlanProposalAdoption({bare}, {});

    ASSERT_EQ(adoptions.size(), 1u);
    EXPECT_FALSE(adoptions[0].item.title.empty());
    EXPECT_FALSE(adoptions[0].item.reviewer_name.empty());
}

TEST(ProposalAdoption, IgnoresAProposalWithNoId) {
    core::reviews::ReviewProposal anonymous;
    anonymous.title = "No id";

    EXPECT_TRUE(app::PlanProposalAdoption({anonymous}, {}).empty());
}
