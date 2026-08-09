#include "sacm/compare/semantic_compare.h"
#include "sacm/io/xmi.h"
#include "sacm/model/document.h"
#include "sacm/validation/codes.h"
#include "sacm/validation/validate.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace {

using sacm::commands::AddTaggedValue;
using sacm::commands::CreateArgumentPackage;
using sacm::commands::CreateAssuranceCasePackage;
using sacm::commands::CreateClaim;
using sacm::commands::SetDescription;
using sacm::commands::SetDescriptionAt;
using sacm::commands::SetGid;
using sacm::commands::SetName;
using sacm::io::LoadOptions;
using sacm::io::LoadResult;
using sacm::io::Mode;
using sacm::io::SaveOptions;
using sacm::model::Document;
using sacm::model::ElementId;

std::filesystem::path fixture(std::string_view name) {
    return std::filesystem::path(SACM_TEST_DATA_DIR) / "sacm23" / name;
}

bool has_code(const std::vector<sacm::validation::Diagnostic>& diagnostics, std::string_view code) {
    return std::ranges::any_of(diagnostics, [&](const auto& diagnostic) { return diagnostic.code == code; });
}

TEST(Sacm23BaseModel, SACM23_BASE_001_InheritedMetadataRoundTrips) {
    const LoadResult first =
        sacm::io::load_xmi_file(fixture("base-metadata-valid.sacm.xmi"), LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(first.ok) << (first.diagnostics.empty() ? "" : first.diagnostics.front().message);
    const auto& document = *first.document;

    // gid, multilang description, note, tagged value on the package.
    const auto& acp = *document.roots().front();
    EXPECT_EQ(acp.gid(), "urn:example:case-1");
    ASSERT_EQ(acp.descriptions().size(), 1u);
    EXPECT_EQ(*acp.description().find("ja"), "基本メタデータ付きのケース。");
    ASSERT_EQ(acp.notes().size(), 1u);
    EXPECT_EQ(acp.notes().front()->content().primary(), "A reviewer note.");
    ASSERT_EQ(acp.tagged_values().size(), 1u);
    EXPECT_EQ(acp.tagged_values().front()->key().primary(), "reviewStatus");
    EXPECT_EQ(acp.tagged_values().front()->content().primary(), "approved");

    // Abstraction and citation flags with implementation constraint.
    const auto* pattern = document.find_as<sacm::model::Claim>(ElementId{"claim_pattern"});
    ASSERT_NE(pattern, nullptr);
    EXPECT_TRUE(pattern->is_abstract());
    ASSERT_EQ(pattern->implementation_constraints().size(), 1u);
    const auto* concrete = document.find_as<sacm::model::Claim>(ElementId{"claim_concrete"});
    ASSERT_NE(concrete, nullptr);
    EXPECT_TRUE(concrete->is_citation());
    ASSERT_TRUE(concrete->cited_element().has_value());
    EXPECT_EQ(concrete->cited_element()->value(), "claim_pattern");

    // Full validation is clean, and everything survives a round-trip.
    EXPECT_TRUE(sacm::validation::validate(document).empty());
    const auto saved = sacm::io::save_xmi_string(document);
    ASSERT_TRUE(saved.ok);
    const LoadResult second = sacm::io::load_xmi_string(saved.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(second.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(document, *second.document).empty());
}

TEST(Sacm23BaseModel, SACM23_BASE_002_CitationWithoutFlagIsInvalid) {
    const LoadResult result =
        sacm::io::load_xmi_file(fixture("invalid/citation-flag-invalid.sacm.xmi"), LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(result.document.has_value());
    const auto diagnostics = sacm::validation::validate(*result.document);
    EXPECT_TRUE(has_code(diagnostics, sacm::validation::codes::kCitationInvalid));
}

TEST(Sacm23BaseModel, SACM23_BASE_002_ImplementationConstraintRequiresAbstract) {
    Document document;
    ASSERT_TRUE(document.apply(CreateAssuranceCasePackage{.id = ElementId{"acp_1"}, .name = "A"}).applied);
    ASSERT_TRUE(
        document.apply(CreateArgumentPackage{.parent = ElementId{"acp_1"}, .id = ElementId{"argpkg_1"}, .name = "Arg"})
            .applied);
    ASSERT_TRUE(
        document.apply(CreateClaim{.parent = ElementId{"argpkg_1"}, .id = ElementId{"claim_1"}, .name = "C"}).applied);
    // Build the invalid state via a fixture-load instead: commands cannot
    // create implementation constraints yet, so exercise the validator on
    // the abstract-pattern fixture with the flag flipped through XML.
    constexpr std::string_view kXml = R"(<?xml version="1.0"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:id="acp_1">
  <name content="Case"/>
  <argumentPackage xmi:id="argpkg_1">
    <name content="Arg"/>
    <argumentElement xsi:type="sacm:Claim" xmi:id="claim_1">
      <name content="Not abstract"/>
      <implementationConstraint xmi:id="ic_1">
        <content><value lang="en" content="constraint"/></content>
      </implementationConstraint>
    </argumentElement>
  </argumentPackage>
</sacm:AssuranceCasePackage>)";
    const LoadResult loaded = sacm::io::load_xmi_string(kXml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(loaded.ok);
    const auto diagnostics = sacm::validation::validate(*loaded.document);
    EXPECT_TRUE(has_code(diagnostics, sacm::validation::codes::kCitationInvalid));
}

TEST(Sacm23BaseModel, SACM23_BASE_001_MultiLanguageNameMapsToTaggedValue) {
    // Legacy multi-language names keep one LangString name (clause 8.6) and
    // preserve the remaining languages via TaggedValue "sacm.import.name".
    constexpr std::string_view kXml = R"(<?xml version="1.0"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="acp_1">
  <name>
    <content lang="en">Open Autonomy Safety Case</content>
    <content lang="ja">オープン自律安全ケース</content>
  </name>
</sacm:AssuranceCasePackage>)";
    const LoadResult loaded = sacm::io::load_xmi_string(kXml);
    ASSERT_TRUE(loaded.ok);
    const auto& acp = *loaded.document->roots().front();
    EXPECT_EQ(acp.name().lang, "en");
    EXPECT_EQ(acp.name().content, "Open Autonomy Safety Case");
    ASSERT_EQ(acp.tagged_values().size(), 1u);
    EXPECT_EQ(acp.tagged_values().front()->key().primary(), "sacm.import.name");
    EXPECT_EQ(*acp.tagged_values().front()->content().find("ja"), "オープン自律安全ケース");

    // Stable through a strict round-trip (TaggedValue is standard SACM).
    const auto saved = sacm::io::save_xmi_string(*loaded.document);
    ASSERT_TRUE(saved.ok);
    const LoadResult reloaded = sacm::io::load_xmi_string(saved.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(reloaded.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(*loaded.document, *reloaded.document).empty());
}

// The losslessness gate. Round-trip testing structurally cannot catch data lost
// at import -- anything dropped on the way in is absent from both sides of the
// comparison, which is exactly how the vendor-attribute defect survived every
// existing test. An unprefixed attribute the serialization does not define is
// therefore preserved and reported rather than quietly ignored.
TEST(Sacm23BaseModel, SACM23_XMI_003_UnknownUnprefixedAttributeIsPreservedNotIgnored) {
    constexpr std::string_view kXml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" )"
        R"(xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:version="2.0" xmi:id="acp_1" )"
        R"(confidenceScore="0.87"><name content="Case"/></sacm:AssuranceCasePackage>)";

    const LoadResult loaded = sacm::io::load_xmi_string(kXml);
    ASSERT_TRUE(loaded.ok);
    const auto& acp = *loaded.document->roots().front();
    ASSERT_EQ(acp.preserved_attributes().size(), 1u)
        << "an attribute the reader does not understand was dropped without trace";
    EXPECT_NE(acp.preserved_attributes().front().find("confidenceScore"), std::string::npos);

    // Strict save refuses; compatibility save round-trips it.
    EXPECT_FALSE(sacm::io::save_xmi_string(*loaded.document).ok);
    const auto compat = sacm::io::save_xmi_string(*loaded.document, SaveOptions{.mode = Mode::Tolerant});
    ASSERT_TRUE(compat.ok);
    EXPECT_NE(compat.xml.find(R"(confidenceScore="0.87")"), std::string::npos);

    // Attributes the serialization *does* define must not trip the gate --
    // otherwise every real document would look like it carried vendor content.
    constexpr std::string_view kKnown =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" )"
        R"(xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:version="2.0" xmi:id="acp_1" )"
        R"(gid="urn:x" isCitation="false"><name content="Case"/></sacm:AssuranceCasePackage>)";
    const LoadResult clean = sacm::io::load_xmi_string(kKnown);
    ASSERT_TRUE(clean.ok);
    EXPECT_TRUE(clean.document->roots().front()->preserved_attributes().empty())
        << "a normative attribute was misreported as unknown";
}

// Vendor-extension *attributes* were silently discarded: no diagnostic, nothing
// in preserved content, and strict save therefore succeeded while dropping
// them. Unknown child elements were handled correctly all along, so the two
// paths must now behave the same -- preserve, diagnose, refuse strict save.
TEST(Sacm23BaseModel, SACM23_COMPAT_001_VendorAttributesPreservedAndStrictSaveRefuses) {
    constexpr std::string_view kXml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" )"
        R"(xmlns:xmi="http://www.omg.org/spec/XMI/20131001" )"
        R"(xmlns:acme="http://acme.example/sacm" xmi:version="2.0" xmi:id="acp_1" )"
        R"(acme:owner="alice"><name content="Case"/></sacm:AssuranceCasePackage>)";

    const LoadResult loaded = sacm::io::load_xmi_string(kXml);
    ASSERT_TRUE(loaded.ok);
    const auto& acp = *loaded.document->roots().front();
    ASSERT_EQ(acp.preserved_attributes().size(), 1u) << "vendor attribute was dropped";
    EXPECT_NE(acp.preserved_attributes().front().find("acme:owner"), std::string::npos);

    // Strict save refuses rather than silently dropping it.
    const auto strict = sacm::io::save_xmi_string(*loaded.document);
    EXPECT_FALSE(strict.ok) << "strict save succeeded while discarding a vendor attribute";
    EXPECT_TRUE(has_code(strict.diagnostics, sacm::validation::codes::kXmiStrictSaveRefused));

    // Compatibility save re-emits it verbatim and stays semantically stable.
    const auto compat = sacm::io::save_xmi_string(*loaded.document, SaveOptions{.mode = Mode::Tolerant});
    ASSERT_TRUE(compat.ok);
    EXPECT_NE(compat.xml.find(R"(acme:owner="alice")"), std::string::npos);
    const LoadResult reloaded = sacm::io::load_xmi_string(compat.xml);
    ASSERT_TRUE(reloaded.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(*loaded.document, *reloaded.document).empty());
}

TEST(Sacm23BaseModel, SACM23_COMPAT_001_VendorContentPreservedAndStrictSaveRefuses) {
    const LoadResult loaded = sacm::io::load_xmi_file(fixture("vendor-extension-valid.sacm.xmi"));
    ASSERT_TRUE(loaded.ok);
    const auto& acp = *loaded.document->roots().front();
    ASSERT_EQ(acp.preserved_content().size(), 1u);
    EXPECT_NE(acp.preserved_content().front().xml.find("vendorMetadata"), std::string::npos);

    // Strict save refuses rather than silently dropping.
    const auto strict = sacm::io::save_xmi_string(*loaded.document);
    EXPECT_FALSE(strict.ok);
    EXPECT_TRUE(has_code(strict.diagnostics, sacm::validation::codes::kXmiStrictSaveRefused));

    // Compatibility save re-emits the fragment and stays semantically stable.
    const auto compat = sacm::io::save_xmi_string(*loaded.document, SaveOptions{.mode = Mode::Tolerant});
    ASSERT_TRUE(compat.ok);
    EXPECT_NE(compat.xml.find("vendorMetadata"), std::string::npos);
    const LoadResult reloaded = sacm::io::load_xmi_string(compat.xml);
    ASSERT_TRUE(reloaded.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(*loaded.document, *reloaded.document).empty());
}

// Legacy Assurance Forge files split a claim's text across a `content=`
// attribute (the statement) and a `<description>` child (a secondary note).
// SACM has one Description that provides the claim's content (clause 8.9), so
// the statement must be the primary Description and the note secondary --
// otherwise description() returns the note and the statement is buried.
TEST(Sacm23BaseModel, SACM23_XMI_001_LegacyContentStatementIsThePrimaryDescription) {
    constexpr std::string_view kXml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:version="2.0" xmi:id="acp_1">
  <argumentPackage xmi:id="ap_1">
    <argumentElement xsi:type="sacm:Claim" xmi:id="G1" content="The system is safe">
      <name content="Top Goal"/>
      <description><content><value lang="en" content="A secondary note"/></content></description>
    </argumentElement>
  </argumentPackage>
</sacm:AssuranceCasePackage>)";

    const LoadResult loaded = sacm::io::load_xmi_string(kXml);
    ASSERT_TRUE(loaded.ok);
    const auto* claim = loaded.document->find_as<sacm::model::Claim>(ElementId{"G1"});
    ASSERT_NE(claim, nullptr);

    // Both texts are kept, and the statement is primary.
    ASSERT_EQ(claim->descriptions().size(), 2u) << "the note was lost";
    EXPECT_EQ(claim->description().primary(), "The system is safe")
        << "the content= statement must be the primary Description";
    EXPECT_EQ(claim->descriptions()[1]->content().primary(), "A secondary note");

    // Both survive a round-trip.
    const sacm::io::SaveResult saved = sacm::io::save_xmi_string(*loaded.document);
    ASSERT_TRUE(saved.ok);
    const LoadResult reloaded = sacm::io::load_xmi_string(saved.xml);
    ASSERT_TRUE(reloaded.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(*loaded.document, *reloaded.document).empty());
}

// Clause 8.2 makes gid String[0..1], so an explicit gid="" and an absent gid
// are different documents. Modelling gid as a plain string with
// empty-means-absent silently rewrote the first into the second on save, which
// is exactly the kind of quiet edit the source-of-truth rule forbids.
TEST(Sacm23BaseModel, SACM23_BASE_001_EmptyGidIsDistinctFromAbsentGid) {
    const auto load = [](std::string_view gid_attribute) {
        return sacm::io::load_xmi_string(
            std::format(R"(<?xml version="1.0" encoding="UTF-8"?>)"
                        R"(<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" )"
                        R"(xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:version="2.0" )"
                        R"(xmi:id="acp_1"{}/>)",
                        gid_attribute));
    };

    const LoadResult absent = load("");
    ASSERT_TRUE(absent.ok);
    EXPECT_FALSE(absent.document->roots().front()->gid().has_value());

    const LoadResult empty = load(R"( gid="")");
    ASSERT_TRUE(empty.ok);
    ASSERT_TRUE(empty.document->roots().front()->gid().has_value()) << "an explicit empty gid was read as absent";
    EXPECT_EQ(*empty.document->roots().front()->gid(), "");

    // The distinction must survive export, not just import.
    const sacm::io::SaveResult saved_absent = sacm::io::save_xmi_string(*absent.document);
    const sacm::io::SaveResult saved_empty = sacm::io::save_xmi_string(*empty.document);
    ASSERT_TRUE(saved_absent.ok);
    ASSERT_TRUE(saved_empty.ok);
    EXPECT_EQ(saved_absent.xml.find("gid="), std::string::npos);
    EXPECT_NE(saved_empty.xml.find(R"(gid="")"), std::string::npos) << "an explicit empty gid was dropped on save";
}

// LangString identity is an explicit, tested scope exclusion rather than an
// oversight. LangString generalizes Element (not SACMElement) and every one of
// its appearances in the SACM 2.3 metamodel is a containment role, so no
// reference can target one and dropping the id cannot break the model. What is
// not acceptable is dropping it in silence, so the reader announces it.
TEST(Sacm23BaseModel, SACM23_XMI_001_LangStringIdIsNotPreservedButIsReported) {
    const std::string xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" )"
        R"(xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:version="2.0" xmi:id="acp_1">
  <name xmi:id="ls_name_1" lang="en" content="Case"/>
</sacm:AssuranceCasePackage>)";

    const LoadResult loaded = sacm::io::load_xmi_string(xml);
    ASSERT_TRUE(loaded.ok);

    const bool reported = std::ranges::any_of(loaded.diagnostics, [](const auto& diagnostic) {
        return diagnostic.message.find("ls_name_1") != std::string::npos &&
               diagnostic.message.find("not preserved") != std::string::npos;
    });
    EXPECT_TRUE(reported) << "LangString id was dropped without a diagnostic";

    // The id really is absent from the round-tripped output -- this documents
    // the exclusion, it does not claim preservation.
    const sacm::io::SaveResult saved = sacm::io::save_xmi_string(*loaded.document);
    ASSERT_TRUE(saved.ok);
    EXPECT_EQ(saved.xml.find("ls_name_1"), std::string::npos);
    EXPECT_NE(saved.xml.find("acp_1"), std::string::npos);
}

TEST(Sacm23BaseModel, SACM23_BASE_001_SetNameSetDescriptionAddTaggedValue) {
    Document document;
    ASSERT_TRUE(document.apply(CreateAssuranceCasePackage{.id = ElementId{"acp_1"}, .name = "A"}).applied);
    ASSERT_TRUE(
        document.apply(SetName{.element = ElementId{"acp_1"}, .name = "Renamed Case", .language = "en"}).applied);
    ASSERT_TRUE(
        document.apply(SetDescription{.element = ElementId{"acp_1"}, .text = "A description.", .language = "en"})
            .applied);
    ASSERT_TRUE(
        document.apply(SetDescription{.element = ElementId{"acp_1"}, .text = "説明。", .language = "ja"}).applied);
    ASSERT_TRUE(
        document.apply(AddTaggedValue{.element = ElementId{"acp_1"}, .key = "reviewStatus", .value = "approved"})
            .applied);

    const auto& acp = *document.roots().front();
    EXPECT_EQ(acp.name().content, "Renamed Case");
    ASSERT_EQ(acp.descriptions().size(), 1u);
    EXPECT_EQ(*acp.description().find("en"), "A description.");
    EXPECT_EQ(*acp.description().find("ja"), "説明。");
    ASSERT_EQ(acp.tagged_values().size(), 1u);
    EXPECT_TRUE(sacm::validation::validate(document).empty());

    // Round-trips through strict save.
    const auto saved = sacm::io::save_xmi_string(document);
    ASSERT_TRUE(saved.ok);
    const LoadResult reloaded = sacm::io::load_xmi_string(saved.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(reloaded.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(document, *reloaded.document).empty());
}

// SetGid assigns the clause-8.2 SACMElement.gid, applies to any element (here a
// package and a claim, neither a citation), records a before/after ChangeRecord,
// round-trips through strict save, and clears to absent on an empty gid. This is
// the operation the Assurance Forge terminology seams use to mint the legacy
// `gid-<id>` at create time.
TEST(Sacm23BaseModel, SACM23_BASE_001_SetGidAssignsElementGid) {
    Document document;
    ASSERT_TRUE(document.apply(CreateAssuranceCasePackage{.id = ElementId{"acp_1"}, .name = "A"}).applied);
    ASSERT_TRUE(
        document.apply(CreateArgumentPackage{.parent = ElementId{"acp_1"}, .id = ElementId{"ap_1"}, .name = "Args"})
            .applied);
    ASSERT_TRUE(
        document
            .apply(CreateClaim{
                .parent = ElementId{"ap_1"}, .id = ElementId{"c_1"}, .name = "G", .description = "d", .language = "en"})
            .applied);

    EXPECT_FALSE(document.find(ElementId{"acp_1"})->gid().has_value()); // vacuity guard

    const sacm::commands::MutationResult set_pkg =
        document.apply(SetGid{.element = ElementId{"acp_1"}, .gid = "gid-acp_1"});
    ASSERT_TRUE(set_pkg.applied);
    const sacm::commands::MutationResult set_claim =
        document.apply(SetGid{.element = ElementId{"c_1"}, .gid = "gid-c_1"});
    ASSERT_TRUE(set_claim.applied);

    ASSERT_TRUE(document.find(ElementId{"acp_1"})->gid().has_value());
    EXPECT_EQ(*document.find(ElementId{"acp_1"})->gid(), "gid-acp_1");
    EXPECT_EQ(*document.find(ElementId{"c_1"})->gid(), "gid-c_1");

    // The change record carries the before (absent) and after value.
    ASSERT_EQ(set_pkg.changes.size(), 1u);
    EXPECT_EQ(set_pkg.changes.front().property, "gid");
    EXPECT_FALSE(set_pkg.changes.front().before.has_value());
    ASSERT_TRUE(set_pkg.changes.front().after.has_value());
    EXPECT_EQ(*set_pkg.changes.front().after, "gid-acp_1");

    EXPECT_TRUE(sacm::validation::validate(document).empty());

    // The gid round-trips through strict save.
    const auto saved = sacm::io::save_xmi_string(document);
    ASSERT_TRUE(saved.ok);
    const LoadResult reloaded = sacm::io::load_xmi_string(saved.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(reloaded.ok);
    ASSERT_TRUE(reloaded.document->find(ElementId{"acp_1"})->gid().has_value());
    EXPECT_EQ(*reloaded.document->find(ElementId{"acp_1"})->gid(), "gid-acp_1");

    // An empty gid clears it back to absent.
    ASSERT_TRUE(document.apply(SetGid{.element = ElementId{"acp_1"}, .gid = ""}).applied);
    EXPECT_FALSE(document.find(ElementId{"acp_1"})->gid().has_value());

    // A missing target fails, unchanged.
    const sacm::commands::MutationResult missing = document.apply(SetGid{.element = ElementId{"nope"}, .gid = "gid-x"});
    EXPECT_FALSE(missing.applied);
}

// SetDescriptionAt addresses a ModelElement's Description list by ordinal slot
// (clause 8.9 Description[0..*]): slot 0 is the statement, slot 1 a second note.
// Appending at the count grows the list; an in-range index edits in place; a gap
// (index > count) is rejected; and the two descriptions round-trip through
// strict save. This is the operation the Assurance Forge claim seam uses to set
// a note in the second Description without disturbing the front statement.
TEST(Sacm23BaseModel, SACM23_BASE_001_SetDescriptionAtAddressesDescriptionSlots) {
    Document document;
    ASSERT_TRUE(document.apply(CreateAssuranceCasePackage{.id = ElementId{"acp_1"}, .name = "A"}).applied);
    ASSERT_TRUE(
        document.apply(CreateArgumentPackage{.parent = ElementId{"acp_1"}, .id = ElementId{"ap_1"}, .name = "Args"})
            .applied);
    ASSERT_TRUE(document
                    .apply(CreateClaim{.parent = ElementId{"ap_1"},
                                       .id = ElementId{"c_1"},
                                       .name = "G",
                                       .description = "The statement.",
                                       .language = "en"})
                    .applied);
    const auto& claim = *document.find_as<sacm::model::ModelElement>(ElementId{"c_1"});
    ASSERT_EQ(claim.descriptions().size(), 1u); // vacuity guard: only the statement

    // A gap (index 2 when there is 1 description) is rejected, unchanged.
    const sacm::commands::MutationResult gap =
        document.apply(SetDescriptionAt{.element = ElementId{"c_1"}, .index = 2, .text = "x", .language = "en"});
    EXPECT_FALSE(gap.applied);
    EXPECT_EQ(claim.descriptions().size(), 1u);

    // Appending at index 1 creates the second Description (the note).
    ASSERT_TRUE(
        document.apply(SetDescriptionAt{.element = ElementId{"c_1"}, .index = 1, .text = "A note.", .language = "en"})
            .applied);
    ASSERT_EQ(claim.descriptions().size(), 2u);
    EXPECT_EQ(*claim.descriptions().front()->content().find("en"), "The statement.");
    EXPECT_EQ(*claim.descriptions()[1]->content().find("en"), "A note.");

    // An in-range index edits that slot without touching the other.
    ASSERT_TRUE(document
                    .apply(SetDescriptionAt{
                        .element = ElementId{"c_1"}, .index = 1, .text = "A revised note.", .language = "en"})
                    .applied);
    ASSERT_EQ(claim.descriptions().size(), 2u);
    EXPECT_EQ(*claim.descriptions().front()->content().find("en"), "The statement.");
    EXPECT_EQ(*claim.descriptions()[1]->content().find("en"), "A revised note.");

    // Empty text CLEARS the language entry, and the effect must report the value
    // as absent (not as an empty string) so audit/undo consumers can tell a clear
    // from a set-to-empty -- the same convention SetGid uses.
    {
        const sacm::commands::MutationResult cleared =
            document.apply(SetDescriptionAt{.element = ElementId{"c_1"}, .index = 1, .text = "", .language = "en"});
        ASSERT_TRUE(cleared.applied);
        ASSERT_EQ(cleared.changes.size(), 1u);
        EXPECT_FALSE(cleared.changes.front().after.has_value());
        EXPECT_EQ(claim.descriptions()[1]->content().find("en"), nullptr);
    }
    // Restore the note for the round-trip below.
    ASSERT_TRUE(document
                    .apply(SetDescriptionAt{
                        .element = ElementId{"c_1"}, .index = 1, .text = "A revised note.", .language = "en"})
                    .applied);

    // A second Description is valid in the machine model (Description[0..*]);
    // the validator only warns on the surplus (clause 8.6 spec text is [0..1]),
    // so there must be no Error, and the sole diagnostic is that warning.
    const auto diagnostics = sacm::validation::validate(document);
    for (const auto& diagnostic : diagnostics) {
        EXPECT_NE(diagnostic.severity, sacm::validation::Severity::Error) << diagnostic.message;
    }

    // Both descriptions survive a strict save round-trip.
    const auto saved = sacm::io::save_xmi_string(document);
    ASSERT_TRUE(saved.ok);
    const LoadResult reloaded = sacm::io::load_xmi_string(saved.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(reloaded.ok);
    const auto& reloaded_claim = *reloaded.document->find_as<sacm::model::ModelElement>(ElementId{"c_1"});
    ASSERT_EQ(reloaded_claim.descriptions().size(), 2u);
    EXPECT_EQ(*reloaded_claim.descriptions()[1]->content().find("en"), "A revised note.");
}

// Two claims whose attribute lists the caller supplies, so each clause-8.2
// negative below differs only in the attribute under test.
std::vector<sacm::validation::Diagnostic> validate_two_claims(std::string_view first_attrs,
                                                              std::string_view second_attrs) {
    const std::string xml =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)"
                    R"(<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" )"
                    R"(xmlns:xmi="http://www.omg.org/spec/XMI/20131001" )"
                    R"(xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:version="2.0" xmi:id="acp_1">)"
                    R"(<argumentPackage xmi:id="ap_1">)"
                    R"(<argumentElement xsi:type="sacm:Claim" xmi:id="claim_a" )") +
        std::string(first_attrs) + R"(><name content="A"/></argumentElement>)" +
        R"(<argumentElement xsi:type="sacm:Claim" xmi:id="claim_b" )" + std::string(second_attrs) +
        R"(><name content="B"/></argumentElement>)" + R"(</argumentPackage></sacm:AssuranceCasePackage>)";
    const LoadResult result = sacm::io::load_xmi_string(xml, LoadOptions{.mode = Mode::Strict});
    EXPECT_TRUE(result.document.has_value()) << (result.diagnostics.empty() ? "" : result.diagnostics.front().message);
    if (!result.document.has_value()) {
        return {};
    }
    return sacm::validation::validate(*result.document);
}

bool mentions(const std::vector<sacm::validation::Diagnostic>& diagnostics, std::string_view fragment) {
    return std::ranges::any_of(diagnostics, [&](const sacm::validation::Diagnostic& diagnostic) {
        return diagnostic.message.find(fragment) != std::string::npos;
    });
}

// Clause 8.2: gid is "a unique identifier that is unique within the scope of
// the model instance". The validator checked xmi:id only, so two elements
// sharing the gid third-party tools key on validated clean.
TEST(Sacm23BaseModel, SACM23_BASE_002_DuplicateGidIsDiagnosed) {
    const auto duplicated = validate_two_claims(R"(gid="urn:af:claim-1")", R"(gid="urn:af:claim-1")");
    EXPECT_TRUE(has_code(duplicated, sacm::validation::codes::kGidDuplicate));
    EXPECT_TRUE(mentions(duplicated, "urn:af:claim-1"))
        << (duplicated.empty() ? "no diagnostics at all" : duplicated.front().message);

    const auto distinct = validate_two_claims(R"(gid="urn:af:claim-1")", R"(gid="urn:af:claim-2")");
    EXPECT_TRUE(distinct.empty()) << (distinct.empty() ? "" : distinct.front().message);

    // Absent on both is not a collision: clause 8.2 makes gid [0..1].
    EXPECT_TRUE(validate_two_claims("", "").empty());
}

// Clause 8.2: "When +abstractForm is used to refer to another SACMElement,
// +isAbstract of the SACMElement is false, and the +isAbstract of the referred
// SACMElement should be true. The referred SACMElement should be of the same
// type of the SACMElement." Severities follow the clause's own modal verbs --
// the flat statement is an error, the two "should"s are warnings.
TEST(Sacm23BaseModel, SACM23_BASE_002_AbstractFormConstraintsAreValidated) {
    // Conformant: a concrete claim conforming to an abstract claim.
    const auto conformant = validate_two_claims(R"(abstractForm="claim_b")", R"(isAbstract="true")");
    EXPECT_TRUE(conformant.empty()) << (conformant.empty() ? "" : conformant.front().message);

    // The citing element is itself abstract.
    const auto citing_abstract =
        validate_two_claims(R"(isAbstract="true" abstractForm="claim_b")", R"(isAbstract="true")");
    EXPECT_TRUE(has_code(citing_abstract, sacm::validation::codes::kAbstractnessInvalid));
    EXPECT_TRUE(std::ranges::any_of(citing_abstract, [](const sacm::validation::Diagnostic& diagnostic) {
        return diagnostic.code == sacm::validation::codes::kAbstractnessInvalid &&
               diagnostic.severity == sacm::validation::Severity::Error;
    }));

    // The referred element is not abstract -- a "should", so a warning.
    const auto referred_concrete = validate_two_claims(R"(abstractForm="claim_b")", "");
    ASSERT_TRUE(has_code(referred_concrete, sacm::validation::codes::kAbstractnessInvalid));
    EXPECT_TRUE(std::ranges::all_of(referred_concrete, [](const sacm::validation::Diagnostic& diagnostic) {
        return diagnostic.severity == sacm::validation::Severity::Warning;
    }));
}

TEST(Sacm23BaseModel, SACM23_BASE_002_AbstractFormMustNameTheSameType) {
    const std::string xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" )"
        R"(xmlns:xmi="http://www.omg.org/spec/XMI/20131001" )"
        R"(xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:version="2.0" xmi:id="acp_1">)"
        R"(<argumentPackage xmi:id="ap_1">)"
        R"(<argumentElement xsi:type="sacm:Claim" xmi:id="claim_a" abstractForm="reasoning_1">)"
        R"(<name content="A"/></argumentElement>)"
        R"(<argumentElement xsi:type="sacm:ArgumentReasoning" xmi:id="reasoning_1" isAbstract="true">)"
        R"(<name content="R"/></argumentElement>)"
        R"(</argumentPackage></sacm:AssuranceCasePackage>)";
    const LoadResult result = sacm::io::load_xmi_string(xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(result.document.has_value());
    const auto diagnostics = sacm::validation::validate(*result.document);
    EXPECT_TRUE(has_code(diagnostics, sacm::validation::codes::kAbstractnessInvalid));
    EXPECT_TRUE(mentions(diagnostics, "same type"))
        << (diagnostics.empty() ? "no diagnostics at all" : diagnostics.front().message);
}

} // namespace
