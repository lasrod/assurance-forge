#include "app/app_events.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>

TEST(AppEventsTest, DispatchesOnlyMatchingListeners) {
    app::AppEvents events;
    std::vector<std::string> received;

    events.Subscribe<app::StatusMessageEvent>(
        [&](const app::StatusMessageEvent& event) { received.push_back("status:" + event.message); });
    events.Subscribe<app::TreeDirtyEvent>([&](const app::TreeDirtyEvent&) { received.push_back("tree"); });

    events.Emit(app::StatusMessageEvent{"Ready"});

    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0], "status:Ready");
}

TEST(AppEventsTest, DispatchesMatchingListenersInSubscriptionOrder) {
    app::AppEvents events;
    std::vector<int> order;

    events.Subscribe<app::DocumentDirtyEvent>([&](const app::DocumentDirtyEvent&) { order.push_back(1); });
    events.Subscribe<app::DocumentDirtyEvent>([&](const app::DocumentDirtyEvent&) { order.push_back(2); });

    events.Emit(app::DocumentDirtyEvent{});

    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
}

TEST(AppEventsTest, UnsubscribeRemovesListener) {
    app::AppEvents events;
    int count = 0;

    const app::AppEvents::SubscriptionId id =
        events.Subscribe<app::ReviewItemsDirtyEvent>([&](const app::ReviewItemsDirtyEvent&) { ++count; });

    events.Emit(app::ReviewItemsDirtyEvent{});
    events.Unsubscribe(id);
    events.Emit(app::ReviewItemsDirtyEvent{});

    EXPECT_EQ(count, 1);
}

TEST(AppEventsTest, EmitsSafelyWithoutListeners) {
    app::AppEvents events;

    EXPECT_NO_THROW(events.Emit(app::ModalRequestEvent{app::ModalKind::NotImplemented, true, "Feature"}));
}

TEST(AppEventsTest, DispatchesElementReviewVisualEvents) {
    app::AppEvents events;
    std::vector<app::ElementReviewVisualEvent> received;

    events.Subscribe<app::ElementReviewVisualEvent>(
        [&](const app::ElementReviewVisualEvent& event) { received.push_back(event); });

    events.Emit(app::ElementReviewVisualEvent{
        app::ElementReviewVisualEventKind::AiNoFindings, "claim-1", "profile-1", "Profile 1", "No findings."});

    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].kind, app::ElementReviewVisualEventKind::AiNoFindings);
    EXPECT_EQ(received[0].element_id, "claim-1");
    EXPECT_EQ(received[0].review_profile_id, "profile-1");
    EXPECT_EQ(received[0].review_profile_name, "Profile 1");
}
