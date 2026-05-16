#include "ui/confidence_model.h"

#include <algorithm>
#include <cmath>

namespace ui {

namespace {

struct BarycentricWeights {
    float uncertainty = 0.0f;
    float disbelief = 0.0f;
    float belief = 0.0f;
};

float Dot(ConfidencePoint left, ConfidencePoint right) {
    return left.x * right.x + left.y * right.y;
}

ConfidencePoint Subtract(ConfidencePoint left, ConfidencePoint right) {
    return {left.x - right.x, left.y - right.y};
}

float DistanceSquared(ConfidencePoint left, ConfidencePoint right) {
    const float dx = left.x - right.x;
    const float dy = left.y - right.y;
    return dx * dx + dy * dy;
}

ConfidencePoint ClosestPointOnSegment(ConfidencePoint point, ConfidencePoint start, ConfidencePoint end) {
    const ConfidencePoint segment = Subtract(end, start);
    const float length_squared = Dot(segment, segment);
    if (length_squared <= 1e-6f)
        return start;

    const float t = std::clamp(Dot(Subtract(point, start), segment) / length_squared, 0.0f, 1.0f);
    return {start.x + segment.x * t, start.y + segment.y * t};
}

BarycentricWeights ComputeBarycentricWeights(ConfidencePoint point,
                                             ConfidencePoint uncertainty_vertex,
                                             ConfidencePoint disbelief_vertex,
                                             ConfidencePoint belief_vertex) {
    const ConfidencePoint v0 = Subtract(disbelief_vertex, uncertainty_vertex);
    const ConfidencePoint v1 = Subtract(belief_vertex, uncertainty_vertex);
    const ConfidencePoint v2 = Subtract(point, uncertainty_vertex);

    const float d00 = Dot(v0, v0);
    const float d01 = Dot(v0, v1);
    const float d11 = Dot(v1, v1);
    const float d20 = Dot(v2, v0);
    const float d21 = Dot(v2, v1);
    const float denominator = d00 * d11 - d01 * d01;
    if (std::fabs(denominator) <= 1e-6f)
        return {1.0f, 0.0f, 0.0f};

    const float disbelief = (d11 * d20 - d01 * d21) / denominator;
    const float belief = (d00 * d21 - d01 * d20) / denominator;
    const float uncertainty = 1.0f - disbelief - belief;
    return {uncertainty, disbelief, belief};
}

bool IsInsideTriangle(const BarycentricWeights& weights) {
    constexpr float tolerance = -1e-5f;
    return weights.uncertainty >= tolerance && weights.disbelief >= tolerance && weights.belief >= tolerance;
}

} // namespace

float ClampConfidenceValue(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float ProjectedConfidence(const SubjectiveOpinion& opinion) {
    return ClampConfidenceValue(opinion.belief + ClampConfidenceValue(opinion.baseRate) * opinion.uncertainty);
}

float SubjectiveOpinion::ProjectedConfidence() const {
    return ui::ProjectedConfidence(*this);
}

void NormalizeOpinion(SubjectiveOpinion& opinion) {
    float belief = ClampConfidenceValue(opinion.belief);
    float disbelief = ClampConfidenceValue(opinion.disbelief);
    float uncertainty = ClampConfidenceValue(opinion.uncertainty);
    const float sum = belief + disbelief + uncertainty;

    if (sum <= 1e-6f) {
        opinion.belief = 1.0f / 3.0f;
        opinion.disbelief = 1.0f / 3.0f;
        opinion.uncertainty = 1.0f - opinion.belief - opinion.disbelief;
    } else {
        opinion.belief = belief / sum;
        opinion.disbelief = disbelief / sum;
        opinion.uncertainty = 1.0f - opinion.belief - opinion.disbelief;
    }

    opinion.baseRate = ClampConfidenceValue(opinion.baseRate);
}

SubjectiveOpinion NormalizedOpinion(SubjectiveOpinion opinion) {
    NormalizeOpinion(opinion);
    return opinion;
}

void SetOpinionComponent(SubjectiveOpinion& opinion, OpinionComponent component, float value) {
    NormalizeOpinion(opinion);

    const float target = ClampConfidenceValue(value);
    const float remaining = 1.0f - target;
    float* selected = &opinion.belief;
    float* first_other = &opinion.disbelief;
    float* second_other = &opinion.uncertainty;

    if (component == OpinionComponent::Disbelief) {
        selected = &opinion.disbelief;
        first_other = &opinion.belief;
        second_other = &opinion.uncertainty;
    } else if (component == OpinionComponent::Uncertainty) {
        selected = &opinion.uncertainty;
        first_other = &opinion.belief;
        second_other = &opinion.disbelief;
    }

    const float other_sum = *first_other + *second_other;
    *selected = target;
    if (other_sum <= 1e-6f) {
        *first_other = remaining * 0.5f;
        *second_other = remaining - *first_other;
    } else {
        *first_other = remaining * (*first_other / other_sum);
        *second_other = remaining - *first_other;
    }

    NormalizeOpinion(opinion);
}

ConfidencePoint OpinionToPoint(const SubjectiveOpinion& opinion,
                               ConfidencePoint uncertainty_vertex,
                               ConfidencePoint disbelief_vertex,
                               ConfidencePoint belief_vertex) {
    const SubjectiveOpinion normalized = NormalizedOpinion(opinion);
    return {uncertainty_vertex.x * normalized.uncertainty + disbelief_vertex.x * normalized.disbelief +
                belief_vertex.x * normalized.belief,
            uncertainty_vertex.y * normalized.uncertainty + disbelief_vertex.y * normalized.disbelief +
                belief_vertex.y * normalized.belief};
}

SubjectiveOpinion OpinionFromPoint(ConfidencePoint point,
                                   ConfidencePoint uncertainty_vertex,
                                   ConfidencePoint disbelief_vertex,
                                   ConfidencePoint belief_vertex,
                                   float base_rate) {
    const ConfidencePoint clamped_point =
        ClosestPointInOpinionTriangle(point, uncertainty_vertex, disbelief_vertex, belief_vertex);
    const BarycentricWeights weights =
        ComputeBarycentricWeights(clamped_point, uncertainty_vertex, disbelief_vertex, belief_vertex);

    SubjectiveOpinion opinion;
    opinion.belief = weights.belief;
    opinion.disbelief = weights.disbelief;
    opinion.uncertainty = weights.uncertainty;
    opinion.baseRate = base_rate;
    NormalizeOpinion(opinion);
    return opinion;
}

ConfidencePoint ClosestPointInOpinionTriangle(ConfidencePoint point,
                                              ConfidencePoint uncertainty_vertex,
                                              ConfidencePoint disbelief_vertex,
                                              ConfidencePoint belief_vertex) {
    const BarycentricWeights weights =
        ComputeBarycentricWeights(point, uncertainty_vertex, disbelief_vertex, belief_vertex);
    if (IsInsideTriangle(weights))
        return point;

    const ConfidencePoint on_left = ClosestPointOnSegment(point, uncertainty_vertex, disbelief_vertex);
    const ConfidencePoint on_right = ClosestPointOnSegment(point, uncertainty_vertex, belief_vertex);
    const ConfidencePoint on_bottom = ClosestPointOnSegment(point, disbelief_vertex, belief_vertex);

    ConfidencePoint closest = on_left;
    float closest_distance = DistanceSquared(point, on_left);

    const float right_distance = DistanceSquared(point, on_right);
    if (right_distance < closest_distance) {
        closest = on_right;
        closest_distance = right_distance;
    }

    const float bottom_distance = DistanceSquared(point, on_bottom);
    if (bottom_distance < closest_distance)
        closest = on_bottom;

    return closest;
}

} // namespace ui