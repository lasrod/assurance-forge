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

// The same case carrying only the vendor *element*. Preserved child elements are
// re-emitted verbatim and re-read as preserved content, so this document is a
// fixed point of save->load->save; a preserved *attribute* is not (see the
// known-gap note in SACM23_LIB_002_UnknownContentSurvivesLoadEditSaveReload).
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

    // KNOWN LIBRARY GAP, not an Assurance Forge one. The tolerant writer re-emits
    // preserved vendor *attributes* and *elements* but never re-declares the
    // foreign namespace prefix they use, so the saved file carries `acme:owner`
    // (and `<acme:vendorMetadata>`) with an undeclared prefix. A preserved child
    // element survives that anyway -- it is re-read verbatim as preserved content
    // -- but a preserved attribute is only recognized as a vendor attribute when
    // its prefix resolves, so it is lost on the NEXT load. `semantic_compare`
    // does not cover preserved_attributes, which is why the library's own
    // SACM23-COMPAT-001 round-trip test cannot see this.
    //
    // Pinned rather than asserted-as-desired so the gap stays visible: this
    // expectation fails loudly once the library declares the prefix.
    EXPECT_FALSE(Contains(resaved.xml, kVendorAttributeMarker))
        << "the library now round-trips preserved vendor ATTRIBUTES -- flip this to EXPECT_TRUE, "
           "extend SACM23_LIB_002_RepeatedSavesAreByteStable to the vendor-attribute fixture, and "
           "drop the known-gap note";
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
    // Vendor-element fixture: the preserved-attribute namespace gap noted in
    // SACM23_LIB_002_UnknownContentSurvivesLoadEditSaveReload would otherwise
    // make the save->load->save leg fail for a reason unrelated to the save path.
    ProjectFixture fixture = MakeProject("stable", kVendorElementOnlySacm);

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
