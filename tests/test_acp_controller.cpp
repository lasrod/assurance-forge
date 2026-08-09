// Phase 2 slice 2c-1: ACP record CRUD is now AUDITED. An ACP add through the
// controller routes through the command bus (library-primary), so the edit is a
// recorded audit transaction AND the library-owned document reflects it -- the
// controller no longer bypasses the audit path (the pre-slice behavior this test
// used to assert, an out-of-band library re-derive callback, is retired).

#include "app/app_runtime_state.h"
#include "app/controllers/acp_controller.h"

#include "core/audit/audit_store.h"
#include "core/audit/audit_transaction.h"
#include "core/commands/command_bus.h"
#include "core/derived_views.h"
#include "core/project_model.h"
#include "parser/xml_parser.h"
#include "legacy_sacm/sacm_model.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/library_load.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::filesystem::path repo_root() {
    return std::filesystem::path(AF_REPO_ROOT);
}

std::string ReadFileText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::filesystem::path MakeTempProjectRoot() {
    auto root = std::filesystem::temp_directory_path() /
                ("af_acpctl_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

// Wires an AppRuntimeState to an audited project loaded from the ACP fixture,
// mirroring how AppState::load_file retains the library document and how the app
// opens the command bus -- so a controller call takes the real DispatchAuditedCommand
// path.
void SetUpAuditedState(app::AppRuntimeState& state) {
    const std::filesystem::path fixture = repo_root() / "tests" / "data" / "fixture_acp_edit.sacm.xml";
    ASSERT_TRUE(std::filesystem::exists(fixture)) << fixture.string();

    const std::filesystem::path root = MakeTempProjectRoot();
    const std::filesystem::path sacm_rel = "argument.sacm";
    const std::filesystem::path sacm_abs = root / sacm_rel;
    {
        std::filesystem::create_directories(sacm_abs.parent_path());
        std::ofstream out(sacm_abs, std::ios::binary);
        const std::string text = ReadFileText(fixture);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    core::AssuranceProject project;
    project.id = "p";
    project.name = "Project";
    project.rootPath = root;
    core::ProjectFileEntry entry;
    entry.id = "f1";
    entry.relativePath = sacm_rel;
    entry.role = core::ProjectFileRole::SacmArgument;
    project.files.push_back(entry);

    core::audit::EnsureAuditStoreResult ensure;
    std::string error;
    ASSERT_TRUE(core::audit::EnsureAuditStore(project, sacm_rel, ensure, error)) << error;

    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(sacm_abs);
    ASSERT_TRUE(loaded.ok);
    ASSERT_NE(loaded.document, nullptr);

    parser::AssuranceCase model;
    sacm::AssuranceCasePackage package;
    core::RebuildDerivedViewsFromLibrary(*loaded.document, model, package);

    state.app_state.current_project = project;
    state.app_state.loaded_case = std::move(model);
    state.app_state.sacm_package = std::move(package);
    state.app_state.library_document = std::move(loaded.document);
    state.command_bus = core::commands::CommandBus::Open(project, sacm_abs, error);
    ASSERT_TRUE(state.command_bus) << error;
}

std::size_t LibraryAcpCount(const app::AppRuntimeState& state) {
    return sacm_adapter::project_case(*state.app_state.library_document).acps.size();
}

} // namespace

// SACM23-INT-001: an ACP add through the controller now flows through the audited
// command bus (library-primary), so it is recorded as an audit transaction AND the
// library-owned document reflects it -- it no longer bypasses the audit path.
TEST(AcpController, SACM23_INT_001_AuditedAcpAddIsLibraryPrimary) {
    app::AppRuntimeState state;
    SetUpAuditedState(state);
    if (::testing::Test::HasFatalFailure())
        return;
    ASSERT_EQ(LibraryAcpCount(state), 0u); // fixture carries no ACPs

    const std::size_t txns_before = state.command_bus->Store().Transactions().size();

    // S1 is an ArtifactReference -> eligible for an element ACP.
    const bool applied = state.acp_controller->AddElementAcp(state, "S1");
    ASSERT_TRUE(applied);

    // The edit is recorded as one AddAcp audit transaction ...
    const std::vector<core::audit::AuditTransaction> txns = state.command_bus->Store().Transactions();
    ASSERT_EQ(txns.size(), txns_before + 1);
    ASSERT_EQ(txns.back().events.size(), 1u);
    EXPECT_EQ(txns.back().events.front().event_type, "AddAcp");

    // ... and the library-owned document carries the ACP (the library-primary flip
    // mutated the library, not just the legacy views).
    EXPECT_EQ(LibraryAcpCount(state), 1u);
}
