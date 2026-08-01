#include "sacm/io/xmi.h"

#include "sacm/compare/semantic_compare.h"
#include "sacm/validation/codes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace {

using sacm::io::LoadOptions;
using sacm::io::LoadResult;
using sacm::io::Mode;
using sacm::model::ElementId;

std::filesystem::path fixture(std::string_view name) {
    return std::filesystem::path(SACM_TEST_DATA_DIR) / "sacm23" / name;
}

bool has_code(const std::vector<sacm::validation::Diagnostic>& diagnostics, std::string_view code) {
    return std::ranges::any_of(diagnostics, [&](const auto& diagnostic) { return diagnostic.code == code; });
}

TEST(Sacm23XmiConformance, SACM23_XMI_001_ImportsMinimalAssuranceCasePackage) {
    const LoadResult result =
        sacm::io::load_xmi_file(fixture("package-minimal-valid.sacm.xmi"), LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(result.ok) << (result.diagnostics.empty() ? "" : result.diagnostics.front().message);
    ASSERT_TRUE(result.document.has_value());
    const auto& document = *result.document;
    ASSERT_EQ(document.roots().size(), 1u);
    EXPECT_EQ(document.roots().front()->name().content, "Minimal Case");

    const auto* claim = document.find_as<sacm::model::Claim>(ElementId{"claim_1"});
    ASSERT_NE(claim, nullptr);
    EXPECT_EQ(claim->name().content, "Top Claim");
    ASSERT_EQ(claim->descriptions().size(), 1u);
    EXPECT_EQ(*claim->description().find("en"), "The system is acceptably safe.");
    EXPECT_EQ(result.source_namespace, "http://www.omg.org/spec/SACM/20220301");
}

TEST(Sacm23XmiConformance, SACM23_XMI_001_AcceptsXmiWrapperAndXsiType) {
    const LoadResult result =
        sacm::io::load_xmi_file(fixture("argument-claim-xsitype-valid.sacm.xmi"), LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(result.ok) << (result.diagnostics.empty() ? "" : result.diagnostics.front().message);
    const auto& document = *result.document;
    const auto* inference = document.find_as<sacm::model::AssertedInference>(ElementId{"inf_1"});
    ASSERT_NE(inference, nullptr);
    ASSERT_EQ(inference->sources().size(), 1u);
    EXPECT_EQ(inference->sources().front().value(), "claim_sub");
    ASSERT_EQ(inference->targets().size(), 1u);
    EXPECT_EQ(inference->targets().front().value(), "claim_top");
    ASSERT_TRUE(inference->reasoning().has_value());
    EXPECT_EQ(inference->reasoning()->value(), "ar_1");
}

TEST(Sacm23XmiConformance, SACM23_XMI_002_ImportIsPrefixIndependent) {
    // Same document, three different prefix spellings.
    constexpr std::string_view kTemplateA = R"(<?xml version="1.0"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:id="acp_1"><name content="Case"/></sacm:AssuranceCasePackage>)";
    constexpr std::string_view kTemplateB = R"(<?xml version="1.0"?>
<S:AssuranceCasePackage xmlns:S="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:id="acp_1"><name content="Case"/></S:AssuranceCasePackage>)";
    constexpr std::string_view kTemplateC = R"(<?xml version="1.0"?>
<AssuranceCasePackage xmlns="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:id="acp_1"><name content="Case"/></AssuranceCasePackage>)";

    const LoadResult a = sacm::io::load_xmi_string(kTemplateA, LoadOptions{.mode = Mode::Strict});
    const LoadResult b = sacm::io::load_xmi_string(kTemplateB, LoadOptions{.mode = Mode::Strict});
    const LoadResult c = sacm::io::load_xmi_string(kTemplateC, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(a.ok);
    ASSERT_TRUE(b.ok);
    ASSERT_TRUE(c.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(*a.document, *b.document).empty());
    EXPECT_TRUE(sacm::compare::semantic_compare(*a.document, *c.document).empty());
}

TEST(Sacm23Validation, SACM23_XMI_003_ReportsBrokenReference) {
    const LoadResult result =
        sacm::io::load_xmi_file(fixture("invalid/ref-dangling-invalid.sacm.xmi"), LoadOptions{.mode = Mode::Strict});
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result.diagnostics, sacm::validation::codes::kRefDangling));
}

TEST(Sacm23Validation, SACM23_XMI_003_ReportsDuplicateIds) {
    const LoadResult result =
        sacm::io::load_xmi_file(fixture("invalid/ids-duplicate-invalid.sacm.xmi"), LoadOptions{.mode = Mode::Strict});
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result.diagnostics, sacm::validation::codes::kIdDuplicate));
}

// A generated id ("generated_N") minted for an id-less element on a prior load
// can be persisted (e.g. onto a TaggedValue) and reappear as an explicit id on a
// later load. The reader must not manufacture a spurious duplicate by minting the
// same "generated_N" again for an id-less element it encounters first. Here the
// id-less <description> is parsed before the taggedValue that already carries
// "generated_1"; without pre-reserving every explicit id, the description would
// be minted "generated_1" and collide (SACM-ID-001). Regression for a real
// round-trip failure: a project's ACP/purpose TaggedValues carried persisted
// generated ids and the re-load reported them as duplicates.
TEST(Sacm23Validation, SACM23_XMI_003_GeneratedIdDoesNotCollideWithPersistedExplicitId) {
    const std::string xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301"
    xmlns:xmi="http://www.omg.org/spec/XMI/20131001"
    xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:id="CASE">
  <argumentPackage xmi:id="AP">
    <argumentElement xsi:type="sacm:Claim" xmi:id="G1">
      <description>
        <content lang="en" content="a claim with an id-less description" />
      </description>
      <taggedValue xmi:id="generated_1" key="assuranceForge.example" value="v" />
    </argumentElement>
  </argumentPackage>
</sacm:AssuranceCasePackage>)";
    const LoadResult result = sacm::io::load_xmi_string(xml, LoadOptions{.mode = Mode::Strict});
    EXPECT_TRUE(result.ok) << "load failed; a persisted generated id collided with a freshly minted one";
    EXPECT_FALSE(has_code(result.diagnostics, sacm::validation::codes::kIdDuplicate))
        << "reader minted 'generated_1' for the id-less description, duplicating the persisted tag id";
}

// End-to-end closure of the above through the writer, mirroring the
// core::library_xmi_from_package (serialize -> reload -> save) pipeline whose
// failure surfaced the bug: load a document carrying a persisted generated id
// alongside an id-less element, save it, and load the result again. Both loads
// must be clean and the two models must match. (Without the pre-reservation the
// first load already fails on a duplicate id, so this gates the fix too.)
TEST(Sacm23Validation, SACM23_XMI_003_PersistedGeneratedIdRoundTripsCleanly) {
    const std::string xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301"
    xmlns:xmi="http://www.omg.org/spec/XMI/20131001"
    xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:id="CASE">
  <argumentPackage xmi:id="AP">
    <argumentElement xsi:type="sacm:Claim" xmi:id="G1">
      <description>
        <content lang="en" content="a claim with an id-less description" />
      </description>
      <taggedValue xmi:id="generated_1" key="assuranceForge.example" value="v" />
    </argumentElement>
  </argumentPackage>
</sacm:AssuranceCasePackage>)";
    const LoadResult first = sacm::io::load_xmi_string(xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(first.ok) << "first load failed on a persisted generated id";
    ASSERT_TRUE(first.document.has_value());

    const sacm::io::SaveResult saved = sacm::io::save_xmi_string(*first.document);
    ASSERT_TRUE(saved.ok);

    const LoadResult second = sacm::io::load_xmi_string(saved.xml);
    ASSERT_TRUE(second.ok);
    EXPECT_FALSE(has_code(second.diagnostics, sacm::validation::codes::kIdDuplicate));
    EXPECT_TRUE(sacm::compare::semantic_compare(*first.document, *second.document).empty())
        << "round-trip changed the model";
}

TEST(Sacm23Validation, SACM23_XMI_001_RejectsNonPackageRoot) {
    const LoadResult result = sacm::io::load_xmi_file(fixture("invalid/root-invalid.sacm.xmi"));
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result.diagnostics, sacm::validation::codes::kXmiInvalidRoot));
}

TEST(Sacm23Validation, SACM23_SEC_001_RejectsDoctype) {
    const LoadResult result = sacm::io::load_xmi_file(fixture("invalid/sec-doctype-invalid.sacm.xmi"));
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result.diagnostics, sacm::validation::codes::kXmlDoctypeRejected));
}

TEST(Sacm23Validation, SACM23_VAL_001_DiagnosticsAreMachineReadable) {
    const LoadResult result =
        sacm::io::load_xmi_file(fixture("invalid/ref-dangling-invalid.sacm.xmi"), LoadOptions{.mode = Mode::Strict});
    ASSERT_FALSE(result.diagnostics.empty());
    const auto it = std::ranges::find_if(result.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == sacm::validation::codes::kRefDangling;
    });
    ASSERT_NE(it, result.diagnostics.end());
    EXPECT_EQ(it->severity, sacm::validation::Severity::Error);
    EXPECT_FALSE(it->requirement_id.empty());
    EXPECT_FALSE(it->message.empty());
    ASSERT_EQ(it->affected.size(), 2u);
    EXPECT_EQ(it->affected[0].value(), "inf_1");
    EXPECT_EQ(it->affected[1].value(), "claim_missing");
}

TEST(Sacm23Library, SACM23_LIB_003_PublicApiDoesNotExposeLayoutOrGoalTerminology) {
    // Scan every installed public header for GSN/UI/layout vocabulary.
    const std::filesystem::path include_dir =
        std::filesystem::path(SACM_TEST_DATA_DIR).parent_path().parent_path() / "include";
    ASSERT_TRUE(std::filesystem::is_directory(include_dir));
    constexpr std::string_view kForbidden[] = {
        "Goal",
        "Strategy",
        "Solution",
        "Canvas",
        "TreeItem",
        "ImGui",
        "coordinate",
        "layout",
    };
    for (const auto& entry : std::filesystem::recursive_directory_iterator(include_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".h") {
            continue;
        }
        std::ifstream stream(entry.path());
        std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        for (const std::string_view term : kForbidden) {
            EXPECT_EQ(content.find(term), std::string::npos)
                << entry.path().string() << " contains forbidden term '" << term << "'";
        }
    }
}

} // namespace
