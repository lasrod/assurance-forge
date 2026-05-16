#include "ui/confidence_model.h"

#include <gtest/gtest.h>

#include <cmath>

namespace {

constexpr ui::ConfidencePoint kUncertaintyVertex{0.0f, 1.0f};
constexpr ui::ConfidencePoint kDisbeliefVertex{-1.0f, 0.0f};
constexpr ui::ConfidencePoint kBeliefVertex{1.0f, 0.0f};

float Sum(const ui::SubjectiveOpinion& opinion) {
    return opinion.belief + opinion.disbelief + opinion.uncertainty;
}

} // namespace

TEST(ConfidenceModelTest, ProjectedConfidenceUsesBeliefPlusBaseRateUncertainty) {
    ui::SubjectiveOpinion opinion;
    opinion.belief = 0.70f;
    opinion.disbelief = 0.10f;
    opinion.uncertainty = 0.20f;
    opinion.baseRate = 0.50f;

    EXPECT_NEAR(opinion.ProjectedConfidence(), 0.80f, 0.0001f);
}

TEST(ConfidenceModelTest, NormalizeOpinionClampsAndPreservesUnitSum) {
    ui::SubjectiveOpinion opinion;
    opinion.belief = 2.0f;
    opinion.disbelief = -1.0f;
    opinion.uncertainty = 1.0f;
    opinion.baseRate = 1.5f;

    ui::NormalizeOpinion(opinion);

    EXPECT_NEAR(Sum(opinion), 1.0f, 0.0001f);
    EXPECT_GE(opinion.belief, 0.0f);
    EXPECT_LE(opinion.belief, 1.0f);
    EXPECT_GE(opinion.disbelief, 0.0f);
    EXPECT_LE(opinion.disbelief, 1.0f);
    EXPECT_GE(opinion.uncertainty, 0.0f);
    EXPECT_LE(opinion.uncertainty, 1.0f);
    EXPECT_EQ(opinion.baseRate, 1.0f);
}

TEST(ConfidenceModelTest, VertexPointsMapToExpectedOpinions) {
    const ui::SubjectiveOpinion top =
        ui::OpinionFromPoint(kUncertaintyVertex, kUncertaintyVertex, kDisbeliefVertex, kBeliefVertex);
    const ui::SubjectiveOpinion left =
        ui::OpinionFromPoint(kDisbeliefVertex, kUncertaintyVertex, kDisbeliefVertex, kBeliefVertex);
    const ui::SubjectiveOpinion right =
        ui::OpinionFromPoint(kBeliefVertex, kUncertaintyVertex, kDisbeliefVertex, kBeliefVertex);

    EXPECT_NEAR(top.uncertainty, 1.0f, 0.0001f);
    EXPECT_NEAR(top.disbelief, 0.0f, 0.0001f);
    EXPECT_NEAR(top.belief, 0.0f, 0.0001f);

    EXPECT_NEAR(left.uncertainty, 0.0f, 0.0001f);
    EXPECT_NEAR(left.disbelief, 1.0f, 0.0001f);
    EXPECT_NEAR(left.belief, 0.0f, 0.0001f);

    EXPECT_NEAR(right.uncertainty, 0.0f, 0.0001f);
    EXPECT_NEAR(right.disbelief, 0.0f, 0.0001f);
    EXPECT_NEAR(right.belief, 1.0f, 0.0001f);
}

TEST(ConfidenceModelTest, CentroidMapsToBalancedOpinion) {
    const ui::ConfidencePoint centroid{0.0f, 1.0f / 3.0f};

    const ui::SubjectiveOpinion opinion =
        ui::OpinionFromPoint(centroid, kUncertaintyVertex, kDisbeliefVertex, kBeliefVertex);

    EXPECT_NEAR(opinion.belief, 1.0f / 3.0f, 0.0001f);
    EXPECT_NEAR(opinion.disbelief, 1.0f / 3.0f, 0.0001f);
    EXPECT_NEAR(opinion.uncertainty, 1.0f / 3.0f, 0.0001f);
    EXPECT_NEAR(Sum(opinion), 1.0f, 0.0001f);
}

TEST(ConfidenceModelTest, OutsidePointProjectsIntoTriangleAndNormalizes) {
    const ui::ConfidencePoint outside{2.5f, -1.0f};

    const ui::SubjectiveOpinion opinion =
        ui::OpinionFromPoint(outside, kUncertaintyVertex, kDisbeliefVertex, kBeliefVertex);
    const ui::ConfidencePoint marker = ui::OpinionToPoint(opinion, kUncertaintyVertex, kDisbeliefVertex, kBeliefVertex);

    EXPECT_NEAR(Sum(opinion), 1.0f, 0.0001f);
    EXPECT_GE(opinion.belief, 0.0f);
    EXPECT_GE(opinion.disbelief, 0.0f);
    EXPECT_GE(opinion.uncertainty, 0.0f);
    EXPECT_LE(marker.x, 1.0f);
    EXPECT_GE(marker.x, -1.0f);
    EXPECT_GE(marker.y, 0.0f);
    EXPECT_LE(marker.y, 1.0f);
}

TEST(ConfidenceModelTest, SetOpinionComponentPreservesUnitSumAndScalesOtherValues) {
    ui::SubjectiveOpinion opinion;
    opinion.belief = 0.50f;
    opinion.disbelief = 0.25f;
    opinion.uncertainty = 0.25f;

    ui::SetOpinionComponent(opinion, ui::OpinionComponent::Belief, 0.80f);

    EXPECT_NEAR(opinion.belief, 0.80f, 0.0001f);
    EXPECT_NEAR(opinion.disbelief, 0.10f, 0.0001f);
    EXPECT_NEAR(opinion.uncertainty, 0.10f, 0.0001f);
    EXPECT_NEAR(Sum(opinion), 1.0f, 0.0001f);
}

TEST(ConfidenceModelTest, SetOpinionComponentSplitsRemainingValueWhenOthersAreZero) {
    ui::SubjectiveOpinion opinion;
    opinion.belief = 1.0f;
    opinion.disbelief = 0.0f;
    opinion.uncertainty = 0.0f;

    ui::SetOpinionComponent(opinion, ui::OpinionComponent::Belief, 0.40f);

    EXPECT_NEAR(opinion.belief, 0.40f, 0.0001f);
    EXPECT_NEAR(opinion.disbelief, 0.30f, 0.0001f);
    EXPECT_NEAR(opinion.uncertainty, 0.30f, 0.0001f);
    EXPECT_NEAR(Sum(opinion), 1.0f, 0.0001f);
}