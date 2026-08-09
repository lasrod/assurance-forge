#include "sacm/compare/semantic_compare.h"
#include "sacm/io/xmi.h"
#include "sacm/model/document.h"
#include "sacm/validation/codes.h"
#include "sacm/validation/validate.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

using sacm::commands::ChangeRecord;
using sacm::commands::CreateAssuranceCasePackage;
using sacm::commands::CrossPackageReferencePolicy;
using sacm::commands::DeleteElement;
using sacm::commands::MutationResult;
using sacm::commands::OperationPreview;
using sacm::commands::PackageDeletePolicy;
using sacm::commands::ReferenceDeletePolicy;
using sacm::io::LoadOptions;
using sacm::io::LoadResult;
using sacm::io::Mode;
using sacm::model::Document;
using sacm::model::ElementId;

std::filesystem::path fixture(std::string_view name) {
    return std::filesystem::path(SACM_TEST_DATA_DIR) / "sacm23" / name;
}

bool has_code(const std::vector<sacm::validation::Diagnostic>& diagnostics, std::string_view code) {
    return std::ranges::any_of(diagnostics, [&](const auto& diagnostic) { return diagnostic.code == code; });
}

LoadResult load_nested_fixture() {
    return sacm::io::load_xmi_file(fixture("package-nested-interfaces-valid.sacm.xmi"),
                                   LoadOptions{.mode = Mode::Strict});
}

TEST(Sacm23Packages, SACM23_PKG_001_NestedPackagesRoundTrip) {
    const LoadResult first = load_nested_fixture();
    ASSERT_TRUE(first.ok) << (first.diagnostics.empty() ? "" : first.diagnostics.front().message);
    const auto& document = *first.document;

    const auto& root = *document.roots().front();
    ASSERT_EQ(root.assurance_case_packages().size(), 1u);
    const auto& nested = *root.assurance_case_packages().front();
    EXPECT_EQ(nested.argument_packages().size(), 4u); // A, A-interface, B, binding
    EXPECT_TRUE(sacm::validation::validate(document).empty());

    const auto saved = sacm::io::save_xmi_string(document);
    ASSERT_TRUE(saved.ok);
    const LoadResult second = sacm::io::load_xmi_string(saved.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(second.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(document, *second.document).empty());
}

TEST(Sacm23Packages, SACM23_PKG_002_InterfacesAndBindingsPreserveParticipants) {
    const LoadResult result = load_nested_fixture();
    ASSERT_TRUE(result.ok);
    const auto& document = *result.document;

    const auto* interface_pkg = document.find_as<sacm::model::ArgumentPackageInterface>(ElementId{"argpkg_a_if"});
    ASSERT_NE(interface_pkg, nullptr);
    ASSERT_TRUE(interface_pkg->implements().has_value());
    EXPECT_EQ(interface_pkg->implements()->value(), "argpkg_a");

    const auto* package = document.find_as<sacm::model::ArgumentPackage>(ElementId{"argpkg_a"});
    ASSERT_NE(package, nullptr);
    ASSERT_EQ(package->interfaces().size(), 1u);
    EXPECT_EQ(package->interfaces().front().value(), "argpkg_a_if");

    const auto* binding = document.find_as<sacm::model::ArgumentPackageBinding>(ElementId{"argpkg_binding"});
    ASSERT_NE(binding, nullptr);
    ASSERT_EQ(binding->participant_packages().size(), 2u);
}

TEST(Sacm23Packages, SACM23_PKG_002_BindingWithOneParticipantIsInvalid) {
    const LoadResult result = sacm::io::load_xmi_file(fixture("invalid/binding-participants-invalid.sacm.xmi"),
                                                      LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(result.document.has_value());
    const auto diagnostics = sacm::validation::validate(*result.document);
    EXPECT_TRUE(has_code(diagnostics, sacm::validation::codes::kMultiplicityViolation));
}

TEST(Sacm23Packages, SACM23_PKG_003_RecursiveDeletePreviewListsNestedContent) {
    const LoadResult result = load_nested_fixture();
    ASSERT_TRUE(result.ok);
    const auto& document = *result.document;

    const OperationPreview preview = document.preview(DeleteElement{
        .target = ElementId{"acp_nested"},
        .reference_policy = ReferenceDeletePolicy::DeleteReferencingRelationships,
        .package_policy = PackageDeletePolicy::DeleteRecursively,
        .cross_package_policy = CrossPackageReferencePolicy::DeleteExternalReferencingRelationships,
    });
    EXPECT_TRUE(preview.can_apply);
    const auto listed = [&preview](std::string_view id) {
        return std::ranges::any_of(preview.effects,
                                   [&](const ChangeRecord& record) { return record.id.value() == id; });
    };
    for (const std::string_view id :
         {"acp_nested", "argpkg_a", "argpkg_a_if", "argpkg_b", "argpkg_binding", "claim_a", "claim_b", "ctx_1"}) {
        EXPECT_TRUE(listed(id)) << "preview should list " << id;
    }
}

TEST(Sacm23Packages, SACM23_PKG_003_CrossPackageReferencePoliciesAreExplicit) {
    LoadResult loaded = load_nested_fixture();
    ASSERT_TRUE(loaded.ok);
    ASSERT_TRUE(loaded.document.has_value());
    Document document = std::move(*loaded.document);

    // ctx_1 lives in argpkg_b and targets claim_a in argpkg_a: deleting
    // claim_a is a cross-package effect and must be explicit.
    const MutationResult rejected = document.apply(DeleteElement{
        .target = ElementId{"claim_a"},
        .reference_policy = ReferenceDeletePolicy::DeleteReferencingRelationships,
    });
    EXPECT_FALSE(rejected.applied);
    ASSERT_FALSE(rejected.diagnostics.empty());
    EXPECT_EQ(rejected.diagnostics.front().code, sacm::validation::codes::kCmdExternalReferences);
    EXPECT_NE(document.find(ElementId{"claim_a"}), nullptr);

    const MutationResult applied = document.apply(DeleteElement{
        .target = ElementId{"claim_a"},
        .reference_policy = ReferenceDeletePolicy::DeleteReferencingRelationships,
        .cross_package_policy = CrossPackageReferencePolicy::DeleteExternalReferencingRelationships,
    });
    ASSERT_TRUE(applied.applied);
    EXPECT_EQ(document.find(ElementId{"claim_a"}), nullptr);
    EXPECT_EQ(document.find(ElementId{"ctx_1"}), nullptr); // cascaded relationship
    EXPECT_NE(document.find(ElementId{"claim_b"}), nullptr);
    EXPECT_TRUE(sacm::validation::validate(document).empty());
    EXPECT_TRUE(std::ranges::any_of(applied.changes, [](const ChangeRecord& record) {
        return record.id.value() == "ctx_1" && record.change == ChangeRecord::Change::RelationshipDeleted;
    }));
}

TEST(Sacm23Packages, SACM23_PKG_001_CreatesNestedAssuranceCasePackages) {
    Document document;
    ASSERT_TRUE(document.apply(CreateAssuranceCasePackage{.id = ElementId{"acp_root"}, .name = "R"}).applied);
    ASSERT_TRUE(document
                    .apply(CreateAssuranceCasePackage{
                        .id = ElementId{"acp_child"}, .name = "C", .parent = ElementId{"acp_root"}})
                    .applied);
    const auto* child = document.find_as<sacm::model::AssuranceCasePackage>(ElementId{"acp_child"});
    ASSERT_NE(child, nullptr);
    ASSERT_NE(child->parent(), nullptr);
    EXPECT_EQ(child->parent()->id().value(), "acp_root");
    EXPECT_EQ(document.roots().size(), 1u);
}

std::string case_document(std::string_view body) {
    return std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)"
                       R"(<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" )"
                       R"(xmlns:xmi="http://www.omg.org/spec/XMI/20131001" )"
                       R"(xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:version="2.0" xmi:id="acp_1">)"
                       R"(<name content="Case"/>)") +
           std::string(body) + R"(</sacm:AssuranceCasePackage>)";
}

std::vector<sacm::validation::Diagnostic> validate_case(std::string_view body) {
    const LoadResult result = sacm::io::load_xmi_string(case_document(body), LoadOptions{.mode = Mode::Strict});
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

// Clause 11.6: an ArgumentPackageInterface is "only allowed with isCitation=true
// and +citedElement refer to ArgumentAssets within the ArgumentPackage
// implementation referred to by implements". An interface full of plain claims
// validated clean.
TEST(Sacm23Packages, SACM23_PKG_002_ArgumentPackageInterfaceHoldsOnlyCitations) {
    constexpr std::string_view kPackages =
        R"(<argumentPackage xmi:id="ap_1"><name content="Args"/>)"
        R"(<argumentElement xsi:type="sacm:Claim" xmi:id="claim_inner"><name content="Inner"/></argumentElement>)"
        R"(</argumentPackage>)";

    const auto non_citation = validate_case(
        std::string(kPackages) +
        R"(<argumentPackage xsi:type="sacm:ArgumentPackageInterface" xmi:id="ap_if" implements="ap_1">)"
        R"(<name content="Interface"/>)"
        R"(<argumentElement xsi:type="sacm:Claim" xmi:id="claim_plain"><name content="Plain"/></argumentElement>)"
        R"(</argumentPackage>)");
    EXPECT_TRUE(has_code(non_citation, sacm::validation::codes::kPackageContentInvalid));
    EXPECT_TRUE(mentions(non_citation, "clause 11.6"))
        << (non_citation.empty() ? "no diagnostics at all" : non_citation.front().message);

    // A citation into the implemented package is the conformant shape.
    const auto citation = validate_case(
        std::string(kPackages) +
        R"(<argumentPackage xsi:type="sacm:ArgumentPackageInterface" xmi:id="ap_if" implements="ap_1">)"
        R"(<name content="Interface"/>)"
        R"(<argumentElement xsi:type="sacm:Claim" xmi:id="claim_cite" isCitation="true" citedElement="claim_inner">)"
        R"(<name content="Cited"/></argumentElement></argumentPackage>)");
    EXPECT_TRUE(citation.empty()) << (citation.empty() ? "" : citation.front().message);
}

// The other half of 11.6: the citation points *into* the implemented package.
// Prose without OCL, so a warning.
TEST(Sacm23Packages, SACM23_PKG_002_InterfaceCitationOutsideTheImplementedPackageWarns) {
    const auto diagnostics = validate_case(
        R"(<argumentPackage xmi:id="ap_1"><name content="Args"/>)"
        R"(<argumentElement xsi:type="sacm:Claim" xmi:id="claim_inner"><name content="Inner"/></argumentElement>)"
        R"(</argumentPackage>)"
        R"(<argumentPackage xmi:id="ap_other"><name content="Other"/>)"
        R"(<argumentElement xsi:type="sacm:Claim" xmi:id="claim_elsewhere"><name content="Elsewhere"/>)"
        R"(</argumentElement></argumentPackage>)"
        R"(<argumentPackage xsi:type="sacm:ArgumentPackageInterface" xmi:id="ap_if" implements="ap_1">)"
        R"(<name content="Interface"/>)"
        R"(<argumentElement xsi:type="sacm:Claim" xmi:id="claim_cite" isCitation="true" )"
        R"(citedElement="claim_elsewhere"><name content="Cited"/></argumentElement></argumentPackage>)");
    ASSERT_TRUE(has_code(diagnostics, sacm::validation::codes::kPackageContentInvalid));
    EXPECT_TRUE(std::ranges::all_of(diagnostics, [](const sacm::validation::Diagnostic& diagnostic) {
        return diagnostic.severity == sacm::validation::Severity::Warning;
    }));
}

// Clause 11.5: "The ArgumentElements contained by an ArgumentPackageBinding must
// be ArgumentElement citations to ArgumentElements contained within the
// ArgumentPackages associated by the participantPackage association."
TEST(Sacm23Packages, SACM23_PKG_002_ArgumentPackageBindingHoldsOnlyCitations) {
    const auto diagnostics = validate_case(
        R"(<argumentPackage xmi:id="ap_1"><name content="A"/></argumentPackage>)"
        R"(<argumentPackage xmi:id="ap_2"><name content="B"/></argumentPackage>)"
        R"(<argumentPackage xsi:type="sacm:ArgumentPackageBinding" xmi:id="ap_bind" )"
        R"(participantPackage="ap_1 ap_2"><name content="Binding"/>)"
        R"(<argumentElement xsi:type="sacm:Claim" xmi:id="claim_plain"><name content="Plain"/></argumentElement>)"
        R"(</argumentPackage>)");
    EXPECT_TRUE(has_code(diagnostics, sacm::validation::codes::kPackageContentInvalid));
    EXPECT_TRUE(mentions(diagnostics, "clause 11.5"))
        << (diagnostics.empty() ? "no diagnostics at all" : diagnostics.front().message);
}

// Clause 12.5, the artifact-side parallel of 11.6.
TEST(Sacm23Packages, SACM23_PKG_002_ArtifactPackageInterfaceHoldsOnlyCitations) {
    const auto diagnostics = validate_case(
        R"(<artifactPackage xmi:id="artp_1"><name content="Artifacts"/>)"
        R"(<artifactElement xsi:type="sacm:Artifact" xmi:id="art_inner"><name content="Inner"/></artifactElement>)"
        R"(</artifactPackage>)"
        R"(<artifactPackage xsi:type="sacm:ArtifactPackageInterface" xmi:id="artp_if" implements="artp_1">)"
        R"(<name content="Interface"/>)"
        R"(<artifactElement xsi:type="sacm:Artifact" xmi:id="art_plain"><name content="Plain"/></artifactElement>)"
        R"(</artifactPackage>)");
    EXPECT_TRUE(has_code(diagnostics, sacm::validation::codes::kPackageContentInvalid));
    EXPECT_TRUE(mentions(diagnostics, "clause 12.5"))
        << (diagnostics.empty() ? "no diagnostics at all" : diagnostics.front().message);
}

// Clause 9.3 OCL: an AssuranceCasePackageInterface contains only interface-typed
// sub-packages.
TEST(Sacm23Packages, SACM23_PKG_002_AssuranceCasePackageInterfaceHoldsOnlyInterfaces) {
    const auto diagnostics = validate_case(
        R"(<assuranceCasePackage xmi:id="acp_impl"><name content="Implementation"/></assuranceCasePackage>)"
        R"(<assuranceCasePackage xsi:type="sacm:AssuranceCasePackageInterface" xmi:id="acp_if" )"
        R"(implements="acp_impl"><name content="Interface"/>)"
        R"(<argumentPackage xmi:id="ap_plain"><name content="Plain"/></argumentPackage>)"
        R"(</assuranceCasePackage>)");
    EXPECT_TRUE(has_code(diagnostics, sacm::validation::codes::kPackageContentInvalid));
    EXPECT_TRUE(mentions(diagnostics, "clause 9.3"))
        << (diagnostics.empty() ? "no diagnostics at all" : diagnostics.front().message);
}

// Clause 9.4's OCL admits a participant only if it is exactly a package or a
// package interface, which excludes a binding. Clause 10.5/10.6's parallel OCL
// uses `oclIsKindOf` and would admit one, so this is a warning, and the
// resolution is recorded in docs/sacm/sacm-decisions-and-questions.md.
TEST(Sacm23Packages, SACM23_PKG_002_ABindingIsNotAParticipantOfABinding) {
    const auto diagnostics =
        validate_case(R"(<argumentPackage xmi:id="ap_1"><name content="A"/></argumentPackage>)"
                      R"(<argumentPackage xmi:id="ap_2"><name content="B"/></argumentPackage>)"
                      R"(<argumentPackage xsi:type="sacm:ArgumentPackageBinding" xmi:id="ap_inner_bind" )"
                      R"(participantPackage="ap_1 ap_2"><name content="Inner"/></argumentPackage>)"
                      R"(<argumentPackage xsi:type="sacm:ArgumentPackageBinding" xmi:id="ap_outer_bind" )"
                      R"(participantPackage="ap_1 ap_inner_bind"><name content="Outer"/></argumentPackage>)");
    ASSERT_TRUE(has_code(diagnostics, sacm::validation::codes::kPackageContentInvalid));
    EXPECT_TRUE(std::ranges::all_of(diagnostics, [](const sacm::validation::Diagnostic& diagnostic) {
        return diagnostic.severity == sacm::validation::Severity::Warning;
    }));
    EXPECT_TRUE(mentions(diagnostics, "itself a binding"));
}

// The four Terminology/Artifact interface and binding classes were instantiated
// by nothing beyond the kind-existence sweep in test_metamodel_coverage.cpp --
// no round trip, no behaviour. This is that coverage.
TEST(Sacm23Packages, SACM23_PKG_002_TerminologyAndArtifactInterfacesAndBindingsRoundTrip) {
    const std::string body =
        R"(<terminologyPackage xmi:id="tp_1"><name content="Vocabulary"/>)"
        R"(<terminologyElement xsi:type="sacm:Term" xmi:id="term_1" value="hazard"><name content="Hazard"/>)"
        R"(</terminologyElement></terminologyPackage>)"
        R"(<terminologyPackage xmi:id="tp_2"><name content="Second"/></terminologyPackage>)"
        R"(<terminologyPackage xsi:type="sacm:TerminologyPackageInterface" xmi:id="tp_if" implements="tp_1">)"
        R"(<name content="Terminology Interface"/>)"
        R"(<terminologyElement xsi:type="sacm:Term" xmi:id="term_cite" isCitation="true" citedElement="term_1">)"
        R"(<name content="Cited"/></terminologyElement></terminologyPackage>)"
        R"(<terminologyPackage xsi:type="sacm:TerminologyPackageBinding" xmi:id="tp_bind" )"
        R"(participantPackage="tp_1 tp_2"><name content="Terminology Binding"/></terminologyPackage>)"
        R"(<artifactPackage xmi:id="artp_1"><name content="Artifacts"/>)"
        R"(<artifactElement xsi:type="sacm:Artifact" xmi:id="art_1"><name content="Report"/></artifactElement>)"
        R"(</artifactPackage>)"
        R"(<artifactPackage xmi:id="artp_2"><name content="Second"/></artifactPackage>)"
        R"(<artifactPackage xsi:type="sacm:ArtifactPackageInterface" xmi:id="artp_if" implements="artp_1">)"
        R"(<name content="Artifact Interface"/>)"
        R"(<artifactElement xsi:type="sacm:Artifact" xmi:id="art_cite" isCitation="true" citedElement="art_1">)"
        R"(<name content="Cited"/></artifactElement></artifactPackage>)"
        R"(<artifactPackage xsi:type="sacm:ArtifactPackageBinding" xmi:id="artp_bind" )"
        R"(participantPackage="artp_1 artp_2"><name content="Artifact Binding"/></artifactPackage>)";

    const LoadResult first = sacm::io::load_xmi_string(case_document(body), LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(first.ok) << (first.diagnostics.empty() ? "" : first.diagnostics.front().message);
    const Document& document = *first.document;

    const auto* terminology_interface = document.find_as<sacm::model::TerminologyPackageInterface>(ElementId{"tp_if"});
    ASSERT_NE(terminology_interface, nullptr);
    ASSERT_TRUE(terminology_interface->implements().has_value());
    EXPECT_EQ(terminology_interface->implements()->value(), "tp_1");
    const auto* terminology_binding = document.find_as<sacm::model::TerminologyPackageBinding>(ElementId{"tp_bind"});
    ASSERT_NE(terminology_binding, nullptr);
    EXPECT_EQ(terminology_binding->participant_packages().size(), 2u);
    const auto* artifact_interface = document.find_as<sacm::model::ArtifactPackageInterface>(ElementId{"artp_if"});
    ASSERT_NE(artifact_interface, nullptr);
    ASSERT_TRUE(artifact_interface->implements().has_value());
    EXPECT_EQ(artifact_interface->implements()->value(), "artp_1");
    const auto* artifact_binding = document.find_as<sacm::model::ArtifactPackageBinding>(ElementId{"artp_bind"});
    ASSERT_NE(artifact_binding, nullptr);
    EXPECT_EQ(artifact_binding->participant_packages().size(), 2u);

    EXPECT_TRUE(sacm::validation::validate(document).empty());
    const auto saved = sacm::io::save_xmi_string(document);
    ASSERT_TRUE(saved.ok);
    const LoadResult second = sacm::io::load_xmi_string(saved.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(second.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(document, *second.document).empty());
}

} // namespace
