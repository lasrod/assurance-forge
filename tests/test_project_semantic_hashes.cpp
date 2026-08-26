#include "core/project_file_io.h"

#include "core/project_model.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

// The manifest's semantic hashes (`semanticHash`, `elementIndexHash`,
// `relationshipGraphHash` in `af.proj`) have to be computed over the elements
// the argument actually contains.
//
// Reported from a saved example project: every argument written in the XMI
// dialect the application itself produces (`<argumentElement
// xsi:type="sacm:Claim">`) carried three identical hashes -- the SHA-256 of the
// empty string -- because the hash read the file through the legacy tag-dialect
// parser, which recognises `<claim>` and sees nothing in `<argumentElement>`.
// The load report then said "Semantic hashes recalculated" over a digest of
// nothing. A hash that is the same for every argument is not evidence of
// anything, and the manifest is read as evidence.

namespace {

// SHA-256 of "" -- what an empty element list hashes to.
constexpr const char* kEmptyDigest = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

constexpr const char* kXmiHeader =
    R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:version="2.0" xmi:id="AC1">
  <name content="Sample" />
  <argumentPackage xmi:id="AP1">
    <name content="Args" />
)";

constexpr const char* kXmiFooter = R"(  </argumentPackage>
</sacm:AssuranceCasePackage>
)";

std::string Claim(const std::string& id, const std::string& text) {
    return "    <argumentElement xsi:type=\"sacm:Claim\" xmi:id=\"" + id + "\">\n      <name content=\"" + id +
           "\" />\n      <description xmi:id=\"d_" + id +
           "\">\n        <content>\n          <value lang=\"en\" content=\"" + text +
           "\" />\n        </content>\n      </description>\n    </argumentElement>\n";
}

std::string Inference(const std::string& id, const std::string& source, const std::string& target) {
    return "    <argumentElement xsi:type=\"sacm:AssertedInference\" xmi:id=\"" + id + "\" source=\"" + source +
           "\" target=\"" + target + "\">\n      <name />\n    </argumentElement>\n";
}

std::string TwoClaimArgument(const std::string& supporting_text, bool linked) {
    std::string xml = kXmiHeader;
    xml += Claim("G1", "The system is acceptably safe.");
    xml += Claim("G2", supporting_text);
    if (linked)
        xml += Inference("R1", "G2", "G1");
    xml += kXmiFooter;
    return xml;
}

// The dialect the legacy parser was written for, which older project files
// still carry.
constexpr const char* kLegacyDialectArgument = R"(<?xml version="1.0" encoding="UTF-8"?>
<assuranceCasePackage id="AC1" name="Legacy">
  <argumentPackage id="AP1">
    <claim id="G1" name="G1" content="The system is acceptably safe." />
    <claim id="G2" name="G2" content="Hazards are mitigated." />
    <assertedInference id="R1">
      <source ref="G2" />
      <target ref="G1" />
    </assertedInference>
  </argumentPackage>
</assuranceCasePackage>
)";

struct HashFixture {
    std::filesystem::path root;

    HashFixture() {
        root = std::filesystem::temp_directory_path() /
               ("af_semantic_hashes_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "_" +
                ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
    }
    ~HashFixture() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    std::filesystem::path Write(const std::string& name, std::string_view content) const {
        const std::filesystem::path path = root / name;
        std::ofstream out(path, std::ios::binary);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        return path;
    }

    core::ProjectFileEntry Hash(const std::string& name, std::string_view content) const {
        const std::filesystem::path path = Write(name, content);
        core::ProjectFileEntry entry;
        entry.id = name;
        entry.relativePath = name;
        entry.role = core::ProjectFileRole::SacmArgument;
        core::ComputeSacmHashes(entry, path);
        return entry;
    }
};

} // namespace

TEST(ProjectSemanticHashes, AnXmiArgumentHashesItsElementsRatherThanNothing) {
    HashFixture fixture;
    const core::ProjectFileEntry entry =
        fixture.Hash("argument.sacm", TwoClaimArgument("Hazards are mitigated.", true));

    EXPECT_EQ(entry.parseStatus, "parsed");
    EXPECT_TRUE(entry.lastError.empty()) << entry.lastError;
    EXPECT_FALSE(entry.semanticHash.empty());
    EXPECT_NE(entry.semanticHash, kEmptyDigest) << "the digest of no elements is the reported defect";
    EXPECT_NE(entry.elementIndexHash, kEmptyDigest);
    EXPECT_NE(entry.relationshipGraphHash, kEmptyDigest);
    // Three different questions, so three different answers.
    EXPECT_NE(entry.semanticHash, entry.elementIndexHash);
    EXPECT_NE(entry.elementIndexHash, entry.relationshipGraphHash);
}

TEST(ProjectSemanticHashes, TheSameArgumentHashesTheSameTwice) {
    HashFixture fixture;
    const core::ProjectFileEntry first = fixture.Hash("a.sacm", TwoClaimArgument("Hazards are mitigated.", true));
    const core::ProjectFileEntry second = fixture.Hash("b.sacm", TwoClaimArgument("Hazards are mitigated.", true));
    EXPECT_EQ(first.semanticHash, second.semanticHash);
    EXPECT_EQ(first.elementIndexHash, second.elementIndexHash);
    EXPECT_EQ(first.relationshipGraphHash, second.relationshipGraphHash);
}

TEST(ProjectSemanticHashes, ChangingAClaimsTextMovesOnlyTheSemanticHash) {
    HashFixture fixture;
    const core::ProjectFileEntry before = fixture.Hash("a.sacm", TwoClaimArgument("Hazards are mitigated.", true));
    const core::ProjectFileEntry after =
        fixture.Hash("b.sacm", TwoClaimArgument("All identified hazards are mitigated.", true));
    EXPECT_NE(before.semanticHash, after.semanticHash);
    EXPECT_EQ(before.elementIndexHash, after.elementIndexHash) << "the same elements are present";
    EXPECT_EQ(before.relationshipGraphHash, after.relationshipGraphHash) << "the same relationships hold";
}

TEST(ProjectSemanticHashes, RemovingARelationshipMovesTheRelationshipGraphHash) {
    HashFixture fixture;
    const core::ProjectFileEntry linked = fixture.Hash("a.sacm", TwoClaimArgument("Hazards are mitigated.", true));
    const core::ProjectFileEntry unlinked = fixture.Hash("b.sacm", TwoClaimArgument("Hazards are mitigated.", false));
    EXPECT_NE(linked.relationshipGraphHash, unlinked.relationshipGraphHash);
    EXPECT_NE(linked.elementIndexHash, unlinked.elementIndexHash) << "the relationship is an element too";
}

TEST(ProjectSemanticHashes, ReformattingTheFileMovesOnlyTheRawHash) {
    HashFixture fixture;
    const std::string original = TwoClaimArgument("Hazards are mitigated.", true);
    std::string reformatted;
    for (const char character : original)
        reformatted += character == '\n' ? std::string("\r\n") : std::string(1, character);

    core::AssuranceProject project;
    project.rootPath = fixture.root;
    core::ProjectFileEntry a;
    a.id = "a";
    a.relativePath = "a.sacm";
    a.role = core::ProjectFileRole::SacmArgument;
    core::ProjectFileEntry b = a;
    b.id = "b";
    b.relativePath = "b.sacm";
    fixture.Write("a.sacm", original);
    fixture.Write("b.sacm", reformatted);
    ASSERT_TRUE(core::RefreshEntryHashes(project, a, false).has_value());
    ASSERT_TRUE(core::RefreshEntryHashes(project, b, false).has_value());

    EXPECT_NE(a.rawHash, b.rawHash);
    EXPECT_EQ(a.semanticHash, b.semanticHash);
    EXPECT_EQ(a.elementIndexHash, b.elementIndexHash);
    EXPECT_EQ(a.relationshipGraphHash, b.relationshipGraphHash);
    EXPECT_EQ(a.state, core::ProjectFileState::Clean);
}

// The tag dialect the legacy parser reads keeps hashing: a project that
// predates the XMI writer does not lose its hashes to the fix for the projects
// that postdate it.
TEST(ProjectSemanticHashes, ALegacyDialectArgumentStillHashesItsElements) {
    HashFixture fixture;
    const core::ProjectFileEntry entry = fixture.Hash("legacy.sacm", kLegacyDialectArgument);
    EXPECT_EQ(entry.parseStatus, "parsed") << entry.lastError;
    EXPECT_NE(entry.semanticHash, kEmptyDigest);
    EXPECT_NE(entry.elementIndexHash, kEmptyDigest);
    EXPECT_NE(entry.relationshipGraphHash, kEmptyDigest);
}

TEST(ProjectSemanticHashes, AFileNeitherReaderAcceptsIsAParseErrorWithNoHashes) {
    HashFixture fixture;
    const core::ProjectFileEntry entry = fixture.Hash("broken.sacm", "<not xml");
    EXPECT_EQ(entry.parseStatus, "parseError");
    EXPECT_EQ(entry.state, core::ProjectFileState::ParseError);
    EXPECT_FALSE(entry.lastError.empty());
    EXPECT_TRUE(entry.semanticHash.empty());
    EXPECT_TRUE(entry.elementIndexHash.empty());
    EXPECT_TRUE(entry.relationshipGraphHash.empty());
}
