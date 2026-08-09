#include "core/audit/audit_manifest.h"
#include "core/audit/audit_paths.h"
#include "core/audit/audit_snapshot.h"
#include "core/audit/audit_store.h"
#include "core/audit/canonical_model_hash.h"
#include "core/audit/event_store.h"
#include "core/audit/audit_event.h"
#include "core/commands/command_bus.h"
#include "core/commands/element_commands.h"
#include "core/element_factory.h"
#include "core/project_model.h"
#include "parser/xml_parser.h"
#include "legacy_sacm/sacm_parser.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/library_load.h"
#include "core/library_package_projection.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
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
                ("af_cmdbus_" + tag + "_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
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
    EXPECT_TRUE(pkg.has_value()) << (pkg.has_value() ? "" : pkg.error());
    f.package = std::move(pkg.value());
    auto parsed = parser::parse_sacm_xml_string(kSampleSacm);
    EXPECT_TRUE(parsed.has_value()) << (parsed.has_value() ? "" : parsed.error());
    f.model = std::move(parsed.value());
    return f;
}

const core::SacmElement* FindProjected(const core::AssuranceCase& assurance_case, const std::string& id) {
    for (const core::SacmElement& element : assurance_case.elements) {
        if (element.id == id) {
            return &element;
        }
    }
    return nullptr;
}

std::size_t ClaimCount(const core::AssuranceCase& assurance_case) {
    std::size_t count = 0;
    for (const core::SacmElement& element : assurance_case.elements) {
        if (element.type == "claim") {
            ++count;
        }
    }
    return count;
}

} // namespace

// Phase 9 Stage 5: a text edit command routes the same edit through the
// library-owned document. The command reports it synced, and the projected
// library document reflects the new value -- proving the edit executed as a
// library operation, not just on the legacy models.
TEST(CommandBus, SACM23_INT_001_UpdateElementTextIsLibraryPrimary) {
    auto f = MakeFixture("primary_text");

    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(f.sacm_abs);
    ASSERT_TRUE(loaded.ok);
    ASSERT_NE(loaded.document, nullptr);

    core::commands::UpdateElementTextCommand cmd("G1", core::ElementTextField::Name, "en", "Renamed Goal");
    core::commands::CommandContext ctx{f.model, f.package, loaded.document.get()};
    core::audit::AuditEvent event;
    std::string error;
    ASSERT_TRUE(cmd.Apply(ctx, event, error)) << error;

    // Phase 2 slice 2b-2: the text edit is now LIBRARY-PRIMARY. It bridges the
    // legacy mutator onto the library (leaving the caller's legacy views for the
    // frame-boundary re-derive), so the library document itself reflects the edit.
    EXPECT_TRUE(ctx.library_primary) << "the text edit should be library-primary";
    const core::AssuranceCase projected = sacm_adapter::project_case(*loaded.document);
    const core::SacmElement* g1 = FindProjected(projected, "G1");
    ASSERT_NE(g1, nullptr);
    EXPECT_EQ(g1->name, "Renamed Goal");
}

// A no-op text edit (new value equals current) is still detected as a no-op via
// WasNoOp. Like any text edit it is now library-primary -- the bridge routes it
// through the library (a harmless reload of the unchanged value). In the app the
// controller short-circuits no-ops before dispatch, so this only exercises the
// command directly.
TEST(CommandBus, SACM23_INT_001_NoOpTextEditIsDetected) {
    auto f = MakeFixture("noop_text");

    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(f.sacm_abs);
    ASSERT_TRUE(loaded.ok);
    ASSERT_NE(loaded.document, nullptr);

    // "Top goal" is already G1's name, so this edit is a no-op.
    core::commands::UpdateElementTextCommand cmd("G1", core::ElementTextField::Name, "en", "Top goal");
    core::commands::CommandContext ctx{f.model, f.package, loaded.document.get()};
    core::audit::AuditEvent event;
    std::string error;
    ASSERT_TRUE(cmd.Apply(ctx, event, error)) << error;

    EXPECT_TRUE(cmd.WasNoOp());
    EXPECT_TRUE(ctx.library_primary) << "a text edit routes through the library";
}

// A command with no library seam yet (CreateTopGoal) leaves `library_synced`
// false; the bus must re-derive the library document from the authoritative
// package so it still reflects the edit rather than drifting stale.
TEST(CommandBus, SACM23_INT_001_UncoveredCommandRederivesLibraryDocument) {
    auto f = MakeFixture("rederive");

    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(f.sacm_abs);
    ASSERT_TRUE(loaded.ok);
    ASSERT_NE(loaded.document, nullptr);
    ASSERT_EQ(ClaimCount(sacm_adapter::project_case(*loaded.document)), 1u); // just G1

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    core::commands::CreateTopGoalCommand cmd;
    core::commands::CommandContext ctx{f.model, f.package, loaded.document.get()};
    const auto result = bus->Execute(cmd, ctx, "tester");
    ASSERT_TRUE(result.success) << result.error;

    EXPECT_FALSE(ctx.library_synced) << "CreateTopGoal has no library seam yet";
    // Re-derived from the authoritative package: the new top goal is present.
    const core::AssuranceCase projected = sacm_adapter::project_case(*loaded.document);
    EXPECT_EQ(ClaimCount(projected), 2u);
    EXPECT_NE(FindProjected(projected, cmd.GeneratedId()), nullptr);
}

TEST(CommandBus, AppendsTransactionAndUpdatesManifestOnCreateChildElement) {
    auto f = MakeFixture("create_child");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    core::commands::CreateChildElementCommand cmd("G1", core::NewElementKind::Strategy);
    core::commands::CommandContext ctx{f.model, f.package};
    const auto result = bus->Execute(cmd, ctx, "tester");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.transaction_sequence, 1u);
    EXPECT_FALSE(result.transaction_id.empty());
    EXPECT_FALSE(result.raw_file_hash_after.empty());
    EXPECT_FALSE(result.canonical_model_hash_after.empty());
    EXPECT_FALSE(cmd.GeneratedId().empty());

    // Manifest reflects new sequence and the post-state hashes.
    core::audit::AuditManifest manifest;
    ASSERT_TRUE(core::audit::ReadAuditManifest(f.project.rootPath, manifest, error)) << error;
    EXPECT_EQ(manifest.latest_transaction_sequence, 1u);
    EXPECT_EQ(manifest.latest_event_sequence, 1u);
    EXPECT_EQ(manifest.last_known_raw_file_hash, result.raw_file_hash_after);
    EXPECT_EQ(manifest.last_known_canonical_model_hash, result.canonical_model_hash_after);
    EXPECT_FALSE(manifest.event_store_hash.empty());

    // The on-disk SACM contains the new element: the strategy we just added
    // should appear in the first argument package. Phase 9 Stage 6: the file is
    // now library XMI, so read it back through the library and project it.
    sacm_adapter::LoadOutcome reparsed = sacm_adapter::load_document(f.sacm_abs);
    ASSERT_TRUE(reparsed.ok);
    ASSERT_NE(reparsed.document, nullptr);
    const sacm::AssuranceCasePackage on_disk = core::project_library_package(*reparsed.document);
    ASSERT_FALSE(on_disk.argumentPackages.empty());
    const auto& ap = on_disk.argumentPackages.front();
    bool found = false;
    for (const auto& reasoning : ap.argumentReasonings) {
        if (reasoning.id == cmd.GeneratedId()) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "newly-added strategy " << cmd.GeneratedId() << " not present on disk";
}

TEST(CommandBus, FailedCommandLeavesNoTransactionOrManifestChange) {
    auto f = MakeFixture("fail_cmd");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    const auto manifest_before = bus->Manifest();
    const std::uint64_t txn_before = bus->Store().LatestTransactionSequence();

    core::commands::CreateChildElementCommand cmd("does-not-exist", core::NewElementKind::Goal);
    core::commands::CommandContext ctx{f.model, f.package};
    const auto result = bus->Execute(cmd, ctx, "tester");

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
    EXPECT_EQ(bus->Store().LatestTransactionSequence(), txn_before);
    EXPECT_EQ(bus->Manifest().latest_transaction_sequence, manifest_before.latest_transaction_sequence);
    EXPECT_EQ(bus->Manifest().event_store_hash, manifest_before.event_store_hash);
}

// `sacm_written` is what the UI uses to tell a user their work is on disk, so
// it must track the file rather than the command outcome.
TEST(CommandBus, ReportsSacmWrittenWhenTheFileMatchesTheModel) {
    auto f = MakeFixture("sacm_written");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    core::commands::CreateChildElementCommand cmd("G1", core::NewElementKind::Strategy);
    core::commands::CommandContext ctx{f.model, f.package};
    const auto result = bus->Execute(cmd, ctx, "tester");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_TRUE(result.sacm_written);

    // The claim must be true of the bytes on disk, not just of the return value:
    // the new element has to be readable back out of the file.
    std::ifstream saved(f.sacm_abs, std::ios::binary);
    ASSERT_TRUE(saved.good());
    const std::string contents((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
    EXPECT_NE(contents.find(cmd.GeneratedId()), std::string::npos)
        << "sacm_written was true but the generated element is absent from the file";
}

TEST(CommandBus, DoesNotReportSacmWrittenWhenTheCommandFailed) {
    auto f = MakeFixture("sacm_not_written");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    core::commands::CreateChildElementCommand cmd("does-not-exist", core::NewElementKind::Goal);
    core::commands::CommandContext ctx{f.model, f.package};
    const auto result = bus->Execute(cmd, ctx, "tester");

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.sacm_written);
}

TEST(CommandBus, ConsecutiveCommandsAdvanceSequences) {
    auto f = MakeFixture("multi_cmd");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    core::commands::CommandContext ctx{f.model, f.package};

    core::commands::CreateChildElementCommand add1("G1", core::NewElementKind::Strategy);
    auto r1 = bus->Execute(add1, ctx, "tester");
    ASSERT_TRUE(r1.success) << r1.error;
    EXPECT_EQ(r1.transaction_sequence, 1u);

    core::commands::CreateChildElementCommand add2("G1", core::NewElementKind::Context);
    auto r2 = bus->Execute(add2, ctx, "tester");
    ASSERT_TRUE(r2.success) << r2.error;
    EXPECT_EQ(r2.transaction_sequence, 2u);
    EXPECT_NE(r1.raw_file_hash_after, r2.raw_file_hash_after);

    core::commands::RemoveElementCommand rm(add1.GeneratedId(), core::RemoveMode::NodeOnly);
    auto r3 = bus->Execute(rm, ctx, "tester");
    ASSERT_TRUE(r3.success) << r3.error;
    EXPECT_EQ(r3.transaction_sequence, 3u);
}

TEST(CommandBus, ReopenSeesPriorTransactions) {
    auto f = MakeFixture("reopen");

    std::string error;
    {
        auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
        ASSERT_TRUE(bus) << error;
        core::commands::CreateChildElementCommand cmd("G1", core::NewElementKind::Solution);
        core::commands::CommandContext ctx{f.model, f.package};
        ASSERT_TRUE(bus->Execute(cmd, ctx, "tester").success);
    }

    auto bus2 = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus2) << error;
    EXPECT_EQ(bus2->Store().LatestTransactionSequence(), 1u);
    EXPECT_EQ(bus2->Manifest().latest_transaction_sequence, 1u);
}

// Regression for the Phase 1.7 close-time snapshot wiring: after a series of
// audited commands, taking a user snapshot should succeed without disturbing
// the manifest's last_known_*_hash invariants, and reopening the bus must
// still see the same transaction sequence. This is the exact path
// AppRuntime::RequestExit follows when it auto-flushes on close.
TEST(CommandBus, CreateUserSnapshotAfterExecuteKeepsManifestConsistent) {
    auto f = MakeFixture("close_snapshot");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    core::commands::CommandContext ctx{f.model, f.package};
    core::commands::CreateChildElementCommand add("G1", core::NewElementKind::Strategy);
    const auto exec_result = bus->Execute(add, ctx, "tester");
    ASSERT_TRUE(exec_result.success) << exec_result.error;

    const auto manifest_after_exec = bus->Manifest();
    ASSERT_FALSE(manifest_after_exec.last_known_raw_file_hash.empty());

    // Take the close-time snapshot. Mirrors what AppRuntime::RequestExit does.
    core::audit::SnapshotMetadata snap;
    ASSERT_TRUE(core::audit::CreateUserSnapshot(f.project.rootPath, "automatic close snapshot", "tester", snap, error))
        << error;
    EXPECT_FALSE(snap.snapshot_id.empty());

    // CreateUserSnapshot must not have mutated the audit manifest's hash
    // chain — only the snapshots/ directory is touched.
    core::audit::AuditManifest manifest_after_snapshot;
    ASSERT_TRUE(core::audit::ReadAuditManifest(f.project.rootPath, manifest_after_snapshot, error)) << error;
    EXPECT_EQ(manifest_after_snapshot.latest_transaction_sequence, manifest_after_exec.latest_transaction_sequence);
    EXPECT_EQ(manifest_after_snapshot.last_known_raw_file_hash, manifest_after_exec.last_known_raw_file_hash);
    EXPECT_EQ(manifest_after_snapshot.last_known_canonical_model_hash,
              manifest_after_exec.last_known_canonical_model_hash);

    // Reopen the bus: simulates the user re-opening the project after close.
    // The committed transaction must still be visible and the on-disk SACM
    // hash must still match what the manifest records (no divergence).
    auto bus2 = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus2) << error;
    EXPECT_EQ(bus2->Store().LatestTransactionSequence(), 1u);
    EXPECT_EQ(bus2->Manifest().last_known_raw_file_hash, manifest_after_exec.last_known_raw_file_hash);
}

// Regression: calling CreateUserSnapshot twice at the same transaction
// sequence (e.g. user closes, reopens, closes again with no edits) must not
// clobber the existing snapshot. The function is documented to fail in that
// case, which RequestExit treats as a non-fatal warning rather than blocking
// the close.
TEST(CommandBus, CreateUserSnapshotIsRejectedAtSameSequence) {
    auto f = MakeFixture("snapshot_idempotent");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    core::commands::CommandContext ctx{f.model, f.package};
    core::commands::CreateChildElementCommand add("G1", core::NewElementKind::Solution);
    ASSERT_TRUE(bus->Execute(add, ctx, "tester").success);

    core::audit::SnapshotMetadata first;
    ASSERT_TRUE(core::audit::CreateUserSnapshot(f.project.rootPath, "automatic close snapshot", "tester", first, error))
        << error;

    core::audit::SnapshotMetadata second;
    std::string second_error;
    const bool ok =
        core::audit::CreateUserSnapshot(f.project.rootPath, "automatic close snapshot", "tester", second, second_error);
    EXPECT_FALSE(ok) << "second snapshot at the same sequence should be rejected";
    EXPECT_FALSE(second_error.empty());
}

TEST(ElementCommands, KindAndModeTokensRoundTrip) {
    using core::NewElementKind;
    using core::RemoveMode;
    NewElementKind k;
    ASSERT_TRUE(core::commands::NewElementKindFromToken("Strategy", k));
    EXPECT_EQ(k, NewElementKind::Strategy);
    ASSERT_TRUE(core::commands::NewElementKindFromToken("Justification", k));
    EXPECT_EQ(k, NewElementKind::Justification);
    EXPECT_FALSE(core::commands::NewElementKindFromToken("Nope", k));
    EXPECT_EQ(core::commands::NewElementKindToToken(NewElementKind::Context), "Context");

    RemoveMode m;
    ASSERT_TRUE(core::commands::RemoveModeFromToken("NodeAndDescendants", m));
    EXPECT_EQ(m, RemoveMode::NodeAndDescendants);
    EXPECT_EQ(core::commands::RemoveModeToToken(RemoveMode::NodeOnly), "NodeOnly");
}
