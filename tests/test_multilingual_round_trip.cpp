#include "core/audit/canonical_model_hash.h"
#include "core/library_package_projection.h"
#include "parser/model_utils.h"
#include "legacy_sacm/sacm_serializer.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/library_load.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <iostream>
#include <optional>

// Round-trip fidelity for elements carrying more than one language.
//
// Reported from a real project: a case with Japanese translations opened with a
// permanent "audit log divergence" that Reconcile could not clear. The audit
// store was not the problem. The replay verifier compares two canonical hashes,
// one of which is computed by serializing the projected package and reading it
// back (`core::library_canonical_hash`) and one of which is not
// (`core::library_canonical_hash_from_file`). When that round trip is not
// faithful the two never agree, Reconcile writes one of them, verification
// recomputes the other, and the project can never be made clean again.
//
// So the invariant these tests pin is not really about hashing. It is that
// projecting a document and serializing it must not change what the argument
// says -- which for a safety case is the whole point.

namespace {

// A claim as the reported project actually stores it: an English `name`, plus a
// reserved "sacm.import.name" TaggedValue holding the overflow languages that
// clause 8.6 cannot fit in the single name LangString.
//
// The second overflow entry carries **no** `lang`. That is the shape that broke:
// an overflow value is by definition a language that is *not* the primary, so
// one without a language must not be allowed to claim the primary's slot.
constexpr std::string_view kMultilingualCase = R"(<?xml version="1.0" encoding="UTF-8"?>
<xmi:XMI xmlns:S="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:version="2.0">
  <S:AssuranceCasePackage xmi:id="acp_1">
    <name content="Kitchen Blender Safety Case"/>
    <argumentPackage xmi:id="argpkg_1">
      <name content="Argument"/>
      <argumentElement xsi:type="S:Claim" xmi:id="G1">
        <name lang="en" content="The kitchen blender is acceptably safe for domestic food preparation."/>
        <description xmi:id="desc_1">
          <content>
            <value lang="en" content="The blender is safe in normal domestic use."/>
            <value lang="ja" content="ブレンダーは通常の家庭内使用において安全である。"/>
          </content>
        </description>
        <taggedValue xmi:id="tv_1">
          <key><value content="sacm.import.name"/></key>
          <content><value lang="ja" content="キッチン用ブレンダーは安全である。"/></content>
        </taggedValue>
        <taggedValue xmi:id="tv_2">
          <key><value content="sacm.import.name"/></key>
          <content><value content="キッチン用ブレンダーは安全である。"/></content>
        </taggedValue>
      </argumentElement>
      <argumentElement xsi:type="S:Claim" xmi:id="G2">
        <name lang="en" content="Blade guarding prevents contact during operation."/>
      </argumentElement>
      <argumentElement xsi:type="S:ArgumentReasoning" xmi:id="S1">
        <name content="Argue over hazards"/>
      </argumentElement>
      <argumentElement xsi:type="S:AssertedInference" xmi:id="inf_1" reasoning="S1" source="G2" target="G1"/>
    </argumentPackage>
  </S:AssuranceCasePackage>
</xmi:XMI>
)";

struct TempFile {
    std::filesystem::path path;
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

TempFile WriteFixture(const std::string& stem, std::string_view content) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("af_multilang_" + stem + "_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + ".sacm");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return TempFile{path};
}

const parser::SacmElement& RequireElement(const parser::AssuranceCase& model, const std::string& id) {
    const parser::SacmElement* element = parser::FindElementById(model, id);
    EXPECT_NE(element, nullptr) << "element " << id << " is missing";
    static const parser::SacmElement kEmpty;
    return element == nullptr ? kEmpty : *element;
}

} // namespace

// The claim's name is English. Nothing in a round trip may replace it with a
// translation of its description -- that changes what the argument asserts, at
// the top of the case, silently.
TEST(MultilingualRoundTrip, KeepsAClaimsNameOutOfItsOverflowTranslations) {
    const TempFile fixture = WriteFixture("name", kMultilingualCase);

    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(fixture.path);
    ASSERT_TRUE(loaded.ok && loaded.document != nullptr);

    const parser::AssuranceCase before = sacm_adapter::project_case(*loaded.document);
    ASSERT_EQ(RequireElement(before, "G1").name,
              "The kitchen blender is acceptably safe for domestic food preparation.");

    // Serialize the projected package and read it back, which is what
    // `core::library_canonical_hash` does on every verification.
    const std::string xml = sacm::serialize_sacm(core::project_library_package(*loaded.document));
    sacm_adapter::LibraryDocument reloaded;
    ASSERT_TRUE(sacm_adapter::reload_document(reloaded, xml));

    const parser::AssuranceCase after = sacm_adapter::project_case(reloaded);
    EXPECT_EQ(RequireElement(after, "G1").name,
              "The kitchen blender is acceptably safe for domestic food preparation.");
}

// The Japanese text must still be Japanese, and still be attached to the field
// and language it was written against.
TEST(MultilingualRoundTrip, KeepsEachTranslationUnderItsOwnLanguage) {
    const TempFile fixture = WriteFixture("langs", kMultilingualCase);

    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(fixture.path);
    ASSERT_TRUE(loaded.ok && loaded.document != nullptr);

    const std::string xml = sacm::serialize_sacm(core::project_library_package(*loaded.document));
    sacm_adapter::LibraryDocument reloaded;
    ASSERT_TRUE(sacm_adapter::reload_document(reloaded, xml));

    const parser::AssuranceCase before = sacm_adapter::project_case(*loaded.document);
    const parser::AssuranceCase after = sacm_adapter::project_case(reloaded);

    EXPECT_EQ(RequireElement(after, "G1").description_langs, RequireElement(before, "G1").description_langs);
    EXPECT_EQ(RequireElement(after, "G1").name_langs, RequireElement(before, "G1").name_langs);
}

// The invariant the replay verifier rests on: the canonical hash of a document
// must not depend on whether it was round-tripped through the projected package
// on the way. When it does, a project reports divergence that Reconcile cannot
// clear -- it writes one of the two hashes and verification recomputes the
// other.
TEST(MultilingualRoundTrip, HashesTheSameThroughEitherPipeline) {
    const TempFile fixture = WriteFixture("hash", kMultilingualCase);

    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(fixture.path);
    ASSERT_TRUE(loaded.ok && loaded.document != nullptr);

    const std::optional<std::string> round_tripped =
        core::library_canonical_hash(core::project_library_package(*loaded.document));
    const std::optional<std::string> from_file = core::library_canonical_hash_from_file(fixture.path);

    ASSERT_TRUE(round_tripped.has_value());
    ASSERT_TRUE(from_file.has_value());
    EXPECT_EQ(*round_tripped, *from_file);
}

// A case with no translations must keep working exactly as before. This is the
// control: it passed before the fix, and a fix that breaks it has traded one
// corruption for another.
TEST(MultilingualRoundTrip, LeavesASingleLanguageCaseAlone) {
    constexpr std::string_view kPlainCase = R"(<?xml version="1.0" encoding="UTF-8"?>
<xmi:XMI xmlns:S="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:version="2.0">
  <S:AssuranceCasePackage xmi:id="acp_1">
    <name content="Plain Case"/>
    <argumentPackage xmi:id="argpkg_1">
      <name content="Argument"/>
      <argumentElement xsi:type="S:Claim" xmi:id="G1">
        <name lang="en" content="The system is acceptably safe."/>
        <description xmi:id="desc_1">
          <content>
            <value lang="en" content="Established by the argument below."/>
          </content>
        </description>
      </argumentElement>
    </argumentPackage>
  </S:AssuranceCasePackage>
</xmi:XMI>
)";
    const TempFile fixture = WriteFixture("plain", kPlainCase);

    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(fixture.path);
    ASSERT_TRUE(loaded.ok && loaded.document != nullptr);

    const std::optional<std::string> round_tripped =
        core::library_canonical_hash(core::project_library_package(*loaded.document));
    const std::optional<std::string> from_file = core::library_canonical_hash_from_file(fixture.path);

    ASSERT_TRUE(round_tripped.has_value());
    ASSERT_TRUE(from_file.has_value());
    EXPECT_EQ(*round_tripped, *from_file);

    const parser::AssuranceCase model = sacm_adapter::project_case(*loaded.document);
    EXPECT_EQ(RequireElement(model, "G1").name, "The system is acceptably safe.");
}

// The reported project had accumulated *two* identical "sacm.import.name"
// TaggedValues and two identical descriptions on one claim -- the fingerprint of
// a cycle that adds something every time it runs. One faithful round trip is not
// enough to promise that; the second must produce the same document as the
// first, or a case degrades a little on every save.
TEST(MultilingualRoundTrip, ConvergesRatherThanAccumulatingOnRepeatedCycles) {
    const TempFile fixture = WriteFixture("repeat", kMultilingualCase);

    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(fixture.path);
    ASSERT_TRUE(loaded.ok && loaded.document != nullptr);

    const std::string first = sacm::serialize_sacm(core::project_library_package(*loaded.document));

    sacm_adapter::LibraryDocument second_doc;
    ASSERT_TRUE(sacm_adapter::reload_document(second_doc, first));
    const std::string second = sacm::serialize_sacm(core::project_library_package(second_doc));

    sacm_adapter::LibraryDocument third_doc;
    ASSERT_TRUE(sacm_adapter::reload_document(third_doc, second));
    const std::string third = sacm::serialize_sacm(core::project_library_package(third_doc));

    EXPECT_EQ(second, third) << "the document is still changing on every cycle";

    const parser::AssuranceCase model = sacm_adapter::project_case(third_doc);
    EXPECT_EQ(RequireElement(model, "G1").name,
              "The kitchen blender is acceptably safe for domestic food preparation.");
}

// A net over every SACM fixture in the repository, not just the one shape that
// broke.
//
// The replay verifier hashes the replayed document through
// `library_canonical_hash` (which serializes and reads back) and the on-disk
// file through `library_canonical_hash_from_file` (which does not). That
// asymmetry is worth keeping -- it is what surfaced this bug at all -- but it
// means any document that does not survive a round trip becomes a project that
// reports divergence forever. Catching that here is the difference between a
// failing test and a user who cannot open their safety case.
TEST(MultilingualRoundTrip, EveryFixtureHashesTheSameThroughEitherPipeline) {
    const std::filesystem::path roots[] = {
        std::filesystem::path(AF_REPO_ROOT) / "tests" / "data",
        std::filesystem::path(AF_REPO_ROOT) / "libs" / "sacm" / "tests" / "data" / "sacm23",
    };

    int checked = 0;
    for (const std::filesystem::path& root : roots) {
        std::error_code ec;
        if (!std::filesystem::exists(root, ec)) {
            continue;
        }
        for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::string name = entry.path().filename().string();
            if (name.find(".sacm") == std::string::npos && name.find(".xmi") == std::string::npos) {
                continue;
            }
            // Fixtures that are deliberately unreadable are not this test's
            // business; it asks only whether a document the library CAN read
            // hashes the same either way.
            sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(entry.path());
            if (!loaded.ok || loaded.document == nullptr) {
                continue;
            }

            const std::optional<std::string> round_tripped =
                core::library_canonical_hash(core::project_library_package(*loaded.document));
            const std::optional<std::string> from_file = core::library_canonical_hash_from_file(entry.path());
            if (!round_tripped.has_value() || !from_file.has_value()) {
                continue;
            }
            ++checked;
            EXPECT_EQ(*round_tripped, *from_file)
                << "projecting and serializing changed this document: " << entry.path().string();
        }
    }

    EXPECT_GT(checked, 0) << "no fixtures were readable, so this proves nothing";
    std::cout << "  checked " << checked << " fixtures" << std::endl;
}
