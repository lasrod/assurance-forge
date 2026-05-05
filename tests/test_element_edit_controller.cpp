#include "app/controllers/element_edit_controller.h"

#include <gtest/gtest.h>

TEST(ElementEditControllerTest, AddTopGoalUpdatesModelAndEmitsEvents) {
    app::AppEvents events;
    app::controllers::ElementEditController controller(events);
    parser::AssuranceCase model;

    std::string selected_id;
    bool tree_dirty = false;
    bool document_dirty = false;
    std::string status;
    events.Subscribe<app::TreeDirtyEvent>([&](const app::TreeDirtyEvent&) { tree_dirty = true; });
    events.Subscribe<app::DocumentDirtyEvent>([&](const app::DocumentDirtyEvent&) { document_dirty = true; });
    events.Subscribe<app::SelectionChangedEvent>(
        [&](const app::SelectionChangedEvent& event) { selected_id = event.element_id; });
    events.Subscribe<app::StatusMessageEvent>([&](const app::StatusMessageEvent& event) { status = event.message; });

    ASSERT_TRUE(controller.AddTopGoal(model, nullptr));

    ASSERT_EQ(model.elements.size(), 1u);
    EXPECT_EQ(model.elements.front().type, "claim");
    EXPECT_EQ(selected_id, model.elements.front().id);
    EXPECT_TRUE(tree_dirty);
    EXPECT_TRUE(document_dirty);
    EXPECT_EQ(status, "Added " + model.elements.front().id);
}

TEST(ElementEditControllerTest, AddChildWithoutSelectionEmitsStatusOnly) {
    app::AppEvents events;
    app::controllers::ElementEditController controller(events);
    parser::AssuranceCase model;
    bool tree_dirty = false;
    std::string status;
    events.Subscribe<app::TreeDirtyEvent>([&](const app::TreeDirtyEvent&) { tree_dirty = true; });
    events.Subscribe<app::StatusMessageEvent>([&](const app::StatusMessageEvent& event) { status = event.message; });

    EXPECT_FALSE(controller.AddChildToSelected(model, nullptr, "", core::NewElementKind::Goal));

    EXPECT_TRUE(model.elements.empty());
    EXPECT_FALSE(tree_dirty);
    EXPECT_EQ(status, "No element selected.");
}
