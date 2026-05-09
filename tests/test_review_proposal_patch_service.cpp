#include "core/reviews/review_proposal_patch_service.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <optional>
#include <string>

namespace {

parser::SacmElement Element(std::string id, std::string type, std::string name) {
    parser::SacmElement element;
    element.id = std::move(id);
    element.type = std::move(type);
    element.name = std::move(name);
    return element;
}

parser::AssuranceCase MakeModel() {
    parser::AssuranceCase model;
    model.id = "case-1";
    model.name = "Patch Service Case";

    parser::SacmElement root = Element("G1", "claim", "Top Goal");
    root.content = "Original content";
    model.elements.push_back(root);

    parser::SacmElement child = Element("G2", "claim", "Existing Child");
    child.content = "Existing child content";
    model.elements.push_back(child);

    parser::SacmElement inference;
    inference.id = "R1";
    inference.type = "assertedinference";
    inference.target_refs.push_back("G1");
    inference.source_refs.push_back("G2");
    model.elements.push_back(inference);

    return model;
}

const parser::SacmElement* FindElement(const parser::AssuranceCase& model, const std::string& id) {
    for (const parser::SacmElement& element : model.elements) {
        if (element.id == id)
            return &element;
    }
    return nullptr;
}

bool HasInferenceSourceTarget(const parser::AssuranceCase& model,
                              const std::string& source_id,
                              const std::string& target_id) {
    for (const parser::SacmElement& element : model.elements) {
        if (element.type != "assertedinference")
            continue;
        const bool has_source =
            std::find(element.source_refs.begin(), element.source_refs.end(), source_id) != element.source_refs.end();
        const bool has_target =
            std::find(element.target_refs.begin(), element.target_refs.end(), target_id) != element.target_refs.end();
        if (has_source && has_target)
            return true;
    }
    return false;
}

bool HasInferenceReasoningTarget(const parser::AssuranceCase& model,
                                 const std::string& reasoning_id,
                                 const std::string& target_id) {
    for (const parser::SacmElement& element : model.elements) {
        if (element.type != "assertedinference")
            continue;
        const bool has_target =
            std::find(element.target_refs.begin(), element.target_refs.end(), target_id) != element.target_refs.end();
        if (element.reasoning_ref == reasoning_id && has_target)
            return true;
    }
    return false;
}

bool HasEvidenceSourceTarget(const parser::AssuranceCase& model,
                             const std::string& source_id,
                             const std::string& target_id) {
    for (const parser::SacmElement& element : model.elements) {
        if (element.type != "assertedevidence")
            continue;
        const bool has_source =
            std::find(element.source_refs.begin(), element.source_refs.end(), source_id) != element.source_refs.end();
        const bool has_target =
            std::find(element.target_refs.begin(), element.target_refs.end(), target_id) != element.target_refs.end();
        if (has_source && has_target)
            return true;
    }
    return false;
}

core::reviews::ReviewProposal ProposalFor(const parser::AssuranceCase& model) {
    core::reviews::ReviewProposal proposal;
    proposal.id = "proposal-1";
    proposal.anchor_element_id = "G1";
    proposal.affected_existing_element_ids = {"G1"};
    proposal.base_model_hash = core::reviews::ComputeModelSemanticHash(model);
    proposal.base_element_hashes["G1"] = core::reviews::ComputeElementSemanticHash(*FindElement(model, "G1"));
    return proposal;
}

core::reviews::PatchOperation Create(core::reviews::PatchOperationType type, std::string create_ref, std::string text) {
    core::reviews::PatchOperation operation;
    operation.type = type;
    operation.create_ref = std::move(create_ref);
    operation.text = std::move(text);
    return operation;
}

core::reviews::PatchOperation AddSupportedBy(core::reviews::ElementRef source, core::reviews::ElementRef target) {
    core::reviews::PatchOperation operation;
    operation.type = core::reviews::PatchOperationType::AddSupportedBy;
    operation.source = std::move(source);
    operation.target = std::move(target);
    return operation;
}

} // namespace

TEST(ReviewProposalPatchServiceTest, AppliesUpdateElementText) {
    parser::AssuranceCase model = MakeModel();
    core::reviews::ReviewProposal proposal = ProposalFor(model);

    core::reviews::PatchOperation update;
    update.type = core::reviews::PatchOperationType::UpdateElementText;
    update.element = core::reviews::ElementRef{"G1", std::nullopt};
    update.field = "content";
    update.new_value = "Updated claim content";
    proposal.operations.push_back(update);

    core::reviews::ReviewProposalPatchService service;
    core::reviews::ApplyProposalResult result = service.ApplyProposal(proposal, model);

    ASSERT_TRUE(result.success) << result.error;
    ASSERT_NE(FindElement(model, "G1"), nullptr);
    EXPECT_EQ(FindElement(model, "G1")->content, "Updated claim content");
}

TEST(ReviewProposalPatchServiceTest, BuildsPreviewWithExistingElementEditWithoutMutatingCurrentModel) {
    parser::AssuranceCase model = MakeModel();
    core::reviews::ReviewProposal proposal = ProposalFor(model);

    core::reviews::PatchOperation update;
    update.type = core::reviews::PatchOperationType::UpdateElementText;
    update.element = core::reviews::ElementRef{"G1", std::nullopt};
    update.field = "content";
    update.old_value = "Original content";
    update.new_value = "Proposed content";
    proposal.operations.push_back(update);

    core::reviews::ReviewProposalPatchService service;
    core::reviews::ProposalPreviewResult preview = service.BuildPreviewModel(proposal, model);

    ASSERT_TRUE(preview.success) << preview.error;
    EXPECT_EQ(FindElement(model, "G1")->content, "Original content");
    ASSERT_NE(FindElement(preview.preview_model, "G1"), nullptr);
    EXPECT_EQ(FindElement(preview.preview_model, "G1")->content, "Proposed content");
}

TEST(ReviewProposalPatchServiceTest, BuildsPreviewWithExistingElementRemovalWithoutMutatingCurrentModel) {
    parser::AssuranceCase model = MakeModel();
    parser::SacmElement grandchild = Element("G3", "claim", "Grandchild");
    grandchild.content = "Grandchild content";
    model.elements.push_back(grandchild);

    parser::SacmElement child_inference;
    child_inference.id = "R2";
    child_inference.type = "assertedinference";
    child_inference.target_refs.push_back("G2");
    child_inference.source_refs.push_back("G3");
    model.elements.push_back(child_inference);

    core::reviews::ReviewProposal proposal = ProposalFor(model);
    proposal.affected_existing_element_ids.push_back("G2");
    proposal.base_element_hashes["G2"] = core::reviews::ComputeElementSemanticHash(*FindElement(model, "G2"));

    core::reviews::PatchOperation remove;
    remove.type = core::reviews::PatchOperationType::RemoveElement;
    remove.element = core::reviews::ElementRef{"G2", std::nullopt};
    remove.field = "node_only";
    proposal.operations.push_back(remove);

    core::reviews::ReviewProposalPatchService service;
    core::reviews::ProposalPreviewResult preview = service.BuildPreviewModel(proposal, model);

    ASSERT_TRUE(preview.success) << preview.error;
    EXPECT_NE(FindElement(model, "G2"), nullptr);
    EXPECT_TRUE(HasInferenceSourceTarget(model, "G2", "G1"));
    EXPECT_EQ(FindElement(preview.preview_model, "G2"), nullptr);
    EXPECT_TRUE(HasInferenceSourceTarget(preview.preview_model, "G3", "G1"));
}

TEST(ReviewProposalPatchServiceTest, BuildsPreviewWithoutMutatingCurrentModel) {
    parser::AssuranceCase model = MakeModel();
    core::reviews::ReviewProposal proposal = ProposalFor(model);
    proposal.operations.push_back(
        Create(core::reviews::PatchOperationType::CreateClaim, "$new_claim_1", "Generated claim content"));
    proposal.operations.push_back(AddSupportedBy(core::reviews::ElementRef{std::nullopt, "$new_claim_1"},
                                                 core::reviews::ElementRef{"G1", std::nullopt}));

    core::reviews::ReviewProposalPatchService service;
    core::reviews::ProposalPreviewResult preview = service.BuildPreviewModel(proposal, model);

    ASSERT_TRUE(preview.success) << preview.error;
    ASSERT_TRUE(preview.generated_ids.count("$new_claim_1") > 0);
    const std::string generated_id = preview.generated_ids["$new_claim_1"];
    EXPECT_EQ(FindElement(model, generated_id), nullptr);
    ASSERT_NE(FindElement(preview.preview_model, generated_id), nullptr);
    EXPECT_EQ(FindElement(preview.preview_model, generated_id)->content, "Generated claim content");
    EXPECT_TRUE(HasInferenceSourceTarget(preview.preview_model, generated_id, "G1"));
}

TEST(ReviewProposalPatchServiceTest, CreatesSolutionAsEvidenceRelationship) {
    parser::AssuranceCase model = MakeModel();
    core::reviews::ReviewProposal proposal = ProposalFor(model);
    proposal.operations.push_back(
        Create(core::reviews::PatchOperationType::CreateSolution, "$new_solution_1", "Verification evidence"));
    proposal.operations.push_back(AddSupportedBy(core::reviews::ElementRef{std::nullopt, "$new_solution_1"},
                                                 core::reviews::ElementRef{"G1", std::nullopt}));

    core::reviews::ReviewProposalPatchService service;
    core::reviews::ProposalPreviewResult preview = service.BuildPreviewModel(proposal, model);

    ASSERT_TRUE(preview.success) << preview.error;
    const std::string generated_id = preview.generated_ids.at("$new_solution_1");
    ASSERT_NE(FindElement(preview.preview_model, generated_id), nullptr);
    EXPECT_EQ(FindElement(preview.preview_model, generated_id)->type, "artifactreference");
    EXPECT_TRUE(HasEvidenceSourceTarget(preview.preview_model, generated_id, "G1"));
    EXPECT_FALSE(HasInferenceSourceTarget(preview.preview_model, generated_id, "G1"));
}

TEST(ReviewProposalPatchServiceTest, CreatesStrategyAndClaimsWithNonCollidingIds) {
    parser::AssuranceCase model = MakeModel();
    model.elements.push_back(Element("G3", "claim", "Existing G3"));
    model.elements.push_back(Element("S1", "argumentreasoning", "Existing S1"));
    core::reviews::ReviewProposal proposal = ProposalFor(model);
    proposal.operations.push_back(
        Create(core::reviews::PatchOperationType::CreateStrategy, "$new_strategy_1", "Strategy content"));
    proposal.operations.push_back(Create(core::reviews::PatchOperationType::CreateClaim, "$new_claim_1", "Claim one"));
    proposal.operations.push_back(Create(core::reviews::PatchOperationType::CreateClaim, "$new_claim_2", "Claim two"));
    proposal.operations.push_back(AddSupportedBy(core::reviews::ElementRef{std::nullopt, "$new_strategy_1"},
                                                 core::reviews::ElementRef{"G1", std::nullopt}));
    proposal.operations.push_back(AddSupportedBy(core::reviews::ElementRef{std::nullopt, "$new_claim_1"},
                                                 core::reviews::ElementRef{std::nullopt, "$new_strategy_1"}));
    proposal.operations.push_back(AddSupportedBy(core::reviews::ElementRef{std::nullopt, "$new_claim_2"},
                                                 core::reviews::ElementRef{std::nullopt, "$new_strategy_1"}));

    core::reviews::ReviewProposalPatchService service;
    core::reviews::ApplyProposalResult result = service.ApplyProposal(proposal, model);

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.generated_ids.at("$new_strategy_1"), "S2");
    EXPECT_EQ(result.generated_ids.at("$new_claim_1"), "G4");
    EXPECT_EQ(result.generated_ids.at("$new_claim_2"), "G5");
    EXPECT_NE(FindElement(model, "S2"), nullptr);
    EXPECT_NE(FindElement(model, "G4"), nullptr);
    EXPECT_NE(FindElement(model, "G5"), nullptr);
    EXPECT_TRUE(HasInferenceReasoningTarget(model, "S2", "G1"));
    EXPECT_TRUE(HasInferenceSourceTarget(model, "G4", "S2"));
    EXPECT_TRUE(HasInferenceSourceTarget(model, "G5", "S2"));
}

TEST(ReviewProposalPatchServiceTest, AddsAndRemovesRelationships) {
    parser::AssuranceCase model = MakeModel();
    core::reviews::ReviewProposal proposal = ProposalFor(model);

    core::reviews::PatchOperation remove;
    remove.type = core::reviews::PatchOperationType::RemoveSupportedBy;
    remove.source = core::reviews::ElementRef{"G2", std::nullopt};
    remove.target = core::reviews::ElementRef{"G1", std::nullopt};
    proposal.operations.push_back(remove);

    proposal.operations.push_back(
        Create(core::reviews::PatchOperationType::CreateContext, "$new_context_1", "Operational context"));
    core::reviews::PatchOperation add_context;
    add_context.type = core::reviews::PatchOperationType::AddInContextOf;
    add_context.source = core::reviews::ElementRef{std::nullopt, "$new_context_1"};
    add_context.target = core::reviews::ElementRef{"G1", std::nullopt};
    proposal.operations.push_back(add_context);

    core::reviews::ReviewProposalPatchService service;
    core::reviews::ApplyProposalResult result = service.ApplyProposal(proposal, model);

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_FALSE(HasInferenceSourceTarget(model, "G2", "G1"));
    const std::string context_id = result.generated_ids.at("$new_context_1");
    const parser::SacmElement* context = FindElement(model, context_id);
    ASSERT_NE(context, nullptr);
    EXPECT_EQ(context->type, "artifactreference");
    EXPECT_EQ(context->description, "Operational context");
    bool has_context = false;
    for (const parser::SacmElement& element : model.elements) {
        if (element.type != "assertedcontext")
            continue;
        const bool has_source =
            std::find(element.source_refs.begin(), element.source_refs.end(), context_id) != element.source_refs.end();
        const bool has_target =
            std::find(element.target_refs.begin(), element.target_refs.end(), "G1") != element.target_refs.end();
        has_context = has_context || (has_source && has_target);
    }
    EXPECT_TRUE(has_context);
}

TEST(ReviewProposalPatchServiceTest, FailedApplyLeavesModelUnchanged) {
    parser::AssuranceCase model = MakeModel();
    const std::string before_hash = core::reviews::ComputeModelSemanticHash(model);
    core::reviews::ReviewProposal proposal = ProposalFor(model);

    core::reviews::PatchOperation update;
    update.type = core::reviews::PatchOperationType::UpdateElementText;
    update.element = core::reviews::ElementRef{"DOES_NOT_EXIST", std::nullopt};
    update.field = "content";
    update.new_value = "This should not apply";
    proposal.operations.push_back(update);

    core::reviews::ReviewProposalPatchService service;
    core::reviews::ApplyProposalResult result = service.ApplyProposal(proposal, model);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
    EXPECT_EQ(core::reviews::ComputeModelSemanticHash(model), before_hash);
    EXPECT_EQ(FindElement(model, "G1")->content, "Original content");
}
