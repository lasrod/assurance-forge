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

using sacm::commands::AddMetaClaim;
using sacm::commands::AddRelationshipSource;
using sacm::commands::ChangeRecord;
using sacm::commands::CreateArgumentPackage;
using sacm::commands::CreateArgumentReasoning;
using sacm::commands::CreateArtifactReference;
using sacm::commands::CreateAssertedRelationship;
using sacm::commands::CreateAssuranceCasePackage;
using sacm::commands::CreateClaim;
using sacm::commands::DeleteElement;
using sacm::commands::OperationPreview;
using sacm::commands::ReferenceDeletePolicy;
using sacm::commands::ReorderPackageElements;
using sacm::commands::SetAssertionDeclaration;
using sacm::commands::SetMetaClaims;
using sacm::commands::SetName;
using sacm::commands::SetRelationshipEnds;
using sacm::io::LoadOptions;
using sacm::io::LoadResult;
using sacm::io::Mode;
using sacm::metadata::ElementKind;
using sacm::model::AssertionDeclaration;

// GSN "undeveloped" and SACM "needsSupport" are the same concept — a goal not
// yet argued. GSN's own transformation maps one to the other. Assurance Forge
// historically wrote a separate `undeveloped="true"` attribute; the library
// unifies that onto the SACM-native declaration so the concept survives without
// a non-standard boolean, and so strict export stays clean.
TEST(Sacm23Argumentation, SACM23_ARG_001_LegacyUndevelopedNormalizesToNeedsSupport) {
    const auto load = [](std::string_view claim_attrs) {
        return sacm::io::load_xmi_string(
            std::format(R"(<?xml version="1.0" encoding="UTF-8"?>)"
                        R"(<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" )"
                        R"(xmlns:xmi="http://www.omg.org/spec/XMI/20131001" )"
                        R"(xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:version="2.0" xmi:id="acp_1">)"
                        R"(<argumentPackage xmi:id="ap_1">)"
                        R"(<argumentElement xsi:type="sacm:Claim" xmi:id="G1" {}><name content="G1"/>)"
                        R"(</argumentElement></argumentPackage></sacm:AssuranceCasePackage>)",
                        claim_attrs));
    };

    const auto declaration_of = [](const LoadResult& result) {
        return result.document->find_as<sacm::model::Claim>(sacm::model::ElementId{"G1"})->assertion_declaration();
    };

    // Legacy undeveloped attribute -> needsSupport.
    const LoadResult legacy = load(R"(undeveloped="true")");
    ASSERT_TRUE(legacy.ok);
    EXPECT_EQ(declaration_of(legacy), AssertionDeclaration::NeedsSupport);
    // It normalizes rather than being kept as opaque vendor content, so strict
    // save does not refuse.
    EXPECT_TRUE(sacm::io::save_xmi_string(*legacy.document).ok);

    // A file already using the SACM-native form is unchanged.
    const LoadResult native = load(R"(assertionDeclaration="needsSupport")");
    ASSERT_TRUE(native.ok);
    EXPECT_EQ(declaration_of(native), AssertionDeclaration::NeedsSupport);

    // An explicit, more specific declaration wins over the legacy boolean.
    const LoadResult specific = load(R"(undeveloped="true" assertionDeclaration="assumed")");
    ASSERT_TRUE(specific.ok);
    EXPECT_EQ(declaration_of(specific), AssertionDeclaration::Assumed);
}

// A GSN Justification predates the vendor gsn.role encoding: old Assurance Forge
// files wrote a non-standard assertionDeclaration="justification". The library
// normalizes it to the standards-correct `axiomatic` (docs/sacm/sacm-gsn-mapping.md)
// and preserves the original GSN role in a reserved TaggedValue, so a client can
// still tell a Justification from a plain axiomatic Goal and strict save stays clean.
TEST(Sacm23Argumentation, SACM23_ARG_001_LegacyJustificationNormalizesToAxiomatic) {
    const LoadResult legacy = sacm::io::load_xmi_string(
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" )"
        R"(xmlns:xmi="http://www.omg.org/spec/XMI/20131001" )"
        R"(xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:version="2.0" xmi:id="acp_1">)"
        R"(<argumentPackage xmi:id="ap_1">)"
        R"(<argumentElement xsi:type="sacm:Claim" xmi:id="J1" assertionDeclaration="justification">)"
        R"(<name content="Just"/></argumentElement></argumentPackage></sacm:AssuranceCasePackage>)");
    ASSERT_TRUE(legacy.ok);
    const auto* claim = legacy.document->find_as<sacm::model::Claim>(sacm::model::ElementId{"J1"});
    ASSERT_NE(claim, nullptr);
    EXPECT_EQ(claim->assertion_declaration(), AssertionDeclaration::Axiomatic);

    bool has_role_tag = false;
    for (const auto& tag : claim->tagged_values()) {
        if (tag->key().primary() == "sacm.import.assertionDeclaration" && tag->content().primary() == "justification") {
            has_role_tag = true;
        }
    }
    EXPECT_TRUE(has_role_tag) << "the GSN Justification role was not preserved";

    // Normalized, not opaque -- strict save accepts it and it round-trips.
    const auto saved = sacm::io::save_xmi_string(*legacy.document);
    ASSERT_TRUE(saved.ok);
    const LoadResult reloaded = sacm::io::load_xmi_string(saved.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(reloaded.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(*legacy.document, *reloaded.document).empty());
}

// Assurance Forge encodes two product-critical GSN v3 concepts in SACM: a
// Challenge is a relationship with isCounter=true (SACM-native, clause 11.13),
// and an Assurance Claim Point is a vendor TaggedValue keyed
// "assuranceForge.acp" (clause 8.12 extension mechanism). Neither has a GSN
// metamodel representation -- v2.2 predates GSN v3 and its OCL even forbids
// isCounter -- so the library is the only thing keeping them intact. Phase 9
// migrates the app onto this library, so losing either here would silently
// drop dialectic argumentation and confidence data from every saved case.
TEST(Sacm23Argumentation, SACM23_ARG_001_ChallengeAndAcpEncodingsSurviveStrictRoundTrip) {
    const std::string xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" )"
        R"(xmlns:xmi="http://www.omg.org/spec/XMI/20131001" )"
        R"(xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:version="2.0" xmi:id="acp_1">
  <argumentPackage xmi:id="ap_1">
    <argumentElement xsi:type="sacm:Claim" xmi:id="G1"><name content="G1"/></argumentElement>
    <argumentElement xsi:type="sacm:Claim" xmi:id="G2"><name content="G2"/></argumentElement>
    <argumentElement xsi:type="sacm:AssertedInference" xmi:id="R1" source="G2" target="G1" isCounter="true">
      <taggedValue xmi:id="TV1">
        <key><value content="assuranceForge.acp"/></key>
        <value content="ACP1"/>
      </taggedValue>
    </argumentElement>
  </argumentPackage>
</sacm:AssuranceCasePackage>)";

    const LoadResult loaded = sacm::io::load_xmi_string(xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(loaded.ok) << (loaded.diagnostics.empty() ? "load failed" : loaded.diagnostics.front().message);

    // Challenge: isCounter is SACM-native and must survive as model state.
    const auto* relationship =
        loaded.document->find_as<sacm::model::AssertedRelationship>(sacm::model::ElementId{"R1"});
    ASSERT_NE(relationship, nullptr);
    EXPECT_TRUE(relationship->is_counter()) << "Challenge (isCounter) was lost on import";

    // Both must still be there after a strict save -- a vendor TaggedValue is a
    // standard extension point, so strict mode must not refuse or drop it.
    const sacm::io::SaveResult saved = sacm::io::save_xmi_string(*loaded.document);
    ASSERT_TRUE(saved.ok) << "strict save refused a document using standard extension points";
    EXPECT_NE(saved.xml.find("isCounter=\"true\""), std::string::npos);
    EXPECT_NE(saved.xml.find("assuranceForge.acp"), std::string::npos) << "ACP TaggedValue was dropped on strict save";

    const LoadResult reloaded = sacm::io::load_xmi_string(saved.xml);
    ASSERT_TRUE(reloaded.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(*loaded.document, *reloaded.document).empty());
}
using sacm::model::Document;
using sacm::model::ElementId;

std::filesystem::path fixture(std::string_view name) {
    return std::filesystem::path(SACM_TEST_DATA_DIR) / "sacm23" / name;
}

bool has_code(const std::vector<sacm::validation::Diagnostic>& diagnostics, std::string_view code) {
    return std::ranges::any_of(diagnostics, [&](const auto& diagnostic) { return diagnostic.code == code; });
}

TEST(Sacm23Argumentation, SACM23_ARG_001_FullArgumentationFixtureRoundTrips) {
    const LoadResult first =
        sacm::io::load_xmi_file(fixture("argumentation-full-valid.sacm.xmi"), LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(first.ok) << (first.diagnostics.empty() ? "" : first.diagnostics.front().message);
    const auto& document = *first.document;

    // Assertion declarations.
    const auto* claim_top = document.find_as<sacm::model::Claim>(ElementId{"claim_top"});
    ASSERT_NE(claim_top, nullptr);
    EXPECT_EQ(claim_top->assertion_declaration(), AssertionDeclaration::NeedsSupport);
    const auto* claim_counter = document.find_as<sacm::model::Claim>(ElementId{"claim_counter"});
    ASSERT_NE(claim_counter, nullptr);
    EXPECT_EQ(claim_counter->assertion_declaration(), AssertionDeclaration::Defeated);

    // Inference with reasoning, multiple sources, and a meta-claim.
    const auto* inference = document.find_as<sacm::model::AssertedInference>(ElementId{"inf_1"});
    ASSERT_NE(inference, nullptr);
    EXPECT_EQ(inference->sources().size(), 2u);
    ASSERT_EQ(inference->meta_claims().size(), 1u);
    EXPECT_EQ(inference->meta_claims().front().value(), "claim_meta");
    ASSERT_TRUE(inference->reasoning().has_value());

    // Counter-argumentation.
    const auto* counter = document.find_as<sacm::model::AssertedInference>(ElementId{"counter_1"});
    ASSERT_NE(counter, nullptr);
    EXPECT_TRUE(counter->is_counter());

    // Artifact support/context families and the reasoning structure ref.
    EXPECT_NE(document.find_as<sacm::model::AssertedArtifactSupport>(ElementId{"support_1"}), nullptr);
    EXPECT_NE(document.find_as<sacm::model::AssertedArtifactContext>(ElementId{"actx_1"}), nullptr);
    const auto* reasoning = document.find_as<sacm::model::ArgumentReasoning>(ElementId{"ar_decompose"});
    ASSERT_NE(reasoning, nullptr);
    ASSERT_TRUE(reasoning->structure().has_value());
    EXPECT_EQ(reasoning->structure()->value(), "argpkg_detail");

    // Group membership and evidence reference into the artifact package.
    const auto* group = document.find_as<sacm::model::ArgumentGroup>(ElementId{"group_core"});
    ASSERT_NE(group, nullptr);
    EXPECT_EQ(group->argument_elements().size(), 3u);
    const auto* evidence = document.find_as<sacm::model::ArtifactReference>(ElementId{"ev_fmea"});
    ASSERT_NE(evidence, nullptr);
    ASSERT_EQ(evidence->referenced_artifact_elements().size(), 1u);

    EXPECT_TRUE(sacm::validation::validate(document).empty());
    const auto saved = sacm::io::save_xmi_string(document);
    ASSERT_TRUE(saved.ok);
    const LoadResult second = sacm::io::load_xmi_string(saved.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(second.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(document, *second.document).empty());
}

TEST(Sacm23Argumentation, SACM23_ARG_001_InvalidAssertionDeclarationIsDiagnosed) {
    const LoadResult result = sacm::io::load_xmi_file(fixture("invalid/assertion-declaration-invalid.sacm.xmi"));
    EXPECT_TRUE(has_code(result.diagnostics, sacm::validation::codes::kEnumInvalidLiteral));
}

TEST(Sacm23Argumentation, SACM23_ARG_002_RelationshipTargetTypingIsValidated) {
    const LoadResult result = sacm::io::load_xmi_file(
        fixture("invalid/relationship-target-wrong-type-invalid.sacm.xmi"), LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(result.document.has_value());
    const auto diagnostics = sacm::validation::validate(*result.document);
    EXPECT_TRUE(has_code(diagnostics, sacm::validation::codes::kRefWrongType));
}

Document build_argument_case() {
    Document document;
    EXPECT_TRUE(document.apply(CreateAssuranceCasePackage{.id = ElementId{"acp_1"}, .name = "Case"}).applied);
    EXPECT_TRUE(
        document.apply(CreateArgumentPackage{.parent = ElementId{"acp_1"}, .id = ElementId{"argpkg_1"}, .name = "Arg"})
            .applied);
    EXPECT_TRUE(
        document.apply(CreateClaim{.parent = ElementId{"argpkg_1"}, .id = ElementId{"claim_top"}, .name = "Top"})
            .applied);
    EXPECT_TRUE(
        document.apply(CreateClaim{.parent = ElementId{"argpkg_1"}, .id = ElementId{"claim_sub"}, .name = "Sub"})
            .applied);
    return document;
}

TEST(Sacm23Argumentation, SACM23_ARG_001_CreatesRelationshipsWithCommands) {
    Document document = build_argument_case();
    ASSERT_TRUE(document
                    .apply(CreateArgumentReasoning{
                        .parent = ElementId{"argpkg_1"}, .id = ElementId{"ar_1"}, .name = "Decomposition"})
                    .applied);
    ASSERT_TRUE(document
                    .apply(CreateAssertedRelationship{
                        .parent = ElementId{"argpkg_1"},
                        .kind = ElementKind::AssertedInference,
                        .id = ElementId{"inf_1"},
                        .name = "Inference",
                        .sources = {ElementId{"claim_sub"}},
                        .targets = {ElementId{"claim_top"}},
                        .reasoning = ElementId{"ar_1"},
                    })
                    .applied);
    ASSERT_TRUE(document
                    .apply(SetAssertionDeclaration{.element = ElementId{"claim_top"},
                                                   .declaration = AssertionDeclaration::NeedsSupport})
                    .applied);
    ASSERT_TRUE(
        document.apply(CreateClaim{.parent = ElementId{"argpkg_1"}, .id = ElementId{"claim_meta"}, .name = "Meta"})
            .applied);
    ASSERT_TRUE(
        document.apply(AddMetaClaim{.element = ElementId{"inf_1"}, .meta_claim = ElementId{"claim_meta"}}).applied);

    EXPECT_TRUE(sacm::validation::validate(document).empty());
    const auto saved = sacm::io::save_xmi_string(document);
    ASSERT_TRUE(saved.ok);
    const LoadResult reloaded = sacm::io::load_xmi_string(saved.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(reloaded.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(document, *reloaded.document).empty());
}

// `AddMetaClaim` could attach a meta-claim (clause 11.6) and nothing could
// detach one, so the association was one-way: a client retracting a meta-claim --
// an Assurance Claim Point whose resolution changes or is deleted -- had no
// operation for it. `SetMetaClaims` replaces the list, the way
// `SetExpressionCategories` replaces an ExpressionElement's categories.
//
// Asserts all three directions (replace, extend, clear), that a non-Claim id is
// rejected with the document unchanged, and that the result round-trips.
TEST(Sacm23Argumentation, SACM23_ARG_001_SetsAndClearsMetaClaims) {
    Document document = build_argument_case();
    ASSERT_TRUE(document
                    .apply(CreateAssertedRelationship{
                        .parent = ElementId{"argpkg_1"},
                        .kind = ElementKind::AssertedInference,
                        .id = ElementId{"inf_1"},
                        .name = "Inference",
                        .sources = {ElementId{"claim_sub"}},
                        .targets = {ElementId{"claim_top"}},
                    })
                    .applied);
    for (const char* id : {"claim_meta_a", "claim_meta_b"}) {
        ASSERT_TRUE(
            document.apply(CreateClaim{.parent = ElementId{"argpkg_1"}, .id = ElementId{id}, .name = "Meta"}).applied);
    }
    const auto meta_claims_of = [&document]() {
        const auto* assertion = document.find_as<sacm::model::Assertion>(ElementId{"inf_1"});
        std::vector<std::string> ids;
        for (const ElementId& id : assertion->meta_claims())
            ids.push_back(id.value());
        return ids;
    };

    ASSERT_TRUE(
        document.apply(AddMetaClaim{.element = ElementId{"inf_1"}, .meta_claim = ElementId{"claim_meta_a"}}).applied);
    EXPECT_EQ(meta_claims_of(), (std::vector<std::string>{"claim_meta_a"}));

    // Replace: the one that was there goes, the new one arrives. This is the
    // direction that had no operation at all.
    ASSERT_TRUE(document.apply(SetMetaClaims{.element = ElementId{"inf_1"}, .meta_claims = {ElementId{"claim_meta_b"}}})
                    .applied);
    EXPECT_EQ(meta_claims_of(), (std::vector<std::string>{"claim_meta_b"}));

    // Extend, preserving order.
    ASSERT_TRUE(document
                    .apply(SetMetaClaims{.element = ElementId{"inf_1"},
                                         .meta_claims = {ElementId{"claim_meta_b"}, ElementId{"claim_meta_a"}}})
                    .applied);
    EXPECT_EQ(meta_claims_of(), (std::vector<std::string>{"claim_meta_b", "claim_meta_a"}));

    // A target that is not a Claim is rejected, and rejection leaves the list
    // alone rather than half-applied.
    const auto rejected = document.apply(SetMetaClaims{
        .element = ElementId{"inf_1"}, .meta_claims = {ElementId{"claim_meta_b"}, ElementId{"argpkg_1"}}});
    EXPECT_FALSE(rejected.applied);
    EXPECT_FALSE(rejected.diagnostics.empty());
    EXPECT_EQ(meta_claims_of(), (std::vector<std::string>{"claim_meta_b", "claim_meta_a"}));

    EXPECT_TRUE(sacm::validation::validate(document).empty());
    const auto saved = sacm::io::save_xmi_string(document);
    ASSERT_TRUE(saved.ok);
    const LoadResult reloaded = sacm::io::load_xmi_string(saved.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(reloaded.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(document, *reloaded.document).empty());

    // Clear.
    ASSERT_TRUE(document.apply(SetMetaClaims{.element = ElementId{"inf_1"}, .meta_claims = {}}).applied);
    EXPECT_TRUE(meta_claims_of().empty());
}

// `AddRelationshipSource` could extend a relationship's endpoints and nothing
// could withdraw one, so a tool repairing a document could not drop a broken
// endpoint. `SetRelationshipEnds` replaces all three slots together, because the
// clause-11.13 multiplicity spans them.
//
// The interesting half is the repair exception. The reader stores endpoint ids
// verbatim and reports the unresolvable ones separately, so documents with
// dangling endpoints exist and have to be fixable. A strict "every id must
// resolve" check would make a relationship with TWO broken endpoints
// unrepairable -- dropping either one still leaves the other in the list being
// written. So an unresolved id is accepted where the relationship already
// carried it, and rejected where it would be introduced.
TEST(Sacm23Argumentation, SACM23_ARG_001_SetsRelationshipEndsAndToleratesOnlyInheritedDangles) {
    Document document = build_argument_case();
    ASSERT_TRUE(
        document.apply(CreateClaim{.parent = ElementId{"argpkg_1"}, .id = ElementId{"claim_two"}, .name = "Second"})
            .applied);
    ASSERT_TRUE(document
                    .apply(CreateAssertedRelationship{
                        .parent = ElementId{"argpkg_1"},
                        .kind = ElementKind::AssertedInference,
                        .id = ElementId{"inf_1"},
                        .name = "Inference",
                        .sources = {ElementId{"claim_sub"}, ElementId{"claim_two"}},
                        .targets = {ElementId{"claim_top"}},
                    })
                    .applied);
    const auto sources_of = [&document]() {
        const auto* rel = document.find_as<sacm::model::AssertedRelationship>(ElementId{"inf_1"});
        std::vector<std::string> ids;
        for (const ElementId& id : rel->sources())
            ids.push_back(id.value());
        return ids;
    };

    // Withdraw one source -- the direction that had no operation.
    ASSERT_TRUE(document
                    .apply(SetRelationshipEnds{.relationship = ElementId{"inf_1"},
                                               .sources = {ElementId{"claim_two"}},
                                               .targets = {ElementId{"claim_top"}}})
                    .applied);
    EXPECT_EQ(sources_of(), (std::vector<std::string>{"claim_two"}));

    // Introducing an id that resolves to nothing is refused, and refusal leaves
    // the endpoints alone.
    const auto introduced =
        document.apply(SetRelationshipEnds{.relationship = ElementId{"inf_1"},
                                           .sources = {ElementId{"claim_two"}, ElementId{"claim_missing"}},
                                           .targets = {ElementId{"claim_top"}}});
    EXPECT_FALSE(introduced.applied);
    EXPECT_FALSE(introduced.diagnostics.empty());
    EXPECT_EQ(sources_of(), (std::vector<std::string>{"claim_two"}));

    // Emptying a slot is refused too: clause 11.13 needs one of each, and a
    // caller who wants the relationship gone should delete it.
    const auto emptied = document.apply(
        SetRelationshipEnds{.relationship = ElementId{"inf_1"}, .sources = {}, .targets = {ElementId{"claim_top"}}});
    EXPECT_FALSE(emptied.applied);
    EXPECT_EQ(sources_of(), (std::vector<std::string>{"claim_two"}));

    // Now the repair case, built the way it actually arises: a document that
    // ARRIVES with dangling endpoints. The reader stores endpoint ids verbatim,
    // so this loads with two sources naming nothing.
    //
    // (Deleting an element to manufacture the dangle does not work -- the scrub
    // takes it out of the relationship, and writing it back is then correctly
    // refused as introducing one. That the operation refuses it is the point.)
    constexpr const char* kDanglingXmi = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:version="2.0" xmi:id="acp_1">
  <argumentPackage xmi:id="argpkg_1">
    <argumentElement xsi:type="sacm:Claim" xmi:id="claim_top"/>
    <argumentElement xsi:type="sacm:Claim" xmi:id="claim_real"/>
    <argumentElement xsi:type="sacm:AssertedInference" xmi:id="inf_broken" source="claim_real gone_a gone_b" target="claim_top"/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";
    LoadResult broken = sacm::io::load_xmi_string(kDanglingXmi, LoadOptions{.mode = Mode::Tolerant});
    ASSERT_TRUE(broken.document.has_value());
    Document& repaired = *broken.document;
    const auto broken_sources = [&repaired]() {
        const auto* rel = repaired.find_as<sacm::model::AssertedRelationship>(ElementId{"inf_broken"});
        std::vector<std::string> ids;
        for (const ElementId& id : rel->sources())
            ids.push_back(id.value());
        return ids;
    };
    ASSERT_EQ(broken_sources(), (std::vector<std::string>{"claim_real", "gone_a", "gone_b"}))
        << "the reader did not keep the dangling endpoints, so this measures nothing";

    // Drop ONE of the two dangles. The other is still in the list being written,
    // and a strict check would refuse the whole repair because of it.
    ASSERT_TRUE(repaired
                    .apply(SetRelationshipEnds{.relationship = ElementId{"inf_broken"},
                                               .sources = {ElementId{"claim_real"}, ElementId{"gone_b"}},
                                               .targets = {ElementId{"claim_top"}}})
                    .applied)
        << "a two-fault relationship could not be repaired one endpoint at a time";
    EXPECT_EQ(broken_sources(), (std::vector<std::string>{"claim_real", "gone_b"}));

    // Drop the second, and the relationship is clean.
    ASSERT_TRUE(repaired
                    .apply(SetRelationshipEnds{.relationship = ElementId{"inf_broken"},
                                               .sources = {ElementId{"claim_real"}},
                                               .targets = {ElementId{"claim_top"}}})
                    .applied);
    EXPECT_EQ(broken_sources(), (std::vector<std::string>{"claim_real"}));

    // A NEW dangle is still refused on the very document that carries them.
    EXPECT_FALSE(repaired
                     .apply(SetRelationshipEnds{.relationship = ElementId{"inf_broken"},
                                                .sources = {ElementId{"claim_real"}, ElementId{"never_seen"}},
                                                .targets = {ElementId{"claim_top"}}})
                     .applied);
}

// A strategy's inference is materialized with its first sub-goal as source, then
// extended with a second source when the next sub-goal is added -- the GSN
// incremental-construction workflow. AddRelationshipSource must append the source
// and the result must round-trip.
namespace {

// The ids a package contains, in document order (clause 11.4 `argumentElement`).
std::vector<std::string> package_order(const Document& document, const char* package_id) {
    std::vector<std::string> ids;
    const auto* package = document.find_as<sacm::model::ArgumentPackage>(ElementId{package_id});
    if (package == nullptr) {
        return ids;
    }
    for (const std::unique_ptr<sacm::model::ArgumentationElement>& element : package->argument_elements()) {
        ids.push_back(element->id().value());
    }
    return ids;
}

} // namespace

// `ReorderPackageElements` permutes the NAMED elements through the positions they
// already occupy, so everything unnamed stays where it was. A client reordering the
// relationships of a package must not have to enumerate its claims, its groups or
// its nested packages -- it usually cannot see all of them.
TEST(Sacm23Argumentation, SACM23_ARG_001_ReordersNamedPackageElementsAndLeavesTheRestInPlace) {
    Document document = build_argument_case();
    ASSERT_TRUE(
        document.apply(CreateClaim{.parent = ElementId{"argpkg_1"}, .id = ElementId{"claim_a"}, .name = "A"}).applied);
    ASSERT_TRUE(
        document.apply(CreateClaim{.parent = ElementId{"argpkg_1"}, .id = ElementId{"claim_b"}, .name = "B"}).applied);
    ASSERT_TRUE(
        document.apply(CreateClaim{.parent = ElementId{"argpkg_1"}, .id = ElementId{"claim_c"}, .name = "C"}).applied);

    const std::vector<std::string> before = package_order(document, "argpkg_1");
    const std::size_t count = before.size();
    ASSERT_GE(count, 5u) << "the fixture must hold elements this test does not name";

    // Reverse claim_a/claim_b/claim_c, naming nothing else.
    const auto reordered = document.apply(ReorderPackageElements{
        .package = ElementId{"argpkg_1"},
        .ordered = {ElementId{"claim_c"}, ElementId{"claim_a"}, ElementId{"claim_b"}},
    });
    ASSERT_TRUE(reordered.applied) << (reordered.diagnostics.empty() ? "" : reordered.diagnostics.front().message);

    const std::vector<std::string> after = package_order(document, "argpkg_1");
    ASSERT_EQ(after.size(), count) << "the reorder added or dropped an element";
    // Same content, so nothing was created or destroyed.
    std::vector<std::string> sorted_before = before;
    std::vector<std::string> sorted_after = after;
    std::sort(sorted_before.begin(), sorted_before.end());
    std::sort(sorted_after.begin(), sorted_after.end());
    EXPECT_EQ(sorted_before, sorted_after);

    // The three named elements occupy the same three slots, in the new order.
    std::vector<std::size_t> named_slots;
    for (std::size_t index = 0; index < before.size(); ++index) {
        if (before[index] == "claim_a" || before[index] == "claim_b" || before[index] == "claim_c")
            named_slots.push_back(index);
    }
    ASSERT_EQ(named_slots.size(), 3u);
    EXPECT_EQ(after[named_slots[0]], "claim_c");
    EXPECT_EQ(after[named_slots[1]], "claim_a");
    EXPECT_EQ(after[named_slots[2]], "claim_b");

    // Everything the caller did not name is untouched, which is the property that
    // lets a projection-driven client use this at all.
    for (std::size_t index = 0; index < before.size(); ++index) {
        const bool named = before[index] == "claim_a" || before[index] == "claim_b" || before[index] == "claim_c";
        if (!named) {
            EXPECT_EQ(after[index], before[index]) << "unnamed element at " << index << " moved";
        }
    }
}

// Reordering something the package does not contain is refused rather than
// ignored. A caller relies on the resulting order; silently skipping an id would
// let it believe an order was applied that was not.
TEST(Sacm23Argumentation, SACM23_ARG_001_ReorderRefusesAnElementThePackageDoesNotContain) {
    Document document = build_argument_case();
    const std::vector<std::string> before = package_order(document, "argpkg_1");
    ASSERT_FALSE(before.empty());

    const auto refused = document.apply(ReorderPackageElements{
        .package = ElementId{"argpkg_1"},
        .ordered = {ElementId{before.front()}, ElementId{"no_such_element"}},
    });
    EXPECT_FALSE(refused.applied);
    EXPECT_FALSE(refused.diagnostics.empty()) << "refused with no diagnostic";
    EXPECT_EQ(package_order(document, "argpkg_1"), before) << "the refused reorder still moved something";

    // The same for an id named twice: an order that mentions one element in two
    // places does not say where it goes.
    const auto ambiguous = document.apply(ReorderPackageElements{
        .package = ElementId{"argpkg_1"},
        .ordered = {ElementId{before.front()}, ElementId{before.front()}},
    });
    EXPECT_FALSE(ambiguous.applied);
    EXPECT_EQ(package_order(document, "argpkg_1"), before);
}

TEST(Sacm23Argumentation, SACM23_ARG_001_AddsSourceToExistingRelationship) {
    Document document = build_argument_case();
    ASSERT_TRUE(document
                    .apply(CreateArgumentReasoning{
                        .parent = ElementId{"argpkg_1"}, .id = ElementId{"ar_1"}, .name = "Strategy"})
                    .applied);
    ASSERT_TRUE(document
                    .apply(CreateAssertedRelationship{
                        .parent = ElementId{"argpkg_1"},
                        .kind = ElementKind::AssertedInference,
                        .id = ElementId{"inf_1"},
                        .sources = {ElementId{"claim_sub"}},
                        .targets = {ElementId{"claim_top"}},
                        .reasoning = ElementId{"ar_1"},
                    })
                    .applied);
    ASSERT_TRUE(
        document.apply(CreateClaim{.parent = ElementId{"argpkg_1"}, .id = ElementId{"claim_sub2"}, .name = "Sub2"})
            .applied);

    const auto added =
        document.apply(AddRelationshipSource{.relationship = ElementId{"inf_1"}, .source = ElementId{"claim_sub2"}});
    ASSERT_TRUE(added.applied) << (added.diagnostics.empty() ? "" : added.diagnostics.front().message);
    ASSERT_EQ(added.changes.size(), 1u);
    EXPECT_EQ(added.changes.front().property.value_or(""), "source");
    EXPECT_EQ(added.changes.front().change, ChangeRecord::Change::Modified);

    const auto* inference = document.find_as<sacm::model::AssertedRelationship>(ElementId{"inf_1"});
    ASSERT_NE(inference, nullptr);
    ASSERT_EQ(inference->sources().size(), 2u);
    EXPECT_EQ(inference->sources().back(), ElementId{"claim_sub2"});

    EXPECT_TRUE(sacm::validation::validate(document).empty());
    const auto saved = sacm::io::save_xmi_string(document);
    ASSERT_TRUE(saved.ok);
    const LoadResult reloaded = sacm::io::load_xmi_string(saved.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(reloaded.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(document, *reloaded.document).empty());
}

// AddRelationshipSource enforces the same typing as CreateAssertedRelationship
// (relationship must be an AssertedRelationship; source must be an ArgumentAsset)
// and rejects a duplicate source rather than silently repeating it.
TEST(Sacm23Argumentation, SACM23_ARG_002_AddSourceValidatesTargetAndDuplicate) {
    Document document = build_argument_case();
    ASSERT_TRUE(document
                    .apply(CreateAssertedRelationship{
                        .parent = ElementId{"argpkg_1"},
                        .kind = ElementKind::AssertedInference,
                        .id = ElementId{"inf_1"},
                        .sources = {ElementId{"claim_sub"}},
                        .targets = {ElementId{"claim_top"}},
                    })
                    .applied);

    // The relationship id must resolve to an AssertedRelationship.
    const auto not_relationship =
        document.apply(AddRelationshipSource{.relationship = ElementId{"claim_top"}, .source = ElementId{"claim_sub"}});
    EXPECT_FALSE(not_relationship.applied);
    EXPECT_TRUE(has_code(not_relationship.diagnostics, sacm::validation::codes::kCmdTargetNotFound));

    // A source must be an ArgumentAsset (a package is not).
    const auto wrong_type =
        document.apply(AddRelationshipSource{.relationship = ElementId{"inf_1"}, .source = ElementId{"argpkg_1"}});
    EXPECT_FALSE(wrong_type.applied);
    EXPECT_TRUE(has_code(wrong_type.diagnostics, sacm::validation::codes::kRefWrongType));

    // A source already present is rejected.
    const auto duplicate =
        document.apply(AddRelationshipSource{.relationship = ElementId{"inf_1"}, .source = ElementId{"claim_sub"}});
    EXPECT_FALSE(duplicate.applied);
    EXPECT_TRUE(has_code(duplicate.diagnostics, sacm::validation::codes::kMultiplicityViolation));
}

TEST(Sacm23Argumentation, SACM23_ARG_002_RejectsRelationshipWithWrongTargetKind) {
    Document document = build_argument_case();
    const auto result = document.apply(CreateAssertedRelationship{
        .parent = ElementId{"argpkg_1"},
        .kind = ElementKind::AssertedInference,
        .sources = {ElementId{"claim_sub"}},
        .targets = {ElementId{"argpkg_1"}}, // a package is not an ArgumentAsset
    });
    EXPECT_FALSE(result.applied);
    EXPECT_TRUE(has_code(result.diagnostics, sacm::validation::codes::kRefWrongType));

    const auto empty_sources = document.apply(CreateAssertedRelationship{
        .parent = ElementId{"argpkg_1"},
        .kind = ElementKind::AssertedEvidence,
        .targets = {ElementId{"claim_top"}},
    });
    EXPECT_FALSE(empty_sources.applied);
    EXPECT_TRUE(has_code(empty_sources.diagnostics, sacm::validation::codes::kMultiplicityViolation));
}

TEST(Sacm23Argumentation, SACM23_ARG_003_ClaimDeletePreviewListsAffectedRelationships) {
    Document document = build_argument_case();
    ASSERT_TRUE(document
                    .apply(CreateAssertedRelationship{
                        .parent = ElementId{"argpkg_1"},
                        .kind = ElementKind::AssertedInference,
                        .id = ElementId{"inf_1"},
                        .sources = {ElementId{"claim_sub"}},
                        .targets = {ElementId{"claim_top"}},
                    })
                    .applied);

    // Preview names the affected relationship before any mutation.
    const OperationPreview preview = document.preview(DeleteElement{
        .target = ElementId{"claim_sub"},
        .reference_policy = ReferenceDeletePolicy::DeleteReferencingRelationships,
    });
    ASSERT_TRUE(preview.can_apply);
    EXPECT_TRUE(std::ranges::any_of(preview.effects, [](const ChangeRecord& record) {
        return record.id.value() == "inf_1" && record.change == ChangeRecord::Change::RelationshipDeleted;
    }));

    // Applying leaves no dangling references.
    const auto result =
        document.apply(DeleteElement{.target = ElementId{"claim_sub"},
                                     .reference_policy = ReferenceDeletePolicy::DeleteReferencingRelationships},
                       preview.document_revision);
    ASSERT_TRUE(result.applied);
    EXPECT_EQ(document.find(ElementId{"inf_1"}), nullptr);
    EXPECT_TRUE(sacm::validation::validate(document).empty());
}

// The ScrubReferences delete policy removes the deleted element from a
// referencing relationship's source list and KEEPS the relationship as long as
// it stays structurally valid (source[1..*] / target[1..*], clause 11.13),
// dropping it only once scrubbing empties it. This is the scrub-then-drop the
// GSN editor needs and that DeleteReferencingRelationships cannot do: an
// inference with several sub-goal sources must survive the removal of one
// sub-goal, scrubbed to the rest.
TEST(Sacm23Argumentation, SACM23_CMD_005_ScrubReferencesKeepsMultiSourceInference) {
    Document document = build_argument_case();
    // A second sub-goal so the inference has two sources.
    ASSERT_TRUE(
        document.apply(CreateClaim{.parent = ElementId{"argpkg_1"}, .id = ElementId{"claim_sub2"}, .name = "Sub2"})
            .applied);
    ASSERT_TRUE(document
                    .apply(CreateAssertedRelationship{
                        .parent = ElementId{"argpkg_1"},
                        .kind = ElementKind::AssertedInference,
                        .id = ElementId{"inf_1"},
                        .sources = {ElementId{"claim_sub"}, ElementId{"claim_sub2"}},
                        .targets = {ElementId{"claim_top"}},
                    })
                    .applied);

    // Removing ONE source scrubs it and keeps the inference: the preview lists
    // the inference as Modified (scrubbed), not RelationshipDeleted.
    const OperationPreview preview = document.preview(DeleteElement{
        .target = ElementId{"claim_sub"},
        .reference_policy = ReferenceDeletePolicy::ScrubReferences,
    });
    ASSERT_TRUE(preview.can_apply);
    EXPECT_TRUE(std::ranges::any_of(preview.effects, [](const ChangeRecord& record) {
        return record.id.value() == "inf_1" && record.change == ChangeRecord::Change::Modified;
    }));
    EXPECT_FALSE(std::ranges::any_of(preview.effects, [](const ChangeRecord& record) {
        return record.id.value() == "inf_1" && record.change == ChangeRecord::Change::RelationshipDeleted;
    }));

    const auto scrubbed = document.apply(
        DeleteElement{.target = ElementId{"claim_sub"}, .reference_policy = ReferenceDeletePolicy::ScrubReferences},
        preview.document_revision);
    ASSERT_TRUE(scrubbed.applied);
    const auto* inference = document.find_as<sacm::model::AssertedRelationship>(ElementId{"inf_1"});
    ASSERT_NE(inference, nullptr) << "scrub dropped an inference that still had a source";
    ASSERT_EQ(inference->sources().size(), 1u);
    EXPECT_EQ(inference->sources().front(), ElementId{"claim_sub2"});
    EXPECT_EQ(document.find(ElementId{"claim_sub"}), nullptr);
    EXPECT_TRUE(sacm::validation::validate(document).empty());

    // The scrubbed inference round-trips through strict save.
    const auto saved = sacm::io::save_xmi_string(document);
    ASSERT_TRUE(saved.ok);
    const LoadResult reloaded = sacm::io::load_xmi_string(saved.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(reloaded.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(document, *reloaded.document).empty());

    // Removing the LAST source empties the inference, so scrubbing now DROPS it.
    const auto emptied = document.apply(
        DeleteElement{.target = ElementId{"claim_sub2"}, .reference_policy = ReferenceDeletePolicy::ScrubReferences});
    ASSERT_TRUE(emptied.applied);
    EXPECT_EQ(document.find(ElementId{"inf_1"}), nullptr) << "scrub kept an inference with no remaining source";
    EXPECT_TRUE(std::ranges::any_of(emptied.changes, [](const ChangeRecord& record) {
        return record.id.value() == "inf_1" && record.change == ChangeRecord::Change::RelationshipDeleted;
    }));
    EXPECT_EQ(document.find(ElementId{"claim_sub2"}), nullptr);
    EXPECT_TRUE(sacm::validation::validate(document).empty());
}

// An argument package holding the elements each negative below perturbs. The
// relationship line is the only thing that varies, so a failing test names the
// constraint rather than the fixture.
std::string argument_case_with(std::string_view relationship_elements) {
    return std::string(
               R"(<?xml version="1.0" encoding="UTF-8"?>)"
               R"(<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" )"
               R"(xmlns:xmi="http://www.omg.org/spec/XMI/20131001" )"
               R"(xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:version="2.0" xmi:id="acp_1">)"
               R"(<artifactPackage xmi:id="artpkg_1"><name content="Evidence"/>)"
               R"(<artifactElement xsi:type="sacm:Artifact" xmi:id="art_1"><name content="FMEA"/>)"
               R"(</artifactElement></artifactPackage>)"
               R"(<argumentPackage xmi:id="argpkg_1"><name content="Args"/>)"
               R"(<argumentElement xsi:type="sacm:Claim" xmi:id="claim_a"><name content="A"/></argumentElement>)"
               R"(<argumentElement xsi:type="sacm:Claim" xmi:id="claim_b"><name content="B"/></argumentElement>)"
               R"(<argumentElement xsi:type="sacm:ArtifactReference" xmi:id="ref_1" )"
               R"(referencedArtifactElement="art_1"><name content="Ref"/></argumentElement>)") +
           std::string(relationship_elements) + R"(</argumentPackage></sacm:AssuranceCasePackage>)";
}

std::vector<sacm::validation::Diagnostic> validate_argument_case(std::string_view relationship_elements) {
    const LoadResult result =
        sacm::io::load_xmi_string(argument_case_with(relationship_elements), LoadOptions{.mode = Mode::Strict});
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

// Clause 11.15: "The source of AssertedEvidence relationships must be
// ArtifactReference. OCL: self.source->forall(s|s.oclIsTypeOf(ArtifactReference))"
TEST(Sacm23Argumentation, SACM23_ARG_002_AssertedEvidenceSourceMustBeAnArtifactReference) {
    const auto bad = validate_argument_case(
        R"(<argumentElement xsi:type="sacm:AssertedEvidence" xmi:id="rel_1" source="claim_a" target="claim_b"/>)");
    EXPECT_TRUE(has_code(bad, sacm::validation::codes::kRefWrongType));
    EXPECT_TRUE(mentions(bad, "AssertedEvidence")) << (bad.empty() ? "no diagnostics at all" : bad.front().message);

    const auto good = validate_argument_case(
        R"(<argumentElement xsi:type="sacm:AssertedEvidence" xmi:id="rel_1" source="ref_1" target="claim_b"/>)");
    EXPECT_TRUE(good.empty()) << (good.empty() ? "" : good.front().message);
}

// Clauses 11.17/11.18: "The source and target of AssertedArtifactSupport /
// AssertedArtifactContext must be of type ArtifactReference."
TEST(Sacm23Argumentation, SACM23_ARG_002_ArtifactSupportAndContextEndsMustBeArtifactReferences) {
    for (const std::string_view family : {"AssertedArtifactSupport", "AssertedArtifactContext"}) {
        const std::string claim_target = std::string(R"(<argumentElement xsi:type="sacm:)") + std::string(family) +
                                         R"(" xmi:id="rel_1" source="ref_1" target="claim_b"/>)";
        const auto diagnostics = validate_argument_case(claim_target);
        EXPECT_TRUE(has_code(diagnostics, sacm::validation::codes::kRefWrongType)) << family;
        EXPECT_TRUE(mentions(diagnostics, "target")) << family;
    }
}

// The GSN mapping emits `Solution -> ArtifactReference` with
// `SupportedBy -> AssertedInference`, so an inference legitimately carries
// ArtifactReference ends. 11.14 and 11.16 state no end-typing constraint, and
// tightening them would reject conformant GSN-derived arguments.
TEST(Sacm23Argumentation, SACM23_ARG_002_InferenceAndContextKeepTheGenericEndTyping) {
    const auto inference = validate_argument_case(
        R"(<argumentElement xsi:type="sacm:AssertedInference" xmi:id="rel_1" source="ref_1" target="claim_b"/>)");
    EXPECT_TRUE(inference.empty()) << (inference.empty() ? "" : inference.front().message);

    const auto context = validate_argument_case(
        R"(<argumentElement xsi:type="sacm:AssertedContext" xmi:id="rel_2" source="claim_a" target="claim_b"/>)");
    EXPECT_TRUE(context.empty()) << (context.empty() ? "" : context.front().message);
}

// Clause 11.13 declares target:ArgumentAsset[1]. Only the lower bound was
// checked, so a two-target relationship round-tripped and validated clean while
// reading as a relationship with two conclusions.
TEST(Sacm23Argumentation, SACM23_ARG_002_RelationshipTargetUpperBoundIsOne) {
    const auto diagnostics =
        validate_argument_case(R"(<argumentElement xsi:type="sacm:AssertedInference" xmi:id="rel_1" source="ref_1" )"
                               R"(target="claim_a claim_b"/>)");
    EXPECT_TRUE(has_code(diagnostics, sacm::validation::codes::kMultiplicityViolation));
    EXPECT_TRUE(mentions(diagnostics, "target[1]"))
        << (diagnostics.empty() ? "no diagnostics at all" : diagnostics.front().message);
}

// Clause 11.4: "If an ArgumentPackage has nested ArgumentPackages, then it is
// only allowed to contain ArgumentPackages."
TEST(Sacm23Argumentation, SACM23_ARG_002_NestingAPackageForbidsOtherContent) {
    const auto mixed = validate_argument_case(
        R"(<argumentElement xsi:type="sacm:ArgumentPackage" xmi:id="argpkg_nested"><name content="Nested"/>)"
        R"(</argumentElement>)");
    EXPECT_TRUE(has_code(mixed, sacm::validation::codes::kPackageContentInvalid));
    EXPECT_TRUE(mentions(mixed, "clause 11.4")) << (mixed.empty() ? "no diagnostics at all" : mixed.front().message);

    // A package that nests nothing keeps its heterogeneous content.
    const auto flat = validate_argument_case("");
    EXPECT_TRUE(flat.empty()) << (flat.empty() ? "" : flat.front().message);
}

} // namespace

// A TOLERANT load accepts a document with a dangling reference -- the reader
// keeps endpoint ids verbatim and reports the unresolvable ones -- and
// `validate_structure` grades one as an Error. `Document::apply` used to assert
// the WHOLE document was error-free after every mutation, so a debug build could
// not edit such a document at all: an unrelated rename aborted, and so did the
// repair that exists to fix the dangle. Assurance Forge's `UnresolvedEndpoint`
// quick fix is that repair.
//
// SACM23-VAL-002 is now "introduces no structural error", which is what the
// contract was always for, said about the mutation rather than about the file
// someone opened. This is the regression test: an ordinary edit on a document
// that arrived broken must simply work.
TEST(Sacm23Argumentation, SACM23_ARG_001_EditsADocumentThatArrivedWithADanglingReference) {
    constexpr const char* kDangling = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:version="2.0" xmi:id="acp_1">
  <argumentPackage xmi:id="argpkg_1">
    <argumentElement xsi:type="sacm:Claim" xmi:id="claim_top"/>
    <argumentElement xsi:type="sacm:Claim" xmi:id="claim_real"/>
    <argumentElement xsi:type="sacm:AssertedInference" xmi:id="inf_broken" source="claim_real gone_a" target="claim_top"/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";
    LoadResult loaded = sacm::io::load_xmi_string(kDangling, LoadOptions{.mode = Mode::Tolerant});
    ASSERT_TRUE(loaded.document.has_value());
    // Non-vacuity: the document really is structurally invalid as loaded.
    EXPECT_TRUE(sacm::validation::has_errors(sacm::validation::validate_structure(*loaded.document)));

    EXPECT_TRUE(loaded.document->apply(SetName{.element = ElementId{"claim_real"}, .name = "Renamed"}).applied)
        << "an edit unrelated to the broken reference was refused";

    // And the repair itself, which is the case that found this.
    EXPECT_TRUE(loaded.document
                    ->apply(SetRelationshipEnds{.relationship = ElementId{"inf_broken"},
                                                .sources = {ElementId{"claim_real"}},
                                                .targets = {ElementId{"claim_top"}}})
                    .applied);
    EXPECT_FALSE(sacm::validation::has_errors(sacm::validation::validate_structure(*loaded.document)))
        << "the repair did not actually clear the dangling reference";
}
