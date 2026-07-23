#include "core/audit/strategy_migration.h"

#include "core/audit/audit_manifest.h"
#include "core/audit/audit_store.h"
#include "core/audit/replay_verifier.h"
#include "core/commands/command_bus.h"
#include "core/commands/element_commands.h"
#include "core/project_model.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"
#include "sacm/sacm_parser.h"
#include "sacm/sacm_serializer.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

constexpr const char* kStrategyTargetTagKey = "assuranceForge.gsn.strategyTarget";

// A package in the legacy bare-inference strategy encoding: strategy S1 under
// goal G1 with two sub-goals A and B, wired as a bare inference plus one
// inference per sub-goal (target = the strategy), and NO strategyTarget tag.
sacm::AssuranceCasePackage MakeLegacyStrategyPackage() {
    sacm::AssuranceCasePackage package;
    sacm::ArgumentPackage ap;
    ap.id = "AP1";

    sacm::Claim g1;
    g1.id = "G1";
    g1.name = "Top goal";
    ap.claims.push_back(g1);

    sacm::ArgumentReasoning s1;
    s1.id = "S1";
    s1.name = "Strategy";
    ap.argumentReasonings.push_back(s1);

    sacm::Claim a;
    a.id = "A";
    ap.claims.push_back(a);
    sacm::Claim b;
    b.id = "B";
    ap.claims.push_back(b);

    sacm::AssertedInference bare;  // {reasoning=S1, target=G1, no source}
    bare.id = "R_bare";
    bare.reasoning = "S1";
    bare.targets = {"G1"};
    ap.assertedInferences.push_back(bare);

    sacm::AssertedInference ia;  // {target=S1, source=A}
    ia.id = "R_A";
    ia.targets = {"S1"};
    ia.sources = {"A"};
    ap.assertedInferences.push_back(ia);

    sacm::AssertedInference ib;  // {target=S1, source=B}
    ib.id = "R_B";
    ib.targets = {"S1"};
    ib.sources = {"B"};
    ap.assertedInferences.push_back(ib);

    package.argumentPackages.push_back(ap);
    return package;
}

int CountStrategyInferences(const sacm::ArgumentPackage& ap, const std::string& strategy_id) {
    int count = 0;
    for (const auto& inf : ap.assertedInferences)
        if (inf.reasoning == strategy_id)
            ++count;
    return count;
}

bool HasStrategyTargetTag(const sacm::ArgumentReasoning& reasoning, const std::string& value) {
    for (const auto& tag : reasoning.taggedValues)
        if (tag.key == kStrategyTargetTagKey && tag.value == value)
            return true;
    return false;
}

} // namespace

TEST(StrategyMigration, DetectsLegacyEncoding) {
    const auto package = MakeLegacyStrategyPackage();
    EXPECT_TRUE(core::audit::PackageHasLegacyStrategyEncoding(package));
}

TEST(StrategyMigration, NormalizeCollapsesToSingleInference) {
    auto package = MakeLegacyStrategyPackage();
    ASSERT_TRUE(core::audit::NormalizeStrategyEncoding(package));
    const sacm::ArgumentPackage& ap = package.argumentPackages.front();

    // Exactly one inference wires the strategy, {reasoning=S1, target=G1,
    // sources=[A,B]}, and the legacy {target=S1} inferences are gone.
    ASSERT_EQ(CountStrategyInferences(ap, "S1"), 1);
    const sacm::AssertedInference* single = nullptr;
    for (const auto& inf : ap.assertedInferences) {
        if (inf.reasoning == "S1")
            single = &inf;
        EXPECT_FALSE(inf.reasoning.empty() && !inf.targets.empty() && inf.targets.front() == "S1")
            << "legacy sub-goal inference targeting the strategy survived";
    }
    ASSERT_NE(single, nullptr);
    ASSERT_EQ(single->targets.size(), 1u);
    EXPECT_EQ(single->targets.front(), "G1");
    ASSERT_EQ(single->sources.size(), 2u);
    EXPECT_EQ(single->sources[0], "A");
    EXPECT_EQ(single->sources[1], "B");

    EXPECT_TRUE(HasStrategyTargetTag(ap.argumentReasonings.front(), "G1"));

    // Idempotent: a migrated package is no longer legacy and does not change again.
    EXPECT_FALSE(core::audit::PackageHasLegacyStrategyEncoding(package));
    EXPECT_FALSE(core::audit::NormalizeStrategyEncoding(package));
}

TEST(StrategyMigration, NormalizeBareStrategyKeepsOnlyTag) {
    sacm::AssuranceCasePackage package;
    sacm::ArgumentPackage ap;
    ap.id = "AP1";
    sacm::Claim g1;
    g1.id = "G1";
    ap.claims.push_back(g1);
    sacm::ArgumentReasoning s1;
    s1.id = "S1";
    ap.argumentReasonings.push_back(s1);
    sacm::AssertedInference bare;
    bare.id = "R_bare";
    bare.reasoning = "S1";
    bare.targets = {"G1"};
    ap.assertedInferences.push_back(bare);
    package.argumentPackages.push_back(ap);

    ASSERT_TRUE(core::audit::NormalizeStrategyEncoding(package));
    const sacm::ArgumentPackage& out = package.argumentPackages.front();
    // A bare strategy (no sub-goals) keeps only the tag; its placement is a
    // render-only placeholder synthesized on load, not a persisted inference.
    EXPECT_TRUE(out.assertedInferences.empty());
    EXPECT_TRUE(HasStrategyTargetTag(out.argumentReasonings.front(), "G1"));
    EXPECT_FALSE(core::audit::PackageHasLegacyStrategyEncoding(package));
}

TEST(StrategyMigration, DoesNotDetectTaggedStrategy) {
    // A strategy whose sub-goals were all removed leaves a sourceless inference
    // too, but it carries the strategyTarget tag -- it is already new-encoded and
    // must NOT be re-migrated.
    sacm::AssuranceCasePackage package;
    sacm::ArgumentPackage ap;
    ap.id = "AP1";
    sacm::ArgumentReasoning s1;
    s1.id = "S1";
    s1.taggedValues.push_back(
        sacm::TaggedValue{.id = "S1__strategyTarget", .key = kStrategyTargetTagKey, .value = "G1"});
    ap.argumentReasonings.push_back(s1);
    sacm::AssertedInference sourceless;
    sourceless.id = "R";
    sourceless.reasoning = "S1";
    sourceless.targets = {"G1"};
    ap.assertedInferences.push_back(sourceless);
    package.argumentPackages.push_back(ap);

    EXPECT_FALSE(core::audit::PackageHasLegacyStrategyEncoding(package));
    EXPECT_FALSE(core::audit::NormalizeStrategyEncoding(package));
}

// ---- End-to-end: an audited project holding the legacy encoding on disk ----

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
                ("af_migrate_" + tag + "_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

void WriteFile(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

struct Fixture {
    core::AssuranceProject     project;
    std::filesystem::path      sacm_abs;
    std::filesystem::path      sacm_rel;
    sacm::AssuranceCasePackage package;
    parser::AssuranceCase      model;
};

Fixture MakeFixture(const std::string& tag) {
    Fixture f;
    const auto root = MakeTempProjectRoot(tag);
    f.sacm_rel = "argument.sacm";
    WriteFile(root / f.sacm_rel, kSampleSacm);

    f.project.id = "p";
    f.project.name = "Project";
    f.project.rootPath = root;
    core::ProjectFileEntry entry;
    entry.id = "f1";
    entry.relativePath = f.sacm_rel;
    entry.role = core::ProjectFileRole::SacmArgument;
    f.project.files.push_back(entry);

    core::audit::EnsureAuditStoreResult ensure;
    std::string error;
    EXPECT_TRUE(core::audit::EnsureAuditStore(f.project, f.sacm_rel, ensure, error)) << error;

    f.sacm_abs = root / f.sacm_rel;
    auto pkg = sacm::parse_sacm(f.sacm_abs.string());
    EXPECT_TRUE(pkg.has_value());
    f.package = std::move(pkg.value());
    auto parsed = parser::parse_sacm_xml_string(kSampleSacm);
    EXPECT_TRUE(parsed.has_value());
    f.model = std::move(parsed.value());
    return f;
}

// Serialize the legacy bare-inference encoding of the strategy the bus just
// created (same element ids) as the on-disk state, simulating a project written
// before the single-inference encoding existed.
void WriteLegacyStrategyOnDisk(const std::filesystem::path& sacm_abs,
                               const std::string& strategy_id,
                               const std::string& sub1,
                               const std::string& sub2) {
    sacm::AssuranceCasePackage package;
    package.id = "AC1";
    package.name = "Sample";
    sacm::ArgumentPackage ap;
    ap.id = "AP1";
    ap.name = "Args";

    sacm::Claim g1;
    g1.id = "G1";
    g1.name = "Top goal";
    ap.claims.push_back(g1);
    sacm::ArgumentReasoning strategy;
    strategy.id = strategy_id;
    ap.argumentReasonings.push_back(strategy);
    sacm::Claim c1;
    c1.id = sub1;
    ap.claims.push_back(c1);
    sacm::Claim c2;
    c2.id = sub2;
    ap.claims.push_back(c2);

    sacm::AssertedInference bare;
    bare.id = "R_legacy_bare";
    bare.reasoning = strategy_id;
    bare.targets = {"G1"};
    ap.assertedInferences.push_back(bare);
    sacm::AssertedInference i1;
    i1.id = "R_legacy_1";
    i1.targets = {strategy_id};
    i1.sources = {sub1};
    ap.assertedInferences.push_back(i1);
    sacm::AssertedInference i2;
    i2.id = "R_legacy_2";
    i2.targets = {strategy_id};
    i2.sources = {sub2};
    ap.assertedInferences.push_back(i2);

    package.argumentPackages.push_back(ap);
    WriteFile(sacm_abs, sacm::serialize_sacm(package));
}

} // namespace

TEST(StrategyMigration, MigratesLegacyProjectAndVerifyConverges) {
    Fixture f = MakeFixture("legacy_project");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CommandContext ctx{f.model, f.package};

    // Create a strategy + two sub-goals through the current (single-inference)
    // factory so the audit log exists and replay reconstructs the new encoding.
    core::commands::CreateChildElementCommand add_strategy("G1", core::NewElementKind::Strategy);
    ASSERT_TRUE(bus->Execute(add_strategy, ctx, "tester").success);
    const std::string strategy_id = add_strategy.GeneratedId();
    core::commands::CreateChildElementCommand add_sub1(strategy_id, core::NewElementKind::Goal);
    ASSERT_TRUE(bus->Execute(add_sub1, ctx, "tester").success);
    core::commands::CreateChildElementCommand add_sub2(strategy_id, core::NewElementKind::Goal);
    ASSERT_TRUE(bus->Execute(add_sub2, ctx, "tester").success);

    // Simulate the pre-migration on-disk state: the same argument in the legacy
    // bare-inference encoding. Replay (new encoding) now diverges from disk.
    WriteLegacyStrategyOnDisk(f.sacm_abs, strategy_id, add_sub1.GeneratedId(), add_sub2.GeneratedId());

    const core::audit::ReplayVerificationResult before = core::audit::VerifyProject(f.project);
    ASSERT_TRUE(before.ran);
    EXPECT_FALSE(before.success) << "legacy on-disk encoding should diverge from replay before migration";

    // Migrate: rewrite the on-disk SACM to single-inference and promote a trusted
    // baseline at HEAD.
    core::audit::StrategyMigrationResult migration;
    ASSERT_TRUE(core::audit::MigrateStrategyEncodingIfNeeded(f.project, f.sacm_rel, migration, error)) << error;
    EXPECT_TRUE(migration.migrated);
    EXPECT_FALSE(migration.baseline_snapshot_id.empty());

    // The trusted baseline is now the replay root.
    core::audit::AuditManifest manifest;
    ASSERT_TRUE(core::audit::ReadAuditManifest(f.project.rootPath, manifest, error)) << error;
    EXPECT_EQ(manifest.replay_root_snapshot_id, migration.baseline_snapshot_id);

    // Verify now converges: replay starts from the migrated baseline and matches
    // the migrated on-disk bytes by construction.
    const core::audit::ReplayVerificationResult after = core::audit::VerifyProject(f.project);
    ASSERT_TRUE(after.ran);
    EXPECT_TRUE(after.success) << "post-migration verify should converge; diagnostics: "
                               << (after.diagnostics.empty() ? "" : after.diagnostics.front());

    // A second migration is a no-op (already single-inference).
    core::audit::StrategyMigrationResult again;
    ASSERT_TRUE(core::audit::MigrateStrategyEncodingIfNeeded(f.project, f.sacm_rel, again, error)) << error;
    EXPECT_FALSE(again.migrated);
}

TEST(StrategyMigration, SkipsWhenPathIsNotTheAuditedSacm) {
    Fixture f = MakeFixture("wrong_path");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CommandContext ctx{f.model, f.package};
    core::commands::CreateChildElementCommand add_strategy("G1", core::NewElementKind::Strategy);
    ASSERT_TRUE(bus->Execute(add_strategy, ctx, "tester").success);
    const std::string strategy_id = add_strategy.GeneratedId();
    core::commands::CreateChildElementCommand add_sub1(strategy_id, core::NewElementKind::Goal);
    ASSERT_TRUE(bus->Execute(add_sub1, ctx, "tester").success);

    WriteLegacyStrategyOnDisk(f.sacm_abs, strategy_id, add_sub1.GeneratedId(), add_sub1.GeneratedId());

    // A path that is not the manifest's current_sacm must never be migrated -- the
    // audit store does not cover it, so rewriting it would corrupt an unrelated file.
    core::audit::StrategyMigrationResult migration;
    ASSERT_TRUE(core::audit::MigrateStrategyEncodingIfNeeded(f.project, "some-other-argument.sacm",
                                                            migration, error))
        << error;
    EXPECT_FALSE(migration.migrated);
    EXPECT_TRUE(migration.baseline_snapshot_id.empty());
}
