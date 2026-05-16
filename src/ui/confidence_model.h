#pragma once

namespace ui {

enum class ConfidenceInputMode {
    DirectValue,
    OpinionTriangle,
};

enum class OpinionComponent {
    Belief,
    Disbelief,
    Uncertainty,
};

struct ConfidencePoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct SubjectiveOpinion {
    float belief = 0.5f;
    float disbelief = 0.1f;
    float uncertainty = 0.4f;
    float baseRate = 0.5f;

    float ProjectedConfidence() const;
};

struct ElementConfidence {
    bool enabled = false;
    ConfidenceInputMode mode = ConfidenceInputMode::OpinionTriangle;
    float directValue = 0.75f;
    SubjectiveOpinion opinion;
};

float ClampConfidenceValue(float value);
float ProjectedConfidence(const SubjectiveOpinion& opinion);
void NormalizeOpinion(SubjectiveOpinion& opinion);
SubjectiveOpinion NormalizedOpinion(SubjectiveOpinion opinion);
void SetOpinionComponent(SubjectiveOpinion& opinion, OpinionComponent component, float value);

ConfidencePoint OpinionToPoint(const SubjectiveOpinion& opinion,
                               ConfidencePoint uncertainty_vertex,
                               ConfidencePoint disbelief_vertex,
                               ConfidencePoint belief_vertex);
SubjectiveOpinion OpinionFromPoint(ConfidencePoint point,
                                   ConfidencePoint uncertainty_vertex,
                                   ConfidencePoint disbelief_vertex,
                                   ConfidencePoint belief_vertex,
                                   float base_rate = 0.5f);
ConfidencePoint ClosestPointInOpinionTriangle(ConfidencePoint point,
                                              ConfidencePoint uncertainty_vertex,
                                              ConfidencePoint disbelief_vertex,
                                              ConfidencePoint belief_vertex);

} // namespace ui