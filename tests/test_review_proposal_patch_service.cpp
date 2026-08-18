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

// The two halves of one rule: an element's text lives in exactly one field,
// decided by its kind (core::ClaimLikeCarriesStatementAsDescription). Naming the
// other field is refused HERE, while the operation is being staged, because the
// promotion seam cannot write it either -- and a refusal that waits until
// promotion produces a draft the user can see, cannot accept, and cannot fix.
//
// Only the claim half was guarded. An MCP client followed the tool schema's
// "e.g. \"content\"" onto a context, staged it, watched it appear in the working
// draft, and then could never accept it: `apply_text_edit` maps Content to a
// Description only for Claim and ArgumentReasoning, so promotion failed with
// "writing text on C2 failed" and no way forward.
TEST(ReviewProposalPatchServiceTest, RefusesContentOnAnElementWhoseTextIsItsDescription) {
    parser::AssuranceCase model = MakeModel();
    parser::SacmElement context = Element("C1", "artifactreference", "System boundary");
    context.description = "The case covers the motor and the jar.";
    model.elements.push_back(context);
    core::reviews::ReviewProposal proposal = ProposalFor(model);

    core::reviews::PatchOperation update;
    update.type = core::reviews::PatchOperationType::UpdateElementText;
    update.element = core::reviews::ElementRef{"C1", std::nullopt};
    update.field = "content";
    update.new_value = "The case covers the motor, the jar and the lid.";
    proposal.operations.push_back(update);

    core::reviews::ReviewProposalPatchService service;
    core::reviews::ApplyProposalResult result = service.ApplyProposal(proposal, model);

    ASSERT_FALSE(result.success) << "staging must refuse a field the promotion seam cannot write";
    EXPECT_NE(result.error.find("description"), std::string::npos)
        << "the refusal must name the field to use instead, not only the one refused: " << result.error;
    ASSERT_NE(FindElement(model, "C1"), nullptr);
    EXPECT_TRUE(FindElement(model, "C1")->content.empty()) << "a refused operation must leave the element untouched";
}

TEST(ReviewProposalPatchServiceTest, RefusesDescriptionOnAClaimWhoseDescriptionIsItsStatement) {
    parser::AssuranceCase model = MakeModel();
    core::reviews::ReviewProposal proposal = ProposalFor(model);

    core::reviews::PatchOperation update;
    update.type = core::reviews::PatchOperationType::UpdateElementText;
    update.element = core::reviews::ElementRef{"G1", std::nullopt};
    update.field = "description";
    update.new_value = "A note beside the claim.";
    proposal.operations.push_back(update);

    core::reviews::ReviewProposalPatchService service;
    core::reviews::ApplyProposalResult result = service.ApplyProposal(proposal, model);

    ASSERT_FALSE(result.success) << "a claim carries one description and it is the statement (ADR 0012)";
    EXPECT_NE(result.error.find("content"), std::string::npos)
        << "the refusal must name the field to use instead: " << result.error;
    EXPECT_EQ(FindElement(model, "G1")->content, "Original content");
    EXPECT_TRUE(FindElement(model, "G1")->description.empty());
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

// ---------------------------------------------------------------------------
// Bilingual argument
//
// A case reviewed in two languages is only reviewable in both if the argument
// exists in both. These pin the rules that make that safe: a translation never
// displaces the primary language, translating existing argument does not
// disturb the English, and no element arrives in a translation alone.
// ---------------------------------------------------------------------------

TEST(ReviewProposalPatchServiceTest, CreatesClaimInBothLanguages) {
    parser::AssuranceCase model = MakeModel();
    core::reviews::ReviewProposal proposal = ProposalFor(model);

    core::reviews::PatchOperation create =
        Create(core::reviews::PatchOperationType::CreateClaim, "$sub", "The interlock prevents operation when open.");
    create.translations["ja"] = "インターロックにより開状態での動作を防止する。";
    proposal.operations.push_back(create);
    proposal.operations.push_back(
        AddSupportedBy(core::reviews::ElementRef{std::nullopt, "$sub"}, core::reviews::ElementRef{"G1", std::nullopt}));

    core::reviews::ReviewProposalPatchService service;
    core::reviews::ApplyProposalResult result = service.ApplyProposal(proposal, model);

    ASSERT_TRUE(result.success) << result.error;
    const parser::SacmElement* claim = FindElement(model, result.generated_ids.at("$sub"));
    ASSERT_NE(claim, nullptr);
    // The scalar and the "en" entry carry the English; the translation lives
    // beside it and never in it.
    EXPECT_EQ(claim->content, "The interlock prevents operation when open.");
    EXPECT_EQ(claim->content_langs.at("en"), "The interlock prevents operation when open.");
    EXPECT_EQ(claim->content_langs.at("ja"), "インターロックにより開状態での動作を防止する。");
}

TEST(ReviewProposalPatchServiceTest, TranslationOnlyUpdateLeavesPrimaryTextUnchanged) {
    parser::AssuranceCase model = MakeModel();
    core::reviews::ReviewProposal proposal = ProposalFor(model);

    core::reviews::PatchOperation update;
    update.type = core::reviews::PatchOperationType::UpdateElementText;
    update.element = core::reviews::ElementRef{"G1", std::nullopt};
    update.field = "content";
    update.translations["ja"] = "トップゴール。";
    proposal.operations.push_back(update);

    core::reviews::ReviewProposalPatchService service;
    core::reviews::ApplyProposalResult result = service.ApplyProposal(proposal, model);

    ASSERT_TRUE(result.success) << result.error;
    const parser::SacmElement* goal = FindElement(model, "G1");
    ASSERT_NE(goal, nullptr);
    // Translating an argument must not blank the argument.
    EXPECT_EQ(goal->content, "Original content");
    EXPECT_EQ(goal->content_langs.at("ja"), "トップゴール。");
}

TEST(ReviewProposalPatchServiceTest, EmptyNewValueStillClearsWhenNoTranslationIsCarried) {
    parser::AssuranceCase model = MakeModel();
    core::reviews::ReviewProposal proposal = ProposalFor(model);

    core::reviews::PatchOperation update;
    update.type = core::reviews::PatchOperationType::UpdateElementText;
    update.element = core::reviews::ElementRef{"G1", std::nullopt};
    update.field = "content";
    update.new_value = "";
    proposal.operations.push_back(update);

    core::reviews::ReviewProposalPatchService service;
    core::reviews::ApplyProposalResult result = service.ApplyProposal(proposal, model);

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(FindElement(model, "G1")->content, "");
}

TEST(ReviewProposalPatchServiceTest, EmptyTranslationRemovesTheTranslation) {
    parser::AssuranceCase model = MakeModel();
    for (parser::SacmElement& element : model.elements) {
        if (element.id == "G1")
            element.content_langs["ja"] = "古い翻訳。";
    }
    core::reviews::ReviewProposal proposal = ProposalFor(model);

    core::reviews::PatchOperation update;
    update.type = core::reviews::PatchOperationType::UpdateElementText;
    update.element = core::reviews::ElementRef{"G1", std::nullopt};
    update.field = "content";
    update.translations["ja"] = "";
    proposal.operations.push_back(update);

    core::reviews::ReviewProposalPatchService service;
    core::reviews::ApplyProposalResult result = service.ApplyProposal(proposal, model);

    ASSERT_TRUE(result.success) << result.error;
    const parser::SacmElement* goal = FindElement(model, "G1");
    ASSERT_NE(goal, nullptr);
    // A stale translation the author removed must not survive the merge.
    EXPECT_EQ(goal->content_langs.count("ja"), 0u);
    EXPECT_EQ(goal->content, "Original content");
}

TEST(ReviewProposalPatchServiceTest, RefusesToCreateAnElementThatExistsOnlyInTranslation) {
    parser::AssuranceCase model = MakeModel();
    const std::string before_hash = core::reviews::ComputeModelSemanticHash(model);
    core::reviews::ReviewProposal proposal = ProposalFor(model);

    core::reviews::PatchOperation create = Create(core::reviews::PatchOperationType::CreateClaim, "$sub", "");
    create.translations["ja"] = "英語のない主張。";
    proposal.operations.push_back(create);

    core::reviews::ReviewProposalPatchService service;
    core::reviews::ApplyProposalResult result = service.ApplyProposal(proposal, model);

    // Such a claim renders as an empty node for every reader who has not
    // switched languages: an invisible claim in a safety argument.
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("translations"), std::string::npos);
    EXPECT_EQ(core::reviews::ComputeModelSemanticHash(model), before_hash);
}

TEST(ReviewProposalPatchServiceTest, TranslatingAnElementChangesItsSemanticHash) {
    parser::AssuranceCase model = MakeModel();
    const parser::SacmElement* before = FindElement(model, "G1");
    ASSERT_NE(before, nullptr);
    const std::string untranslated = core::reviews::ComputeElementSemanticHash(*before);

    core::reviews::ReviewProposal proposal = ProposalFor(model);
    core::reviews::PatchOperation update;
    update.type = core::reviews::PatchOperationType::UpdateElementText;
    update.element = core::reviews::ElementRef{"G1", std::nullopt};
    update.field = "content";
    update.translations["ja"] = "トップゴール。";
    proposal.operations.push_back(update);

    core::reviews::ReviewProposalPatchService service;
    ASSERT_TRUE(service.ApplyProposal(proposal, model).success);

    // Otherwise a proposal written against the untranslated element still looks
    // current, and staged work would overwrite a translation it never saw.
    EXPECT_NE(core::reviews::ComputeElementSemanticHash(*FindElement(model, "G1")), untranslated);
}

// ---------------------------------------------------------------------------
// Terminology operations. A term's value lives in `content` and its definition
// in `description`, mirroring how the library projection lays a Term out, so
// everything downstream of the patch service -- diffs, hashes, the plan -- sees
// a term the same way it sees one loaded from a file.
// ---------------------------------------------------------------------------

namespace {

parser::SacmElement TermElement(std::string id, std::string value, std::string definition) {
    parser::SacmElement element = Element(std::move(id), "term", "");
    element.content = std::move(value);
    element.content_langs["en"] = element.content;
    element.description = std::move(definition);
    element.description_langs["en"] = element.description;
    return element;
}

} // namespace

TEST(ReviewProposalPatchServiceTest, CreatesTermWithValueDefinitionAndTranslation) {
    parser::AssuranceCase model = MakeModel();
    core::reviews::ReviewProposal proposal = ProposalFor(model);

    core::reviews::PatchOperation create = Create(core::reviews::PatchOperationType::CreateTerm, "$term", "hazard");
    create.new_value = "A system state that could lead to harm.";
    create.translations["ja"] = "危害につながり得るシステム状態。";
    proposal.operations.push_back(create);

    core::reviews::ReviewProposalPatchService service;
    core::reviews::ApplyProposalResult result = service.ApplyProposal(proposal, model);

    ASSERT_TRUE(result.success) << result.error;
    const std::string id = result.generated_ids.at("$term");
    const parser::SacmElement* term = FindElement(model, id);
    ASSERT_NE(term, nullptr);
    EXPECT_EQ(term->type, "term");
    EXPECT_EQ(term->content, "hazard");
    EXPECT_EQ(term->description, "A system state that could lead to harm.");
    EXPECT_EQ(term->description_langs.at("ja"), "危害につながり得るシステム状態。");
}

TEST(ReviewProposalPatchServiceTest, CreateTermRequiresTheTermItself) {
    parser::AssuranceCase model = MakeModel();
    core::reviews::ReviewProposal proposal = ProposalFor(model);
    proposal.operations.push_back(Create(core::reviews::PatchOperationType::CreateTerm, "$term", ""));

    core::reviews::ReviewProposalPatchService service;
    core::reviews::ApplyProposalResult result = service.ApplyProposal(proposal, model);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("term itself"), std::string::npos);
}

TEST(ReviewProposalPatchServiceTest, CreateTermRefusesTranslationsWithoutADefinition) {
    parser::AssuranceCase model = MakeModel();
    core::reviews::ReviewProposal proposal = ProposalFor(model);

    core::reviews::PatchOperation create = Create(core::reviews::PatchOperationType::CreateTerm, "$term", "hazard");
    create.translations["ja"] = "危害につながり得るシステム状態。";
    proposal.operations.push_back(create);

    core::reviews::ReviewProposalPatchService service;
    core::reviews::ApplyProposalResult result = service.ApplyProposal(proposal, model);

    // A definition that exists only in Japanese is invisible to every reader of
    // the primary language -- the same rule a created claim follows.
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("definition"), std::string::npos);
}

TEST(ReviewProposalPatchServiceTest, UpdateTermRewritesValueDefinitionAndName) {
    parser::AssuranceCase model = MakeModel();
    model.elements.push_back(TermElement("T1", "ALARP", "As low as reasonably practicable."));
    core::reviews::ReviewProposal proposal = ProposalFor(model);

    core::reviews::PatchOperation value;
    value.type = core::reviews::PatchOperationType::UpdateTerm;
    value.element = core::reviews::ElementRef{"T1", std::nullopt};
    value.field = "value";
    value.new_value = "ALARP principle";
    proposal.operations.push_back(value);

    core::reviews::PatchOperation definition;
    definition.type = core::reviews::PatchOperationType::UpdateTerm;
    definition.element = core::reviews::ElementRef{"T1", std::nullopt};
    definition.field = "definition";
    definition.new_value = "Risk reduced as low as reasonably practicable, per UK safety law.";
    definition.translations["ja"] = "合理的に実行可能な限り低減されたリスク。";
    proposal.operations.push_back(definition);

    core::reviews::PatchOperation name;
    name.type = core::reviews::PatchOperationType::UpdateTerm;
    name.element = core::reviews::ElementRef{"T1", std::nullopt};
    name.field = "name";
    name.new_value = "ALARP";
    proposal.operations.push_back(name);

    core::reviews::ReviewProposalPatchService service;
    core::reviews::ApplyProposalResult result = service.ApplyProposal(proposal, model);

    ASSERT_TRUE(result.success) << result.error;
    const parser::SacmElement* term = FindElement(model, "T1");
    ASSERT_NE(term, nullptr);
    EXPECT_EQ(term->content, "ALARP principle");
    EXPECT_EQ(term->description, "Risk reduced as low as reasonably practicable, per UK safety law.");
    EXPECT_EQ(term->description_langs.at("ja"), "合理的に実行可能な限り低減されたリスク。");
    EXPECT_EQ(term->name, "ALARP");
}

TEST(ReviewProposalPatchServiceTest, UpdateTermRefusesNonTermsAndTranslatedValues) {
    parser::AssuranceCase model = MakeModel();
    model.elements.push_back(TermElement("T1", "ALARP", "As low as reasonably practicable."));

    // Targeting an argument element: the operation is for glossary work only.
    {
        core::reviews::ReviewProposal proposal = ProposalFor(model);
        core::reviews::PatchOperation update;
        update.type = core::reviews::PatchOperationType::UpdateTerm;
        update.element = core::reviews::ElementRef{"G1", std::nullopt};
        update.field = "value";
        update.new_value = "x";
        proposal.operations.push_back(update);
        core::reviews::ReviewProposalPatchService service;
        core::reviews::ApplyProposalResult result = service.ApplyProposal(proposal, model);
        EXPECT_FALSE(result.success);
        EXPECT_NE(result.error.find("not a term"), std::string::npos);
    }

    // A term's value is one string (SACM 10.11); translations ride on the
    // definition, and a value carrying one must be refused at staging rather
    // than declined at acceptance.
    {
        core::reviews::ReviewProposal proposal = ProposalFor(model);
        core::reviews::PatchOperation update;
        update.type = core::reviews::PatchOperationType::UpdateTerm;
        update.element = core::reviews::ElementRef{"T1", std::nullopt};
        update.field = "value";
        update.new_value = "ALARP";
        update.translations["ja"] = "アラープ";
        proposal.operations.push_back(update);
        core::reviews::ReviewProposalPatchService service;
        core::reviews::ApplyProposalResult result = service.ApplyProposal(proposal, model);
        EXPECT_FALSE(result.success);
        EXPECT_NE(result.error.find("single string"), std::string::npos);
    }

    // Blanking the value would leave a term detection can never match.
    {
        core::reviews::ReviewProposal proposal = ProposalFor(model);
        core::reviews::PatchOperation update;
        update.type = core::reviews::PatchOperationType::UpdateTerm;
        update.element = core::reviews::ElementRef{"T1", std::nullopt};
        update.field = "value";
        proposal.operations.push_back(update);
        core::reviews::ReviewProposalPatchService service;
        core::reviews::ApplyProposalResult result = service.ApplyProposal(proposal, model);
        EXPECT_FALSE(result.success);
        EXPECT_NE(result.error.find("blank"), std::string::npos);
    }
}

TEST(ReviewProposalPatchServiceTest, CategoriesAndSourcesAreSetOnTermsThroughUpdateTerm) {
    parser::AssuranceCase model = MakeModel();
    model.elements.push_back(TermElement("T1", "ALARP", "As low as reasonably practicable."));
    core::reviews::ReviewProposal proposal = ProposalFor(model);

    // The shape that answers the terminology check's two Info findings: a
    // category to classify the term, and a citation for where it comes from.
    core::reviews::PatchOperation create_category =
        Create(core::reviews::PatchOperationType::CreateCategory, "$cat", "Regulatory terms");
    create_category.new_value = "Terms drawn from regulation and standards.";
    proposal.operations.push_back(create_category);

    // A field value is a plain string, so the category is addressed by the id
    // the create allocated rather than by its `$cat` handle. Previewing first is
    // how an agent learns that id too -- staging reports it as a created id.
    core::reviews::ReviewProposalPatchService service;
    const core::reviews::ProposalPreviewResult preview = service.BuildPreviewModel(proposal, model);
    ASSERT_TRUE(preview.success) << preview.error;
    const std::string category_id = preview.generated_ids.at("$cat");

    core::reviews::PatchOperation classify;
    classify.type = core::reviews::PatchOperationType::UpdateTerm;
    classify.element = core::reviews::ElementRef{"T1", std::nullopt};
    classify.field = "category";
    classify.new_value = category_id;
    proposal.operations.push_back(classify);

    core::reviews::PatchOperation cite;
    cite.type = core::reviews::PatchOperationType::UpdateTerm;
    cite.element = core::reviews::ElementRef{"T1", std::nullopt};
    cite.field = "external_reference";
    cite.new_value = "HSE R2P2, 2001";
    proposal.operations.push_back(cite);

    const core::reviews::ApplyProposalResult result = service.ApplyProposal(proposal, model);
    ASSERT_TRUE(result.success) << result.error;
    ASSERT_EQ(result.generated_ids.at("$cat"), category_id);

    const parser::SacmElement* category = FindElement(model, category_id);
    ASSERT_NE(category, nullptr);
    EXPECT_EQ(category->type, "category");
    EXPECT_EQ(category->name, "Regulatory terms");
    EXPECT_EQ(category->description, "Terms drawn from regulation and standards.");

    const parser::SacmElement* term = FindElement(model, "T1");
    ASSERT_NE(term, nullptr);
    ASSERT_EQ(term->category_refs.size(), 1u);
    EXPECT_EQ(term->category_refs.front(), category_id);
    EXPECT_EQ(term->external_reference, "HSE R2P2, 2001");
}

TEST(ReviewProposalPatchServiceTest, ATermBelongsToACategoryOnceHoweverOftenItIsNamed) {
    parser::AssuranceCase model = MakeModel();
    model.elements.push_back(TermElement("T1", "ALARP", "As low as reasonably practicable."));
    parser::SacmElement category = Element("CAT1", "category", "Regulatory terms");
    model.elements.push_back(category);
    core::reviews::ReviewProposal proposal = ProposalFor(model);

    // The list is space separated, so repeating an id is an easy slip. Storing
    // it twice would show the category twice in `list_terms` and move the
    // element's hash without changing what it classifies.
    core::reviews::PatchOperation classify;
    classify.type = core::reviews::PatchOperationType::UpdateTerm;
    classify.element = core::reviews::ElementRef{"T1", std::nullopt};
    classify.field = "category";
    classify.new_value = "CAT1 CAT1  #CAT1";
    proposal.operations.push_back(classify);

    core::reviews::ReviewProposalPatchService service;
    const core::reviews::ApplyProposalResult result = service.ApplyProposal(proposal, model);

    ASSERT_TRUE(result.success) << result.error;
    const parser::SacmElement* term = FindElement(model, "T1");
    ASSERT_NE(term, nullptr);
    ASSERT_EQ(term->category_refs.size(), 1u) << "the same category was stored more than once";
    EXPECT_EQ(term->category_refs.front(), "CAT1");
}

TEST(ReviewProposalPatchServiceTest, UpdateTermRefusesACategoryThatIsNotOne) {
    parser::AssuranceCase model = MakeModel();
    model.elements.push_back(TermElement("T1", "ALARP", "As low as reasonably practicable."));

    // An id that does not exist, and one that exists but is a claim. Both are
    // refused at staging rather than at acceptance: the library would reject
    // them either way, and a message the agent gets while it is still building
    // the group is one it can act on.
    for (const std::string& bad_ref : {std::string("CAT_NOPE"), std::string("G1")}) {
        core::reviews::ReviewProposal proposal = ProposalFor(model);
        core::reviews::PatchOperation classify;
        classify.type = core::reviews::PatchOperationType::UpdateTerm;
        classify.element = core::reviews::ElementRef{"T1", std::nullopt};
        classify.field = "category";
        classify.new_value = bad_ref;
        proposal.operations.push_back(classify);

        core::reviews::ReviewProposalPatchService service;
        core::reviews::ApplyProposalResult result = service.ApplyProposal(proposal, model);
        EXPECT_FALSE(result.success) << bad_ref;
        EXPECT_NE(result.error.find(bad_ref), std::string::npos);
    }

    // An origin has to resolve too -- it is an element reference, not a
    // citation string, and the message says which field takes the other.
    core::reviews::ReviewProposal proposal = ProposalFor(model);
    core::reviews::PatchOperation origin;
    origin.type = core::reviews::PatchOperationType::UpdateTerm;
    origin.element = core::reviews::ElementRef{"T1", std::nullopt};
    origin.field = "origin";
    origin.new_value = "https://example.com/standard";
    proposal.operations.push_back(origin);
    core::reviews::ReviewProposalPatchService service;
    core::reviews::ApplyProposalResult result = service.ApplyProposal(proposal, model);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("external_reference"), std::string::npos);
}

TEST(ReviewProposalPatchServiceTest, ATermsClassificationIsPartOfItsSemanticHash) {
    parser::AssuranceCase model = MakeModel();
    model.elements.push_back(TermElement("T1", "ALARP", "As low as reasonably practicable."));
    const parser::SacmElement* before = FindElement(model, "T1");
    ASSERT_NE(before, nullptr);
    const std::string unclassified = core::reviews::ComputeElementSemanticHash(*before);

    parser::SacmElement categorized = *before;
    categorized.category_refs.push_back("CAT1");
    const std::string classified = core::reviews::ComputeElementSemanticHash(categorized);

    parser::SacmElement cited = *before;
    cited.external_reference = "HSE R2P2, 2001";

    // Otherwise a proposal written against an uncategorized term still looks
    // current after someone categorized it, and accepting it would quietly
    // revert their work.
    EXPECT_NE(classified, unclassified);
    EXPECT_NE(core::reviews::ComputeElementSemanticHash(cited), unclassified);

    // A term that has none of these hashes exactly as it did before the fields
    // were covered, so proposals saved against one stay valid.
    parser::SacmElement untouched = *before;
    EXPECT_EQ(core::reviews::ComputeElementSemanticHash(untouched), unclassified);
}

TEST(ReviewProposalPatchServiceTest, RemoveTermRemovesOnlyTerms) {
    parser::AssuranceCase model = MakeModel();
    model.elements.push_back(TermElement("T1", "ALARP", "As low as reasonably practicable."));

    {
        core::reviews::ReviewProposal proposal = ProposalFor(model);
        core::reviews::PatchOperation remove;
        remove.type = core::reviews::PatchOperationType::RemoveTerm;
        remove.element = core::reviews::ElementRef{"G2", std::nullopt};
        proposal.operations.push_back(remove);
        core::reviews::ReviewProposalPatchService service;
        core::reviews::ApplyProposalResult result = service.ApplyProposal(proposal, model);
        EXPECT_FALSE(result.success);
        EXPECT_NE(result.error.find("not a term"), std::string::npos);
        EXPECT_NE(FindElement(model, "G2"), nullptr);
    }

    {
        core::reviews::ReviewProposal proposal = ProposalFor(model);
        core::reviews::PatchOperation remove;
        remove.type = core::reviews::PatchOperationType::RemoveTerm;
        remove.element = core::reviews::ElementRef{"T1", std::nullopt};
        proposal.operations.push_back(remove);
        core::reviews::ReviewProposalPatchService service;
        core::reviews::ApplyProposalResult result = service.ApplyProposal(proposal, model);
        ASSERT_TRUE(result.success) << result.error;
        EXPECT_EQ(FindElement(model, "T1"), nullptr);
    }
}
