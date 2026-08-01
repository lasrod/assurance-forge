#include "core/audit/strategy_migration.h"

#include "core/audit/audit_manifest.h"
#include "core/audit/audit_paths.h"
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
#include <iterator>
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

    sacm::AssertedInference bare; // {reasoning=S1, target=G1, no source}
    bare.id = "R_bare";
    bare.reasoning = "S1";
    bare.targets = {"G1"};
    ap.assertedInferences.push_back(bare);

    sacm::AssertedInference ia; // {target=S1, source=A}
    ia.id = "R_A";
    ia.targets = {"S1"};
    ia.sources = {"A"};
    ap.assertedInferences.push_back(ia);

    sacm::AssertedInference ib; // {target=S1, source=B}
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
    core::AssuranceProject project;
    std::filesystem::path sacm_abs;
    std::filesystem::path sacm_rel;
    sacm::AssuranceCasePackage package;
    parser::AssuranceCase model;
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

constexpr std::string_view kVendorNamespace = "http://acme.example/toolchain";
constexpr std::string_view kVendorElementMarker = "vendorMetadata";
constexpr std::string_view kVendorAttributeMarker = "Q3-2026";

// The same legacy on-disk state, plus a foreign-namespace element on the root
// package. The legacy serializer has no way to emit one, so it is injected into
// its output -- which keeps the surrounding structure exactly what the legacy
// writer produces and adds only the content a tolerant library load preserves
// and no POD projection can carry.
void WriteLegacyStrategyOnDiskWithVendorContent(const std::filesystem::path& sacm_abs,
                                                const std::string& strategy_id,
                                                const std::string& sub1,
                                                const std::string& sub2) {
    WriteLegacyStrategyOnDisk(sacm_abs, strategy_id, sub1, sub2);

    std::ifstream in(sacm_abs, std::ios::binary);
    std::string xml((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    // The root element name carries whatever namespace prefix the package had,
    // so anchor on the class name rather than on a spelling.
    const std::size_t root = xml.find("AssuranceCasePackage");
    ASSERT_NE(root, std::string::npos) << "legacy serializer output changed shape";
    const std::size_t root_end = xml.find('>', root);
    ASSERT_NE(root_end, std::string::npos);

    xml.insert(root_end, " xmlns:acme=\"" + std::string(kVendorNamespace) + "\"");
    const std::size_t insert_at = xml.find('>', root) + 1;
    xml.insert(insert_at, "\n  <acme:vendorMetadata reviewCycle=\"Q3-2026\"/>");
    WriteFile(sacm_abs, xml);
}

std::string ReadFileText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
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

// The migration rewrites the TRACKED WORKING FILE and promotes a trusted
// baseline that becomes the replay root. It runs silently at project open, so
// anything it drops is gone before the user sees the case -- and unrecoverable
// from inside the app, because restore-from-audit replays from the promoted
// baseline and the baseline is also an undo wall.
//
// It holds a library document (it loads one to read the file) and must serialize
// THAT for both writes. Routing either write through a POD projection
// (`core::library_xmi_from_package` / `sacm::serialize_sacm`) destroys the
// unknown/foreign XML only a tolerant load preserves.
//
// Asserted on the BYTES of both artifacts. A canonical-hash assertion cannot see
// this: the hash re-projects through the tagless projection on both sides and
// drops the same content twice.
TEST(StrategyMigration, SACM23_LIB_002_StrategyMigrationPreservesUnknownContent) {
    Fixture f = MakeFixture("vendor_content");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CommandContext ctx{f.model, f.package};

    core::commands::CreateChildElementCommand add_strategy("G1", core::NewElementKind::Strategy);
    ASSERT_TRUE(bus->Execute(add_strategy, ctx, "tester").success);
    const std::string strategy_id = add_strategy.GeneratedId();
    core::commands::CreateChildElementCommand add_sub1(strategy_id, core::NewElementKind::Goal);
    ASSERT_TRUE(bus->Execute(add_sub1, ctx, "tester").success);
    core::commands::CreateChildElementCommand add_sub2(strategy_id, core::NewElementKind::Goal);
    ASSERT_TRUE(bus->Execute(add_sub2, ctx, "tester").success);

    WriteLegacyStrategyOnDiskWithVendorContent(f.sacm_abs, strategy_id, add_sub1.GeneratedId(), add_sub2.GeneratedId());

    // Non-vacuity: the pre-migration file really does carry the vendor content,
    // so a pass cannot come from never having had anything to lose.
    const std::string before = ReadFileText(f.sacm_abs);
    ASSERT_NE(before.find(kVendorElementMarker), std::string::npos);
    ASSERT_NE(before.find(kVendorAttributeMarker), std::string::npos);

    core::audit::StrategyMigrationResult migration;
    ASSERT_TRUE(core::audit::MigrateStrategyEncodingIfNeeded(f.project, f.sacm_rel, migration, error)) << error;
    ASSERT_TRUE(migration.migrated) << "nothing was migrated, so this test proves nothing";
    ASSERT_FALSE(migration.baseline_snapshot_id.empty());

    // (1) The tracked working file.
    const std::string migrated = ReadFileText(f.sacm_abs);
    EXPECT_NE(migrated.find(kVendorElementMarker), std::string::npos)
        << "the migration destroyed the preserved vendor element in the working file";
    EXPECT_NE(migrated.find(kVendorAttributeMarker), std::string::npos)
        << "the migration destroyed the preserved vendor attribute in the working file";
    EXPECT_NE(migrated.find(kVendorNamespace), std::string::npos)
        << "the migrated file does not declare the foreign namespace, so the next load drops it";

    // (2) The promoted trusted baseline -- the replay root, and the floor undo
    // and restore-from-audit can reach. If the content is missing here it is
    // unrecoverable even though the working file has it.
    const std::string baseline =
        ReadFileText(core::audit::SnapshotSacmPath(f.project.rootPath, migration.baseline_snapshot_id));
    ASSERT_FALSE(baseline.empty());
    EXPECT_NE(baseline.find(kVendorElementMarker), std::string::npos)
        << "the promoted baseline lost the vendor element; restore-from-audit cannot recover it";
    EXPECT_NE(baseline.find(kVendorAttributeMarker), std::string::npos)
        << "the promoted baseline lost the vendor attribute";
    EXPECT_NE(baseline.find(kVendorNamespace), std::string::npos)
        << "the promoted baseline does not declare the foreign namespace";

    // The migration's own reason for existing must still hold.
    const core::audit::ReplayVerificationResult after = core::audit::VerifyProject(f.project);
    ASSERT_TRUE(after.ran);
    EXPECT_TRUE(after.success) << "post-migration verify should converge; diagnostics: "
                               << (after.diagnostics.empty() ? "" : after.diagnostics.front());
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
    ASSERT_TRUE(core::audit::MigrateStrategyEncodingIfNeeded(f.project, "some-other-argument.sacm", migration, error))
        << error;
    EXPECT_FALSE(migration.migrated);
    EXPECT_TRUE(migration.baseline_snapshot_id.empty());
}
