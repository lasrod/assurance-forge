#include "app/controllers/modal_coordinator.h"

#include <gtest/gtest.h>

TEST(ModalCoordinatorTest, AppliesGenericModalRequests) {
    app::controllers::ModalCoordinator coordinator;

    coordinator.ApplyModalRequest(app::ModalRequestEvent{app::ModalKind::NotImplemented, true, "Export"});
    EXPECT_TRUE(coordinator.show_not_implemented_modal);
    EXPECT_EQ(coordinator.not_implemented_feature, "Export");

    coordinator.ApplyModalRequest(app::ModalRequestEvent{app::ModalKind::Preferences, true});
    EXPECT_TRUE(coordinator.show_preferences_window);

    coordinator.ApplyModalRequest(app::ModalRequestEvent{app::ModalKind::Preferences, false});
    EXPECT_FALSE(coordinator.show_preferences_window);
}

TEST(ModalCoordinatorTest, ConsumeCloseRequestReturnsAndClearsRequest) {
    app::controllers::ModalCoordinator coordinator;

    coordinator.RequestClose();

    EXPECT_TRUE(coordinator.ConsumeCloseRequest());
    EXPECT_FALSE(coordinator.ConsumeCloseRequest());
}

TEST(ModalCoordinatorTest, CancelCloseDismissesExitModalAndClearsPendingRequest) {
    app::controllers::ModalCoordinator coordinator;

    coordinator.RequestClose();
    coordinator.show_save_before_exit_modal = true;

    coordinator.CancelClose();

    EXPECT_FALSE(coordinator.show_save_before_exit_modal);
    EXPECT_FALSE(coordinator.ConsumeCloseRequest());
}
