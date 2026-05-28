#include "core/audit/audit_paths.h"
#include "core/audit/audit_store.h"
#include "core/audit/canonical_model_hash.h"
#include "core/audit/event_replayer.h"
#include "core/audit/history_reconstruction.h"
#include "core/commands/command_bus.h"
#include "core/commands/element_commands.h"
#include "core/element_factory.h"
#include "core/project_model.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_parser.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <string_view>

namespace {

constexpr const char* kSampleSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";

std::filesystem::path MakeTempProjectRoot(const std::string& tag) {
    auto root = std::filesystem::temp_directory_path() /
                ("af_history_" + tag + "_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

void WriteFile(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

struct ProjectFixture {
    core::AssuranceProject     project;
    std::filesystem::path      sacm_abs;
    sacm::AssuranceCasePackage package;
    parser::AssuranceCase      model;
};

ProjectFixture MakeFixture(const std::string& tag) {
    ProjectFixture f;
    const auto root = MakeTempProjectRoot(tag);
    const std::filesystem::path sacm_rel = "argument.sacm";
    WriteFile(root / sacm_rel, kSampleSacm);

    f.project.id = "p";
    f.project.name = "Project";
    f.project.rootPath = root;
    core::ProjectFileEntry entry;
    entry.id = "f1";
    entry.relativePath = sacm_rel;
    entry.role = core::ProjectFileRole::SacmArgument;
    f.project.files.push_back(entry);

    core::audit::EnsureAuditStoreResult ensure;
    std::string error;
    EXPECT_TRUE(core::audit::EnsureAuditStore(f.project, sacm_rel, ensure, error)) << error;

    f.sacm_abs = f.project.rootPath / sacm_rel;
    auto pkg = sacm::parse_sacm(f.sacm_abs.string());
    EXPECT_TRUE(pkg.has_value());
    f.package = std::move(pkg.value());
    auto parsed = parser::parse_sacm_xml_string(kSampleSacm);
    EXPECT_TRUE(parsed.has_value());
    f.model = std::move(parsed.value());
    return f;
}

} // namespace

TEST(HistoryReconstruction, ReturnsSnapshotStateAtSequenceZero) {
    auto f = MakeFixture("seq_zero");

    auto state = core::audit::ReconstructAtSequence(f.project, 0);
    ASSERT_TRUE(state.has_value()) << (state.has_value() ? "" : state.error());

    // Snapshot is the empty initial SACM: only G1 should be present.
    bool found_g1 = false;
    for (const auto& e : state->model.elements) {
        if (e.id == "G1") found_g1 = true;
    }
    EXPECT_TRUE(found_g1);
}

TEST(HistoryReconstruction, ReconstructsUpToRequestedSequence) {
    auto f = MakeFixture("partial_seq");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    core::commands::CommandContext ctx{f.model, f.package};
    core::commands::CreateChildElementCommand a("G1", core::NewElementKind::Strategy);
    ASSERT_TRUE(bus->Execute(a, ctx, "tester").success);
    core::commands::CreateChildElementCommand b("G1", core::NewElementKind::Context);
    ASSERT_TRUE(bus->Execute(b, ctx, "tester").success);

    auto at_one = core::audit::ReconstructAtSequence(f.project, 1);
    ASSERT_TRUE(at_one.has_value());
    bool at_one_has_a = false, at_one_has_b = false;
    for (const auto& e : at_one->model.elements) {
        if (e.id == a.GeneratedId()) at_one_has_a = true;
        if (e.id == b.GeneratedId()) at_one_has_b = true;
    }
    EXPECT_TRUE(at_one_has_a);
    EXPECT_FALSE(at_one_has_b);

    auto at_two = core::audit::ReconstructAtSequence(f.project, 2);
    ASSERT_TRUE(at_two.has_value());
    bool at_two_has_b = false;
    for (const auto& e : at_two->model.elements) {
        if (e.id == b.GeneratedId()) at_two_has_b = true;
    }
    EXPECT_TRUE(at_two_has_b);
}

TEST(HistoryReconstruction, ReconstructionAtLatestSequenceMatchesLiveCanonicalHash) {
    auto f = MakeFixture("latest_matches");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CommandContext ctx{f.model, f.package};
    core::commands::CreateChildElementCommand a("G1", core::NewElementKind::Strategy);
    auto live_result = bus->Execute(a, ctx, "tester");
    ASSERT_TRUE(live_result.success);

    auto state = core::audit::ReconstructAtSequence(f.project,
                                                    std::numeric_limits<std::uint64_t>::max());
    ASSERT_TRUE(state.has_value());

    EXPECT_EQ(core::audit::CanonicalModelHash(state->package),
              core::audit::CanonicalModelHash(f.package));
}

TEST(HistoryReconstruction, FailsForProjectWithoutAuditStore) {
    core::AssuranceProject project;
    project.rootPath = MakeTempProjectRoot("no_store");
    auto state = core::audit::ReconstructAtSequence(project, 0);
    ASSERT_FALSE(state.has_value());
    EXPECT_NE(state.error().find("audit store"), std::string::npos);
}
