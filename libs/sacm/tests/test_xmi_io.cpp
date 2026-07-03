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

bool has_code(const std::vector<sacm::validation::Diagnostic>& diagnostics,
              std::string_view code) {
    return std::ranges::any_of(diagnostics, [&](const auto& diagnostic) {
        return diagnostic.code == code;
    });
}

TEST(Sacm23XmiConformance, SACM23_XMI_001_ImportsMinimalAssuranceCasePackage) {
    const LoadResult result = sacm::io::load_xmi_file(fixture("package-minimal-valid.sacm.xmi"),
                                                      LoadOptions{.mode = Mode::Strict});
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
    const LoadResult result = sacm::io::load_xmi_file(
        fixture("argument-claim-xsitype-valid.sacm.xmi"), LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(result.ok) << (result.diagnostics.empty() ? "" : result.diagnostics.front().message);
    const auto& document = *result.document;
    const auto* inference =
        document.find_as<sacm::model::AssertedInference>(ElementId{"inf_1"});
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
    const LoadResult result = sacm::io::load_xmi_file(
        fixture("invalid/ref-dangling-invalid.sacm.xmi"), LoadOptions{.mode = Mode::Strict});
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result.diagnostics, sacm::validation::codes::kRefDangling));
}

TEST(Sacm23Validation, SACM23_XMI_003_ReportsDuplicateIds) {
    const LoadResult result = sacm::io::load_xmi_file(
        fixture("invalid/ids-duplicate-invalid.sacm.xmi"), LoadOptions{.mode = Mode::Strict});
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result.diagnostics, sacm::validation::codes::kIdDuplicate));
}

TEST(Sacm23Validation, SACM23_XMI_001_RejectsNonPackageRoot) {
    const LoadResult result = sacm::io::load_xmi_file(fixture("invalid/root-invalid.sacm.xmi"));
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result.diagnostics, sacm::validation::codes::kXmiInvalidRoot));
}

TEST(Sacm23Validation, SACM23_SEC_001_RejectsDoctype) {
    const LoadResult result =
        sacm::io::load_xmi_file(fixture("invalid/sec-doctype-invalid.sacm.xmi"));
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result.diagnostics, sacm::validation::codes::kXmlDoctypeRejected));
}

TEST(Sacm23Validation, SACM23_VAL_001_DiagnosticsAreMachineReadable) {
    const LoadResult result = sacm::io::load_xmi_file(
        fixture("invalid/ref-dangling-invalid.sacm.xmi"), LoadOptions{.mode = Mode::Strict});
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
        "Goal",   "Strategy", "Solution",  "Canvas",
        "TreeItem", "ImGui",  "coordinate", "layout",
    };
    for (const auto& entry : std::filesystem::recursive_directory_iterator(include_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".h") {
            continue;
        }
        std::ifstream stream(entry.path());
        std::string content((std::istreambuf_iterator<char>(stream)),
                            std::istreambuf_iterator<char>());
        for (const std::string_view term : kForbidden) {
            EXPECT_EQ(content.find(term), std::string::npos)
                << entry.path().string() << " contains forbidden term '" << term << "'";
        }
    }
}

}  // namespace
