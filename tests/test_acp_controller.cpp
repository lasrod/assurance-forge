// Phase 9 Stage 5: ACP edits bypass the command bus (they are unaudited), so
// the bus re-derive net does not cover them. AcpController is given a post-edit
// callback that re-derives the library-owned document; this test proves that
// after an ACP add through the controller, the library document reflects it
// rather than drifting stale.

#include "app/controllers/acp_controller.h"

#include "app/app_events.h"
#include "core/app_state.h"
#include "core/problems/problems_manager.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_parser.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/library_load.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

std::filesystem::path repo_root() { return std::filesystem::path(AF_REPO_ROOT); }

// Populates an AppState from a fixture the way a load would: legacy models plus
// the library-owned document.
core::AppState LoadState(const std::filesystem::path& path) {
    core::AppState state;
    auto legacy = parser::parse_sacm_xml(path.string());
    EXPECT_TRUE(legacy.has_value()) << (legacy.has_value() ? "" : legacy.error());
    state.loaded_case = std::move(legacy.value());
    auto package = sacm::parse_sacm(path.string());
    EXPECT_TRUE(package.has_value()) << (package.has_value() ? "" : package.error());
    state.sacm_package = std::move(package.value());
    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(path);
    EXPECT_TRUE(loaded.ok);
    state.library_document = std::move(loaded.document);
    return state;
}

std::size_t LibraryAcpCount(const core::AppState& state) {
    return sacm_adapter::project_case(*state.library_document).acps.size();
}

} // namespace

// SACM23-INT-001: an ACP add through the (unaudited) controller re-derives the
// library document via the post-edit callback, closing the gap the command-bus
// re-derive net cannot reach.
TEST(AcpController, SACM23_INT_001_UnauditedAcpAddRederivesLibraryDocument) {
    const std::filesystem::path path = repo_root() / "tests" / "data" / "fixture_acp_edit.sacm.xml";
    ASSERT_TRUE(std::filesystem::exists(path)) << path.string();

    core::AppState state = LoadState(path);
    ASSERT_NE(state.library_document, nullptr);
    ASSERT_EQ(LibraryAcpCount(state), 0u);  // fixture carries no ACPs

    app::AppEvents events;
    core::ProblemsManager problems;
    app::controllers::AcpController controller(events, problems,
                                               [&state]() { state.sync_library_document(); });

    // S1 is an ArtifactReference -> eligible for an element ACP.
    const bool applied = controller.AddElementAcp(state.loaded_case.value(),
                                                  &state.sacm_package.value(), "S1");
    ASSERT_TRUE(applied);

    // The library document was re-derived from the authoritative package and now
    // carries the ACP -- it did not drift stale.
    EXPECT_EQ(LibraryAcpCount(state), 1u);
}
