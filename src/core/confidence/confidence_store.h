#pragma once

#include "parser/xml_parser.h"

#include <optional>
#include <string>
#include <vector>

namespace core::confidence {

enum class ConfidenceTargetKind {
    Element,
    Relationship,
};

enum class ConfidenceMethod {
    FixedValue,
    JosangOpinion,
};

enum class ConfidenceStatus {
    Active,
    Archived,
};

struct SacmSource {
    std::string sourceId = "main";
    std::string path;
    std::string fileHash;
};

struct ConfidenceTarget {
    ConfidenceTargetKind kind = ConfidenceTargetKind::Element;
    std::string sourceId = "main";
    std::string sacmGid;
    std::string sacmType;

    std::string relationshipGid;
    std::string sourceGid;
    std::string targetGid;
    std::string relationshipType;
};

struct JosangOpinion {
    double belief = 0.65;
    double disbelief = 0.10;
    double uncertainty = 0.25;
    double baseRate = 0.50;
};

struct FixedConfidenceValue {
    double value = 0.75;
};

struct ConfidenceDerived {
    double expectedConfidence = 0.75;
    std::string calculation = "fixedValue";
    int calculationVersion = 1;
};

struct ConfidenceAssessment {
    std::string id;
    ConfidenceTarget target;
    ConfidenceMethod method = ConfidenceMethod::FixedValue;
    std::optional<JosangOpinion> josangOpinion;
    std::optional<FixedConfidenceValue> fixedValue;
    ConfidenceDerived derived;
    ConfidenceStatus status = ConfidenceStatus::Active;
    bool stale = false;
    std::string targetFingerprint;
    std::string createdAt;
    std::string updatedAt;
};

struct ConfidenceStore {
    std::string schema = "assurance-forge.confidence";
    int schemaVersion = 1;
    std::string projectId;
    std::vector<SacmSource> sacmSources;
    std::vector<ConfidenceAssessment> assessments;
};

const char* ToString(ConfidenceTargetKind kind);
const char* ToString(ConfidenceMethod method);
const char* ToString(ConfidenceStatus status);

ConfidenceTargetKind ConfidenceTargetKindFromString(const std::string& value);
ConfidenceMethod ConfidenceMethodFromString(const std::string& value);
ConfidenceStatus ConfidenceStatusFromString(const std::string& value);

double ExpectedConfidence(const JosangOpinion& opinion);
double ExpectedConfidence(const FixedConfidenceValue& value);
ConfidenceDerived DerivedFor(const JosangOpinion& opinion);
ConfidenceDerived DerivedFor(const FixedConfidenceValue& value);

bool ValidateJosangOpinion(const JosangOpinion& opinion, std::string& error);
bool ValidateFixedConfidenceValue(const FixedConfidenceValue& value, std::string& error);
bool ValidateAssessment(const ConfidenceAssessment& assessment, std::string& error);

std::string SerializeConfidenceStore(const ConfidenceStore& store);
bool DeserializeConfidenceStore(const std::string& content, ConfidenceStore& store, std::string& error);

std::string DisplaySacmType(const parser::SacmElement& element);
std::string FingerprintElement(const parser::SacmElement& element);
std::string NextAssessmentId(const ConfidenceStore& store);

const ConfidenceAssessment* FindActiveAssessment(const ConfidenceStore& store,
                                                 const std::string& source_id,
                                                 const std::string& sacm_gid);
ConfidenceAssessment* FindActiveAssessment(ConfidenceStore& store,
                                           const std::string& source_id,
                                           const std::string& sacm_gid);
bool RemoveActiveAssessment(ConfidenceStore& store, const std::string& source_id, const std::string& sacm_gid);
void UpsertSacmSource(ConfidenceStore& store, SacmSource source);
bool RefreshStaleFlags(ConfidenceStore& store, const std::string& source_id, const parser::AssuranceCase& model);

} // namespace core::confidence