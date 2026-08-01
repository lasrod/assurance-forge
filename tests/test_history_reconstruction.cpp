#include "core/audit/audit_paths.h"
#include "core/audit/audit_store.h"
#include "core/audit/canonical_model_hash.h"
#include "core/audit/event_replayer.h"
#include "core/audit/history_reconstruction.h"
#include "core/commands/acp_commands.h"
#include "core/commands/command_bus.h"
#include "core/commands/element_commands.h"
#include "core/derived_views.h"
#include "core/element_factory.h"
#include "core/library_package_projection.h"
#include "core/project_model.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_parser.h"
#include "sacm_adapter/gsn_role_tag.h"
#include "sacm_adapter/library_load.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
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
    core::AssuranceProject project;
    std::filesystem::path sacm_abs;
    sacm::AssuranceCasePackage package;
    parser::AssuranceCase model;
    // Null for the legacy-parsed fixture below. The library-backed variant
    // populates it, mirroring production where `AppState` always holds one.
    std::unique_ptr<sacm_adapter::LibraryDocument> document;
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

// The same project, but with the views derived the way `AppState::load_file`
// derives them and the library document retained — so commands take the
// library-primary path they take in production.
ProjectFixture MakeLibraryBackedFixture(const std::string& tag) {
    ProjectFixture f = MakeFixture(tag);
    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(f.sacm_abs);
    EXPECT_TRUE(loaded.ok);
    EXPECT_NE(loaded.document, nullptr);
    if (loaded.document == nullptr)
        return f;
    core::RebuildDerivedViewsFromLibrary(*loaded.document, f.model, f.package);
    f.document = std::move(loaded.document);
    return f;
}

// Run a command through the bus and mirror the app's frame boundary: a flipped
// command leaves the live views for `AppRuntime::RebuildDerivedViewsIfNeeded`,
// and the next command plans its ids from them.
void RunCommand(ProjectFixture& f,
                core::commands::CommandBus& bus,
                core::commands::ICommand& command,
                core::commands::CommandContext& ctx) {
    const core::commands::CommandResult result = bus.Execute(command, ctx, "tester");
    ASSERT_TRUE(result.success) << result.error;
    // The bus reports a lossy/degraded save as a SOFT warning: success stays
    // true and `error` carries the diagnostic. A preservation test that ignored
    // it could pass while the bus was announcing the very degradation the test
    // exists to rule out.
    ASSERT_TRUE(result.error.empty()) << "bus reported a soft warning: " << result.error;
    if (ctx.library_primary && f.document != nullptr)
        core::RebuildDerivedViewsFromLibrary(*f.document, f.model, f.package);
}

int CountTaggedValuesWithKey(const sacm::AssuranceCasePackage& package, const std::string& key) {
    int total = 0;
    const auto count = [&](const std::vector<sacm::TaggedValue>& tags) {
        for (const sacm::TaggedValue& tag : tags) {
            if (tag.key.rfind(key, 0) == 0)
                ++total;
        }
    };
    for (const sacm::ArgumentPackage& ap : package.argumentPackages) {
        count(ap.taggedValues);
        for (const sacm::Claim& c : ap.claims)
            count(c.taggedValues);
        for (const sacm::ArgumentReasoning& r : ap.argumentReasonings)
            count(r.taggedValues);
        for (const sacm::ArtifactReference& r : ap.artifactReferences)
            count(r.taggedValues);
    }
    return total;
}

// Build the state the defect is about: a bare strategy (one `strategyTarget`
// tag), a solution carrying an ACP (several `assuranceForge.acp*` tags), and
// the ACP's confidence argument tree (a SECOND ArgumentPackage). Every one of
// those is vendor TaggedValue content or package structure that the POD
// projection cannot carry.
void BuildVendorTagState(ProjectFixture& f, core::commands::CommandBus& bus, core::commands::CommandContext& ctx) {
    core::commands::CreateChildElementCommand add_strategy("G1", core::NewElementKind::Strategy);
    RunCommand(f, bus, add_strategy, ctx);

    core::commands::CreateChildElementCommand add_solution("G1", core::NewElementKind::Solution);
    RunCommand(f, bus, add_solution, ctx);

    core::commands::AddAcpCommand add_acp("element", add_solution.GeneratedId());
    RunCommand(f, bus, add_acp, ctx);
    ASSERT_FALSE(add_acp.GeneratedAcpId().empty());

    core::commands::CreateConfidenceArgumentTreeForAcpCommand add_tree(add_acp.GeneratedAcpId());
    RunCommand(f, bus, add_tree, ctx);
}

} // namespace

TEST(HistoryReconstruction, ReturnsSnapshotStateAtSequenceZero) {
    auto f = MakeFixture("seq_zero");

    auto state = core::audit::ReconstructAtSequence(f.project, 0);
    ASSERT_TRUE(state.has_value()) << (state.has_value() ? "" : state.error());

    // Snapshot is the empty initial SACM: only G1 should be present.
    bool found_g1 = false;
    for (const auto& e : state->views.model.elements) {
        if (e.id == "G1")
            found_g1 = true;
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
    for (const auto& e : at_one->views.model.elements) {
        if (e.id == a.GeneratedId())
            at_one_has_a = true;
        if (e.id == b.GeneratedId())
            at_one_has_b = true;
    }
    EXPECT_TRUE(at_one_has_a);
    EXPECT_FALSE(at_one_has_b);

    auto at_two = core::audit::ReconstructAtSequence(f.project, 2);
    ASSERT_TRUE(at_two.has_value());
    bool at_two_has_b = false;
    for (const auto& e : at_two->views.model.elements) {
        if (e.id == b.GeneratedId())
            at_two_has_b = true;
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

    auto state = core::audit::ReconstructAtSequence(f.project, std::numeric_limits<std::uint64_t>::max());
    ASSERT_TRUE(state.has_value());

    // Compare on the projection-invariant library hash (the one the verifier uses),
    // not the legacy CanonicalModelHash. Phase 1b routes snapshot loading through
    // the library, so the reconstruction base is library-projected while this
    // fixture's live package is legacy-parsed; the legacy hash is not invariant
    // under that projection, but the authoritative library hash converges.
    const auto reconstructed_hash = core::library_canonical_hash(state->views.package);
    const auto live_hash = core::library_canonical_hash(f.package);
    ASSERT_TRUE(reconstructed_hash.has_value());
    ASSERT_TRUE(live_hash.has_value());
    EXPECT_EQ(*reconstructed_hash, *live_hash);
}

// The reconstructed state is not a hash input -- `UndoLastTransactionCommand`
// assigns it straight over the live model and package, which the bus then
// SERIALIZES. So it has to be derived the way a load derives it, or an undo
// writes a document stripped of everything the POD projection cannot carry:
// every vendor TaggedValue (ACPs, a bare strategy's `strategyTarget`) and the
// separate confidence ArgumentPackage, which the audit projection is entitled
// to collapse.
TEST(HistoryReconstruction, SACM23_LIB_002_ReconstructionPreservesVendorTaggedValues) {
    auto f = MakeLibraryBackedFixture("vendor_tags");
    ASSERT_NE(f.document, nullptr);

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CommandContext ctx{f.model, f.package, f.document.get()};
    BuildVendorTagState(f, *bus, ctx);

    // What the live state carries, so the reconstruction is compared against a
    // measured expectation rather than a guessed one.
    const int live_strategy_tags = CountTaggedValuesWithKey(f.package, sacm_adapter::kGsnStrategyTargetTagKey);
    const int live_acp_tags = CountTaggedValuesWithKey(f.package, "assuranceForge.acp");
    ASSERT_GT(live_strategy_tags, 0);
    ASSERT_GT(live_acp_tags, 0);
    ASSERT_EQ(f.package.argumentPackages.size(), 2u) << "the confidence tree did not get its own package";

    auto state = core::audit::ReconstructAtSequence(f.project, std::numeric_limits<std::uint64_t>::max());
    ASSERT_TRUE(state.has_value()) << state.error();

    EXPECT_EQ(CountTaggedValuesWithKey(state->views.package, sacm_adapter::kGsnStrategyTargetTagKey),
              live_strategy_tags)
        << "a bare strategy would lose the goal it supports";
    EXPECT_EQ(CountTaggedValuesWithKey(state->views.package, "assuranceForge.acp"), live_acp_tags)
        << "every Assurance Claim Point would be destroyed";
    EXPECT_EQ(state->views.package.argumentPackages.size(), f.package.argumentPackages.size())
        << "the confidence argument package would be merged into the main one";
}

// The render passes a load applies must apply here too: the reconstructed model
// feeds both the historical canvas and (through undo) the live one, and a bare
// strategy with no synthesized placement renders detached from its goal.
TEST(HistoryReconstruction, SACM23_LIB_002_ReconstructionCarriesBareStrategyPlacement) {
    auto f = MakeLibraryBackedFixture("bare_strategy");
    ASSERT_NE(f.document, nullptr);

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CommandContext ctx{f.model, f.package, f.document.get()};

    core::commands::CreateChildElementCommand add_strategy("G1", core::NewElementKind::Strategy);
    RunCommand(f, *bus, add_strategy, ctx);
    const std::string placement_id = add_strategy.GeneratedId() + "__pending_inference";

    auto state = core::audit::ReconstructAtSequence(f.project, std::numeric_limits<std::uint64_t>::max());
    ASSERT_TRUE(state.has_value()) << state.error();

    bool found = false;
    for (const parser::SacmElement& element : state->views.model.elements) {
        if (element.id == placement_id)
            found = true;
    }
    EXPECT_TRUE(found) << "reconstructed model lost the bare strategy's placement";
}

TEST(HistoryReconstruction, FailsForProjectWithoutAuditStore) {
    core::AssuranceProject project;
    project.rootPath = MakeTempProjectRoot("no_store");
    auto state = core::audit::ReconstructAtSequence(project, 0);
    ASSERT_FALSE(state.has_value());
    EXPECT_NE(state.error().find("audit store"), std::string::npos);
}
