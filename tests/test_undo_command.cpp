// Phase 5 — Undo command + replayer-skip + boundary tests.
//
// These tests verify:
//  1. UndoLastTransactionCommand wholesale-replaces model/package and
//     appends a single "Undo" audit event.
//  2. Two successive undos each produce a distinct transaction and
//     correctly walk effective state backward.
//  3. Replaying with Undo markers reproduces the live post-undo state.
//  4. FindUndoBoundary and CanUndo respect snapshot and baseline walls.
//  5. FindUndoTarget skips pure-Undo transactions and resolves
//     transitively (undo-of-undo = redo).
#include "core/audit/audit_baseline.h"
#include "core/audit/audit_manifest.h"
#include "core/audit/audit_paths.h"
#include "core/audit/audit_snapshot.h"
#include "core/audit/audit_store.h"
#include "core/audit/audit_transaction.h"
#include "core/audit/canonical_model_hash.h"
#include "core/audit/event_replayer.h"
#include "core/audit/event_store.h"
#include "core/audit/history_reconstruction.h"
#include "core/audit/undo_boundary.h"
#include "core/audit/undo_resolver.h"
#include "core/commands/acp_commands.h"
#include "core/commands/command_bus.h"
#include "core/commands/element_commands.h"
#include "core/commands/undo_command.h"
#include "core/derived_views.h"
#include "core/element_factory.h"
#include "core/project_model.h"
#include "parser/xml_parser.h"
#include "legacy_sacm/sacm_parser.h"
#include "sacm_adapter/library_load.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
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

// The same case carrying a vendor element in a foreign namespace. A tolerant
// library load PRESERVES it (SACM23-COMPAT-001); `sacm::AssuranceCasePackage`
// has no field for it, so anything routed through the POD drops it without
// trace. This is what separates a library-primary undo from a legacy one.
constexpr const char* kVendorExtendedSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation"
    xmlns:acme="http://acme.example/toolchain" id="AC1" name="Sample">
  <acme:vendorMetadata reviewCycle="Q3-2026"/>
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";

constexpr std::string_view kVendorElementMarker = "vendorMetadata";

std::filesystem::path MakeTempProjectRoot(const std::string& tag) {
    auto root = std::filesystem::temp_directory_path() /
                ("af_undo_" + tag + "_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
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
    // Null for the legacy-parsed fixture below; populated by the library-backed
    // variant, which is how production always runs.
    std::unique_ptr<sacm_adapter::LibraryDocument> document;
};

ProjectFixture MakeFixture(const std::string& tag, const char* sacm_xml = kSampleSacm) {
    ProjectFixture f;
    const auto root = MakeTempProjectRoot(tag);
    const std::filesystem::path sacm_rel = "argument.sacm";
    WriteFile(root / sacm_rel, sacm_xml);

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
    auto parsed = parser::parse_sacm_xml_string(sacm_xml);
    EXPECT_TRUE(parsed.has_value());
    f.model = std::move(parsed.value());
    return f;
}

// The same project with its views derived from the library document the way
// `AppState::load_file` derives them, so commands take the library-primary path.
ProjectFixture MakeLibraryBackedFixture(const std::string& tag, const char* sacm_xml = kSampleSacm) {
    ProjectFixture f = MakeFixture(tag, sacm_xml);
    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(f.sacm_abs);
    EXPECT_TRUE(loaded.ok);
    EXPECT_NE(loaded.document, nullptr);
    if (loaded.document == nullptr)
        return f;
    core::RebuildDerivedViewsFromLibrary(*loaded.document, f.model, f.package);
    f.document = std::move(loaded.document);
    return f;
}

// Mirror the app's frame boundary after a flipped command.
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

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// Execute Undo on the given bus, mirroring the orchestration in
// `AppRuntime::Undo()` but without the AppRuntime / UI dependencies —
// including handing the reconstructed DOCUMENT to the command (which is what
// makes the undo library-primary) and the frame-boundary re-derive that
// follows a flipped command.
bool DoUndo(ProjectFixture& f,
            core::commands::CommandBus& bus,
            core::commands::CommandContext& ctx,
            std::string& error_out) {
    const auto& transactions = bus.Store().Transactions();
    const auto target = core::audit::FindUndoTarget(transactions);
    if (!target.has_target) {
        error_out = "nothing to undo";
        return false;
    }
    auto prior = core::audit::ReconstructAtSequence(f.project, target.target_sequence - 1);
    if (!prior.has_value()) {
        error_out = prior.error();
        return false;
    }
    core::commands::UndoLastTransactionCommand cmd(target.target_sequence,
                                                   target.target_command_name,
                                                   std::move(prior.value().views.model),
                                                   std::move(prior.value().views.package),
                                                   std::move(prior.value().document));
    const auto result = bus.Execute(cmd, ctx, "tester");
    if (!result.success) {
        error_out = result.error;
        return false;
    }
    // A soft warning (success with a non-empty error) means the bus degraded
    // the save; surface it as a failure here so a preservation assertion cannot
    // pass over it.
    if (!result.error.empty()) {
        error_out = "bus reported a soft warning: " + result.error;
        return false;
    }
    if (ctx.library_primary && f.document != nullptr)
        core::RebuildDerivedViewsFromLibrary(*f.document, f.model, f.package);
    return true;
}

} // namespace

// Undo replaces the live model and package wholesale and the bus SERIALIZES the
// result, so whatever the reconstruction cannot carry is not merely missing from
// a view -- it is written to the tracked file. Asserted on the saved BYTES: the
// canonical hash drops vendor TaggedValues on both sides and is structurally
// blind to this class of loss, which is why it went unnoticed.
TEST(UndoCommand, SACM23_LIB_002_UndoPreservesVendorTaggedValuesInTheSavedFile) {
    auto f = MakeLibraryBackedFixture("vendor_tags");
    ASSERT_NE(f.document, nullptr);

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CommandContext ctx{f.model, f.package, f.document.get()};

    // A bare strategy carries a `strategyTarget` tag; an ACP on a solution
    // carries several `assuranceForge.acp*` tags.
    core::commands::CreateChildElementCommand add_strategy("G1", core::NewElementKind::Strategy);
    RunCommand(f, *bus, add_strategy, ctx);
    core::commands::CreateChildElementCommand add_solution("G1", core::NewElementKind::Solution);
    RunCommand(f, *bus, add_solution, ctx);
    core::commands::AddAcpCommand add_acp("element", add_solution.GeneratedId());
    RunCommand(f, *bus, add_acp, ctx);
    ASSERT_FALSE(add_acp.GeneratedAcpId().empty());

    // One more edit, which is the one the undo takes back.
    core::commands::CreateChildElementCommand add_goal("G1", core::NewElementKind::Goal);
    RunCommand(f, *bus, add_goal, ctx);

    const std::string before = ReadFile(f.sacm_abs);
    ASSERT_NE(before.find("assuranceForge.gsn.strategyTarget"), std::string::npos);
    ASSERT_NE(before.find("assuranceForge.acp"), std::string::npos);
    const int acp_tags_before = CountTaggedValuesWithKey(f.package, "assuranceForge.acp");
    ASSERT_GT(acp_tags_before, 0);

    ASSERT_TRUE(DoUndo(f, *bus, ctx, error)) << error;

    // The undo did what it says: the last-added goal is gone.
    bool goal_survived = false;
    for (const parser::SacmElement& element : f.model.elements) {
        if (element.id == add_goal.GeneratedId())
            goal_survived = true;
    }
    EXPECT_FALSE(goal_survived) << "undo did not take back the edit";

    // ...and took nothing else with it.
    const std::string after = ReadFile(f.sacm_abs);
    EXPECT_NE(after.find("assuranceForge.gsn.strategyTarget"), std::string::npos)
        << "undo stripped the strategy's target goal from the saved file";
    EXPECT_NE(after.find("assuranceForge.acp"), std::string::npos)
        << "undo destroyed the Assurance Claim Point in the saved file";
    EXPECT_EQ(CountTaggedValuesWithKey(f.package, "assuranceForge.acp"), acp_tags_before)
        << "undo destroyed the Assurance Claim Point in the live package";
}

// What the library-primary routing buys over the legacy one: the reconstructed
// DOCUMENT becomes the live document and the bus serializes IT, so content no
// projection can carry -- unknown/foreign XML preserved by a tolerant load --
// survives an undo. The legacy routing serializes a POD projection, which has
// no field for it.
//
// The document is not rebuilt from the projection either: it was replayed from
// the snapshot through the library, so it holds the preserved content of the
// state being restored rather than of the state being replaced.
TEST(UndoCommand, SACM23_LIB_002_LibraryPrimaryUndoPreservesUnknownVendorContent) {
    auto f = MakeLibraryBackedFixture("vendor_xml", kVendorExtendedSacm);
    ASSERT_NE(f.document, nullptr);

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CommandContext ctx{f.model, f.package, f.document.get()};

    // Non-vacuity: the snapshot the reconstruction replays from must itself
    // carry the vendor content, or this test would pass on a technicality.
    const std::filesystem::path snapshot =
        core::audit::SnapshotSacmPath(f.project.rootPath, core::audit::kInitialSnapshotId);
    ASSERT_NE(ReadFile(snapshot).find(kVendorElementMarker), std::string::npos)
        << "the snapshot lost the vendor content before the undo path was even reached";

    core::commands::CreateChildElementCommand add_goal("G1", core::NewElementKind::Goal);
    RunCommand(f, *bus, add_goal, ctx);
    ASSERT_NE(ReadFile(f.sacm_abs).find(kVendorElementMarker), std::string::npos)
        << "the edit itself dropped the vendor content; this test cannot say anything about undo";

    ASSERT_TRUE(DoUndo(f, *bus, ctx, error)) << error;
    EXPECT_TRUE(ctx.library_primary) << "the undo did not take the library-primary path";

    bool goal_survived = false;
    for (const parser::SacmElement& element : f.model.elements) {
        if (element.id == add_goal.GeneratedId())
            goal_survived = true;
    }
    EXPECT_FALSE(goal_survived) << "undo did not take back the edit";
    EXPECT_NE(ReadFile(f.sacm_abs).find(kVendorElementMarker), std::string::npos)
        << "undo wrote a projection and erased the preserved vendor content";
}

TEST(UndoBoundary, ImplicitInitialSnapshotIsAlwaysTheFloor) {
    // Empty snapshot/baseline lists still pin the wall at sequence 0.
    EXPECT_EQ(core::audit::FindUndoBoundary({}, {}, /*current_sequence=*/5), 0u);
    EXPECT_TRUE(core::audit::CanUndo(/*current_sequence=*/5, /*boundary=*/0));
    EXPECT_FALSE(core::audit::CanUndo(/*current_sequence=*/0, /*boundary=*/0));
}

TEST(UndoBoundary, PicksHighestCheckpointAtOrBeforeCurrent) {
    std::vector<core::audit::SnapshotMetadata> snapshots;
    snapshots.push_back({.snapshot_id = "snapshot_000003", .transaction_sequence = 3});
    snapshots.push_back({.snapshot_id = "snapshot_000007", .transaction_sequence = 7});
    std::vector<core::audit::BaselineMetadata> baselines;
    baselines.push_back({.baseline_id = "b1", .transaction_sequence = 5});

    EXPECT_EQ(core::audit::FindUndoBoundary(snapshots, baselines, /*current=*/10), 7u);
    EXPECT_EQ(core::audit::FindUndoBoundary(snapshots, baselines, /*current=*/6), 5u);
    EXPECT_EQ(core::audit::FindUndoBoundary(snapshots, baselines, /*current=*/3), 3u);
    EXPECT_EQ(core::audit::FindUndoBoundary(snapshots, baselines, /*current=*/2), 0u);
}

TEST(UndoResolver, FindsLatestNonUndoTarget) {
    std::vector<core::audit::AuditTransaction> txs;
    auto make = [](std::uint64_t seq, const std::string& name) {
        core::audit::AuditTransaction tx;
        tx.transaction_sequence = seq;
        tx.command_name = name;
        core::audit::AuditEvent ev;
        ev.event_sequence = seq;
        ev.event_type = name;
        tx.events.push_back(std::move(ev));
        return tx;
    };
    auto make_undo = [](std::uint64_t seq, std::uint64_t undone) {
        core::audit::AuditTransaction tx;
        tx.transaction_sequence = seq;
        tx.command_name = "Undo";
        core::audit::AuditEvent ev;
        ev.event_sequence = seq;
        ev.event_type = "Undo";
        ev.payload = {{"undone_transaction_sequence", undone}, {"undone_command_name", "Edit"}};
        tx.events.push_back(std::move(ev));
        return tx;
    };

    txs.push_back(make(1, "EditA"));
    txs.push_back(make(2, "EditB"));
    txs.push_back(make_undo(3, /*undone=*/2)); // cancels EditB

    auto target = core::audit::FindUndoTarget(txs);
    ASSERT_TRUE(target.has_target);
    EXPECT_EQ(target.target_sequence, 1u);
    EXPECT_EQ(target.target_command_name, "EditA");

    // Undo-of-undo restores EditB as the most-recent in-force tx.
    txs.push_back(make_undo(4, /*undone=*/3));
    target = core::audit::FindUndoTarget(txs);
    ASSERT_TRUE(target.has_target);
    EXPECT_EQ(target.target_sequence, 2u);
}

TEST(UndoCommand, EmitsUndoEventAndRestoresPriorState) {
    auto f = MakeFixture("emit_event");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    core::commands::CommandContext ctx{f.model, f.package};

    core::commands::CreateChildElementCommand add("G1", core::NewElementKind::Strategy);
    const auto add_result = bus->Execute(add, ctx, "tester");
    ASSERT_TRUE(add_result.success) << add_result.error;
    const std::string added_id = add.GeneratedId();
    ASSERT_FALSE(added_id.empty());

    const auto pre_undo_hash = core::audit::CanonicalModelHash(f.package);

    std::string undo_err;
    ASSERT_TRUE(DoUndo(f, *bus, ctx, undo_err)) << undo_err;

    // The added strategy is gone from both model and package.
    for (const auto& e : f.model.elements) {
        EXPECT_NE(e.id, added_id);
    }

    // The audit log gained a single Undo transaction referencing tx 1.
    const auto& txs = bus->Store().Transactions();
    ASSERT_EQ(txs.size(), 2u);
    EXPECT_EQ(txs.back().command_name, "Undo");
    ASSERT_EQ(txs.back().events.size(), 1u);
    EXPECT_EQ(txs.back().events.front().event_type, "Undo");
    EXPECT_EQ(txs.back().events.front().payload.at("undone_transaction_sequence").get<std::uint64_t>(), 1u);

    // The post-undo state matches the original (pre-add) snapshot:
    // canonical hash differs from pre_undo_hash and matches what
    // ReconstructAtSequence(0) returns.
    EXPECT_NE(core::audit::CanonicalModelHash(f.package), pre_undo_hash);
    auto reconstructed = core::audit::ReconstructAtSequence(f.project, 0);
    ASSERT_TRUE(reconstructed.has_value()) << reconstructed.error();
    EXPECT_EQ(core::audit::CanonicalModelHash(f.package),
              core::audit::CanonicalModelHash(reconstructed->views.package));
}

TEST(UndoCommand, ReplayerSkipsUndoneTransactions) {
    auto f = MakeFixture("replayer_skip");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    core::commands::CommandContext ctx{f.model, f.package};

    core::commands::CreateChildElementCommand add1("G1", core::NewElementKind::Strategy);
    ASSERT_TRUE(bus->Execute(add1, ctx, "tester").success);
    core::commands::CreateChildElementCommand add2("G1", core::NewElementKind::Context);
    ASSERT_TRUE(bus->Execute(add2, ctx, "tester").success);

    std::string undo_err;
    ASSERT_TRUE(DoUndo(f, *bus, ctx, undo_err)) << undo_err; // undoes add2
    const auto live_hash = core::audit::CanonicalModelHash(f.package);

    // Replay from snapshot zero produces the same canonical hash.
    auto replayed = core::audit::ReconstructAtSequence(f.project, std::numeric_limits<std::uint64_t>::max());
    ASSERT_TRUE(replayed.has_value()) << replayed.error();
    EXPECT_EQ(core::audit::CanonicalModelHash(replayed->views.package), live_hash);
}

TEST(UndoCommand, TwoSuccessiveUndosWalkBackwards) {
    auto f = MakeFixture("two_undos");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    core::commands::CommandContext ctx{f.model, f.package};

    core::commands::CreateChildElementCommand add1("G1", core::NewElementKind::Strategy);
    ASSERT_TRUE(bus->Execute(add1, ctx, "tester").success);
    core::commands::CreateChildElementCommand add2("G1", core::NewElementKind::Context);
    ASSERT_TRUE(bus->Execute(add2, ctx, "tester").success);

    std::string undo_err;
    ASSERT_TRUE(DoUndo(f, *bus, ctx, undo_err)) << undo_err; // undoes add2
    ASSERT_TRUE(DoUndo(f, *bus, ctx, undo_err)) << undo_err; // undoes add1

    const auto& txs = bus->Store().Transactions();
    ASSERT_EQ(txs.size(), 4u);
    EXPECT_EQ(txs[2].command_name, "Undo");
    EXPECT_EQ(txs[3].command_name, "Undo");
    EXPECT_EQ(txs[2].events.front().payload.at("undone_transaction_sequence").get<std::uint64_t>(), 2u);
    EXPECT_EQ(txs[3].events.front().payload.at("undone_transaction_sequence").get<std::uint64_t>(), 1u);

    // Effective state is the original.
    auto original = core::audit::ReconstructAtSequence(f.project, 0);
    ASSERT_TRUE(original.has_value());
    EXPECT_EQ(core::audit::CanonicalModelHash(f.package), core::audit::CanonicalModelHash(original->views.package));
}

TEST(UndoCommand, NothingToUndoOnFreshProject) {
    auto f = MakeFixture("fresh");
    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    auto target = core::audit::FindUndoTarget(bus->Store().Transactions());
    EXPECT_FALSE(target.has_target);
}
