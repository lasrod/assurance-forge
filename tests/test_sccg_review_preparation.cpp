// Steps 1-5 of the SCCG review workflow, exercised through the one function
// that performs them.
//
// These used to be reachable only from `AiReviewController::BeginReviewForSelection`,
// which needs an application, a task runner and a rendered frame. What a test
// could reach was the individual pieces; what shipped was their composition.

#include "review/sccg/sccg_review_preparation.h"

#include "core/assurance_tree.h"
#include "core/guideline_catalog.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {

const core::GuidelineCatalog& Catalog() {
    static const core::GuidelineCatalog catalog = [] {
        core::GuidelineCatalog loaded;
        std::string error;
        EXPECT_TRUE(core::LoadGuidelineCatalog(loaded, error)) << error;
        return loaded;
    }();
    return catalog;
}

parser::SacmElement
MakeElement(const std::string& id, const std::string& type, const std::string& name, const std::string& content = {}) {
    parser::SacmElement element;
    element.id = id;
    element.type = type;
    element.name = name;
    element.content = content;
    return element;
}

parser::SacmElement MakeRelationship(const std::string& id,
                                     const std::string& type,
                                     std::vector<std::string> sources,
                                     std::vector<std::string> targets) {
    parser::SacmElement relationship;
    relationship.id = id;
    relationship.type = type;
    relationship.source_refs = std::move(sources);
    relationship.target_refs = std::move(targets);
    return relationship;
}

// The fragment Paper A section 6.1 reviews: a top goal, a strategy over "the
// main hazard types", two sub-goals and two evidence items. Written out here
// rather than loaded from the paper repository so the test states its own
// input.
parser::AssuranceCase PaperAInitialFragment() {
    parser::AssuranceCase assurance_case;
    assurance_case.elements.push_back(MakeElement("G1", "claim", {}, "The kitchen blender is safe."));
    assurance_case.elements.push_back(
        MakeElement("A1", "argumentreasoning", {}, "Argument over the main hazard types."));
    assurance_case.elements.push_back(MakeElement("G2", "claim", {}, "Blade contact hazards are controlled."));
    assurance_case.elements.push_back(MakeElement("G3", "claim", {}, "Electrical hazards are controlled."));
    assurance_case.elements.push_back(
        MakeElement("E1", "artifactreference", {}, "BL-TR-003 rev. A: motor did not start unless the jar was seated."));
    assurance_case.elements.push_back(
        MakeElement("E2", "artifactreference", {}, "BL-TR-005 rev. A: insulation resistance above 7 MOhm."));
    parser::SacmElement inference = MakeRelationship("R1", "assertedinference", {"G2", "G3"}, {"G1"});
    inference.reasoning_ref = "A1";
    assurance_case.elements.push_back(inference);
    assurance_case.elements.push_back(MakeRelationship("R2", "assertedevidence", {"E1"}, {"G2"}));
    assurance_case.elements.push_back(MakeRelationship("R3", "assertedevidence", {"E2"}, {"G3"}));
    return assurance_case;
}

bool Contains(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

const review::AiReviewUnavailableDataPackage* FindUnavailable(const review::AiReviewDataPackageBundle& packages,
                                                              const std::string& id) {
    const auto found =
        std::find_if(packages.unavailable.begin(),
                     packages.unavailable.end(),
                     [&](const review::AiReviewUnavailableDataPackage& package) { return package.id == id; });
    return found == packages.unavailable.end() ? nullptr : &*found;
}

} // namespace

// Profile selection is decided on the element's SCCG role, so the three roles
// in one fragment must draw three different profiles from one action.
TEST(SccgReviewPreparationTest, SelectsTheProfileForEachElementRole) {
    const parser::AssuranceCase assurance_case = PaperAInitialFragment();
    const core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);
    const review::AiReviewCaseContext case_context;

    struct Expectation {
        const char* element_id;
        const char* role;
        const char* profile_id;
    };
    const std::vector<Expectation> expectations{
        {"G1", "claim", "claim_review"},
        {"A1", "strategy", "strategy_review"},
        {"E1", "evidence", "evidence_review"},
    };

    for (const Expectation& expectation : expectations) {
        SCOPED_TRACE(expectation.element_id);
        const review::SccgReviewPreparation preparation =
            review::PrepareSccgReview(&assurance_case, tree, expectation.element_id, {}, case_context, &Catalog());
        ASSERT_TRUE(preparation.ok()) << preparation.error_message;
        EXPECT_EQ(preparation.element_role, expectation.role);
        EXPECT_EQ(preparation.review_profile_id, expectation.profile_id);
        EXPECT_FALSE(preparation.request.prompt.empty());
        EXPECT_FALSE(preparation.request.systemInstruction.empty());
        EXPECT_TRUE(Contains(preparation.reviewed_element_ids, expectation.element_id));
    }
}

// The guidelines Paper A section 6.2 cites have to be in the request for the
// element the paper cites them against, or the AI review cannot reproduce the
// manual finding whatever the model does.
TEST(SccgReviewPreparationTest, CarriesTheGuidelinesPaperACitesForEachElement) {
    const parser::AssuranceCase assurance_case = PaperAInitialFragment();
    const core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);
    const review::AiReviewCaseContext case_context;

    struct Expectation {
        const char* element_id;
        std::vector<std::string> guideline_ids;
    };
    const std::vector<Expectation> expectations{
        {"G1", {"CL.4", "AR.6"}},
        {"A1", {"AR.2"}},
        {"E1", {"EV.5"}},
        {"E2", {"EV.5"}},
    };

    for (const Expectation& expectation : expectations) {
        SCOPED_TRACE(expectation.element_id);
        const review::SccgReviewPreparation preparation =
            review::PrepareSccgReview(&assurance_case, tree, expectation.element_id, {}, case_context, &Catalog());
        ASSERT_TRUE(preparation.ok()) << preparation.error_message;
        for (const std::string& guideline_id : expectation.guideline_ids)
            EXPECT_TRUE(Contains(preparation.guideline_ids, guideline_id))
                << guideline_id << " is not in the " << preparation.review_profile_id << " request.";
    }
}

// A package the tool builds, for a case that has nothing to put in it, is
// EMPTY. Reporting it as not-implemented tells the review the tool could not
// supply the context, when the truth is that the argument does not have it --
// and those two facts lead a reviewer to opposite conclusions.
TEST(SccgReviewPreparationTest, ReportsAnImplementedButEmptyPackageAsEmpty) {
    const parser::AssuranceCase assurance_case = PaperAInitialFragment();
    const core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);
    const review::AiReviewCaseContext case_context;

    const review::SccgReviewPreparation preparation =
        review::PrepareSccgReview(&assurance_case, tree, "G1", {}, case_context, &Catalog());
    ASSERT_TRUE(preparation.ok()) << preparation.error_message;

    // G1 is the root goal and carries no context: three packages the tool
    // builds, none of which this argument fills.
    for (const char* package_id : {"PARENT", "DIRECT_CONTEXT", "INHERITED_CONTEXT"}) {
        SCOPED_TRACE(package_id);
        const review::AiReviewUnavailableDataPackage* package = FindUnavailable(preparation.data_packages, package_id);
        ASSERT_NE(package, nullptr);
        EXPECT_EQ(package->absence, review::DataPackageAbsence::Empty);
        EXPECT_FALSE(package->reason.empty());
    }
}

// The counterpart: a package with a required field this tool cannot fill stays
// not-implemented, so the distinction the states exist to draw is real in both
// directions.
TEST(SccgReviewPreparationTest, ReportsAPackageWithNoSourceAsNotImplemented) {
    const parser::AssuranceCase assurance_case = PaperAInitialFragment();
    const core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);
    const review::AiReviewCaseContext case_context;

    const review::SccgReviewPreparation preparation =
        review::PrepareSccgReview(&assurance_case, tree, "E1", {}, case_context, &Catalog());
    ASSERT_TRUE(preparation.ok()) << preparation.error_message;

    const review::AiReviewUnavailableDataPackage* links = FindUnavailable(preparation.data_packages, "STANDARD_LINKS");
    ASSERT_NE(links, nullptr);
    EXPECT_EQ(links->absence, review::DataPackageAbsence::NotImplemented);
}

// EVIDENCE_BASIS is supplied for an evidence element, empty, rather than
// declared absent.
//
// SCCG gives the package no required fields, so a tool that can address the
// element can always send one; declaring it unavailable triggers
// `evidence_review`'s `when_absent` statement, which forbids reporting an
// absent basis as a finding and names EV.5, EV.6, SU.3, SU.6, SU.7, SU.8, LF.5
// and LF.7 unassessable. Measured, that cost EV.5 entirely: 0 of 3 runs against
// evidence whose argument stated no sufficiency basis, while the same guideline
// fired 3 of 3 under justification_review, which publishes no such statement.
// An empty package says the true thing -- this case records no acceptance basis
// -- which is the finding rather than a gap in the review.
TEST(SccgReviewPreparationTest, SuppliesAnEmptyEvidenceBasisForEvidenceRatherThanDeclaringItAbsent) {
    const parser::AssuranceCase assurance_case = PaperAInitialFragment();
    const core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);
    const review::AiReviewCaseContext case_context;

    const review::SccgReviewPreparation preparation =
        review::PrepareSccgReview(&assurance_case, tree, "E1", {}, case_context, &Catalog());
    ASSERT_TRUE(preparation.ok()) << preparation.error_message;

    EXPECT_EQ(FindUnavailable(preparation.data_packages, "EVIDENCE_BASIS"), nullptr)
        << "a package with no required fields must not be reported as unavailable";

    const review::AiReviewDataPackage* basis = nullptr;
    for (const review::AiReviewDataPackage& package : preparation.data_packages.available) {
        if (package.id == "EVIDENCE_BASIS")
            basis = &package;
    }
    ASSERT_NE(basis, nullptr) << "EVIDENCE_BASIS should be supplied for an evidence element";

    // Every optional field present and empty, so "we hold none of this" is
    // stated rather than left to be inferred from a missing key.
    for (const char* field :
         {"acceptance_criteria", "coverage", "thresholds", "scenario_set", "configuration", "limitations"}) {
        SCOPED_TRACE(field);
        EXPECT_NE(basis->json.find(field), std::string::npos);
    }
    // And it must not read as "a basis exists somewhere and was withheld".
    EXPECT_NE(basis->json.find("no acceptance criteria"), std::string::npos);
}

// A claim has no acceptance basis of its own, so for a non-evidence element the
// package is genuinely empty -- and says which of the two reasons that is.
TEST(SccgReviewPreparationTest, ReportsEvidenceBasisAsEmptyForANonEvidenceElement) {
    const parser::AssuranceCase assurance_case = PaperAInitialFragment();
    const core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);
    const review::AiReviewCaseContext case_context;

    const review::SccgReviewPreparation preparation =
        review::PrepareSccgReview(&assurance_case, tree, "G1", {}, case_context, &Catalog());
    ASSERT_TRUE(preparation.ok()) << preparation.error_message;

    const review::AiReviewUnavailableDataPackage* basis = FindUnavailable(preparation.data_packages, "EVIDENCE_BASIS");
    ASSERT_NE(basis, nullptr);
    EXPECT_EQ(basis->absence, review::DataPackageAbsence::Empty);
}

TEST(SccgReviewPreparationTest, RefusesWithoutACase) {
    const parser::AssuranceCase assurance_case;
    const core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);
    const review::AiReviewCaseContext case_context;

    EXPECT_EQ(review::PrepareSccgReview(nullptr, tree, "G1", {}, case_context, &Catalog()).failure,
              review::SccgReviewPreparationFailure::NoCase);
    EXPECT_EQ(review::PrepareSccgReview(&assurance_case, tree, {}, {}, case_context, &Catalog()).failure,
              review::SccgReviewPreparationFailure::NoSelection);
    EXPECT_EQ(review::PrepareSccgReview(&assurance_case, tree, "nope", {}, case_context, &Catalog()).failure,
              review::SccgReviewPreparationFailure::ElementNotFound);
}

// Naming a profile that does not review this element's role is refused rather
// than run: the wrong criteria applied to an element is a review whose findings
// look authoritative and are not.
TEST(SccgReviewPreparationTest, RefusesAProfileThatDoesNotApplyToTheElement) {
    const parser::AssuranceCase assurance_case = PaperAInitialFragment();
    const core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);
    const review::AiReviewCaseContext case_context;

    const review::SccgReviewPreparation preparation =
        review::PrepareSccgReview(&assurance_case, tree, "G1", "evidence_review", case_context, &Catalog());
    EXPECT_EQ(preparation.failure, review::SccgReviewPreparationFailure::ProfileIncompatible);
    EXPECT_FALSE(preparation.error_message.empty());
}
