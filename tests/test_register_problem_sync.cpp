// AF-ENG-016: an assessment whose subject left the argument is kept, and must
// also be visible. Kept-but-invisible is the failure mode these tests guard:
// core::registers refuses to delete a reviewer's assessment when an id moves, so
// unless something surfaces it, the reviewer never learns it went stale.

#include "app/register_problem_sync.h"
#include "core/problems/problems_manager.h"
#include "core/registers/register_model.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {

parser::SacmElement Element(std::string id, std::string type, std::string name = {}) {
    parser::SacmElement element;
    element.id = std::move(id);
    element.type = std::move(type);
    element.name = std::move(name);
    return element;
}

parser::SacmElement EvidenceLink(std::string id, std::vector<std::string> sources, std::vector<std::string> targets) {
    parser::SacmElement element;
    element.id = std::move(id);
    element.type = "assertedevidence";
    element.source_refs = std::move(sources);
    element.target_refs = std::move(targets);
    return element;
}

// G1 rests on Sn1, and nothing else.
parser::AssuranceCase OneClaimOnOneEvidence() {
    parser::AssuranceCase model;
    model.elements = {Element("G1", "claim", "Top goal"),
                      Element("Sn1", "artifactreference", "Test report"),
                      EvidenceLink("R1", {"Sn1"}, {"G1"})};
    return model;
}

core::registers::RegisterStore StoreWithCseAssessment(const std::string& cse_id) {
    core::registers::RegisterStore store;
    core::registers::CseMetadata& metadata = store.cse[cse_id];
    metadata.claim_owner = "Alice";
    metadata.assessment_status = "Insufficiently Supported";
    metadata.notes = "Needs a second test run.";
    return store;
}

std::vector<core::ProblemItem> RegisterProblems(const core::ProblemsManager& problems_manager) {
    std::vector<core::ProblemItem> found;
    for (const core::ProblemItem& problem : problems_manager.GetProblems()) {
        if (problem.type == app::kRegisterAssessmentOrphanedProblemType)
            found.push_back(problem);
    }
    return found;
}

} // namespace

TEST(RegisterProblemSyncTest, OrphanedCseAssessmentIsReportedWithWhatItHolds) {
    const parser::AssuranceCase model = OneClaimOnOneEvidence();
    // An assessment about a pairing the argument does not contain: the link was
    // removed, or one endpoint's id changed under it.
    const std::string orphan_id = core::registers::MakeCseId("G1", "Sn-deleted");
    const core::registers::RegisterStore store = StoreWithCseAssessment(orphan_id);

    core::ProblemsManager problems_manager;
    app::SyncRegisterProblems(problems_manager, &model, &store);

    const std::vector<core::ProblemItem> problems = RegisterProblems(problems_manager);
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_EQ(problems[0].severity, core::ProblemSeverity::Warning);
    EXPECT_NE(problems[0].message.find(orphan_id), std::string::npos) << problems[0].message;
    // The content travels with the problem; there is no table left that shows it.
    EXPECT_NE(problems[0].message.find("Alice"), std::string::npos) << problems[0].message;
    EXPECT_NE(problems[0].message.find("Needs a second test run."), std::string::npos) << problems[0].message;
    EXPECT_FALSE(problems[0].quick_fix_label.empty());

    app::RegisterAssessmentRef ref;
    ASSERT_TRUE(app::DecodeRegisterAssessmentPayload(problems[0].quick_fix_payload, ref));
    EXPECT_EQ(ref.kind, app::RegisterAssessmentKind::Cse);
    EXPECT_EQ(ref.key, orphan_id);
}

TEST(RegisterProblemSyncTest, AssessmentOfALiveLinkIsNotReported) {
    const parser::AssuranceCase model = OneClaimOnOneEvidence();
    const core::registers::RegisterStore store = StoreWithCseAssessment(core::registers::MakeCseId("G1", "Sn1"));

    core::ProblemsManager problems_manager;
    app::SyncRegisterProblems(problems_manager, &model, &store);

    EXPECT_TRUE(RegisterProblems(problems_manager).empty());
}

TEST(RegisterProblemSyncTest, OrphanedEvidenceAssessmentIsReported) {
    const parser::AssuranceCase model = OneClaimOnOneEvidence();
    core::registers::RegisterStore store;
    store.evidence["Sn1"].evidence_owner = "Bob";
    core::registers::EvidenceMetadata& gone = store.evidence["Sn-deleted"];
    gone.evidence_owner = "Carol";
    gone.maturity = "Draft";

    core::ProblemsManager problems_manager;
    app::SyncRegisterProblems(problems_manager, &model, &store);

    const std::vector<core::ProblemItem> problems = RegisterProblems(problems_manager);
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_NE(problems[0].message.find("Sn-deleted"), std::string::npos) << problems[0].message;
    EXPECT_NE(problems[0].message.find("Carol"), std::string::npos) << problems[0].message;

    app::RegisterAssessmentRef ref;
    ASSERT_TRUE(app::DecodeRegisterAssessmentPayload(problems[0].quick_fix_payload, ref));
    EXPECT_EQ(ref.kind, app::RegisterAssessmentKind::Evidence);
    EXPECT_EQ(ref.key, "Sn-deleted");
}

TEST(RegisterProblemSyncTest, RestoringTheLinkClearsTheWarning) {
    const std::string cse_id = core::registers::MakeCseId("G1", "Sn1");
    const core::registers::RegisterStore store = StoreWithCseAssessment(cse_id);

    // The pairing is missing, then present again — as when an edit is undone.
    parser::AssuranceCase without_link = OneClaimOnOneEvidence();
    without_link.elements.erase(without_link.elements.begin() + 2);

    core::ProblemsManager problems_manager;
    app::SyncRegisterProblems(problems_manager, &without_link, &store);
    ASSERT_EQ(RegisterProblems(problems_manager).size(), 1u);

    const parser::AssuranceCase with_link = OneClaimOnOneEvidence();
    app::SyncRegisterProblems(problems_manager, &with_link, &store);
    EXPECT_TRUE(RegisterProblems(problems_manager).empty());
}

TEST(RegisterProblemSyncTest, NoModelOrNoStoreLeavesNoWarningsStanding) {
    const parser::AssuranceCase model = OneClaimOnOneEvidence();
    const core::registers::RegisterStore store = StoreWithCseAssessment(core::registers::MakeCseId("G1", "Sn-deleted"));

    core::ProblemsManager problems_manager;
    app::SyncRegisterProblems(problems_manager, &model, &store);
    ASSERT_EQ(RegisterProblems(problems_manager).size(), 1u);

    // Closing the document must not leave warnings about an argument that is no
    // longer open.
    app::SyncRegisterProblems(problems_manager, nullptr, &store);
    EXPECT_TRUE(RegisterProblems(problems_manager).empty());

    app::SyncRegisterProblems(problems_manager, &model, nullptr);
    EXPECT_TRUE(RegisterProblems(problems_manager).empty());
}

TEST(RegisterProblemSyncTest, PayloadRoundTripsKeysThatContainSeparators) {
    // A CSE key is itself "CSE:<claim>-><evidence>", so a decoder that split on
    // every ':' would hand back a truncated key and discard the wrong entry.
    const app::RegisterAssessmentRef cse{app::RegisterAssessmentKind::Cse,
                                        core::registers::MakeCseId("G1:a", "Sn1:b")};
    app::RegisterAssessmentRef decoded;
    ASSERT_TRUE(app::DecodeRegisterAssessmentPayload(app::EncodeRegisterAssessmentPayload(cse), decoded));
    EXPECT_EQ(decoded.kind, app::RegisterAssessmentKind::Cse);
    EXPECT_EQ(decoded.key, cse.key);

    const app::RegisterAssessmentRef evidence{app::RegisterAssessmentKind::Evidence, "Sn1"};
    ASSERT_TRUE(app::DecodeRegisterAssessmentPayload(app::EncodeRegisterAssessmentPayload(evidence), decoded));
    EXPECT_EQ(decoded.kind, app::RegisterAssessmentKind::Evidence);
    EXPECT_EQ(decoded.key, "Sn1");

    EXPECT_FALSE(app::DecodeRegisterAssessmentPayload("", decoded));
    EXPECT_FALSE(app::DecodeRegisterAssessmentPayload("cse:", decoded));
    EXPECT_FALSE(app::DecodeRegisterAssessmentPayload("Sn1", decoded));
    EXPECT_FALSE(app::DecodeRegisterAssessmentPayload("confidence:Sn1", decoded));
}
