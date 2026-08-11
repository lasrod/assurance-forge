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
#include "core/commands/tree_commands.h"
#include "core/commands/acp_commands.h"
#include "core/commands/gid_commands.h"
#include "core/commands/package_commands.h"
#include "core/commands/proposal_commands.h"
#include "core/commands/terminology_commands.h"
#include "core/derived_views.h"
#include "core/element_factory.h"
#include "core/library_package_projection.h"
#include "core/project_model.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/library_load.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <unordered_map>
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

// A counter (dialectic challenge) relationship on an otherwise ordinary case:
// `isCounter` is standard SACM 2.3 (clause 11.13), not a vendor tag, and it is
// the shape `core::AddChallenge` produces.
constexpr const char* kCounterArgumentSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
    <claim id="CG1" name="Counter goal" description="Braking distance exceeds the limit in rain."/>
    <assertedInference id="R_counter" source="CG1" target="G1" isCounter="true"/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";

// Movable so a fixture can be returned by value; the moved-from instance clears
// its path and therefore removes nothing.
struct TempDir {
    std::filesystem::path path;
    explicit TempDir(std::filesystem::path value) : path(std::move(value)) {}
    TempDir(TempDir&& other) noexcept : path(std::move(other.path)) {
        other.path.clear();
    }
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
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("af_save_from_library_" + tag + "_" + std::to_string(stamp));
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
    TempDir temp;
    core::AssuranceProject project;
    std::filesystem::path sacm_relative = "argument.sacm";
    std::filesystem::path sacm_absolute;
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
    EXPECT_TRUE(core::audit::EnsureAuditStore(fixture.project, fixture.sacm_relative, ensure, error)) << error;

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
    const sacm::AssuranceCasePackage projected = core::project_library_package_with_tags(*state.library_document);
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
    core::commands::CommandContext ctx{
        state.loaded_case.value(), state.sacm_package.value(), state.library_document.get()};
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
    core::RebuildDerivedViewsFromLibrary(
        *state.library_document, state.loaded_case.value(), state.sacm_package.value());

    // Save site #1 (explicit save).
    const std::filesystem::path explicit_path = fixture.temp.path / "explicit-save.sacm";
    ASSERT_TRUE(state.save_file(explicit_path.string())) << state.status_message;
    const std::string explicitly_saved = ReadFile(explicit_path);
    EXPECT_TRUE(Contains(explicitly_saved, kVendorAttributeMarker)) << "explicit save dropped the vendor attribute";
    EXPECT_TRUE(Contains(explicitly_saved, kVendorElementMarker)) << "explicit save dropped the vendor element";

    // Reloading the autosaved file through the application yields both the edit
    // and the preserved content -- so the content is not merely echoed into the
    // bytes, it is round-tripping through the model.
    core::AppState reopened;
    ASSERT_TRUE(reopened.load_file(fixture.sacm_absolute.string())) << reopened.status_message;
    ASSERT_NE(reopened.library_document, nullptr);
    const core::AssuranceCase reprojected = sacm_adapter::project_case(*reopened.library_document);
    EXPECT_TRUE(HasProjectedElement(reprojected, "G1"));
    EXPECT_TRUE(HasProjectedElement(reprojected, command.GeneratedId())) << "the audited edit did not survive the save";

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
    core::commands::CommandContext ctx{
        state.loaded_case.value(), state.sacm_package.value(), state.library_document.get()};
    const core::commands::CommandResult result = bus->Execute(command, ctx, "tester");
    ASSERT_TRUE(result.success) << result.error;

    core::RebuildDerivedViewsFromLibrary(
        *state.library_document, state.loaded_case.value(), state.sacm_package.value());

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
    core::commands::CommandContext ctx{
        state.loaded_case.value(), state.sacm_package.value(), state.library_document.get()};
    ASSERT_TRUE(bus->Execute(command, ctx, "tester").success);

    // Something outside the bus overwrites the file, losing both the edit and
    // the vendor content; the verifier sees the divergence.
    WriteFile(fixture.sacm_absolute, kTamperedSacm);
    const core::audit::ReplayVerificationResult before = core::audit::VerifyProject(fixture.project);
    ASSERT_TRUE(before.ran);
    ASSERT_FALSE(before.success);

    core::audit::RestoreSacmFromAuditResult restored;
    ASSERT_TRUE(core::audit::RestoreSacmFromAudit(fixture.project, fixture.sacm_relative, "tester", restored, error))
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
    EXPECT_TRUE(HasProjectedElement(sacm_adapter::project_case(*reopened.library_document), command.GeneratedId()));

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

// A NATIVE rename. `UpdateElementTextCommand` routes Name and Content through
// the `apply_text_edit` seam since the phase 3f flip; it falls back to the bridge
// only for shapes the seam cannot express. Tests that need the vendor-content
// guarantee use this; tests that need the BRIDGE use RunBridgedProposalRename.
core::commands::CommandResult RunNativeRename(ProjectFixture& fixture,
                                              core::AppState& state,
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
    core::commands::UpdateElementTextCommand command(element_id, core::ElementTextField::Name, "en", new_name);
    core::commands::CommandContext ctx{
        state.loaded_case.value(), state.sacm_package.value(), state.library_document.get()};
    const core::commands::CommandResult result = bus->Execute(command, ctx, "tester");
    out_library_primary = ctx.library_primary;
    return result;
}

// A genuinely BRIDGED edit, which keeps getting harder to come by: every routine
// command is native now, so the guard rides on a shape the seams DECLINE.
//
// This one attaches an ALREADY-EXISTING strategy to a goal. The patch service
// produces an AssertedInference whose only end is `reasoning`, and the planner
// defers that shape only when it created the strategy itself -- tagging an
// element the plan never touched would be guessing. So it declines, and the
// bridge is what runs.
//
// Re-pointed four times now (rename -> ApplyProposal -> translated rename ->
// this), each time a fallback went native. That is the intended maintenance: the
// refusal guard must outlive every bridged path and be DELETED in the same change
// as the bridge, never left passing for the wrong reason.
core::commands::CommandResult RunBridgedStrategyAttach(ProjectFixture& fixture,
                                                       core::AppState& state,
                                                       const std::string& strategy_id,
                                                       const std::string& target_id,
                                                       bool& out_library_primary) {
    std::string error;
    std::unique_ptr<core::commands::CommandBus> bus =
        core::commands::CommandBus::Open(fixture.project, fixture.sacm_absolute, error);
    EXPECT_TRUE(bus) << error;
    if (!bus) {
        return core::commands::CommandResult{};
    }
    core::reviews::ReviewProposal proposal;
    proposal.id = "bridged-refusal-probe";
    proposal.anchor_element_id = target_id;
    core::reviews::PatchOperation attach;
    attach.type = core::reviews::PatchOperationType::AddSupportedBy;
    attach.source = core::reviews::ElementRef{strategy_id, std::nullopt};
    attach.target = core::reviews::ElementRef{target_id, std::nullopt};
    proposal.operations.push_back(attach);

    core::commands::ApplyProposalCommand command(proposal);
    core::commands::CommandContext ctx{
        state.loaded_case.value(), state.sacm_package.value(), state.library_document.get()};
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
    const core::commands::CommandResult result = RunNativeRename(fixture, state, "G1", "Renamed goal", library_primary);
    ASSERT_TRUE(result.success) << result.error;
    ASSERT_TRUE(library_primary)
        << "the rename did not take the bridged library-primary path; this test measures nothing";

    const std::string autosaved = ReadFile(fixture.sacm_absolute);
    EXPECT_TRUE(Contains(autosaved, kVendorAttributeMarker)) << "a bridged edit dropped the vendor attribute:\n"
                                                             << autosaved;
    EXPECT_TRUE(Contains(autosaved, kVendorElementMarker)) << "a bridged edit dropped the vendor element:\n"
                                                           << autosaved;
    EXPECT_TRUE(Contains(autosaved, "Renamed goal")) << autosaved;
}

// A dialectic challenge is `isCounter = true` on the relationship (clause 11.13).
// It is not a vendor tag and not unknown content -- it is standard SACM 2.3 that
// the library reads and writes correctly. The bridge round-trips through the
// legacy POD, whose rebuild never copied the flag back, so a bridged edit
// re-serialized a REBUTTAL as an inference SUPPORTING the claim it attacks.
//
// That is the project's "never silently modify or reinterpret safety arguments"
// constraint, inverted on the most common edit in the application. Asserted on
// the saved bytes: the canonical hash is computed through the same projection on
// both sides and cannot see it.
TEST(SaveFromLibrary, SACM23_LIB_002_BridgedEditPreservesCounterRelationships) {
    // A counter relationship on a case the projection CAN represent -- the shape
    // the application's own dialectic-challenge feature produces. (The library's
    // argumentation-full fixture also carries one, but it carries an ArgumentGroup
    // too, so the representability guard refuses edits on it and this test would
    // measure the refusal instead of the flag.)
    ProjectFixture fixture = MakeProject("bridged-counter", kCounterArgumentSacm);

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);

    const std::size_t before = CountOccurrences(ReadFile(fixture.sacm_absolute), "isCounter");
    ASSERT_GT(before, 0u) << "fixture carries no counter relationship; this test measures nothing";

    bool library_primary = false;
    const core::commands::CommandResult result =
        RunNativeRename(fixture, state, "G1", "Renamed top claim", library_primary);
    ASSERT_TRUE(result.success) << result.error;
    ASSERT_TRUE(library_primary)
        << "the rename did not take the bridged library-primary path; this test measures nothing";

    const std::string autosaved = ReadFile(fixture.sacm_absolute);
    EXPECT_EQ(CountOccurrences(autosaved, "isCounter"), before)
        << "a bridged rename cleared isCounter -- the rebuttal is now recorded as SUPPORTING the "
           "claim it was raised against";
    EXPECT_TRUE(Contains(autosaved, "Renamed top claim")) << autosaved;
}

// Standard SACM the legacy POD can now carry but the rebuild used to drop:
// a metaClaim on a relationship (clause 11.10) and an ArgumentReasoning's
// structure reference (11.12). Both passed the element-level guard -- every
// ELEMENT was representable -- and both vanished from the saved bytes on any
// bridged edit (round-3 verification, probe b). Asserted on saved bytes, like
// isCounter above, because the canonical hash projects through the same
// rebuild on both sides and cannot see the loss.
constexpr const char* kMetaClaimStructureSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
    <claim id="CG1" name="Sub goal" description="Hazards are controlled."/>
    <claim id="MC1" name="Meta claim" description="The inference is sufficiently strong."/>
    <argumentReasoning id="AR1" name="Decomposition" structure="AP_detail"/>
    <assertedInference id="R1" source="CG1" target="G1" reasoning="AR1" metaClaim="MC1"/>
  </argumentPackage>
  <argumentPackage id="AP_detail" name="Detailed reasoning"/>
</sacm:AssuranceCasePackage>
)";

TEST(SaveFromLibrary, SACM23_LIB_002_BridgedEditPreservesMetaClaimAndReasoningStructure) {
    ProjectFixture fixture = MakeProject("bridged-metaclaim", kMetaClaimStructureSacm);

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);

    // Non-vacuity: the tracked file really carries both references.
    const std::string before = ReadFile(fixture.sacm_absolute);
    ASSERT_TRUE(Contains(before, "MC1")) << "fixture carries no metaClaim; this test measures nothing";
    ASSERT_TRUE(Contains(before, "AP_detail")) << "fixture carries no structure target; this test measures nothing";

    bool library_primary = false;
    const core::commands::CommandResult result =
        RunNativeRename(fixture, state, "G1", "Renamed top goal", library_primary);
    ASSERT_TRUE(result.success) << result.error;
    ASSERT_TRUE(library_primary)
        << "the rename did not take the bridged library-primary path; this test measures nothing";

    const std::string autosaved = ReadFile(fixture.sacm_absolute);
    EXPECT_TRUE(Contains(autosaved, "metaClaim"))
        << "a bridged rename dropped the relationship's metaClaim (clause 11.10):\n"
        << autosaved;
    EXPECT_TRUE(Contains(autosaved, "MC1")) << autosaved;
    EXPECT_TRUE(Contains(autosaved, "structure"))
        << "a bridged rename severed the reasoning's structure reference (clause 11.12):\n"
        << autosaved;
    EXPECT_TRUE(Contains(autosaved, "Renamed top goal")) << autosaved;
}

// An EMPTY nested ArgumentPackage: every element in the document is
// representable, so the old element-level guard passed -- and the bridge
// deleted the package silently, because `sacm::ArgumentPackage` has no field
// for a nested package (round-3 verification, probe b). The guard now sweeps
// the document inventory, packages included, so this refuses.
TEST(SaveFromLibrary, SACM23_LIB_002_BridgedEditRefusesRatherThanDropEmptyNestedArgumentPackage) {
    constexpr const char* kNestedPackageSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" content="The system is safe."/>
    <argumentReasoning id="S1" name="Strategy"/>
    <argumentPackage id="AP_nested" name="Nested"/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";
    ProjectFixture fixture = MakeProject("bridged-nested-pkg", kNestedPackageSacm);

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);

    const std::string before = ReadFile(fixture.sacm_absolute);
    ASSERT_TRUE(Contains(before, "AP_nested")) << "fixture carries no nested package; this test measures nothing";

    bool library_primary = false;
    const core::commands::CommandResult result = RunBridgedStrategyAttach(fixture, state, "S1", "G1", library_primary);

    EXPECT_FALSE(result.success) << "the bridged edit was applied and dropped the nested ArgumentPackage";
    EXPECT_TRUE(Contains(result.error, "ArgumentPackage"))
        << "the refusal does not say what would have been destroyed: " << result.error;
    EXPECT_EQ(ReadFile(fixture.sacm_absolute), before) << "the refused edit still rewrote the tracked file";
}

// The projection the bridge round-trips through cannot express every SACM 2.3
// element kind, and it is RELOADED over the live document rather than compared --
// so an unrepresentable element is deleted, on a command that reaches disk.
//
// The bridge now REFUSES instead. Preserving would mean growing the legacy POD to
// cover all of SACM 2.3 (a model this migration exists to retire) or new library
// API to re-adopt typed elements across the round trip; failing the command turns
// a silent corruption of a safety argument into a visible refusal, which is what
// "never silently modify or reinterpret safety arguments" requires.
//
// Asserted on the saved bytes: the refusal is worth nothing if the file changed
// anyway.
TEST(SaveFromLibrary, SACM23_LIB_002_BridgedEditRefusesRatherThanDeleteUnrepresentableElements) {
    const std::string full_case = ReadFile(std::filesystem::path(AF_REPO_ROOT) / "libs" / "sacm" / "tests" / "data" /
                                           "sacm23" / "argumentation-full-valid.sacm.xmi");
    ASSERT_FALSE(full_case.empty());
    ProjectFixture fixture = MakeProject("bridged-unrepresentable", full_case.c_str());

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);

    // Non-vacuity: the case really does carry a kind the projection cannot hold.
    const std::string before = ReadFile(fixture.sacm_absolute);
    ASSERT_TRUE(Contains(before, "ArgumentGroup"))
        << "fixture carries no unrepresentable element; this test measures nothing";

    bool library_primary = false;
    const core::commands::CommandResult result =
        RunBridgedStrategyAttach(fixture, state, "ar_decompose", "claim_top", library_primary);

    EXPECT_FALSE(result.success) << "the bridged edit was applied and deleted part of the case";
    EXPECT_TRUE(Contains(result.error, "ArgumentGroup"))
        << "the refusal does not say what would have been destroyed: " << result.error;

    // The case is untouched -- a refusal that still rewrote the file would be the
    // same data loss with a worse message.
    EXPECT_EQ(ReadFile(fixture.sacm_absolute), before) << "the refused edit still rewrote the tracked file";
}

// CreateTopGoal on a document with NO ArgumentPackage: the library seam reports
// `supported == false`, and the fallback used to run the raw legacy mutator --
// which succeeded, after which the bus wrote lossy projection bytes over the
// tracked file while reporting success (round-4 probe A: 8 of 9 clause-12
// elements deleted from disk on the exact interchange shape SACM23-CP-003
// certifies, with `has_unsaved_changes` cleared). The fallback now goes through
// the guarded bridge, whose document-inventory sweep refuses this document by
// construction.
TEST(SaveFromLibrary, SACM23_LIB_002_TopGoalFallbackRefusesRatherThanDeleteArtifactContent) {
    const std::string artifact_case = ReadFile(std::filesystem::path(AF_REPO_ROOT) / "libs" / "sacm" / "tests" /
                                               "data" / "sacm23" / "artifact-full-valid.sacm.xmi");
    ASSERT_FALSE(artifact_case.empty());
    ProjectFixture fixture = MakeProject("topgoal-artifact", artifact_case.c_str());

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);

    const std::string before = ReadFile(fixture.sacm_absolute);
    ASSERT_TRUE(Contains(before, "event_release"))
        << "fixture carries no clause-12 content; this test measures nothing";

    std::string error;
    std::unique_ptr<core::commands::CommandBus> bus =
        core::commands::CommandBus::Open(fixture.project, fixture.sacm_absolute, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CreateTopGoalCommand command;
    core::commands::CommandContext ctx{
        state.loaded_case.value(), state.sacm_package.value(), state.library_document.get()};
    const core::commands::CommandResult result = bus->Execute(command, ctx, "tester");

    EXPECT_FALSE(result.success) << "the top-goal fallback was applied and degraded the artifact case";
    EXPECT_TRUE(Contains(result.error, "Refused")) << "the failure is not the guard's refusal: " << result.error;
    EXPECT_EQ(ReadFile(fixture.sacm_absolute), before) << "the refused command still rewrote the tracked file";

    // Live-document content, per the round-4 record's required pin.
    bool event_alive = false;
    bool property_alive = false;
    bool group_alive = false;
    for (const sacm_adapter::DocumentElement& element : sacm_adapter::list_document_elements(*state.library_document)) {
        if (element.id == "event_release")
            event_alive = true;
        if (element.id == "prop_confidentiality")
            property_alive = true;
        if (element.id == "group_evidence")
            group_alive = true;
    }
    EXPECT_TRUE(event_alive && property_alive && group_alive) << "the refused command still degraded the live document";
}

// The bare-reasoning shape from round-4 probe B: adding a child under an
// ArgumentReasoning is seam-unsupported, and the legacy mutator's visible
// failure was the only thing standing between that shape and the probe-A
// class. The fallback now runs inside the guarded bridge, so the safe-fail is
// structural rather than accidental -- pinned here so a change to the legacy
// mutator cannot silently reopen it.
TEST(SaveFromLibrary, SACM23_LIB_002_ChildUnderReasoningFallbackFailsWithFileUntouched) {
    // BARE: no inference references AR_bare, so the seam cannot extend an
    // existing inference (the supported strategy-materialization shape) and
    // reports unsupported.
    constexpr const char* kBareReasoningSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
    <argumentReasoning id="AR_bare" name="Unattached reasoning"/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";
    ProjectFixture fixture = MakeProject("child-under-reasoning", kBareReasoningSacm);

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);

    const std::string before = ReadFile(fixture.sacm_absolute);

    std::string error;
    std::unique_ptr<core::commands::CommandBus> bus =
        core::commands::CommandBus::Open(fixture.project, fixture.sacm_absolute, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CreateChildElementCommand command("AR_bare", core::NewElementKind::Goal);
    core::commands::CommandContext ctx{
        state.loaded_case.value(), state.sacm_package.value(), state.library_document.get()};
    const core::commands::CommandResult result = bus->Execute(command, ctx, "tester");

    EXPECT_FALSE(result.success) << "a child was created under a bare ArgumentReasoning";
    EXPECT_FALSE(result.error.empty()) << "the failure is silent";
    EXPECT_EQ(ReadFile(fixture.sacm_absolute), before) << "the failed command still rewrote the tracked file";
}

// --- Phase 1 of the bridge retirement: the terminology tranche goes native ----
//
// The ten terminology commands and RemoveArgumentPackage no longer project the
// document into the legacy POD; they call the `sacm_adapter` seams the audit
// replayer has always used. Two things have to be measured on the SAVED BYTES,
// because the canonical hash is computed through the same projection on both
// sides of every comparison and is blind to exactly what these assert.

namespace {

// Runs one command through a real bus over `fixture`, reporting whether the flip
// engaged. Unlike RunBridgedRename this takes any command, because the point is
// which ROUTE the command took, not what it edited.
core::commands::CommandResult
RunOnBus(ProjectFixture& fixture, core::AppState& state, core::commands::ICommand& command, bool& out_library_primary) {
    // Written before anything can fail: a caller that reads it after an aborted
    // run would otherwise be told the flip engaged when nothing ran at all.
    out_library_primary = false;
    std::string error;
    std::unique_ptr<core::commands::CommandBus> bus =
        core::commands::CommandBus::Open(fixture.project, fixture.sacm_absolute, error);
    if (!bus) {
        // Hard failure rather than EXPECT: every assertion after this one would
        // fail as well, and the noise buries the one line that says why.
        ADD_FAILURE() << "could not open a command bus over the fixture: " << error;
        return core::commands::CommandResult{};
    }
    core::commands::CommandContext ctx{
        state.loaded_case.value(), state.sacm_package.value(), state.library_document.get()};
    const core::commands::CommandResult result = bus->Execute(command, ctx, "tester");
    out_library_primary = ctx.library_primary;
    if (ctx.library_primary && state.library_document != nullptr) {
        core::RebuildDerivedViewsFromLibrary(
            *state.library_document, state.loaded_case.value(), state.sacm_package.value());
    }
    return result;
}

} // namespace

// The audit projection must be loadable back through the library, because that
// round trip IS how the replayed side is hashed (`library_canonical_hash`
// serializes the projection and reloads it). It was not, for any document with an
// ArtifactPackage: the flat argument rebuild emitted each Artifact a second time
// as an `<artifactReference>` carrying the artifact's own id, so the package held
// two elements with one id.
//
// Nothing failed visibly for as long as that was true. The snapshot side takes a
// fallback when the hash cannot be computed, the on-disk side only notes it, and
// the replayed side is the one that fails hard -- so the defect needed a project
// that both mutates such a document successfully AND verifies, which no test did
// until the NodeOnly flip below. Two sessions of the flip's replay divergence
// were spent looking at the reparent; the cause was here, and reachable with no
// mutation at all.
TEST(SaveFromLibrary, AuditProjectionOfAnArtifactBearingCaseReloadsThroughTheLibrary) {
    const std::string full_case = ReadFile(std::filesystem::path(AF_REPO_ROOT) / "libs" / "sacm" / "tests" / "data" /
                                           "sacm23" / "argumentation-full-valid.sacm.xmi");
    ASSERT_FALSE(full_case.empty());
    ProjectFixture fixture = MakeProject("audit-projection-reload", full_case.c_str());
    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);

    const sacm::AssuranceCasePackage projected = core::project_library_package(*state.library_document);
    // The fixture has to carry an ArtifactPackage for this to measure anything.
    ASSERT_FALSE(projected.artifactPackages.empty()) << "fixture has no artifact package; this test measures nothing";

    EXPECT_TRUE(core::library_canonical_hash(projected).has_value())
        << "the audit projection cannot be reloaded, so every replay verification of a project holding this "
           "document reports divergence with no usable diagnostic";

    // Named, so a regression says which id collided rather than just "no hash".
    std::unordered_map<std::string, int> seen;
    for (const sacm::ArtifactPackage& ap : projected.artifactPackages) {
        for (const sacm::Artifact& artifact : ap.artifacts)
            ++seen[artifact.id];
    }
    for (const sacm::ArgumentPackage& ap : projected.argumentPackages) {
        for (const sacm::ArtifactReference& reference : ap.artifactReferences)
            ++seen[reference.id];
        for (const sacm::Claim& claim : ap.claims)
            ++seen[claim.id];
    }
    for (const std::pair<const std::string, int>& entry : seen) {
        EXPECT_EQ(entry.second, 1) << "the projection emitted " << entry.first << " " << entry.second << " times";
    }
}

// NodeOnly removal REPARENTS -- a child's inference is retargeted from the removed
// node onto its parent, and a strategy interposed as a reasoning has that
// reasoning cleared -- which no set of per-id deletes can express. It was the last
// element command on the guarded bridge, and the history is why the bridge is
// there: before it caught this path, the raw legacy mutator ran and the bus
// autosaved lossy projection bytes over the tracked file, silently deleting the
// ArgumentGroup, both artifact relationships, the nested package, every metaClaim
// and every structure reference -- from the file AND the live document, from a
// context-menu action, with zero diagnostics (round-3 verification, probe a).
//
// Phase 3c expresses the retarget with `SetRelationshipEnds`, so the removal now
// applies natively and this test INVERTS: what used to be refused must succeed,
// and every element the projection cannot represent must still be there. Both
// halves matter -- succeeding while dropping them is the round-3 defect back
// again, which is exactly what a test asserting only success would miss.
TEST(SaveFromLibrary, SACM23_LIB_002_NodeOnlyRemovalRunsNativelyAndKeepsUnrepresentableElements) {
    const std::string full_case = ReadFile(std::filesystem::path(AF_REPO_ROOT) / "libs" / "sacm" / "tests" / "data" /
                                           "sacm23" / "argumentation-full-valid.sacm.xmi");
    ASSERT_FALSE(full_case.empty());
    ProjectFixture fixture = MakeProject("nodeonly-unrepresentable", full_case.c_str());

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);

    const std::vector<std::string> unrepresentable = {
        "ArgumentGroup", "AssertedArtifactSupport", "AssertedArtifactContext", "argpkg_detail"};
    const std::string before = ReadFile(fixture.sacm_absolute);
    for (const std::string& marker : unrepresentable) {
        ASSERT_TRUE(Contains(before, marker))
            << "fixture no longer carries " << marker << "; this test measures nothing";
    }

    // `claim_sub1` is an interior claim: it is a source of the main inference and
    // the target of an evidence relationship, so removing it NodeOnly reparents.
    bool library_primary = false;
    core::commands::RemoveElementCommand command("claim_sub1", core::RemoveMode::NodeOnly);
    const core::commands::CommandResult result = RunOnBus(fixture, state, command, library_primary);
    ASSERT_TRUE(result.success) << "the NodeOnly removal was refused, so it is still going through the bridge: "
                                << result.error;
    ASSERT_TRUE(library_primary) << "the removal did not reach the library at all";

    const std::string autosaved = ReadFile(fixture.sacm_absolute);
    for (const std::string& marker : unrepresentable) {
        EXPECT_TRUE(Contains(autosaved, marker))
            << "the native NodeOnly removal deleted " << marker << " from the tracked file";
    }
    EXPECT_FALSE(Contains(autosaved, "claim_sub1")) << "the removal did not actually remove the node";
    // The evidence that pointed AT the removed claim was reparented rather than
    // dropped, which is the whole difference between NodeOnly and NodeAndDescendants.
    EXPECT_TRUE(Contains(autosaved, "ev_fmea")) << "the reparent took the evidence with the node";

    // The LIVE document too -- the round-3 probe degraded both, so byte-identity
    // of the file alone would not prove the session is safe to keep working in.
    bool group_alive = false;
    for (const sacm_adapter::DocumentElement& element : sacm_adapter::list_document_elements(*state.library_document)) {
        if (element.id == "group_core")
            group_alive = true;
    }
    EXPECT_TRUE(group_alive) << "the removal deleted the ArgumentGroup from the live document";

    const core::audit::ReplayVerificationResult verified = core::audit::VerifyProject(fixture.project);
    EXPECT_TRUE(verified.success) << (verified.diagnostics.empty() ? std::string{} : verified.diagnostics.front());
}

// (1) The refusal shrinks, and this is the test that PROVES the flip -- the only
// observable that separates a native seam call from the bridge, since both set
// `library_primary` and both preserve vendor content.
//
// `argumentation-full-valid.sacm.xmi` carries an ArgumentGroup, an
// AssertedArtifactSupport, an AssertedArtifactContext and a second
// ArgumentPackage: four kinds the legacy POD has no field for, so a BRIDGED edit
// on it is refused outright (pinned directly above by
// SACM23_LIB_002_BridgedEditRefusesRatherThanDeleteUnrepresentableElements). Every
// command below therefore succeeds only if it reaches the library directly. Route
// any one of them back through the bridge and it fails here.
//
// Both halves matter. Succeeding while quietly dropping the four kinds would be
// strictly worse than the refusal it replaces, so the markers are re-checked
// after every command.
TEST(SaveFromLibrary, SACM23_LIB_002_FlippedTerminologyCommandsRunOnACaseTheBridgeRefuses) {
    const std::string full_case = ReadFile(std::filesystem::path(AF_REPO_ROOT) / "libs" / "sacm" / "tests" / "data" /
                                           "sacm23" / "argumentation-full-valid.sacm.xmi");
    ASSERT_FALSE(full_case.empty());
    ProjectFixture fixture = MakeProject("native-terminology-unrepresentable", full_case.c_str());

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);

    const std::vector<std::string> unrepresentable = {
        "ArgumentGroup", "AssertedArtifactSupport", "AssertedArtifactContext", "argpkg_detail"};
    const std::string before = ReadFile(fixture.sacm_absolute);
    for (const std::string& marker : unrepresentable) {
        ASSERT_TRUE(Contains(before, marker))
            << "fixture no longer carries " << marker << "; this test measures nothing";
    }

    const auto run = [&](core::commands::ICommand& command, const char* what) {
        bool library_primary = false;
        const core::commands::CommandResult result = RunOnBus(fixture, state, command, library_primary);
        EXPECT_TRUE(result.success) << what
                                    << " was refused, so it is still going through the bridge: " << result.error;
        EXPECT_TRUE(library_primary) << what << " did not reach the library at all";
        const std::string autosaved = ReadFile(fixture.sacm_absolute);
        for (const std::string& marker : unrepresentable) {
            EXPECT_TRUE(Contains(autosaved, marker)) << what << " deleted " << marker << " from the tracked file:\n"
                                                     << autosaved;
        }
    };

    core::commands::CreateTerminologyPackageCommand create_pkg("Glossary", "Terms used by this argument.");
    run(create_pkg, "CreateTerminologyPackage");
    const core::TerminologyPackageRef pkg = create_pkg.GeneratedRef();

    core::commands::UpdateTerminologyPackageCommand update_pkg(pkg, "Glossary v2", "Revised.");
    run(update_pkg, "UpdateTerminologyPackage");

    core::TerminologyCategoryDraft category_draft;
    category_draft.name = "Domain";
    core::commands::CreateTerminologyCategoryCommand create_category(pkg, category_draft);
    run(create_category, "CreateTerminologyCategory");
    const core::TerminologyCategoryRef category = create_category.GeneratedRef();

    core::TerminologyCategoryDraft renamed_category;
    renamed_category.name = "Operating domain";
    core::commands::UpdateTerminologyCategoryCommand update_category(pkg, category, renamed_category);
    run(update_category, "UpdateTerminologyCategory");

    core::TerminologyTermDraft draft;
    draft.value = "ODD";
    draft.name = "Operational Design Domain";
    core::commands::CreateTerminologyTermCommand create_term(pkg, draft);
    run(create_term, "CreateTerminologyTerm");
    const core::TerminologyTermRef term = create_term.GeneratedRef();

    core::TerminologyTermDraft updated = draft;
    updated.description = "The operating conditions the system is designed for.";
    core::commands::UpdateTerminologyTermCommand update_term(pkg, term, updated);
    run(update_term, "UpdateTerminologyTerm");

    core::commands::AssociateTerminologyTermWithElementCommand associate("claim_top", pkg, term);
    run(associate, "AssociateTerminologyTermWithElement");

    core::commands::AddTerminologyTermAsVisibleContextCommand add_visible("claim_top", pkg, term);
    run(add_visible, "AddTerminologyTermAsVisibleContext");

    core::commands::DeleteTerminologyCategoryCommand delete_category(pkg, category);
    run(delete_category, "DeleteTerminologyCategory");

    // An unreferenced second term, so the delete is one the library accepts (a
    // term an argument package still references is refused -- see
    // LibraryPrimaryEditFlip.TerminologyTermDeleteRefusesWhileAnArgumentPackage-
    // StillReferencesIt).
    core::TerminologyTermDraft spare;
    spare.value = "MRC";
    core::commands::CreateTerminologyTermCommand create_spare(pkg, spare);
    run(create_spare, "CreateTerminologyTerm (spare)");
    core::commands::DeleteTerminologyTermCommand delete_spare(pkg, create_spare.GeneratedRef());
    run(delete_spare, "DeleteTerminologyTerm");

    const std::string autosaved = ReadFile(fixture.sacm_absolute);
    EXPECT_TRUE(Contains(autosaved, "Glossary v2")) << "the edits that reported success are not in the file:\n"
                                                    << autosaved;
    EXPECT_TRUE(Contains(autosaved, "Operational Design Domain")) << autosaved;
    EXPECT_FALSE(Contains(autosaved, "MRC")) << "the deleted term is still in the file:\n" << autosaved;
}

// (2) The exit criterion's byte pin: a case carrying vendor content survives the
// whole terminology tranche. This one does NOT discriminate native from bridged
// (the bridge preserves vendor content too, since the round-4 fix) -- test (1)
// above is what proves the routing. What this adds is that going native did not
// regress the preservation the bridge had earned, on every command, with the
// audit log still replaying afterwards.
TEST(SaveFromLibrary, SACM23_LIB_002_NativeTerminologyEditsPreserveUnknownContent) {
    ProjectFixture fixture = MakeProject("native-terminology-vendor");

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);

    const auto run = [&](core::commands::ICommand& command, const char* what) {
        bool library_primary = false;
        const core::commands::CommandResult result = RunOnBus(fixture, state, command, library_primary);
        EXPECT_TRUE(result.success) << what << " failed: " << result.error;
        EXPECT_TRUE(library_primary) << what << " did not take the library-primary path";
        const std::string autosaved = ReadFile(fixture.sacm_absolute);
        EXPECT_TRUE(Contains(autosaved, kVendorAttributeMarker)) << what << " dropped the vendor attribute:\n"
                                                                 << autosaved;
        EXPECT_TRUE(Contains(autosaved, kVendorElementMarker)) << what << " dropped the vendor element:\n" << autosaved;
    };

    core::commands::CreateTerminologyPackageCommand create_pkg("Terms", "Shared definitions.");
    run(create_pkg, "CreateTerminologyPackage");
    const core::TerminologyPackageRef pkg = create_pkg.GeneratedRef();

    core::commands::UpdateTerminologyPackageCommand update_pkg(pkg, "Glossary", "Project-wide definitions.");
    run(update_pkg, "UpdateTerminologyPackage");

    core::TerminologyCategoryDraft category_draft;
    category_draft.name = "Domain";
    core::commands::CreateTerminologyCategoryCommand create_category(pkg, category_draft);
    run(create_category, "CreateTerminologyCategory");
    const core::TerminologyCategoryRef category = create_category.GeneratedRef();

    core::TerminologyCategoryDraft renamed_category;
    renamed_category.name = "Operating domain";
    core::commands::UpdateTerminologyCategoryCommand update_category(pkg, category, renamed_category);
    run(update_category, "UpdateTerminologyCategory");

    core::TerminologyTermDraft draft;
    draft.value = "ODD";
    draft.name = "Operational Design Domain";
    core::commands::CreateTerminologyTermCommand create_term(pkg, draft);
    run(create_term, "CreateTerminologyTerm");
    const core::TerminologyTermRef term = create_term.GeneratedRef();

    core::TerminologyTermDraft updated = draft;
    updated.description = "The operating conditions the system is designed for.";
    core::commands::UpdateTerminologyTermCommand update_term(pkg, term, updated);
    run(update_term, "UpdateTerminologyTerm");

    core::commands::AssociateTerminologyTermWithElementCommand associate("G1", pkg, term);
    run(associate, "AssociateTerminologyTermWithElement");

    core::commands::AddTerminologyTermAsVisibleContextCommand add_visible("G1", pkg, term);
    run(add_visible, "AddTerminologyTermAsVisibleContext");

    core::commands::DeleteTerminologyCategoryCommand delete_category(pkg, category);
    run(delete_category, "DeleteTerminologyCategory");

    // A second, unreferenced term, so the delete is one the library accepts (a
    // term an argument package still references is refused -- see
    // LibraryPrimaryEditFlip.TerminologyTermDeleteRefusesWhileAnArgumentPackage-
    // StillReferencesIt).
    core::TerminologyTermDraft spare;
    spare.value = "MRC";
    core::commands::CreateTerminologyTermCommand create_spare(pkg, spare);
    run(create_spare, "CreateTerminologyTerm (spare)");
    core::commands::DeleteTerminologyTermCommand delete_spare(pkg, create_spare.GeneratedRef());
    run(delete_spare, "DeleteTerminologyTerm");

    // ...and the edits are actually there, not just the vendor content.
    const std::string autosaved = ReadFile(fixture.sacm_absolute);
    EXPECT_TRUE(Contains(autosaved, "Glossary")) << autosaved;
    EXPECT_TRUE(Contains(autosaved, "Operational Design Domain")) << autosaved;
    EXPECT_FALSE(Contains(autosaved, "MRC")) << "the deleted term is still in the file:\n" << autosaved;

    const core::audit::ReplayVerificationResult verified = core::audit::VerifyProject(fixture.project);
    EXPECT_TRUE(verified.success) << (verified.diagnostics.empty() ? std::string{} : verified.diagnostics.front());
}

// (2b) A cascading term delete -- the user having confirmed "this also removes N
// elements that reference it" -- must remove exactly those and nothing else. The
// canonical hash cannot see the difference between "removed the reference" and
// "left an ArtifactReference pointing at nothing", so this asserts the saved
// bytes: the husk is what the library's own cross-package cascade would have
// left, and it renders as a context node on the canvas sourcing an empty
// reference.
TEST(SaveFromLibrary, SACM23_LIB_002_ConsentedTermDeleteRemovesTheReferencesFromTheSavedFile) {
    ProjectFixture fixture = MakeProject("consented-term-delete");

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);

    const auto run = [&](core::commands::ICommand& command, const char* what) {
        bool library_primary = false;
        const core::commands::CommandResult result = RunOnBus(fixture, state, command, library_primary);
        EXPECT_TRUE(result.success) << what << " failed: " << result.error;
    };

    core::commands::CreateTerminologyPackageCommand create_pkg("Terms", "");
    run(create_pkg, "CreateTerminologyPackage");
    core::TerminologyTermDraft draft;
    draft.value = "ODD";
    draft.name = "Operational Design Domain";
    core::commands::CreateTerminologyTermCommand create_term(create_pkg.GeneratedRef(), draft);
    run(create_term, "CreateTerminologyTerm");
    core::commands::AddTerminologyTermAsVisibleContextCommand add_visible(
        "G1", create_pkg.GeneratedRef(), create_term.GeneratedRef());
    run(add_visible, "AddTerminologyTermAsVisibleContext");

    const std::string reference_id = add_visible.Result().artifact_reference_id;
    const std::string context_id = add_visible.Result().asserted_context_id;
    ASSERT_FALSE(reference_id.empty());
    ASSERT_FALSE(context_id.empty());
    const std::string before = ReadFile(fixture.sacm_absolute);
    ASSERT_TRUE(Contains(before, reference_id)) << "the fixture never got its reference; this test measures nothing";
    ASSERT_TRUE(Contains(before, context_id));

    core::commands::DeleteTerminologyTermCommand delete_term(
        create_pkg.GeneratedRef(), create_term.GeneratedRef(), /*cascade_references=*/true);
    run(delete_term, "DeleteTerminologyTerm (cascade)");

    const std::string autosaved = ReadFile(fixture.sacm_absolute);
    EXPECT_FALSE(Contains(autosaved, "Operational Design Domain")) << "the term survived the delete:\n" << autosaved;
    EXPECT_FALSE(Contains(autosaved, reference_id))
        << "the ArtifactReference survived as a husk pointing at a deleted term:\n"
        << autosaved;
    EXPECT_FALSE(Contains(autosaved, context_id)) << "the AssertedContext survived, still drawing a context node:\n"
                                                  << autosaved;
    // The rest of the case is untouched -- a cascade that overshoots is worse
    // than one that refuses.
    EXPECT_TRUE(Contains(autosaved, "Top goal")) << autosaved;
    EXPECT_TRUE(Contains(autosaved, kVendorAttributeMarker)) << autosaved;
    EXPECT_TRUE(Contains(autosaved, kVendorElementMarker)) << autosaved;

    const core::audit::ReplayVerificationResult verified = core::audit::VerifyProject(fixture.project);
    EXPECT_TRUE(verified.success) << (verified.diagnostics.empty() ? std::string{} : verified.diagnostics.front());
}

// (3) RemoveArgumentPackage moved in the same phase, onto the library's recursive
// DeleteElement. The fixture nests an empty ArgumentPackage inside the SURVIVING
// package, which the legacy POD cannot express -- so a bridged removal is refused
// (SACM23_LIB_002_BridgedEditRefusesRatherThanDropEmptyNestedArgumentPackage pins
// that on the same shape) and success here means the seam ran. The vendor content
// then has to survive the removal as well.
TEST(SaveFromLibrary, SACM23_LIB_002_NativeArgumentPackageRemovalPreservesUnknownContent) {
    constexpr const char* kTwoPackageVendorSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation"
    xmlns:acme="http://acme.example/toolchain" id="AC1" name="Sample" acme:owner="alice">
  <acme:vendorMetadata reviewCycle="Q3-2026"/>
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
    <argumentPackage id="AP_nested" name="Nested"/>
  </argumentPackage>
  <argumentPackage id="AP2" name="Spare">
    <claim id="G2" name="Spare goal" description="Retired branch."/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";
    ProjectFixture fixture = MakeProject("native-remove-package", kTwoPackageVendorSacm);

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);
    ASSERT_EQ(state.sacm_package->argumentPackages.size(), 2u);
    ASSERT_TRUE(Contains(ReadFile(fixture.sacm_absolute), "AP_nested"))
        << "fixture carries no nested package, so a bridged removal would pass too";

    bool library_primary = false;
    core::commands::RemoveArgumentPackageCommand remove("AP2", "");
    const core::commands::CommandResult result = RunOnBus(fixture, state, remove, library_primary);
    ASSERT_TRUE(result.success) << "the removal was refused, so it is still going through the bridge: " << result.error;
    ASSERT_TRUE(library_primary) << "the removal did not reach the library at all";

    const std::string autosaved = ReadFile(fixture.sacm_absolute);
    EXPECT_TRUE(Contains(autosaved, kVendorAttributeMarker)) << autosaved;
    EXPECT_TRUE(Contains(autosaved, kVendorElementMarker)) << autosaved;
    EXPECT_TRUE(Contains(autosaved, "AP_nested")) << "the removal deleted the nested package:\n" << autosaved;
    EXPECT_FALSE(Contains(autosaved, "Spare goal")) << "the removed package is still in the file:\n" << autosaved;
    EXPECT_TRUE(Contains(autosaved, "Top goal")) << "the removal took the wrong package:\n" << autosaved;
    EXPECT_EQ(state.sacm_package->argumentPackages.size(), 1u);
}

// Phase 2a of the bridge retirement: the two remaining package removals and the
// gid assignment. Same routing proof as phase 1 -- run them on a case a BRIDGED
// edit is refused on, where success means the seam ran -- because nothing else
// distinguishes the two routes.
//
// The fixture nests an empty ArgumentPackage, which the legacy POD cannot express
// (SACM23_LIB_002_BridgedEditRefusesRatherThanDropEmptyNestedArgumentPackage pins
// that on the same shape), and carries vendor content that has to survive each
// edit.
TEST(SaveFromLibrary, SACM23_LIB_002_FlippedPackageAndGidCommandsRunOnACaseTheBridgeRefuses) {
    constexpr const char* kNestedVendorSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation"
    xmlns:acme="http://acme.example/toolchain" id="AC1" name="Sample" acme:owner="alice">
  <acme:vendorMetadata reviewCycle="Q3-2026"/>
  <terminologyPackage id="TP_empty" name="Unused terms"/>
  <artifactPackage id="ARTP1" name="Evidence">
    <artifact id="A1" name="Brake test report"/>
  </artifactPackage>
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
    <argumentPackage id="AP_nested" name="Nested"/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";
    ProjectFixture fixture = MakeProject("phase2a-routing", kNestedVendorSacm);

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);
    ASSERT_TRUE(Contains(ReadFile(fixture.sacm_absolute), "AP_nested"))
        << "fixture carries no nested package, so a bridged edit would pass too";

    const auto run = [&](core::commands::ICommand& command, const char* what) {
        bool library_primary = false;
        const core::commands::CommandResult result = RunOnBus(fixture, state, command, library_primary);
        EXPECT_TRUE(result.success) << what
                                    << " was refused, so it is still going through the bridge: " << result.error;
        EXPECT_TRUE(library_primary) << what << " did not reach the library at all";
        const std::string autosaved = ReadFile(fixture.sacm_absolute);
        EXPECT_TRUE(Contains(autosaved, "AP_nested")) << what << " deleted the nested package";
        EXPECT_TRUE(Contains(autosaved, kVendorAttributeMarker)) << what << " dropped the vendor attribute";
        EXPECT_TRUE(Contains(autosaved, kVendorElementMarker)) << what << " dropped the vendor element";
    };

    core::commands::EnsureElementGidCommand assign_gid("G1");
    run(assign_gid, "SetElementGid");

    core::commands::RemoveTerminologyPackageCommand remove_terminology("TP_empty", "");
    run(remove_terminology, "RemoveTerminologyPackage");

    core::commands::RemoveArtifactPackageCommand remove_artifacts("ARTP1", "");
    run(remove_artifacts, "RemoveArtifactPackage");

    const std::string autosaved = ReadFile(fixture.sacm_absolute);
    EXPECT_FALSE(Contains(autosaved, "TP_empty")) << "the terminology package is still in the file";
    EXPECT_FALSE(Contains(autosaved, "Brake test report")) << "the artifact package is still in the file";
    EXPECT_TRUE(Contains(autosaved, "Top goal")) << "the removals overshot";

    const core::audit::ReplayVerificationResult verified = core::audit::VerifyProject(fixture.project);
    EXPECT_TRUE(verified.success) << (verified.diagnostics.empty() ? std::string{} : verified.diagnostics.front());
}

// Slice 2b: the GSN identifier. Same routing proof -- a bridged edit is refused
// on this fixture, so success means the seam ran.
TEST(SaveFromLibrary, SACM23_LIB_002_FlippedGsnIdentifierRunsOnACaseTheBridgeRefuses) {
    constexpr const char* kNestedVendorSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation"
    xmlns:acme="http://acme.example/toolchain" id="AC1" name="Sample" acme:owner="alice">
  <acme:vendorMetadata reviewCycle="Q3-2026"/>
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
    <argumentPackage id="AP_nested" name="Nested"/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";
    ProjectFixture fixture = MakeProject("phase2b-routing", kNestedVendorSacm);

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);
    ASSERT_TRUE(Contains(ReadFile(fixture.sacm_absolute), "AP_nested"))
        << "fixture carries no nested package, so a bridged edit would pass too";

    bool library_primary = false;
    core::commands::UpdateGsnIdentifierCommand rename("G1", "TOP1");
    const core::commands::CommandResult result = RunOnBus(fixture, state, rename, library_primary);
    ASSERT_TRUE(result.success) << "the rename was refused, so it is still going through the bridge: " << result.error;
    ASSERT_TRUE(library_primary) << "the rename did not reach the library at all";

    const std::string autosaved = ReadFile(fixture.sacm_absolute);
    EXPECT_TRUE(Contains(autosaved, "AP_nested")) << "the rename deleted the nested package";
    EXPECT_TRUE(Contains(autosaved, kVendorAttributeMarker)) << "the rename dropped the vendor attribute";
    EXPECT_TRUE(Contains(autosaved, kVendorElementMarker)) << "the rename dropped the vendor element";
    EXPECT_TRUE(Contains(autosaved, "TOP1")) << "the identifier is not in the saved file";

    const core::audit::ReplayVerificationResult verified = core::audit::VerifyProject(fixture.project);
    EXPECT_TRUE(verified.success) << (verified.diagnostics.empty() ? std::string{} : verified.diagnostics.front());
}

// Slice 3e: ReorderSiblings, the last command that needed a NEW library operation.
// A sibling reorder is two changes -- the `source` order of an inference whose
// sub-goals moved, and the document order of the package's own elements -- and only
// the first was expressible before `ReorderPackageElements`.
//
// Its own fixture rather than argumentation-full-valid: that case has no two claims
// the tree puts in the same group, so a reorder there is rejected before the routing
// question is even reached ("Only siblings in the same tree group can be
// reordered.").
//
// The unrepresentable element is an ArgumentGroup. The first version of this fixture
// used a SECOND argumentPackage and was vacuous -- the legacy POD holds a vector of
// argument packages, so a sibling package round-trips fine and the bridge applied the
// reorder too. Only a NESTED package is unrepresentable, and nesting one here would
// have meant writing a fixture that contradicts clause 11.4 (a package that nests
// packages contains nothing else). The negative check caught it: the test passed with
// the native path disabled.
// Slice 3g routing: a proposal applies through the seams on a case whose
// ArgumentGroup the bridge's projection cannot represent, so the bridge refuses
// it outright. Success here means the flip engaged; the group surviving in the
// SAVED BYTES means it engaged without going through the lossy round trip.
//
// The unrepresentable element is the whole instrument. Without it a bridged
// apply would pass this test too, and the routing claim would rest on nothing.
TEST(SaveFromLibrary, SACM23_LIB_002_FlippedApplyProposalRunsOnACaseTheBridgeRefuses) {
    constexpr const char* kGroupedCase = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301"
    xmlns:xmi="http://www.omg.org/spec/XMI/20131001"
    xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:version="2.0" xmi:id="acp_1">
  <name content="Proposal routing"/>
  <argumentPackage xmi:id="ap_1">
    <name content="Main"/>
    <argumentElement xsi:type="sacm:Claim" xmi:id="G1"><name content="Top"/></argumentElement>
    <argumentElement xsi:type="sacm:Claim" xmi:id="G2"><name content="Sub A"/></argumentElement>
    <argumentElement xsi:type="sacm:AssertedInference" xmi:id="R1" source="G2" target="G1"/>
    <argumentElement xsi:type="sacm:ArgumentGroup" xmi:id="grp_1" argumentElement="G2">
      <name content="Grouped sub-goals"/>
    </argumentElement>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";
    ProjectFixture fixture = MakeProject("phase3g-routing", kGroupedCase);

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);

    const std::string before = ReadFile(fixture.sacm_absolute);
    ASSERT_TRUE(Contains(before, "ArgumentGroup"))
        << "fixture lost the unrepresentable element, so a bridged apply would pass too";

    core::reviews::ReviewProposal proposal;
    proposal.id = "phase3g";
    proposal.anchor_element_id = "G1";
    core::reviews::PatchOperation create;
    create.type = core::reviews::PatchOperationType::CreateClaim;
    create.create_ref = "$new_claim_1";
    create.text = "The new sub-claim holds.";
    proposal.operations.push_back(create);
    core::reviews::PatchOperation attach;
    attach.type = core::reviews::PatchOperationType::AddSupportedBy;
    // GSN's SupportedBy points parent -> child; SACM's AssertedInference runs the
    // other way, premise to conclusion. The patch service owns that swap, so the
    // operation names the new claim as the SOURCE and the top goal as the TARGET.
    attach.source = core::reviews::ElementRef{std::nullopt, "$new_claim_1"};
    attach.target = core::reviews::ElementRef{"G1", std::nullopt};
    proposal.operations.push_back(attach);

    bool library_primary = false;
    core::commands::ApplyProposalCommand command(proposal);
    const core::commands::CommandResult result = RunOnBus(fixture, state, command, library_primary);
    ASSERT_TRUE(result.success) << "the proposal was refused, so it is still going through the bridge: "
                                << result.error;
    ASSERT_TRUE(library_primary) << "the proposal did not reach the library at all";

    const std::string autosaved = ReadFile(fixture.sacm_absolute);
    EXPECT_TRUE(Contains(autosaved, "ArgumentGroup")) << "the native apply deleted the ArgumentGroup";
    EXPECT_TRUE(Contains(autosaved, "The new sub-claim holds.")) << "the proposal's claim text never reached disk";
}

// A non-primary-language NAME edit applies through the seams and SURVIVES a save
// and reload. This was the last functional dependency on the bridge: SACM gives
// an element one name LangString (clause 8.6), so the seam used to decline and
// the bridge carried translated names.
//
// The round trip is the assertion that matters. Writing the tag is easy; writing
// it under the key the reader merges back into `name_langs` is the point, and a
// write that landed anywhere else would look fine in memory and vanish on load.
//
// Edited TWICE in the same language, because that is where `AddTaggedValue` would
// have failed: it always creates, so a second edit left two tags under one key
// and the reader kept the FIRST -- the newer name silently lost.
TEST(SaveFromLibrary, SACM23_LIB_002_NativeTranslatedNameSurvivesSaveAndReload) {
    ProjectFixture fixture = MakeProject("translated-name");

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);

    bool library_primary = false;
    core::commands::UpdateElementTextCommand first("G1", core::ElementTextField::Name, "ja", "初回");
    ASSERT_TRUE(RunOnBus(fixture, state, first, library_primary).success);
    ASSERT_TRUE(library_primary) << "the translated name did not reach the library at all";

    core::commands::UpdateElementTextCommand second("G1", core::ElementTextField::Name, "ja", "二回目");
    bool second_primary = false;
    ASSERT_TRUE(RunOnBus(fixture, state, second, second_primary).success);

    const std::string autosaved = ReadFile(fixture.sacm_absolute);
    EXPECT_TRUE(Contains(autosaved, "sacm.import.name"))
        << "the translated name was not written where the reader looks for it";

    // Reload through the library and project: the SECOND edit must be what comes
    // back, and the primary name must be untouched.
    sacm_adapter::LoadOutcome reloaded = sacm_adapter::load_document(fixture.sacm_absolute);
    ASSERT_TRUE(reloaded.ok);
    ASSERT_NE(reloaded.document, nullptr);
    const core::AssuranceCase projected = sacm_adapter::project_case(*reloaded.document);
    const core::SacmElement* goal = nullptr;
    for (const core::SacmElement& element : projected.elements) {
        if (element.id == "G1")
            goal = &element;
    }
    ASSERT_NE(goal, nullptr);
    ASSERT_TRUE(goal->name_langs.contains("ja")) << "the translated name did not survive the reload";
    EXPECT_EQ(goal->name_langs.at("ja"), "二回目")
        << "the reload returned the FIRST translated name; the second edit was lost";
    EXPECT_EQ(goal->name, "Top goal") << "the translated edit overwrote the primary name";
}

// A proposal that creates a strategy and attaches it applies NATIVELY, on a case
// the bridge refuses. It used to decline: the patch service produces an
// AssertedInference whose only end is `reasoning`, which clause 11.13 forbids
// (source [1..*]) and the seam rightly refuses.
//
// The fix is deferral, not a new relationship shape -- the same one
// `apply_add_child` has always used for a new Strategy. The strategy records the
// goal it will support in a vendor tag, and the inference materializes when the
// first sub-goal gives it a source. This was the LAST functional dependency on
// the bridge outside multi-language names.
TEST(SaveFromLibrary, SACM23_LIB_002_NativeProposalDefersABareStrategysInference) {
    constexpr const char* kGroupedCase = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301"
    xmlns:xmi="http://www.omg.org/spec/XMI/20131001"
    xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:version="2.0" xmi:id="acp_1">
  <name content="Bare strategy"/>
  <argumentPackage xmi:id="ap_1">
    <name content="Main"/>
    <argumentElement xsi:type="sacm:Claim" xmi:id="G1"><name content="Top"/></argumentElement>
    <argumentElement xsi:type="sacm:ArgumentGroup" xmi:id="grp_1" argumentElement="G1">
      <name content="Grouped"/>
    </argumentElement>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";
    ProjectFixture fixture = MakeProject("phase4-bare-strategy", kGroupedCase);

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);
    ASSERT_TRUE(Contains(ReadFile(fixture.sacm_absolute), "ArgumentGroup"))
        << "fixture lost the unrepresentable element, so a bridged apply would pass too";

    core::reviews::ReviewProposal proposal;
    proposal.id = "phase4-bare-strategy";
    proposal.anchor_element_id = "G1";
    core::reviews::PatchOperation create;
    create.type = core::reviews::PatchOperationType::CreateStrategy;
    create.create_ref = "$new_strategy_1";
    create.text = "Argument over the identified hazards.";
    proposal.operations.push_back(create);
    core::reviews::PatchOperation attach;
    attach.type = core::reviews::PatchOperationType::AddSupportedBy;
    attach.source = core::reviews::ElementRef{std::nullopt, "$new_strategy_1"};
    attach.target = core::reviews::ElementRef{"G1", std::nullopt};
    proposal.operations.push_back(attach);

    bool library_primary = false;
    core::commands::ApplyProposalCommand command(proposal);
    const core::commands::CommandResult result = RunOnBus(fixture, state, command, library_primary);
    ASSERT_TRUE(result.success) << "the proposal declined and the bridge refused the case: " << result.error;
    ASSERT_TRUE(library_primary) << "the proposal did not reach the library at all";

    const std::string autosaved = ReadFile(fixture.sacm_absolute);
    EXPECT_TRUE(Contains(autosaved, "ArgumentGroup")) << "the native apply deleted the ArgumentGroup";
    EXPECT_TRUE(Contains(autosaved, "assuranceForge.gsn.strategyTarget"))
        << "the strategy's pending attachment was not recorded, so it will not materialize";
    // And NOT as a relationship: a sourceless AssertedInference is what the
    // library refuses, and writing one would make the file unloadable.
    EXPECT_FALSE(Contains(autosaved, "AssertedInference")) << "a bare inference reached the file after all";
}

// Review of #373: a proposed Strategy's text reached the seam and was dropped.
// `CreateArgumentReasoning` takes no description, and an ArgumentReasoning's
// statement IS its Description -- so it had to be written after the create or not
// at all. The native path silently lost it while the bridge kept it.
//
// Asserted on the SAVED BYTES, because the failure was invisible in the command
// result: the apply succeeded, and the text was simply gone.
TEST(SaveFromLibrary, SACM23_LIB_002_NativeProposalKeepsACreatedStrategysText) {
    ProjectFixture fixture = MakeProject("phase3g-strategy-text");

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);

    core::reviews::ReviewProposal proposal;
    proposal.id = "phase3g-strategy-text";
    proposal.anchor_element_id = "G1";
    core::reviews::PatchOperation create;
    create.type = core::reviews::PatchOperationType::CreateStrategy;
    create.create_ref = "$new_strategy_1";
    create.text = "Argument over the identified hazards.";
    proposal.operations.push_back(create);

    bool library_primary = false;
    core::commands::ApplyProposalCommand command(proposal);
    const core::commands::CommandResult result = RunOnBus(fixture, state, command, library_primary);
    ASSERT_TRUE(result.success) << result.error;
    ASSERT_TRUE(library_primary) << "the proposal did not reach the library at all";

    EXPECT_TRUE(Contains(ReadFile(fixture.sacm_absolute), "Argument over the identified hazards."))
        << "the created strategy's statement never reached disk";
}

// Review of #373: a translated NAME edit planned as representable and then
// hard-failed mid-apply. SACM's name is one LangString (clause 8.6), so the seam
// declines a non-primary language -- and a decline discovered at write time comes
// after elements are already in the document, with no way back.
//
// Measured the same way as the other fallback test: the bridge refuses this
// fixture, so a clean decline shows up as a refusal with the file untouched,
// whereas a mid-apply failure would have left the document changed.
TEST(SaveFromLibrary, SACM23_LIB_002_ProposalWithATranslatedNameDeclinesBeforeWriting) {
    constexpr const char* kGroupedCase = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301"
    xmlns:xmi="http://www.omg.org/spec/XMI/20131001"
    xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:version="2.0" xmi:id="acp_1">
  <name content="Translated name"/>
  <argumentPackage xmi:id="ap_1">
    <name content="Main"/>
    <argumentElement xsi:type="sacm:Claim" xmi:id="G1"><name content="Top"/></argumentElement>
    <argumentElement xsi:type="sacm:ArgumentGroup" xmi:id="grp_1" argumentElement="G1">
      <name content="Grouped"/>
    </argumentElement>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";
    ProjectFixture fixture = MakeProject("phase3g-translated-name", kGroupedCase);

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);

    const std::string before = ReadFile(fixture.sacm_absolute);

    core::reviews::ReviewProposal proposal;
    proposal.id = "phase3g-translated-name";
    proposal.anchor_element_id = "G1";
    core::reviews::PatchOperation rename;
    rename.type = core::reviews::PatchOperationType::UpdateElementName;
    rename.element = core::reviews::ElementRef{"G1", std::nullopt};
    rename.translations["ja"] = "トップ";
    proposal.operations.push_back(rename);

    bool library_primary = false;
    core::commands::ApplyProposalCommand command(proposal);
    const core::commands::CommandResult result = RunOnBus(fixture, state, command, library_primary);

    // Both a plan-time decline and a mid-apply failure make the command fail, so
    // failure alone proves nothing. WHICH failure is the point: a decline routes to
    // the guarded bridge, whose refusal names the element it would have destroyed.
    // A mid-apply failure reports the seam call instead -- and by then the plan is
    // part-written.
    EXPECT_FALSE(result.success) << "a translated name reached the seams, which decline it";
    EXPECT_TRUE(Contains(result.error, "ArgumentGroup"))
        << "this failed inside the seams rather than declining to the bridge: " << result.error;
    EXPECT_EQ(ReadFile(fixture.sacm_absolute), before) << "the declined proposal still rewrote the tracked file";
}

// The other half of slice 3g's routing claim: a proposal the planner CANNOT
// express falls back to the guarded bridge rather than applying a near-miss.
//
// The shape here is a NodeOnly removal, which reparents the removed element's
// children onto its parent. That is a RETARGET -- an existing relationship's
// endpoints change -- and the plan has no operation for it, because the proposal
// vocabulary has no move (#261) and a retarget arriving through a diff means
// something the planner was not built to mirror.
//
// Measured by the bridge REFUSING: the fixture carries an ArgumentGroup the
// projection cannot represent, so a bridged apply fails outright while a native
// one would have succeeded. The failure IS the evidence of which path ran, and
// the tracked file must be untouched either way.
TEST(SaveFromLibrary, SACM23_LIB_002_ProposalThePlannerCannotExpressFallsBackToTheBridge) {
    constexpr const char* kGroupedCase = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301"
    xmlns:xmi="http://www.omg.org/spec/XMI/20131001"
    xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:version="2.0" xmi:id="acp_1">
  <name content="Proposal fallback"/>
  <argumentPackage xmi:id="ap_1">
    <name content="Main"/>
    <argumentElement xsi:type="sacm:Claim" xmi:id="G1"><name content="Top"/></argumentElement>
    <argumentElement xsi:type="sacm:Claim" xmi:id="G2"><name content="Middle"/></argumentElement>
    <argumentElement xsi:type="sacm:Claim" xmi:id="G3"><name content="Leaf"/></argumentElement>
    <argumentElement xsi:type="sacm:AssertedInference" xmi:id="R1" source="G2" target="G1"/>
    <argumentElement xsi:type="sacm:AssertedInference" xmi:id="R2" source="G3" target="G2"/>
    <argumentElement xsi:type="sacm:ArgumentGroup" xmi:id="grp_1" argumentElement="G2">
      <name content="Grouped"/>
    </argumentElement>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";
    ProjectFixture fixture = MakeProject("phase3g-fallback", kGroupedCase);

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);

    const std::string before = ReadFile(fixture.sacm_absolute);
    ASSERT_TRUE(Contains(before, "ArgumentGroup")) << "fixture lost the unrepresentable element";

    core::reviews::ReviewProposal proposal;
    proposal.id = "phase3g-fallback";
    proposal.anchor_element_id = "G1";
    core::reviews::PatchOperation remove;
    remove.type = core::reviews::PatchOperationType::RemoveElement;
    remove.element = core::reviews::ElementRef{"G2", std::nullopt};
    remove.field = core::reviews::kReviewProposalRemoveModeNodeOnly;
    proposal.operations.push_back(remove);

    bool library_primary = false;
    core::commands::ApplyProposalCommand command(proposal);
    const core::commands::CommandResult result = RunOnBus(fixture, state, command, library_primary);

    EXPECT_FALSE(result.success) << "a reparent reached the seams, which have no retarget in this plan";
    EXPECT_EQ(ReadFile(fixture.sacm_absolute), before) << "the refused proposal still rewrote the tracked file";
}

TEST(SaveFromLibrary, SACM23_LIB_002_FlippedReorderSiblingsRunsOnACaseTheBridgeRefuses) {
    constexpr const char* kReorderCase = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301"
    xmlns:xmi="http://www.omg.org/spec/XMI/20131001"
    xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:version="2.0" xmi:id="acp_1">
  <name content="Reorder routing"/>
  <argumentPackage xmi:id="ap_1">
    <name content="Main"/>
    <argumentElement xsi:type="sacm:Claim" xmi:id="G1"><name content="Top"/></argumentElement>
    <argumentElement xsi:type="sacm:Claim" xmi:id="G2"><name content="Sub A"/></argumentElement>
    <argumentElement xsi:type="sacm:Claim" xmi:id="G3"><name content="Sub B"/></argumentElement>
    <argumentElement xsi:type="sacm:AssertedInference" xmi:id="R1" source="G2 G3" target="G1"/>
    <argumentElement xsi:type="sacm:ArgumentGroup" xmi:id="grp_1" argumentElement="G2 G3">
      <name content="Grouped sub-goals"/>
    </argumentElement>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";
    ProjectFixture fixture = MakeProject("phase3e-routing", kReorderCase);

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);

    const std::string before = ReadFile(fixture.sacm_absolute);
    ASSERT_TRUE(Contains(before, "ArgumentGroup"))
        << "fixture lost the unrepresentable element, so a bridged edit would pass too";
    ASSERT_TRUE(Contains(before, "source=\"G2 G3\"")) << "the fixture's source order is not what this test reverses";

    bool library_primary = false;
    core::commands::ReorderSiblingsCommand reorder("G3", "G2", core::TreeDropMode::Before);
    const core::commands::CommandResult result = RunOnBus(fixture, state, reorder, library_primary);
    ASSERT_TRUE(result.success) << "the reorder was refused, so it is still going through the bridge: " << result.error;
    ASSERT_TRUE(library_primary) << "the reorder did not reach the library at all";

    const std::string autosaved = ReadFile(fixture.sacm_absolute);
    EXPECT_TRUE(Contains(autosaved, "ArgumentGroup")) << "the native reorder deleted the ArgumentGroup";
    // The order changed IN THE SAVED BYTES. Asserting only that the command succeeded
    // would pass while persisting nothing -- which is the specific failure a half-flip
    // of this command produces: the tree moves on screen and the file keeps the old
    // order.
    // Matched with the attribute name attached, because the ArgumentGroup in this
    // fixture lists the same two ids: `Contains(autosaved, "G2 G3")` also matches the
    // group's own member list, which a reorder does not touch, and the assertion
    // failed for that reason before it was made precise.
    EXPECT_TRUE(Contains(autosaved, "source=\"G3 G2\"")) << "the reordered source order is not in the saved file";
    EXPECT_FALSE(Contains(autosaved, "source=\"G2 G3\"")) << "the old source order is still in the saved file";

    const core::audit::ReplayVerificationResult verified = core::audit::VerifyProject(fixture.project);
    EXPECT_TRUE(verified.success) << (verified.diagnostics.empty() ? std::string{} : verified.diagnostics.front());
}

// Slice 3d: MoveSubtree. Same routing proof -- a bridged edit is refused on this
// fixture, so success means the seams ran. The move is the case that needs the new
// `apply_add_relationship` seam: claim_sub2 leaves inf_1 (which keeps claim_sub1,
// so it stays valid) and a NEW AssertedInference is created under claim_counter.
TEST(SaveFromLibrary, SACM23_LIB_002_FlippedMoveSubtreeRunsOnACaseTheBridgeRefuses) {
    const std::string full_case = ReadFile(std::filesystem::path(AF_REPO_ROOT) / "libs" / "sacm" / "tests" / "data" /
                                           "sacm23" / "argumentation-full-valid.sacm.xmi");
    ASSERT_FALSE(full_case.empty());
    ProjectFixture fixture = MakeProject("phase3d-routing", full_case.c_str());

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);

    const std::vector<std::string> unrepresentable = {
        "ArgumentGroup", "AssertedArtifactSupport", "AssertedArtifactContext", "argpkg_detail"};
    const std::string before = ReadFile(fixture.sacm_absolute);
    for (const std::string& marker : unrepresentable) {
        ASSERT_TRUE(Contains(before, marker))
            << "fixture no longer carries " << marker << "; this test measures nothing";
    }

    bool library_primary = false;
    core::commands::MoveSubtreeCommand move("claim_sub2", "claim_counter");
    const core::commands::CommandResult result = RunOnBus(fixture, state, move, library_primary);
    ASSERT_TRUE(result.success) << "the move was refused, so it is still going through the bridge: " << result.error;
    ASSERT_TRUE(library_primary) << "the move did not reach the library at all";

    const std::string autosaved = ReadFile(fixture.sacm_absolute);
    for (const std::string& marker : unrepresentable) {
        EXPECT_TRUE(Contains(autosaved, marker)) << "the native move deleted " << marker << " from the tracked file";
    }
    // The move really happened, and did not take the old inference with it: inf_1
    // keeps claim_sub1, and claim_sub2 now hangs off claim_counter.
    EXPECT_TRUE(Contains(autosaved, "inf_1")) << "the move deleted the inference it only had to shrink";
    EXPECT_TRUE(Contains(autosaved, "claim_sub1")) << "the move took the remaining sub-goal with it";
    EXPECT_TRUE(Contains(autosaved, "claim_sub2")) << "the moved element is gone from the file";
    // A relationship was ADDED, not just rewired. This is what distinguishes the
    // case that needs `apply_add_relationship` from the reasoning-retarget case,
    // which `SetRelationshipEnds` alone covers -- without it the test would still
    // pass if the move had merely retargeted inf_1 onto claim_counter and taken
    // claim_sub1 along with it.
    EXPECT_EQ(CountOccurrences(autosaved, "sacm:AssertedInference"),
              CountOccurrences(before, "sacm:AssertedInference") + 1)
        << "no new inference was created, so the move did not go through apply_add_relationship";
    EXPECT_TRUE(Contains(autosaved, "claim_counter")) << "the new parent is gone from the file";

    const core::audit::ReplayVerificationResult verified = core::audit::VerifyProject(fixture.project);
    EXPECT_TRUE(verified.success) << (verified.diagnostics.empty() ? std::string{} : verified.diagnostics.front());
}

TEST(SaveFromLibrary, SACM23_LIB_002_BridgedEditPreservesAcpTaggedValues) {
    const std::string acp_case =
        ReadFile(std::filesystem::path(AF_REPO_ROOT) / "tests" / "data" / "fixture_acp_parity.sacm.xml");
    ASSERT_FALSE(acp_case.empty());
    ProjectFixture fixture = MakeProject("bridged-acp", acp_case.c_str());

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);

    const std::size_t before = CountOccurrences(ReadFile(fixture.sacm_absolute), "assuranceForge.acp");
    ASSERT_GT(before, 0u) << "fixture carries no ACP tags; this test measures nothing";

    bool library_primary = false;
    const core::commands::CommandResult result =
        RunNativeRename(fixture, state, "G1", "Renamed top goal", library_primary);
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
    ASSERT_GT(packages_before, 1u) << "fixture no longer has several argument packages; this test measures nothing";

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
    const core::commands::CommandResult result = RunNativeRename(fixture, state, target_id, "Renamed", library_primary);
    ASSERT_TRUE(result.success) << "a bridged edit failed on a multi-argument-package case: " << result.error;
    ASSERT_TRUE(library_primary);

    core::RebuildDerivedViewsFromLibrary(
        *state.library_document, state.loaded_case.value(), state.sacm_package.value());
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
std::string EditTamperAndRestore(ProjectFixture& fixture,
                                 core::AppState& state,
                                 const std::string& element_id,
                                 core::audit::RestoreSacmFromAuditResult& restored,
                                 std::string& error) {
    std::unique_ptr<core::commands::CommandBus> bus =
        core::commands::CommandBus::Open(fixture.project, fixture.sacm_absolute, error);
    EXPECT_TRUE(bus) << error;
    if (!bus) {
        return {};
    }
    core::commands::UpdateElementTextCommand command(
        element_id, core::ElementTextField::Content, "en", "Edited through the bridge.");
    core::commands::CommandContext ctx{
        state.loaded_case.value(), state.sacm_package.value(), state.library_document.get()};
    const core::commands::CommandResult result = bus->Execute(command, ctx, "tester");
    EXPECT_TRUE(result.success) << result.error;
    if (!result.success) {
        return {};
    }
    bus.reset();

    WriteFile(fixture.sacm_absolute, kTamperedSacm);
    if (!core::audit::RestoreSacmFromAudit(fixture.project, fixture.sacm_relative, "tester", restored, error)) {
        return {};
    }
    return ReadFile(fixture.sacm_absolute);
}

} // namespace

TEST(SaveFromLibrary, SACM23_LIB_002_RestoreAfterBridgedEditPreservesAcpTaggedValues) {
    const std::string acp_case =
        ReadFile(std::filesystem::path(AF_REPO_ROOT) / "tests" / "data" / "fixture_acp_parity.sacm.xml");
    ASSERT_FALSE(acp_case.empty());
    ProjectFixture fixture = MakeProject("restore-acp", acp_case.c_str());

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    const std::size_t before = CountOccurrences(ReadFile(fixture.sacm_absolute), "assuranceForge.acp");
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

    EXPECT_TRUE(Contains(bytes, kVendorAttributeMarker)) << "the restore dropped the vendor attribute:\n" << bytes;
    EXPECT_TRUE(Contains(bytes, kVendorElementMarker)) << "the restore dropped the vendor element:\n" << bytes;
}

// A project whose log contains one bridged content edit must be recoverable at
// all. Before the fix the replay aborted on this case -- the audit projection
// collapsed its four argument packages and duplicated artifact-reference ids, so
// the re-derive was rejected and the flagship case could not be restored.
TEST(SaveFromLibrary, SACM23_LIB_002_RestoreAfterBridgedEditSucceedsOnMultiArgumentPackageCase) {
    const std::string big_case =
        ReadFile(std::filesystem::path(AF_REPO_ROOT) / "data" / "open-autonomy-safety-case.sacm.xml");
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
    ASSERT_FALSE(bytes.empty()) << "a project with one bridged content edit in its log could not be restored: "
                                << error;

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
    ASSERT_TRUE(core::SetElementTextField(state.loaded_case.value(),
                                          &state.sacm_package.value(),
                                          "G1",
                                          core::ElementTextField::Name,
                                          "en",
                                          "Renamed without a bus",
                                          discarded,
                                          error))
        << error;
    state.sync_library_document();

    // The document is what every save site serializes, so the loss would reach
    // disk on the next save whether or not it is visible in memory first.
    const std::filesystem::path saved_path = fixture.temp.path / "after-sync.sacm";
    ASSERT_TRUE(state.save_file(saved_path.string())) << state.status_message;
    const std::string bytes = ReadFile(saved_path);

    EXPECT_TRUE(Contains(bytes, kVendorAttributeMarker))
        << "syncing the library from the legacy package dropped the vendor attribute:\n"
        << bytes;
    EXPECT_TRUE(Contains(bytes, kVendorElementMarker))
        << "syncing the library from the legacy package dropped the vendor element:\n"
        << bytes;
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

    // No production command is unflipped any more -- NodeOnly, the last one,
    // now goes through the guarded bridge. The Stage-5 net still guards any
    // future unflipped dispatch, so this test reaches it the only way left:
    // the kill switch, the same seam the undo kill-switch test uses.
    core::commands::RemoveElementCommand command("G1", core::RemoveMode::NodeOnly);
    core::commands::CommandContext ctx{
        state.loaded_case.value(), state.sacm_package.value(), state.library_document.get()};
    ctx.allow_library_primary = false;
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
        << "the Stage-5 net dropped the vendor attribute from the library document:\n"
        << bytes;
    EXPECT_TRUE(Contains(bytes, kVendorElementMarker))
        << "the Stage-5 net dropped the vendor element from the library document:\n"
        << bytes;
}

// A load can SUCCEED and still have told us something the user must know. The
// sharpest case: an ODE container embedding SACM opens fine, so nothing on
// screen suggests the rest of the file is about to disappear on save. The
// library reports SACM-XMI-009; AppState used to discard every diagnostic on
// the success path, so the warning existed and reached nobody.
TEST(SaveFromLibrary, SACM23_INT_001_LoadSurfacesNonConformanceWarningToTheUser) {
    const std::filesystem::path container = std::filesystem::path(AF_REPO_ROOT) / "libs" / "sacm" / "tests" / "data" /
                                            "interop-thirdparty" / "mobstr-safetycase.integration";
    ASSERT_TRUE(std::filesystem::exists(container)) << container.string();

    core::AppState state;
    ASSERT_TRUE(state.load_file(container.string())) << state.status_message;

    // The user is told, in the status line they already read after every open.
    EXPECT_TRUE(Contains(state.status_message, "SACM-XMI-009"))
        << "the non-conformance warning did not reach the status line: " << state.status_message;
    EXPECT_TRUE(Contains(state.status_message, "does not conform")) << state.status_message;

    ASSERT_FALSE(state.load_warnings.empty());
    EXPECT_TRUE(std::any_of(state.load_warnings.begin(), state.load_warnings.end(), [](const std::string& warning) {
        return warning.find("SACM-XMI-009") != std::string::npos;
    }));

    // A conformant file must not be decorated with warnings it did not earn --
    // a status line that always says something alarming says nothing.
    ProjectFixture clean = MakeProject("clean-load", kVendorElementOnlySacm);
    core::AppState ordinary;
    ASSERT_TRUE(ordinary.load_file(clean.sacm_absolute.string())) << ordinary.status_message;
    EXPECT_FALSE(Contains(ordinary.status_message, "SACM-XMI-009")) << ordinary.status_message;

    // And the warning is cleared by the next load rather than persisting.
    EXPECT_TRUE(
        ordinary.load_warnings.empty() ||
        std::none_of(ordinary.load_warnings.begin(), ordinary.load_warnings.end(), [](const std::string& warning) {
            return warning.find("SACM-XMI-009") != std::string::npos;
        }));
}

// Slice 2c: the four ACP commands. Same routing proof -- a bridged edit is
// refused on this fixture, so success means the seams ran.
//
// An ACP is a set of vendor TaggedValues, and a relationship ACP additionally
// holds a clause-11.6 metaClaim, so this also checks the tags survive in the
// saved bytes rather than only that the command reported success.
TEST(SaveFromLibrary, SACM23_LIB_002_FlippedAcpCommandsRunOnACaseTheBridgeRefuses) {
    constexpr const char* kNestedVendorSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation"
    xmlns:acme="http://acme.example/toolchain" id="AC1" name="Sample" acme:owner="alice">
  <acme:vendorMetadata reviewCycle="Q3-2026"/>
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
    <artifactReference id="Sn1" name="Test report"/>
    <assertedEvidence id="E1" source="Sn1" target="G1"/>
    <argumentPackage id="AP_nested" name="Nested"/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";
    ProjectFixture fixture = MakeProject("phase2c-routing", kNestedVendorSacm);

    core::AppState state;
    ASSERT_TRUE(state.load_file(fixture.sacm_absolute.string())) << state.status_message;
    ASSERT_NE(state.library_document, nullptr);
    ASSERT_TRUE(Contains(ReadFile(fixture.sacm_absolute), "AP_nested"))
        << "fixture carries no nested package, so a bridged edit would pass too";

    const auto run = [&](core::commands::ICommand& command, const char* what) {
        bool library_primary = false;
        const core::commands::CommandResult result = RunOnBus(fixture, state, command, library_primary);
        EXPECT_TRUE(result.success) << what
                                    << " was refused, so it is still going through the bridge: " << result.error;
        EXPECT_TRUE(library_primary) << what << " did not reach the library at all";
        const std::string autosaved = ReadFile(fixture.sacm_absolute);
        EXPECT_TRUE(Contains(autosaved, "AP_nested")) << what << " deleted the nested package";
        EXPECT_TRUE(Contains(autosaved, kVendorAttributeMarker)) << what << " dropped the vendor attribute";
    };

    core::commands::AddAcpCommand add("element", "Sn1");
    run(add, "AddAcp");
    const std::string acp_id = add.GeneratedAcpId();
    ASSERT_FALSE(acp_id.empty());
    EXPECT_TRUE(Contains(ReadFile(fixture.sacm_absolute), "assuranceForge.acp"))
        << "the ACP tags are not in the saved file";

    parser::AcpRecord edited;
    edited.id = acp_id;
    edited.name = "Confidence in the test report";
    edited.target_kind = "element";
    edited.target_id = "Sn1";
    edited.resolution_kind = "none";
    core::commands::UpsertAcpCommand upsert(edited);
    run(upsert, "UpsertAcp");
    EXPECT_TRUE(Contains(ReadFile(fixture.sacm_absolute), "Confidence in the test report"))
        << "the edited ACP name is not in the saved file";

    core::commands::CreateConfidenceArgumentTreeForAcpCommand create_tree(acp_id);
    run(create_tree, "CreateConfidenceArgumentTree");
    const std::string with_tree = ReadFile(fixture.sacm_absolute);
    EXPECT_TRUE(Contains(with_tree, "assuranceForge.argumentPackage.purpose"))
        << "the confidence package carries no purpose tag, so it is an ordinary package:\n"
        << with_tree;
    EXPECT_TRUE(Contains(with_tree, create_tree.GeneratedTopGoalId())) << "the confidence top goal is not in the file";

    core::commands::RemoveAcpCommand remove(acp_id);
    run(remove, "RemoveAcp");
    EXPECT_FALSE(Contains(ReadFile(fixture.sacm_absolute), "assuranceForge.acp"))
        << "the ACP tags survived the removal";

    const core::audit::ReplayVerificationResult verified = core::audit::VerifyProject(fixture.project);
    EXPECT_TRUE(verified.success) << (verified.diagnostics.empty() ? std::string{} : verified.diagnostics.front());
}
