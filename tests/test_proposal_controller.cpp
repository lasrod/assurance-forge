#include <gtest/gtest.h>

#include "app/controllers/proposal_controller.h"

namespace {

parser::AssuranceCase MakeCaseWithClaim() {
    parser::AssuranceCase model;
    parser::SacmElement claim;
    claim.id = "claim-1";
    claim.type = "claim";
    claim.name = "Claim";
    claim.description = "Claim text";
    model.elements.push_back(claim);
    return model;
}

core::reviews::ReviewItem MakeReviewItem() {
    core::reviews::ReviewItem item;
    item.id = "review-1";
    item.element_id = "claim-1";
    item.title = "Needs work";
    item.message = "The claim needs clearer evidence.";
    item.status = core::reviews::ReviewItemStatus::Open;
    return item;
}

}  // namespace

TEST(ProposalControllerTest, BeginDraftInitializesDraftAndCreatorState) {
    app::controllers::ProposalController controller;
    parser::AssuranceCase model = MakeCaseWithClaim();
    core::reviews::ReviewItem item = MakeReviewItem();

    controller.BeginDraft(item, model, model.elements.front(), "Jess");

    EXPECT_TRUE(controller.creator_active);
    EXPECT_FALSE(controller.preview_active);
    EXPECT_EQ(controller.draft.review_item_id, "review-1");
    EXPECT_EQ(controller.draft.anchor_element_id, "claim-1");
    EXPECT_EQ(controller.draft.author_name, "Jess");
    EXPECT_TRUE(controller.HasActiveDraftForItem("review-1"));
    EXPECT_FALSE(controller.CanSaveActiveDraft());
}

TEST(ProposalControllerTest, ClearActiveStateResetsDraftPreviewAndPendingRefresh) {
    app::controllers::ProposalController controller;
    parser::AssuranceCase model = MakeCaseWithClaim();
    controller.BeginDraft(MakeReviewItem(), model, model.elements.front(), "");
    controller.preview_active = true;
    controller.preview_id = "proposal-1";
    controller.preview_model = model;
    controller.creator_preview_refresh_pending = true;
    controller.creator_pending_select_create_ref = "$new_claim_1";
    controller.creator_pending_clear_selection = true;

    controller.ClearActiveState();

    EXPECT_FALSE(controller.creator_active);
    EXPECT_FALSE(controller.preview_active);
    EXPECT_TRUE(controller.preview_id.empty());
    EXPECT_TRUE(controller.preview_model.elements.empty());
    EXPECT_TRUE(controller.draft.id.empty());
    EXPECT_FALSE(controller.creator_preview_refresh_pending);
    EXPECT_FALSE(controller.creator_pending_select_create_ref.has_value());
    EXPECT_FALSE(controller.creator_pending_clear_selection);
}
