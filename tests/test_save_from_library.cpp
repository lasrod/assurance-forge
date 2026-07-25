// Phase 9 Stage 7: every save serializes the LIBRARY-OWNED document.
//
// Stage 6 routed all three save sites through `core::library_xmi_from_package`,
// which serializes the legacy `sacm::AssuranceCasePackage`, reloads it into a
// *fresh* library document and saves that. The persisted bytes were therefore
// "whatever survives the legacy-package projection" -- and the legacy structs
// have no field for unknown/foreign XML, so a tolerant load preserved vendor
// content that every subsequent save silently dropped. That is the caveat the
// conformance matrix records against SACM23-LIB-002.
//
// Now that the library is the sole load path and the live document holds every
// committed edit, the save sites serialize that document instead. These tests
// pin the resulting fidelity win, and pin the property that makes it safe: all
// save sites must produce the same bytes for the same logical state, or the
// audit manifest's raw-file hash would describe a file nobody wrote.

#include "core/app_state.h"
#include "core/audit/audit_manifest.h"
#include "core/audit/audit_recovery.h"
#include "core/audit/audit_store.h"
#include "core/audit/replay_verifier.h"
#include "core/commands/command_bus.h"
#include "core/commands/element_commands.h"
#include "core/derived_views.h"
#include "core/element_factory.h"
#include "core/library_package_projection.h"
#include "core/project_model.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/library_load.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

// A vendor attribute (`acme:owner`) and a vendor child element
// (`acme:vendorMetadata`) in a foreign namespace. The library preserves both on
// a tolerant load (SACM23-COMPAT-001); `sacm::AssuranceCasePackage` has no field
// for either, so anything routed through it drops them without trace.
constexpr const char* kVendorExtendedSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation"
    xmlns:acme="http://acme.example/toolchain" id="AC1" name="Sample" acme:owner="alice">
  <acme:vendorMetadata reviewCycle="Q3-2026"/>
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";

// The same case carrying only the vendor *element*, for the paths that only
// need to prove unknown content survives: a preserved child element is
// re-emitted and re-read verbatim, independent of the namespace-declaration
// handling the vendor *attribute* above depends on.
constexpr const char* kVendorElementOnlySacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation"
    xmlns:acme="http://acme.example/toolchain" id="AC1" name="Sample">
  <acme:vendorMetadata reviewCycle="Q3-2026"/>
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";

// The same case as edited by something that bypassed the bus and dropped the
// vendor content -- used to force the recovery path.
constexpr const char* kTamperedSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="Edited outside the bus."/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";

constexpr std::string_view kVendorAttributeMarker = "acme:owner";
constexpr std::string_view kVendorElementMarker = "vendorMetadata";

// Movable so a fixture can be returned by value; the moved-from instance clears
// its path and therefore removes nothing.
struct TempDir {
    std::filesystem::path path;
    explicit TempDir(std::filesystem::path value) : path(std::move(value)) {}
    TempDir(TempDir&& other) noexcept : path(std::move(other.path)) { other.path.clear(); }
    TempDir& operator=(TempDir&& other) noexcept {
        path = std::move(other.path);
        other.path.clear();
        return *this;
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    ~TempDir() {
        if (path.empty())
            return;
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

std::filesystem::path MakeTempDir(const std::string& tag) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path path = std::filesystem::temp_directory_path() /
                                 ("af_save_from_library_" + tag + "_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

void WriteFile(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool Contains(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

// A project whose tracked SACM file carries the vendor content above, with an
// initialized audit store so a `CommandBus` can be opened over it.
struct ProjectFixture {
    TempDir                 temp;
    core::AssuranceProject  project;
    std::filesystem::path   sacm_relative = "argument.sacm";
    std::filesystem::path   sacm_absolute;
};

// Number of non-overlapping occurrences of `needle` in `haystack`.
std::size_t CountOccurrences(std::string_view haystack, std::string_view needle) {
    std::size_t count = 0;
    for (std::size_t at = haystack.find(needle); at != std::string_view::npos;
         at = haystack.find(needle, at + needle.size())) {
        ++count;
    }
    return count;
}

ProjectFixture MakeProject(const std::string& tag, const char* sacm_xml = kVendorExtendedSacm) {
    ProjectFixture fixture{TempDir(MakeTempDir(tag))};
    WriteFile(fixture.temp.path / fixture.sacm_relative, sacm_xml);

    fixture.project.id = "p";
    fixture.project.name = "Project";
    fixture.project.rootPath = fixture.temp.path;
    core::ProjectFileEntry entry;
    entry.id = "f1";
    entry.relativePath = fixture.sacm_relative;
    entry.role = core::ProjectFileRole::SacmArgument;
    fixture.project.files.push_back(entry);

    core::audit::EnsureAuditStoreResult ensure;
    std::string error;
    EXPECT_TRUE(core::audit::EnsureAuditStore(fixture.project, fixture.sacm_relative, ensure, error))
        << error;

    fixture.sacm_absolute = fixture.temp.path / fixture.sacm_relative;
    return fixture;
}

bool HasProjectedElement(const core::AssuranceCase& projected, const std::string& id) {
    for (const core::SacmElement& element : projected.elements) {
        if (element.id == id)
            return true;
    }
    return false;
}

} // namespace

// The non-vacuity gate for every assertion below: the Stage 6 save path really
// does drop this content, so a test that finds it after a save is measuring the
// change and not a fixture that never carried anything unusual.
TEST(SaveFromLibrary, SACM23_LIB_002_LegacyPackageSavePathDropsUnknownContent) {
    ProjectFixture fixture = MakeProject("nonvacuity");

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);

    // What Stage 6 wrote: serialize the projected legacy package, reload, save.
    const sacm::AssuranceCasePackage projected =
        core::project_library_package_with_tags(*state.library_document);
    const std::optional<std::string> via_package = core::library_xmi_from_package(projected);
    ASSERT_TRUE(via_package.has_value());
    EXPECT_FALSE(Contains(*via_package, kVendorAttributeMarker))
        << "the legacy projection unexpectedly carries the vendor attribute; this test no longer "
           "measures the fidelity difference";
    EXPECT_FALSE(Contains(*via_package, kVendorElementMarker))
        << "the legacy projection unexpectedly carries the vendor element; this test no longer "
           "measures the fidelity difference";

    // What Stage 7 writes: serialize the library document itself.
    const sacm_adapter::SaveOutcome via_document = sacm_adapter::save_document(*state.library_document);
    ASSERT_TRUE(via_document.ok);
    EXPECT_TRUE(Contains(via_document.xml, kVendorAttributeMarker));
    EXPECT_TRUE(Contains(via_document.xml, kVendorElementMarker));
}

// The fidelity win, end to end through the application's own paths: load, edit
// through the audited bus (whose autosave is save site #2), save explicitly
// (save site #1), reload. Content the legacy structs cannot model must survive
// all of it -- and the edit must survive with it.
TEST(SaveFromLibrary, SACM23_LIB_002_UnknownContentSurvivesLoadEditSaveReload) {
    ProjectFixture fixture = MakeProject("survives");

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);
    ASSERT_TRUE(state.loaded_case.has_value());
    ASSERT_TRUE(state.sacm_package.has_value());

    std::string error;
    std::unique_ptr<core::commands::CommandBus> bus =
        core::commands::CommandBus::Open(fixture.project, fixture.sacm_absolute, error);
    ASSERT_TRUE(bus) << error;

    core::commands::CreateChildElementCommand command("G1", core::NewElementKind::Goal);
    core::commands::CommandContext ctx{state.loaded_case.value(), state.sacm_package.value(),
                                       state.library_document.get()};
    const core::commands::CommandResult result = bus->Execute(command, ctx, "tester");
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_TRUE(result.error.empty()) << result.error;
    ASSERT_TRUE(ctx.library_primary) << "the create must be library-primary for the library to hold "
                                        "the edit at autosave time";
    ASSERT_FALSE(command.GeneratedId().empty());

    // Save site #2 (bus autosave) wrote the tracked file.
    const std::string autosaved = ReadFile(fixture.sacm_absolute);
    EXPECT_TRUE(Contains(autosaved, kVendorAttributeMarker)) << "autosave dropped the vendor attribute";
    EXPECT_TRUE(Contains(autosaved, kVendorElementMarker)) << "autosave dropped the vendor element";

    // The runtime re-derives the legacy views from the library at the next frame
    // boundary after a flipped command; mirror that before saving explicitly.
    core::RebuildDerivedViewsFromLibrary(*state.library_document, state.loaded_case.value(),
                                         state.sacm_package.value());

    // Save site #1 (explicit save).
    const std::filesystem::path explicit_path = fixture.temp.path / "explicit-save.sacm";
    ASSERT_TRUE(state.save_file(explicit_path.string())) << state.status_message;
    const std::string explicitly_saved = ReadFile(explicit_path);
    EXPECT_TRUE(Contains(explicitly_saved, kVendorAttributeMarker))
        << "explicit save dropped the vendor attribute";
    EXPECT_TRUE(Contains(explicitly_saved, kVendorElementMarker))
        << "explicit save dropped the vendor element";

    // Reloading the autosaved file through the application yields both the edit
    // and the preserved content -- so the content is not merely echoed into the
    // bytes, it is round-tripping through the model.
    core::AppState reopened;
    ASSERT_TRUE(reopened.load_file(fixture.sacm_absolute.string())) << reopened.status_message;
    ASSERT_NE(reopened.library_document, nullptr);
    const core::AssuranceCase reprojected = sacm_adapter::project_case(*reopened.library_document);
    EXPECT_TRUE(HasProjectedElement(reprojected, "G1"));
    EXPECT_TRUE(HasProjectedElement(reprojected, command.GeneratedId()))
        << "the audited edit did not survive the save";

    const sacm_adapter::SaveOutcome resaved = sacm_adapter::save_document(*reopened.library_document);
    ASSERT_TRUE(resaved.ok);
    EXPECT_TRUE(Contains(resaved.xml, kVendorElementMarker))
        << "the vendor element did not round-trip back into the model";

    // A preserved vendor *attribute* is the harder half: unlike a preserved
    // child element (re-read verbatim as preserved content), an attribute is
    // only recognized as foreign once its prefix resolves. The library now
    // re-declares the foreign namespaces a document arrived with, so the
    // attribute survives this second save instead of vanishing on the reload
    // that precedes it (SACM23-COMPAT-001).
    EXPECT_TRUE(Contains(resaved.xml, kVendorAttributeMarker))
        << "the vendor attribute did not round-trip back into the model";
    EXPECT_TRUE(Contains(resaved.xml, "xmlns:acme=\"http://acme.example/toolchain\""))
        << "the vendor attribute is re-emitted under an undeclared prefix";
}

// The three save sites must agree byte-for-byte for the same logical state.
// The bus records `Sha256(autosaved bytes)` as `manifest.last_known_raw_file_hash`;
// if an explicit save of the same state produced different bytes, that cached
// hash would describe a file that no longer exists on disk.
TEST(SaveFromLibrary, SACM23_LIB_002_AutosaveAndExplicitSaveProduceIdenticalBytes) {
    ProjectFixture fixture = MakeProject("agree");

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);

    std::string error;
    std::unique_ptr<core::commands::CommandBus> bus =
        core::commands::CommandBus::Open(fixture.project, fixture.sacm_absolute, error);
    ASSERT_TRUE(bus) << error;

    core::commands::CreateChildElementCommand command("G1", core::NewElementKind::Solution);
    core::commands::CommandContext ctx{state.loaded_case.value(), state.sacm_package.value(),
                                       state.library_document.get()};
    const core::commands::CommandResult result = bus->Execute(command, ctx, "tester");
    ASSERT_TRUE(result.success) << result.error;

    core::RebuildDerivedViewsFromLibrary(*state.library_document, state.loaded_case.value(),
                                         state.sacm_package.value());

    const std::filesystem::path explicit_path = fixture.temp.path / "explicit-save.sacm";
    ASSERT_TRUE(state.save_file(explicit_path.string())) << state.status_message;

    EXPECT_EQ(ReadFile(fixture.sacm_absolute), ReadFile(explicit_path))
        << "the autosave and explicit-save sites disagree for the same state";

    // And the raw hash the bus cached is the hash of what is actually on disk.
    core::audit::AuditManifest manifest;
    ASSERT_TRUE(core::audit::ReadAuditManifest(fixture.project.rootPath, manifest, error)) << error;
    EXPECT_EQ(manifest.last_known_raw_file_hash, result.raw_file_hash_after);
}

// Saving the same document twice must produce identical bytes. Without this the
// autosave would rewrite the tracked file (and invalidate the manifest's raw
// hash) on every save, and the byte-agreement property above would be accidental.
TEST(SaveFromLibrary, SACM23_LIB_002_RepeatedSavesAreByteStable) {
    // The vendor-*attribute* fixture: its foreign namespace declaration has to
    // be re-emitted for the save->load->save leg to be a fixed point, so this
    // covers the harder of the two preservation paths.
    ProjectFixture fixture = MakeProject("stable", kVendorExtendedSacm);

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);

    const std::filesystem::path first = fixture.temp.path / "save-1.sacm";
    const std::filesystem::path second = fixture.temp.path / "save-2.sacm";
    ASSERT_TRUE(state.save_file(first.string())) << state.status_message;
    ASSERT_TRUE(state.save_file(second.string())) << state.status_message;
    const std::string first_bytes = ReadFile(first);
    EXPECT_EQ(first_bytes, ReadFile(second)) << "two saves of one document differ";

    // Stable across a reload too: loading the saved bytes and saving them again
    // is a fixed point, which is what keeps the manifest's raw hash valid after
    // the file has been reopened.
    core::AppState reopened;
    ASSERT_TRUE(reopened.load_file(first.string())) << reopened.status_message;
    const std::filesystem::path third = fixture.temp.path / "save-3.sacm";
    ASSERT_TRUE(reopened.save_file(third.string())) << reopened.status_message;
    EXPECT_EQ(first_bytes, ReadFile(third)) << "save -> load -> save is not a fixed point";
}

// Save site #3: the audit restore replays into a LIBRARY DOCUMENT, so it must
// serialize that document rather than a package projected from it. Otherwise a
// recovery would quietly strip the unknown content the live save preserves --
// a restored file would differ in kind from an autosaved one, and "restore from
// audit" would become a data-losing operation.
TEST(SaveFromLibrary, SACM23_LIB_002_RestoreFromAuditPreservesUnknownContent) {
    ProjectFixture fixture = MakeProject("restore", kVendorElementOnlySacm);

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);

    std::string error;
    std::unique_ptr<core::commands::CommandBus> bus =
        core::commands::CommandBus::Open(fixture.project, fixture.sacm_absolute, error);
    ASSERT_TRUE(bus) << error;

    core::commands::CreateChildElementCommand command("G1", core::NewElementKind::Goal);
    core::commands::CommandContext ctx{state.loaded_case.value(), state.sacm_package.value(),
                                       state.library_document.get()};
    ASSERT_TRUE(bus->Execute(command, ctx, "tester").success);

    // Something outside the bus overwrites the file, losing both the edit and
    // the vendor content; the verifier sees the divergence.
    WriteFile(fixture.sacm_absolute, kTamperedSacm);
    const core::audit::ReplayVerificationResult before = core::audit::VerifyProject(fixture.project);
    ASSERT_TRUE(before.ran);
    ASSERT_FALSE(before.success);

    core::audit::RestoreSacmFromAuditResult restored;
    ASSERT_TRUE(core::audit::RestoreSacmFromAudit(fixture.project, fixture.sacm_relative, "tester",
                                                  restored, error))
        << error;

    // The restore took the library-document path, not the lossy projection
    // fallback -- which is what makes the preservation assertion below meaningful.
    EXPECT_TRUE(restored.lossy_fallback_warning.empty()) << restored.lossy_fallback_warning;

    const std::string restored_bytes = ReadFile(fixture.sacm_absolute);
    EXPECT_TRUE(Contains(restored_bytes, kVendorElementMarker))
        << "the restore dropped the vendor content the trusted-root snapshot carried";

    // The restored file reloads with both the replayed edit and the vendor
    // content, and the project verifies clean afterwards.
    core::AppState reopened;
    ASSERT_TRUE(reopened.load_file(fixture.sacm_absolute.string())) << reopened.status_message;
    ASSERT_NE(reopened.library_document, nullptr);
    EXPECT_TRUE(HasProjectedElement(sacm_adapter::project_case(*reopened.library_document),
                                    command.GeneratedId()));

    const core::audit::ReplayVerificationResult after = core::audit::VerifyProject(fixture.project);
    EXPECT_TRUE(after.success) << (after.diagnostics.empty() ? "" : after.diagnostics.front());
}

// Byte stability on the repository's real assurance cases, not just a synthetic
// fixture. These carry terminology, artifact packages and ACP tagged values, so
// they exercise far more of the writer than the vendor fixtures above -- and if
// two saves of one document differed here, every autosave would rewrite the
// tracked file and invalidate the manifest's raw hash.
TEST(SaveFromLibrary, SACM23_LIB_002_RepeatedSavesAreByteStableForRepositoryCases) {
    const std::vector<std::string> cases = {
        "data/open-autonomy-safety-case.sacm.xml",
        "data/oasc-ja.xml",
        "tests/data/fixture_acp_parity.sacm.xml",
        "tests/data/fixture_roundtrip_open_autonomy.sacm.xml",
    };

    TempDir temp(MakeTempDir("repo_cases"));
    for (const std::string& relative : cases) {
        SCOPED_TRACE(relative);
        const std::filesystem::path source = std::filesystem::path(AF_REPO_ROOT) / relative;
        ASSERT_TRUE(std::filesystem::exists(source)) << source.string();

        core::AppState state;
        ASSERT_TRUE(state.load_file(source.string())) << state.status_message;
        ASSERT_NE(state.library_document, nullptr);

        const std::filesystem::path first = temp.path / "first.sacm";
        const std::filesystem::path second = temp.path / "second.sacm";
        ASSERT_TRUE(state.save_file(first.string())) << state.status_message;
        ASSERT_TRUE(state.save_file(second.string())) << state.status_message;
        const std::string first_bytes = ReadFile(first);
        ASSERT_FALSE(first_bytes.empty());
        EXPECT_EQ(first_bytes, ReadFile(second)) << "two saves of one document differ";

        core::AppState reopened;
        ASSERT_TRUE(reopened.load_file(first.string())) << reopened.status_message;
        const std::filesystem::path third = temp.path / "third.sacm";
        ASSERT_TRUE(reopened.save_file(third.string())) << reopened.status_message;
        EXPECT_EQ(first_bytes, ReadFile(third)) << "save -> load -> save is not a fixed point";
    }
}

// --- Bridged commands must preserve as much as native-seam commands ---------
//
// The tests above use CreateChildElementCommand, which reaches the library
// through a NATIVE seam. Most audited commands do not: text edits, terminology,
// ACP CRUD, package removal and tree reorder all route through
// `core::commands::BridgeLegacyMutationToLibrary`, which rebuilds the live
// document from a projection of it. Whether that projection is faithful decides
// whether "the library is the serialization source of truth" is true for the
// commands users actually run most.
//
// It was not. The bridge used `core::project_library_package` -- the AUDIT
// projection, which by its own contract "may collapse packages as long as it
// does so consistently on both audit sides" and never calls
// `copy_library_tags_onto_package`. So one rename dropped every vendor
// TaggedValue from the document and from the saved file, and a case with
// several argument packages collapsed into one, duplicating ids until the
// re-derive was rejected outright.
//
// The convergence tests could not see any of this: they compare canonical
// hashes computed through that same collapsing, tag-dropping projection on both
// sides, so the loss is invisible by construction. These assert on the saved
// BYTES instead.

namespace {

// A bridged edit: `UpdateElementTextCommand` goes through
// ApplyLibraryPrimaryOrLegacy, not through a native library operation.
core::commands::CommandResult RunBridgedRename(ProjectFixture& fixture, core::AppState& state,
                                               const std::string& element_id,
                                               const std::string& new_name,
                                               bool& out_library_primary) {
    std::string error;
    std::unique_ptr<core::commands::CommandBus> bus =
        core::commands::CommandBus::Open(fixture.project, fixture.sacm_absolute, error);
    EXPECT_TRUE(bus) << error;
    if (!bus) {
        return core::commands::CommandResult{};
    }
    core::commands::UpdateElementTextCommand command(element_id, core::ElementTextField::Name, "en",
                                                     new_name);
    core::commands::CommandContext ctx{state.loaded_case.value(), state.sacm_package.value(),
                                       state.library_document.get()};
    const core::commands::CommandResult result = bus->Execute(command, ctx, "tester");
    out_library_primary = ctx.library_primary;
    return result;
}

} // namespace

TEST(SaveFromLibrary, SACM23_LIB_002_BridgedEditPreservesUnknownContent) {
    ProjectFixture fixture = MakeProject("bridged-vendor");

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);

    bool library_primary = false;
    const core::commands::CommandResult result =
        RunBridgedRename(fixture, state, "G1", "Renamed goal", library_primary);
    ASSERT_TRUE(result.success) << result.error;
    ASSERT_TRUE(library_primary)
        << "the rename did not take the bridged library-primary path; this test measures nothing";

    const std::string autosaved = ReadFile(fixture.sacm_absolute);
    EXPECT_TRUE(Contains(autosaved, kVendorAttributeMarker))
        << "a bridged edit dropped the vendor attribute:\n" << autosaved;
    EXPECT_TRUE(Contains(autosaved, kVendorElementMarker))
        << "a bridged edit dropped the vendor element:\n" << autosaved;
    EXPECT_TRUE(Contains(autosaved, "Renamed goal")) << autosaved;
}

TEST(SaveFromLibrary, SACM23_LIB_002_BridgedEditPreservesAcpTaggedValues) {
    const std::string acp_case =
        ReadFile(std::filesystem::path(AF_REPO_ROOT) / "tests" / "data" /
                 "fixture_acp_parity.sacm.xml");
    ASSERT_FALSE(acp_case.empty());
    ProjectFixture fixture = MakeProject("bridged-acp", acp_case.c_str());

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);

    const std::size_t before = CountOccurrences(ReadFile(fixture.sacm_absolute), "assuranceForge.acp");
    ASSERT_GT(before, 0u) << "fixture carries no ACP tags; this test measures nothing";

    bool library_primary = false;
    const core::commands::CommandResult result =
        RunBridgedRename(fixture, state, "G1", "Renamed top goal", library_primary);
    ASSERT_TRUE(result.success) << result.error;
    ASSERT_TRUE(library_primary);

    const std::string autosaved = ReadFile(fixture.sacm_absolute);
    EXPECT_EQ(CountOccurrences(autosaved, "assuranceForge.acp"), before)
        << "a bridged rename destroyed Assurance Claim Points -- they are vendor TaggedValues, "
           "and the projection the bridge round-trips through does not carry them";
    EXPECT_TRUE(Contains(autosaved, "Renamed top goal")) << autosaved;
}

// The repository's flagship case has four argument packages. The audit
// projection collapses them into one, which duplicates artifact-reference ids
// and makes the bridge's re-derive fail outright -- so before this, no bridged
// command could be run against it at all.
TEST(SaveFromLibrary, SACM23_LIB_002_BridgedEditSucceedsOnMultiArgumentPackageCase) {
    const std::string big_case =
        ReadFile(std::filesystem::path(AF_REPO_ROOT) / "data" / "open-autonomy-safety-case.sacm.xml");
    ASSERT_FALSE(big_case.empty());
    ProjectFixture fixture = MakeProject("bridged-multipackage", big_case.c_str());

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);

    const std::size_t packages_before = state.sacm_package->argumentPackages.size();
    ASSERT_GT(packages_before, 1u)
        << "fixture no longer has several argument packages; this test measures nothing";

    // Rename whatever the first claim is, so the test does not depend on the
    // case's content beyond its package structure.
    std::string target_id;
    for (const core::SacmElement& element : state.loaded_case->elements) {
        if (element.type == "claim") {
            target_id = element.id;
            break;
        }
    }
    ASSERT_FALSE(target_id.empty());

    bool library_primary = false;
    const core::commands::CommandResult result =
        RunBridgedRename(fixture, state, target_id, "Renamed", library_primary);
    ASSERT_TRUE(result.success)
        << "a bridged edit failed on a multi-argument-package case: " << result.error;
    ASSERT_TRUE(library_primary);

    core::RebuildDerivedViewsFromLibrary(*state.library_document, state.loaded_case.value(),
                                         state.sacm_package.value());
    EXPECT_EQ(state.sacm_package->argumentPackages.size(), packages_before)
        << "the bridged edit collapsed the case's argument packages";
}

// --- Restore-from-audit must preserve as much as the live path -------------
//
// SACM23_LIB_002_RestoreFromAuditPreservesUnknownContent above passes only
// because CreateChildElement replays through a NATIVE seam. The replayer has a
// bridged path too, and it was a second copy of the live bridge: when the live
// one was fixed, the copy kept the defect and relocated it to the restore site,
// where a recovery silently destroyed every Assurance Claim Point and reported
// no degradation at all -- the canonical hash drops the same tags on both sides,
// so neither the verifier nor the warning could see it.
//
// These replay a CONTENT edit, which is bridged on the replay side (Name is
// native there, so it would not exercise this).

namespace {

// Runs a bridged content edit through the bus, tampers with the file so the
// verifier sees divergence, and restores from the audit log. Returns the
// restored bytes.
std::string EditTamperAndRestore(ProjectFixture& fixture, core::AppState& state,
                                 const std::string& element_id,
                                 core::audit::RestoreSacmFromAuditResult& restored,
                                 std::string& error) {
    std::unique_ptr<core::commands::CommandBus> bus =
        core::commands::CommandBus::Open(fixture.project, fixture.sacm_absolute, error);
    EXPECT_TRUE(bus) << error;
    if (!bus) {
        return {};
    }
    core::commands::UpdateElementTextCommand command(element_id, core::ElementTextField::Content,
                                                     "en", "Edited through the bridge.");
    core::commands::CommandContext ctx{state.loaded_case.value(), state.sacm_package.value(),
                                       state.library_document.get()};
    const core::commands::CommandResult result = bus->Execute(command, ctx, "tester");
    EXPECT_TRUE(result.success) << result.error;
    if (!result.success) {
        return {};
    }
    bus.reset();

    WriteFile(fixture.sacm_absolute, kTamperedSacm);
    if (!core::audit::RestoreSacmFromAudit(fixture.project, fixture.sacm_relative, "tester",
                                           restored, error)) {
        return {};
    }
    return ReadFile(fixture.sacm_absolute);
}

} // namespace

TEST(SaveFromLibrary, SACM23_LIB_002_RestoreAfterBridgedEditPreservesAcpTaggedValues) {
    const std::string acp_case = ReadFile(std::filesystem::path(AF_REPO_ROOT) / "tests" / "data" /
                                          "fixture_acp_parity.sacm.xml");
    ASSERT_FALSE(acp_case.empty());
    ProjectFixture fixture = MakeProject("restore-acp", acp_case.c_str());

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    const std::size_t before =
        CountOccurrences(ReadFile(fixture.sacm_absolute), "assuranceForge.acp");
    ASSERT_GT(before, 0u) << "fixture carries no ACP tags; this test measures nothing";

    core::audit::RestoreSacmFromAuditResult restored;
    std::string error;
    const std::string bytes = EditTamperAndRestore(fixture, state, "G1", restored, error);
    ASSERT_FALSE(bytes.empty()) << error;
    EXPECT_TRUE(restored.lossy_fallback_warning.empty()) << restored.lossy_fallback_warning;

    EXPECT_EQ(CountOccurrences(bytes, "assuranceForge.acp"), before)
        << "restoring from the audit log destroyed Assurance Claim Points -- and reported no "
           "degradation while doing it";
}

TEST(SaveFromLibrary, SACM23_LIB_002_RestoreAfterBridgedEditPreservesUnknownContent) {
    ProjectFixture fixture = MakeProject("restore-vendor");

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;

    core::audit::RestoreSacmFromAuditResult restored;
    std::string error;
    const std::string bytes = EditTamperAndRestore(fixture, state, "G1", restored, error);
    ASSERT_FALSE(bytes.empty()) << error;
    EXPECT_TRUE(restored.lossy_fallback_warning.empty()) << restored.lossy_fallback_warning;

    EXPECT_TRUE(Contains(bytes, kVendorAttributeMarker))
        << "the restore dropped the vendor attribute:\n" << bytes;
    EXPECT_TRUE(Contains(bytes, kVendorElementMarker))
        << "the restore dropped the vendor element:\n" << bytes;
}

// A project whose log contains one bridged content edit must be recoverable at
// all. Before the fix the replay aborted on this case -- the audit projection
// collapsed its four argument packages and duplicated artifact-reference ids, so
// the re-derive was rejected and the flagship case could not be restored.
TEST(SaveFromLibrary, SACM23_LIB_002_RestoreAfterBridgedEditSucceedsOnMultiArgumentPackageCase) {
    const std::string big_case = ReadFile(std::filesystem::path(AF_REPO_ROOT) / "data" /
                                          "open-autonomy-safety-case.sacm.xml");
    ASSERT_FALSE(big_case.empty());
    ProjectFixture fixture = MakeProject("restore-multipackage", big_case.c_str());

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;

    std::string target_id;
    for (const core::SacmElement& element : state.loaded_case->elements) {
        if (element.type == "claim") {
            target_id = element.id;
            break;
        }
    }
    ASSERT_FALSE(target_id.empty());

    core::audit::RestoreSacmFromAuditResult restored;
    std::string error;
    const std::string bytes = EditTamperAndRestore(fixture, state, target_id, restored, error);
    ASSERT_FALSE(bytes.empty())
        << "a project with one bridged content edit in its log could not be restored: " << error;

    core::AppState reopened;
    ASSERT_TRUE(reopened.load_file(fixture.sacm_absolute.string())) << reopened.status_message;
    EXPECT_GT(reopened.sacm_package->argumentPackages.size(), 1u)
        << "the restore collapsed the case's argument packages";
}

// The self-rebuild pattern outside the bridge. `AppState::sync_library_document`
// and the command bus's Stage-5 net both re-derive the library document from a
// projection OF THAT DOCUMENT -- the same shape as the bridge, with the same
// consequence, because the legacy package has no field for unknown XML.
//
// The reachable case needs no project at all: a SACM file opened standalone
// takes the no-bus dispatch branch, which calls `sync_library_document` after
// every command. One edit erased the preserved vendor content from the document,
// and the next save wrote the degraded version to disk.
TEST(SaveFromLibrary, SACM23_INT_001_NoBusEditPreservesUnknownContentThroughSync) {
    ProjectFixture fixture = MakeProject("nobus-sync");

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);

    // Mutate the legacy package the way the no-bus path does, then sync.
    std::string discarded;
    std::string error;
    ASSERT_TRUE(core::SetElementTextField(state.loaded_case.value(), &state.sacm_package.value(),
                                          "G1", core::ElementTextField::Name, "en",
                                          "Renamed without a bus", discarded, error))
        << error;
    state.sync_library_document();

    // The document is what every save site serializes, so the loss would reach
    // disk on the next save whether or not it is visible in memory first.
    const std::filesystem::path saved_path = fixture.temp.path / "after-sync.sacm";
    ASSERT_TRUE(state.save_file(saved_path.string())) << state.status_message;
    const std::string bytes = ReadFile(saved_path);

    EXPECT_TRUE(Contains(bytes, kVendorAttributeMarker))
        << "syncing the library from the legacy package dropped the vendor attribute:\n" << bytes;
    EXPECT_TRUE(Contains(bytes, kVendorElementMarker))
        << "syncing the library from the legacy package dropped the vendor element:\n" << bytes;
    EXPECT_TRUE(Contains(bytes, "Renamed without a bus")) << bytes;
}

// The Stage-5 net: the branch that re-derives the library document after an
// UNFLIPPED command (`!library_synced && !library_primary`) -- a NodeOnly
// removal, an undo, an unseamed command. It is the fourth instance of the
// self-rebuild pattern, and the only one no existing assertion could see: the
// single test that reaches the branch compares canonical hashes, and those drop
// preserved content on BOTH sides, so a lossy reload there is invisible.
//
// Asserted through an explicit save, because that is what serializes the
// library document. (The bus's own autosave for an unflipped command writes
// projection bytes, which is a separate, disclosed limitation -- this test is
// about whether the in-memory document survived the re-derive.)
TEST(SaveFromLibrary, SACM23_INT_001_UnflippedBusCommandPreservesUnknownContentInTheDocument) {
    ProjectFixture fixture = MakeProject("unflipped-net");

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);

    std::string error;
    std::unique_ptr<core::commands::CommandBus> bus =
        core::commands::CommandBus::Open(fixture.project, fixture.sacm_absolute, error);
    ASSERT_TRUE(bus) << error;

    // NodeOnly reparents rather than deletes, so it has no library seam and
    // stays unflipped -- which is exactly the branch under test.
    core::commands::RemoveElementCommand command("G1", core::RemoveMode::NodeOnly);
    core::commands::CommandContext ctx{state.loaded_case.value(), state.sacm_package.value(),
                                       state.library_document.get()};
    const core::commands::CommandResult result = bus->Execute(command, ctx, "tester");
    ASSERT_TRUE(result.success) << result.error;
    ASSERT_FALSE(ctx.library_primary)
        << "the command flipped, so this test never reaches the Stage-5 net it exists to cover";

    // The net has re-derived `library_document` from the legacy package. If it
    // used the plain reload, the document no longer carries the vendor content
    // and the next explicit save -- which serializes the document -- writes it
    // out degraded.
    const std::filesystem::path explicit_path = fixture.temp.path / "after-unflipped.sacm";
    ASSERT_TRUE(state.save_file(explicit_path.string())) << state.status_message;
    const std::string bytes = ReadFile(explicit_path);

    EXPECT_TRUE(Contains(bytes, kVendorAttributeMarker))
        << "the Stage-5 net dropped the vendor attribute from the library document:\n" << bytes;
    EXPECT_TRUE(Contains(bytes, kVendorElementMarker))
        << "the Stage-5 net dropped the vendor element from the library document:\n" << bytes;
}

// A load can SUCCEED and still have told us something the user must know. The
// sharpest case: an ODE container embedding SACM opens fine, so nothing on
// screen suggests the rest of the file is about to disappear on save. The
// library reports SACM-XMI-009; AppState used to discard every diagnostic on
// the success path, so the warning existed and reached nobody.
TEST(SaveFromLibrary, SACM23_INT_001_LoadSurfacesNonConformanceWarningToTheUser) {
    const std::filesystem::path container = std::filesystem::path(AF_REPO_ROOT) / "libs" / "sacm" /
                                            "tests" / "data" / "interop-thirdparty" /
                                            "mobstr-safetycase.integration";
    ASSERT_TRUE(std::filesystem::exists(container)) << container.string();

    core::AppState state;
    ASSERT_TRUE(state.load_file(container.string())) << state.status_message;

    // The user is told, in the status line they already read after every open.
    EXPECT_TRUE(Contains(state.status_message, "SACM-XMI-009"))
        << "the non-conformance warning did not reach the status line: " << state.status_message;
    EXPECT_TRUE(Contains(state.status_message, "does not conform")) << state.status_message;

    ASSERT_FALSE(state.load_warnings.empty());
    EXPECT_TRUE(std::any_of(state.load_warnings.begin(), state.load_warnings.end(),
                            [](const std::string& warning) {
                                return warning.find("SACM-XMI-009") != std::string::npos;
                            }));

    // A conformant file must not be decorated with warnings it did not earn --
    // a status line that always says something alarming says nothing.
    ProjectFixture clean = MakeProject("clean-load", kVendorElementOnlySacm);
    core::AppState ordinary;
    ASSERT_TRUE(ordinary.load_file(clean.sacm_absolute.string())) << ordinary.status_message;
    EXPECT_FALSE(Contains(ordinary.status_message, "SACM-XMI-009")) << ordinary.status_message;

    // And the warning is cleared by the next load rather than persisting.
    EXPECT_TRUE(ordinary.load_warnings.empty() ||
                std::none_of(ordinary.load_warnings.begin(), ordinary.load_warnings.end(),
                             [](const std::string& warning) {
                                 return warning.find("SACM-XMI-009") != std::string::npos;
                             }));
}
