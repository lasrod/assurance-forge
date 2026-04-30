#include <gtest/gtest.h>

#include "core/reviews/review_proposal.h"
#include "core/reviews/review_proposal_manager.h"

#include <chrono>
#include <filesystem>

namespace {

struct TempDir {
    std::filesystem::path path;
    explicit TempDir(std::filesystem::path p) : path(std::move(p)) {}
    ~TempDir() { std::filesystem::remove_all(path); }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

std::filesystem::path MakeTempDir() {
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path path = std::filesystem::temp_directory_path() /
                                 ("assurance_forge_review_proposal_test_" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

parser::AssuranceCase MakeCase() {
    parser::AssuranceCase model;
    model.id = "case-1";
    model.name = "Example Case";

    parser::SacmElement root;
    root.id = "G12";
    root.type = "claim";
    root.name = "Mixed claim";
    root.content = "System is safe and secure.";
    root.description = "Original description";
    model.elements.push_back(root);
    return model;
}

core::ReviewProposal MakeProposal(const parser::AssuranceCase& model) {
    core::ReviewProposal proposal;
    proposal.id = "proposal-0001";
    proposal.review_item_id = "review-0017";
    proposal.title = "Split mixed claim into two claims";
    proposal.summary = "Splits safety and cybersecurity into separate subclaims.";
    proposal.author_name = "Reviewer Name";
    proposal.created_utc = "2026-04-30T12:00:00Z";
    proposal.anchor_element_id = "G12";
    proposal.affected_existing_element_ids = {"G12"};
    proposal.base_model_hash = core::ComputeModelSemanticHash(model);
    proposal.base_element_hashes["G12"] = core::ComputeElementSemanticHash(model.elements.front());

    core::PatchOperation update;
    update.type = core::PatchOperationType::UpdateElementText;
    update.element = core::ElementRef{"G12", std::nullopt};
    update.field = "description";
    update.old_value = "Original description";
    update.new_value = "System dependability is adequately addressed.";
    proposal.operations.push_back(update);

    core::PatchOperation create_strategy;
    create_strategy.type = core::PatchOperationType::CreateStrategy;
    create_strategy.create_ref = "$new_strategy_1";
    create_strategy.text = "Argument by separate treatment of safety and cybersecurity.";
    proposal.operations.push_back(create_strategy);

    core::PatchOperation link;
    link.type = core::PatchOperationType::AddSupportedBy;
    link.source = core::ElementRef{"G12", std::nullopt};
    link.target = core::ElementRef{std::nullopt, "$new_strategy_1"};
    proposal.operations.push_back(link);

    return proposal;
}

}  // namespace

TEST(ReviewProposalTest, RoundTripsSemanticPatchJson) {
    parser::AssuranceCase model = MakeCase();
    core::ReviewProposal original = MakeProposal(model);

    std::string error;
    core::ReviewProposal parsed;
    ASSERT_TRUE(core::DeserializeReviewProposal(core::SerializeReviewProposal(original), parsed, error)) << error;

    EXPECT_EQ(parsed.schema, core::kReviewProposalSchema);
    EXPECT_EQ(parsed.id, original.id);
    EXPECT_EQ(parsed.operations.size(), 3u);
    ASSERT_TRUE(parsed.operations[1].create_ref.has_value());
    EXPECT_EQ(parsed.operations[1].create_ref.value(), "$new_strategy_1");
    ASSERT_TRUE(parsed.operations[2].target.has_value());
    ASSERT_TRUE(parsed.operations[2].target->create_ref.has_value());
    EXPECT_EQ(parsed.operations[2].target->create_ref.value(), "$new_strategy_1");
}

TEST(ReviewProposalTest, ValidatesAnchorHashesAndCreateRefs) {
    parser::AssuranceCase model = MakeCase();
    core::ReviewProposal proposal = MakeProposal(model);

    core::ProposalValidityResult result = core::EvaluateReviewProposalValidity(proposal, model);

    EXPECT_EQ(result.validity, core::ProposalValidity::Valid);
    EXPECT_TRUE(result.reason.empty());
}

TEST(ReviewProposalTest, ReportsBrokenWhenAnchorIsMissing) {
    parser::AssuranceCase model = MakeCase();
    core::ReviewProposal proposal = MakeProposal(model);
    model.elements.clear();

    core::ProposalValidityResult result = core::EvaluateReviewProposalValidity(proposal, model);

    EXPECT_EQ(result.validity, core::ProposalValidity::Broken);
    EXPECT_NE(result.reason.find("anchor"), std::string::npos);
}

TEST(ReviewProposalTest, ReportsBrokenWhenAffectedElementHashChanges) {
    parser::AssuranceCase model = MakeCase();
    core::ReviewProposal proposal = MakeProposal(model);
    model.elements.front().description = "Someone edited this first.";

    core::ProposalValidityResult result = core::EvaluateReviewProposalValidity(proposal, model);

    EXPECT_EQ(result.validity, core::ProposalValidity::Broken);
    EXPECT_NE(result.reason.find("changed"), std::string::npos);
}

TEST(ReviewProposalTest, ReportsBrokenForDuplicateCreateRefs) {
    parser::AssuranceCase model = MakeCase();
    core::ReviewProposal proposal = MakeProposal(model);

    core::PatchOperation duplicate;
    duplicate.type = core::PatchOperationType::CreateClaim;
    duplicate.create_ref = "$new_strategy_1";
    proposal.operations.push_back(duplicate);

    core::ProposalValidityResult result = core::EvaluateReviewProposalValidity(proposal, model);

    EXPECT_EQ(result.validity, core::ProposalValidity::Broken);
    EXPECT_NE(result.reason.find("Duplicate"), std::string::npos);
}

TEST(ReviewProposalManagerTest, SavesListsLoadsAndDeletesProposalFiles) {
    TempDir temp(MakeTempDir());
    parser::AssuranceCase model = MakeCase();
    core::ReviewProposal proposal = MakeProposal(model);
    core::ReviewProposalManager manager(temp.path);

    std::filesystem::path relative_path;
    std::string error;
    ASSERT_TRUE(manager.SaveProposal(proposal, &relative_path, error)) << error;
    EXPECT_EQ(relative_path.generic_string(), "reviews/proposals/proposal-0001.afpatch.json");

    std::vector<core::ReviewProposalSummary> summaries = manager.ListProposals(&model);
    ASSERT_EQ(summaries.size(), 1u);
    EXPECT_EQ(summaries[0].id, proposal.id);
    EXPECT_EQ(summaries[0].validity.validity, core::ProposalValidity::Valid);

    std::optional<core::ReviewProposal> loaded = manager.LoadProposal(proposal.id, error);
    ASSERT_TRUE(loaded.has_value()) << error;
    EXPECT_EQ(loaded->title, proposal.title);

    ASSERT_TRUE(manager.DeleteProposal(proposal.id, error)) << error;
    EXPECT_TRUE(manager.ListProposals(&model).empty());
}