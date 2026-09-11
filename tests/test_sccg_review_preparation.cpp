// Steps 1-5 of the SCCG review workflow, exercised through the one function
// that performs them.
//
// These used to be reachable only from `AiReviewController::BeginReviewForSelection`,
// which needs an application, a task runner and a rendered frame. What a test
// could reach was the individual pieces; what shipped was their composition.

#include "review/sccg/sccg_review_preparation.h"

#include "core/assurance_tree.h"
#include "core/guideline_catalog.h"
#include "core/reviews/review_item.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
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

// EVIDENCE_BASIS has no source in this tool, so it is reported not_implemented
// -- and under SCCG 0.8.0 that absence silences nothing.
//
// Under 0.7.0 evidence_review's when_absent statement named EV.5, EV.6, SU.3,
// SU.6, SU.7, SU.8, LF.5 and LF.7 unassessable whenever the package was absent,
// and this tool worked round it by sending the package available with every
// field empty. 0.8.0 fixed the cause (safety-case-core-guidelines#13) and also
// defined a package with no required fields and nothing in it as EMPTY, which
// made the workaround non-conforming. So the package is reported for what it is.
TEST(SccgReviewPreparationTest, ReportsEvidenceBasisAsNotImplementedAndSilencesNothing) {
    const parser::AssuranceCase assurance_case = PaperAInitialFragment();
    const core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);
    const review::AiReviewCaseContext case_context;

    for (const char* element_id : {"E1", "G1"}) {
        SCOPED_TRACE(element_id);
        const review::SccgReviewPreparation preparation =
            review::PrepareSccgReview(&assurance_case, tree, element_id, {}, case_context, &Catalog());
        ASSERT_TRUE(preparation.ok()) << preparation.error_message;

        for (const review::AiReviewDataPackage& package : preparation.data_packages.available)
            EXPECT_NE(package.id, "EVIDENCE_BASIS") << "an empty package must not be reported available";

        const review::AiReviewUnavailableDataPackage* basis =
            FindUnavailable(preparation.data_packages, "EVIDENCE_BASIS");
        ASSERT_NE(basis, nullptr);
        EXPECT_EQ(basis->absence, review::DataPackageAbsence::NotImplemented);
        EXPECT_TRUE(basis->unassessable_guideline_ids.empty());
        EXPECT_TRUE(basis->when_absent_statement.empty());
    }
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

namespace {

std::vector<std::string> GuidelineIdsIn(const std::string& guidelines_json) {
    std::vector<std::string> ids;
    const nlohmann::json parsed = nlohmann::json::parse(guidelines_json, nullptr, false);
    for (const nlohmann::json& guideline : parsed)
        ids.push_back(guideline.value("id", std::string{}));
    return ids;
}

std::vector<std::string> Sorted(std::vector<std::string> values) {
    std::sort(values.begin(), values.end());
    return values;
}

} // namespace

// A provider caches a prompt by prefix, so what reviews share has to come
// first and be byte-identical: the first segment across every review, the
// second across every element reviewed in the same profile and pass. Anything
// of the element's leaking into those would silently turn every cache read into
// a write -- nothing would fail, the reviews would only cost more.
TEST(SccgReviewPreparationTest, OrdersThePromptFromMostSharedToLeastForCaching) {
    const parser::AssuranceCase assurance_case = PaperAInitialFragment();
    const core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);
    const review::AiReviewCaseContext case_context;

    const review::SccgReviewPreparation g1 =
        review::PrepareSccgReview(&assurance_case, tree, "G1", {}, case_context, &Catalog());
    const review::SccgReviewPreparation g2 =
        review::PrepareSccgReview(&assurance_case, tree, "G2", {}, case_context, &Catalog());
    const review::SccgReviewPreparation e1 =
        review::PrepareSccgReview(&assurance_case, tree, "E1", {}, case_context, &Catalog());
    ASSERT_TRUE(g1.ok() && g2.ok() && e1.ok());
    ASSERT_EQ(g1.passes.size(), g2.passes.size());
    ASSERT_GT(g1.passes.size(), 1u);

    const std::string& shared = g1.passes.front().request.promptSegments.at(0);
    for (const review::SccgReviewPreparation* preparation : {&g1, &g2, &e1}) {
        for (const review::SccgReviewPassRequest& pass : preparation->passes) {
            SCOPED_TRACE(preparation->element_id + " " + pass.pass_id);
            const std::vector<std::string>& segments = pass.request.promptSegments;
            ASSERT_EQ(segments.size(), 3u);
            EXPECT_EQ(segments[0] + segments[1] + segments[2], pass.request.prompt);
            EXPECT_EQ(segments[0], shared) << "every review, of every profile and pass, shares the first segment";
            EXPECT_NE(segments[0].find("## Required JSON response"), std::string::npos)
                << "the response contract is the same for every review, so it is cached with the instructions";
            EXPECT_NE(segments[1].find("## SCCG rules"), std::string::npos);
            EXPECT_NE(segments[2].find("## Selected element"), std::string::npos);
            // The elements' text, not their ids: the response contract's own
            // example references an element as {"id": "G1"}.
            for (const char* element_text :
                 {"The kitchen blender is safe.", "Blade contact hazards are controlled.", "BL-TR-003 rev. A"}) {
                EXPECT_EQ(segments[0].find(element_text), std::string::npos) << element_text;
                EXPECT_EQ(segments[1].find(element_text), std::string::npos) << element_text;
            }
        }
    }
    for (std::size_t index = 0; index < g1.passes.size(); ++index) {
        SCOPED_TRACE(g1.passes[index].pass_id);
        EXPECT_EQ(g1.passes[index].request.promptSegments[1], g2.passes[index].request.promptSegments[1])
            << "two claims reviewed in the same pass share the pass's rules";
        EXPECT_EQ(g1.passes[index].request.promptCacheKey, g2.passes[index].request.promptCacheKey);
        EXPECT_NE(g1.passes[index].request.promptSegments[2], g2.passes[index].request.promptSegments[2]);
    }
    EXPECT_NE(g1.passes[0].request.promptSegments[1], g1.passes[1].request.promptSegments[1]);
    EXPECT_NE(g1.passes[0].request.promptCacheKey, g1.passes[1].request.promptCacheKey);
    EXPECT_EQ(g1.passes[0].request.promptCacheKey,
              "sccg-" + Catalog().document.sccg_version + "-claim_review-" + g1.passes[0].pass_id);
}

// SCCG 0.8.0 partitions claim_review into four review passes and lets a tool
// send one request per pass. Each pass request must carry exactly that pass's
// guidelines -- a pass that also carried its neighbours' would reintroduce the
// co-presence that made reviews cite the wrong guideline -- and the passes
// together must carry the whole profile.
TEST(SccgReviewPreparationTest, SendsClaimReviewAsItsPublishedPasses) {
    const parser::AssuranceCase assurance_case = PaperAInitialFragment();
    const core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);
    const review::AiReviewCaseContext case_context;

    const review::SccgReviewPreparation claim =
        review::PrepareSccgReview(&assurance_case, tree, "G1", {}, case_context, &Catalog());
    ASSERT_TRUE(claim.ok()) << claim.error_message;
    const parser::ReviewProfile* profile = Catalog().document.FindReviewProfileById("claim_review");
    ASSERT_NE(profile, nullptr);
    ASSERT_EQ(claim.passes.size(), profile->review_passes.size());
    ASSERT_GT(claim.passes.size(), 1u);

    std::vector<std::string> covered;
    for (std::size_t index = 0; index < claim.passes.size(); ++index) {
        const review::SccgReviewPassRequest& pass = claim.passes[index];
        SCOPED_TRACE(pass.pass_id);
        EXPECT_EQ(pass.pass_id, profile->review_passes[index].id);
        EXPECT_EQ(Sorted(GuidelineIdsIn(pass.request.guidelinesJson)),
                  Sorted(profile->review_passes[index].guideline_ids))
            << "a pass request carries its own guidelines and no others";
        EXPECT_NE(pass.request.prompt.find("this_request_is_pass"), std::string::npos);
        EXPECT_NE(pass.request.prompt.find(profile->review_passes[index].question), std::string::npos);
        // SCCG's own pass instruction, verbatim, with this pass's question in
        // place of the placeholder -- so every tool frames a pass the same way.
        std::string instruction = Catalog().document.review_pass_instruction;
        ASSERT_NE(instruction.find("{question}"), std::string::npos);
        instruction.replace(instruction.find("{question}"), 10, profile->review_passes[index].question);
        EXPECT_NE(pass.request.prompt.find(instruction), std::string::npos) << "SCCG's pass instruction, verbatim";
        EXPECT_EQ(pass.request.prompt.find("{question}"), std::string::npos) << "no placeholder may reach the model";
        // The pass changes which rules are asked about, never what the review
        // is shown.
        EXPECT_EQ(pass.request.availableDataPackagesJson, claim.request.availableDataPackagesJson);
        covered.insert(covered.end(), pass.guideline_ids.begin(), pass.guideline_ids.end());
    }
    EXPECT_EQ(Sorted(covered), Sorted(claim.guideline_ids));

    // A profile that publishes no passes is one request over the whole profile.
    const review::SccgReviewPreparation evidence =
        review::PrepareSccgReview(&assurance_case, tree, "E1", {}, case_context, &Catalog());
    ASSERT_TRUE(evidence.ok()) << evidence.error_message;
    ASSERT_EQ(evidence.passes.size(), 1u);
    EXPECT_TRUE(evidence.passes.front().pass_id.empty());
    EXPECT_EQ(evidence.passes.front().guideline_ids, evidence.guideline_ids);
    EXPECT_EQ(evidence.passes.front().request.prompt, evidence.request.prompt);
}

// SCCG 0.8.0 judges the availability of a package with no required fields by
// whether its PUBLISHED fields are populated, which makes field names
// load-bearing. Every package the collector builds must therefore carry the
// fields the registry names: all of its required fields, and -- for a package
// with none -- at least one of its optional fields. This is the check that
// would have caught USER_REVIEW_INTENT sent as `intent` and CHANGE_HISTORY as
// `review_items`, neither of which the catalogue names.
TEST(SccgReviewPreparationTest, EveryPackageTheCollectorBuildsCarriesItsPublishedFields) {
    parser::AssuranceCase assurance_case = PaperAInitialFragment();
    assurance_case.elements.push_back(MakeElement("C1", "claim", {}, "Intended use: domestic kitchen."));
    assurance_case.elements.push_back(MakeRelationship("RC1", "assertedcontext", {"C1"}, {"G2"}));
    parser::SacmElement term = MakeElement("T1", "term", "Seated", "Seated");
    term.description = "The jar is locked onto the base.";
    assurance_case.elements.push_back(term);
    const core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);

    review::AiReviewCaseContext case_context;
    case_context.user_review_intent = "Is the interlock evidence enough?";
    core::reviews::ReviewItem ai_finding;
    ai_finding.id = "prior-ai";
    ai_finding.element_id = "G2";
    ai_finding.title = "Earlier finding";
    ai_finding.source = core::reviews::ReviewItemSource::AIReview;
    ai_finding.guideline_ids = {"AR.6"};
    core::reviews::ReviewItem comment = ai_finding;
    comment.id = "prior-manual";
    comment.source = core::reviews::ReviewItemSource::Manual;
    case_context.review_items = {ai_finding, comment};

    std::size_t checked = 0;
    for (const char* element_id : {"G2", "E1", "A1", "G1"}) {
        SCOPED_TRACE(element_id);
        const review::SccgReviewPreparation preparation =
            review::PrepareSccgReview(&assurance_case, tree, element_id, {}, case_context, &Catalog());
        ASSERT_TRUE(preparation.ok()) << preparation.error_message;
        for (const review::AiReviewDataPackage& package : preparation.data_packages.available) {
            SCOPED_TRACE(package.id);
            const parser::DataPackage* definition = Catalog().document.FindDataPackageById(package.id);
            ASSERT_NE(definition, nullptr) << "a package the catalogue does not define";
            const nlohmann::json data = nlohmann::json::parse(package.json, nullptr, false);
            ASSERT_TRUE(data.is_object());
            for (const std::string& field : definition->required_fields)
                EXPECT_TRUE(data.contains(field)) << "missing required field " << field;
            if (definition->required_fields.empty()) {
                bool any_published = false;
                for (const std::string& field : definition->optional_fields)
                    any_published = any_published || data.contains(field);
                EXPECT_TRUE(any_published) << "carries none of its published fields";
            }
            ++checked;
        }
    }
    // The case was built to fill the packages with fields worth checking; a run
    // that checked nothing would prove nothing.
    EXPECT_GT(checked, 15u);
}

// Change history goes out under SCCG's published names -- what an AI review
// raised as prior_findings, what a person wrote as review_comments -- and a
// finding recorded against a guideline SCCG has since retired keeps its id, with
// the redirect beside it. Without the redirect a review reads a prior AR.3
// finding as a citation of a rule it was never given.
TEST(SccgReviewPreparationTest, FilesChangeHistoryUnderPublishedFieldsAndRedirectsRetiredIds) {
    const parser::AssuranceCase assurance_case = PaperAInitialFragment();
    const core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);

    review::AiReviewCaseContext case_context;
    core::reviews::ReviewItem retired_finding;
    retired_finding.id = "old";
    retired_finding.element_id = "G1";
    retired_finding.title = "Context used carelessly";
    retired_finding.source = core::reviews::ReviewItemSource::AIReview;
    retired_finding.guideline_ids = {"AR.3"};
    core::reviews::ReviewItem manual = retired_finding;
    manual.id = "manual";
    manual.source = core::reviews::ReviewItemSource::Manual;
    manual.guideline_ids = {"CL.1"};
    case_context.review_items = {retired_finding, manual};

    const review::SccgReviewPreparation preparation =
        review::PrepareSccgReview(&assurance_case, tree, "G1", {}, case_context, &Catalog());
    ASSERT_TRUE(preparation.ok()) << preparation.error_message;

    const review::AiReviewDataPackage* history = nullptr;
    for (const review::AiReviewDataPackage& package : preparation.data_packages.available) {
        if (package.id == "CHANGE_HISTORY")
            history = &package;
    }
    ASSERT_NE(history, nullptr);
    const nlohmann::json data = nlohmann::json::parse(history->json);
    ASSERT_TRUE(data.contains("prior_findings"));
    ASSERT_TRUE(data.contains("review_comments"));
    EXPECT_FALSE(data.contains("review_items"));

    const nlohmann::json& prior = data["prior_findings"].at(0);
    EXPECT_EQ(prior["guideline_ids"], nlohmann::json::array({"AR.3"})) << "a recorded id is kept as recorded";
    ASSERT_TRUE(prior.contains("retired_guidelines"));
    EXPECT_EQ(prior["retired_guidelines"].at(0)["id"], "AR.3");
    EXPECT_EQ(prior["retired_guidelines"].at(0)["replaced_by"], nlohmann::json::array({"AR.7", "AR.6"}));
    EXPECT_FALSE(data["review_comments"].at(0).contains("retired_guidelines"));
}

// The disambiguation and the prescribed repair SCCG publishes per guideline are
// what the model decides with, so they have to be in the request. The repair
// guidance used to be a hand-kept list naming a dozen guidelines, three of them
// since retired; it is now each guideline's own tool.repair.
TEST(SccgReviewPreparationTest, CarriesDisambiguationAndRepairIntoTheRequest) {
    const parser::AssuranceCase assurance_case = PaperAInitialFragment();
    const core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);
    const review::AiReviewCaseContext case_context;

    const review::SccgReviewPreparation preparation =
        review::PrepareSccgReview(&assurance_case, tree, "G1", {}, case_context, &Catalog());
    ASSERT_TRUE(preparation.ok()) << preparation.error_message;

    const parser::Guideline* cl4 = Catalog().document.FindGuidelineById("CL.4");
    ASSERT_NE(cl4, nullptr);
    ASSERT_FALSE(cl4->distinguish_from.empty());
    const review::SccgReviewPassRequest* wording = nullptr;
    for (const review::SccgReviewPassRequest& pass : preparation.passes) {
        if (std::find(pass.guideline_ids.begin(), pass.guideline_ids.end(), "CL.4") != pass.guideline_ids.end())
            wording = &pass;
    }
    ASSERT_NE(wording, nullptr);
    const nlohmann::json guidelines = nlohmann::json::parse(wording->request.guidelinesJson);
    const auto cl4_json = std::find_if(guidelines.begin(), guidelines.end(), [](const nlohmann::json& guideline) {
        return guideline.value("id", std::string{}) == "CL.4";
    });
    ASSERT_NE(cl4_json, guidelines.end());
    ASSERT_EQ((*cl4_json)["distinguish_from"].size(), cl4->distinguish_from.size());
    EXPECT_EQ((*cl4_json)["distinguish_from"].at(0)["note"], cl4->distinguish_from.front().note);
    EXPECT_FALSE((*cl4_json)["tool"]["repair"].empty());

    // No retired guideline is named anywhere in what is sent.
    for (const review::SccgReviewPassRequest& pass : preparation.passes) {
        for (const parser::RetiredGuideline& retired : Catalog().document.retired_guidelines) {
            const std::string quoted = "\"" + retired.id + "\"";
            EXPECT_EQ(pass.request.prompt.find(quoted), std::string::npos) << pass.pass_id << " names " << retired.id;
            EXPECT_EQ(pass.request.prompt.find(retired.id + ","), std::string::npos) << pass.pass_id;
        }
    }
}

// SCCG 0.9.0: "the same rule applies to every package, whether or not it has
// required fields" -- a package is available only when one of its published
// fields is populated. 0.8.0 stated it for packages with no required fields
// alone, which left CHILDREN sent as {"child_elements": []} counted available;
// under 0.9.0 it is empty, and an empty package is a fact the review may rely
// on (a claim with no children has no path to evidence).
TEST(SccgReviewPreparationTest, AppliesTheAvailabilityRuleToEveryPackage) {
    const parser::ReviewProfile* claim = Catalog().document.FindReviewProfileById("claim_review");
    ASSERT_NE(claim, nullptr);

    review::AiReviewDataPackageBundle packages;
    packages.available.push_back({"CHILDREN", R"({"child_elements": []})"});
    packages.available.push_back(
        {"DIRECT_CONTEXT", R"({"context_elements": [], "assumptions": [], "justifications": []})"});
    packages.available.push_back({"PARENT", R"({"element_id": "G0", "element_type": "claim", "text": "Top."})"});
    packages.available.push_back({"CHANGE_HISTORY", R"({"prior_findings": null, "review_comments": ""})"});
    packages.available.push_back({"PROJECT_GLOSSARY", R"({"terms": [{"term": "safe"}]})"});

    review::ApplyContentDefinedAvailability(packages, Catalog().document, claim);

    std::vector<std::string> available;
    for (const review::AiReviewDataPackage& package : packages.available)
        available.push_back(package.id);
    EXPECT_EQ(Sorted(available), (std::vector<std::string>{"PARENT", "PROJECT_GLOSSARY"}));

    for (const char* id : {"CHILDREN", "DIRECT_CONTEXT", "CHANGE_HISTORY"}) {
        SCOPED_TRACE(id);
        const review::AiReviewUnavailableDataPackage* package = FindUnavailable(packages, id);
        ASSERT_NE(package, nullptr) << "a package with its fields all empty is not available";
        EXPECT_EQ(package->absence, review::DataPackageAbsence::Empty);
    }
    // Required-ness follows the profile: claim_review requires CHILDREN and
    // DIRECT_CONTEXT, and takes CHANGE_HISTORY as optional.
    EXPECT_TRUE(FindUnavailable(packages, "CHILDREN")->required);
    EXPECT_TRUE(FindUnavailable(packages, "DIRECT_CONTEXT")->required);
    EXPECT_FALSE(FindUnavailable(packages, "CHANGE_HISTORY")->required);
}
