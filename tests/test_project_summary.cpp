#include "core/project_summary.h"

#include <gtest/gtest.h>

namespace {

core::AssuranceCase MakeCase() {
    core::AssuranceCase model;
    model.elements = {
        {.id = "G1", .type = "claim", .undeveloped = true},
        {.id = "Sn1", .type = "artifactreference"},
        {.id = "Sn2", .type = "artifactreference"},
        {.id = "AE1", .type = "assertedevidence", .source_refs = {"Sn1"}, .target_refs = {"G1"}},
    };
    return model;
}

TEST(ProjectSummaryTest, DerivesWorkflowCountsWithoutExposingFolderLayout) {
    core::AssuranceProject project;
    project.files = {
        {.role = core::ProjectFileRole::SacmArgument},
        {.role = core::ProjectFileRole::EvidenceRegister},
        {.role = core::ProjectFileRole::J3377CaeRegister},
        {.role = core::ProjectFileRole::ExportedReport},
    };
    const core::AssuranceCase model = MakeCase();
    const core::AssuranceTree tree = core::AssuranceTree::Build(model);

    core::ProblemItem warning;
    warning.severity = core::ProblemSeverity::Warning;
    core::ProblemItem error;
    error.severity = core::ProblemSeverity::Error;

    core::reviews::ReviewItem open_item;
    open_item.status = core::reviews::ReviewItemStatus::Open;
    core::reviews::ReviewItem resolved_item;
    resolved_item.status = core::reviews::ReviewItemStatus::Resolved;

    core::reviews::ProposalValidityResult valid;
    valid.validity = core::reviews::ProposalValidity::Valid;
    core::reviews::ProposalValidityResult broken;
    broken.validity = core::reviews::ProposalValidity::Broken;

    const core::ProjectSummary summary = core::BuildProjectSummary(
        &project, &model, &tree, {warning, error}, {open_item, resolved_item}, {valid, broken});

    EXPECT_EQ(summary.argument_files, 1u);
    EXPECT_EQ(summary.claims, 1u);
    EXPECT_EQ(summary.evidence, 2u);
    EXPECT_EQ(summary.undeveloped, 1u);
    EXPECT_EQ(summary.unlinked_evidence, 1u);
    EXPECT_EQ(summary.open_review_items, 1u);
    EXPECT_EQ(summary.resolved_review_items, 1u);
    EXPECT_EQ(summary.valid_proposals, 1u);
    EXPECT_EQ(summary.broken_proposals, 1u);
    EXPECT_EQ(summary.conformance_files, 1u);
    EXPECT_EQ(summary.exported_reports, 1u);
    EXPECT_EQ(summary.attention_count(), 3u);
}

} // namespace
