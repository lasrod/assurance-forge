#include "core/confidence/confidence_store.h"

#include "core/sha256.h"
#include "core/string_utils.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>

namespace core::confidence {
namespace {

constexpr const char* kSchema = "assurance-forge.confidence";
constexpr int kSchemaVersion = 1;
constexpr double kTolerance = 0.0001;

bool InUnitRange(double value) {
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

std::string HashNormalized(const std::string& normalized) {
    return "sha256:" + Sha256::HexDigest(normalized);
}

std::string JsonString(const nlohmann::json& object, const char* key) {
    if (!object.contains(key) || !object[key].is_string())
        return {};
    return object[key].get<std::string>();
}

double JsonDouble(const nlohmann::json& object, const char* key, double fallback) {
    if (!object.contains(key) || !object[key].is_number())
        return fallback;
    return object[key].get<double>();
}

int JsonInt(const nlohmann::json& object, const char* key, int fallback) {
    if (!object.contains(key) || !object[key].is_number_integer())
        return fallback;
    return object[key].get<int>();
}

nlohmann::json ToJson(const SacmSource& source) {
    nlohmann::json object;
    object["sourceId"] = source.sourceId;
    object["path"] = source.path;
    object["fileHash"] = source.fileHash;
    return object;
}

SacmSource SacmSourceFromJson(const nlohmann::json& object) {
    SacmSource source;
    source.sourceId = object.value("sourceId", "main");
    source.path = object.value("path", "");
    source.fileHash = object.value("fileHash", "");
    return source;
}

nlohmann::json ToJson(const ConfidenceTarget& target) {
    nlohmann::json object;
    object["kind"] = ToString(target.kind);
    if (target.kind == ConfidenceTargetKind::Element) {
        object["sourceId"] = target.sourceId;
        object["sacmGid"] = target.sacmGid;
        object["sacmType"] = target.sacmType;
    } else {
        object["relationshipGid"] = target.relationshipGid;
        object["sourceGid"] = target.sourceGid;
        object["targetGid"] = target.targetGid;
        object["relationshipType"] = target.relationshipType;
    }
    return object;
}

ConfidenceTarget TargetFromJson(const nlohmann::json& object) {
    ConfidenceTarget target;
    target.kind = ConfidenceTargetKindFromString(object.value("kind", "element"));
    target.sourceId = object.value("sourceId", "main");
    target.sacmGid = object.value("sacmGid", "");
    target.sacmType = object.value("sacmType", "");
    target.relationshipGid = object.value("relationshipGid", "");
    target.sourceGid = object.value("sourceGid", "");
    target.targetGid = object.value("targetGid", "");
    target.relationshipType = object.value("relationshipType", "");
    return target;
}

nlohmann::json ToJson(const JosangOpinion& opinion) {
    nlohmann::json object;
    object["belief"] = opinion.belief;
    object["disbelief"] = opinion.disbelief;
    object["uncertainty"] = opinion.uncertainty;
    object["baseRate"] = opinion.baseRate;
    return object;
}

JosangOpinion JosangOpinionFromJson(const nlohmann::json& object) {
    JosangOpinion opinion;
    opinion.belief = JsonDouble(object, "belief", opinion.belief);
    opinion.disbelief = JsonDouble(object, "disbelief", opinion.disbelief);
    opinion.uncertainty = JsonDouble(object, "uncertainty", opinion.uncertainty);
    opinion.baseRate = JsonDouble(object, "baseRate", opinion.baseRate);
    return opinion;
}

nlohmann::json ToJson(const FixedConfidenceValue& value) {
    nlohmann::json object;
    object["value"] = value.value;
    return object;
}

FixedConfidenceValue FixedValueFromJson(const nlohmann::json& object) {
    FixedConfidenceValue value;
    value.value = JsonDouble(object, "value", value.value);
    return value;
}

nlohmann::json ToJson(const ConfidenceDerived& derived) {
    nlohmann::json object;
    object["expectedConfidence"] = derived.expectedConfidence;
    object["calculation"] = derived.calculation;
    object["calculationVersion"] = derived.calculationVersion;
    return object;
}

ConfidenceDerived DerivedFromJson(const nlohmann::json& object) {
    ConfidenceDerived derived;
    derived.expectedConfidence = JsonDouble(object, "expectedConfidence", derived.expectedConfidence);
    derived.calculation = object.value("calculation", derived.calculation);
    derived.calculationVersion = JsonInt(object, "calculationVersion", derived.calculationVersion);
    return derived;
}

nlohmann::json ToJson(const ConfidenceAssessment& assessment) {
    nlohmann::json object;
    object["id"] = assessment.id;
    object["target"] = ToJson(assessment.target);
    object["method"] = ToString(assessment.method);
    if (assessment.josangOpinion.has_value()) {
        object["josangOpinion"] = ToJson(assessment.josangOpinion.value());
    } else {
        object["josangOpinion"] = nullptr;
    }
    if (assessment.fixedValue.has_value()) {
        object["fixedValue"] = ToJson(assessment.fixedValue.value());
    } else {
        object["fixedValue"] = nullptr;
    }
    object["derived"] = ToJson(assessment.derived);
    object["status"] = ToString(assessment.status);
    object["stale"] = assessment.stale;
    object["targetFingerprint"] = assessment.targetFingerprint;
    object["createdAt"] = assessment.createdAt;
    object["updatedAt"] = assessment.updatedAt;
    return object;
}

bool AssessmentFromJson(const nlohmann::json& object, ConfidenceAssessment& assessment, std::string& error) {
    if (!object.is_object()) {
        error = "Confidence assessment entry is not an object.";
        return false;
    }
    assessment = ConfidenceAssessment{};
    assessment.id = JsonString(object, "id");
    if (assessment.id.empty()) {
        error = "Confidence assessment is missing an id.";
        return false;
    }
    if (!object.contains("target") || !object["target"].is_object()) {
        error = "Confidence assessment is missing a target.";
        return false;
    }
    assessment.target = TargetFromJson(object["target"]);
    assessment.method = ConfidenceMethodFromString(object.value("method", "fixedValue"));
    if (object.contains("josangOpinion") && object["josangOpinion"].is_object())
        assessment.josangOpinion = JosangOpinionFromJson(object["josangOpinion"]);
    if (object.contains("fixedValue") && object["fixedValue"].is_object())
        assessment.fixedValue = FixedValueFromJson(object["fixedValue"]);
    if (object.contains("derived") && object["derived"].is_object())
        assessment.derived = DerivedFromJson(object["derived"]);
    assessment.status = ConfidenceStatusFromString(object.value("status", "active"));
    assessment.stale = object.value("stale", false);
    assessment.targetFingerprint = object.value("targetFingerprint", "");
    assessment.createdAt = object.value("createdAt", "");
    assessment.updatedAt = object.value("updatedAt", "");
    return ValidateAssessment(assessment, error);
}

void AddLangMap(std::ostringstream& out, const char* name, const std::map<std::string, std::string>& values) {
    for (const auto& [language, value] : values) {
        if (!value.empty())
            out << name << "[" << language << "]=" << value << '\n';
    }
}

} // namespace

const char* ToString(ConfidenceTargetKind kind) {
    switch (kind) {
    case ConfidenceTargetKind::Element:
        return "element";
    case ConfidenceTargetKind::Relationship:
        return "relationship";
    }
    return "element";
}

const char* ToString(ConfidenceMethod method) {
    switch (method) {
    case ConfidenceMethod::FixedValue:
        return "fixedValue";
    case ConfidenceMethod::JosangOpinion:
        return "josangOpinion";
    }
    return "fixedValue";
}

const char* ToString(ConfidenceStatus status) {
    switch (status) {
    case ConfidenceStatus::Active:
        return "active";
    case ConfidenceStatus::Archived:
        return "archived";
    }
    return "active";
}

ConfidenceTargetKind ConfidenceTargetKindFromString(const std::string& value) {
    if (value == "relationship")
        return ConfidenceTargetKind::Relationship;
    return ConfidenceTargetKind::Element;
}

ConfidenceMethod ConfidenceMethodFromString(const std::string& value) {
    if (value == "josangOpinion")
        return ConfidenceMethod::JosangOpinion;
    return ConfidenceMethod::FixedValue;
}

ConfidenceStatus ConfidenceStatusFromString(const std::string& value) {
    if (value == "archived")
        return ConfidenceStatus::Archived;
    return ConfidenceStatus::Active;
}

double ExpectedConfidence(const JosangOpinion& opinion) {
    return opinion.belief + opinion.baseRate * opinion.uncertainty;
}

double ExpectedConfidence(const FixedConfidenceValue& value) {
    return value.value;
}

ConfidenceDerived DerivedFor(const JosangOpinion& opinion) {
    ConfidenceDerived derived;
    derived.expectedConfidence = ExpectedConfidence(opinion);
    derived.calculation = "belief + baseRate * uncertainty";
    derived.calculationVersion = 1;
    return derived;
}

ConfidenceDerived DerivedFor(const FixedConfidenceValue& value) {
    ConfidenceDerived derived;
    derived.expectedConfidence = ExpectedConfidence(value);
    derived.calculation = "fixedValue";
    derived.calculationVersion = 1;
    return derived;
}

bool ValidateJosangOpinion(const JosangOpinion& opinion, std::string& error) {
    if (!InUnitRange(opinion.belief) || !InUnitRange(opinion.disbelief) || !InUnitRange(opinion.uncertainty) ||
        !InUnitRange(opinion.baseRate)) {
        error = "Jøsang opinion values must be between 0 and 1.";
        return false;
    }
    const double sum = opinion.belief + opinion.disbelief + opinion.uncertainty;
    if (std::fabs(sum - 1.0) > kTolerance) {
        error = "Jøsang belief, disbelief, and uncertainty must sum to 1.";
        return false;
    }
    error.clear();
    return true;
}

bool ValidateFixedConfidenceValue(const FixedConfidenceValue& value, std::string& error) {
    if (!InUnitRange(value.value)) {
        error = "Fixed confidence value must be between 0 and 1.";
        return false;
    }
    error.clear();
    return true;
}

bool ValidateAssessment(const ConfidenceAssessment& assessment, std::string& error) {
    if (assessment.id.empty()) {
        error = "Confidence assessment is missing an id.";
        return false;
    }
    if (assessment.target.kind == ConfidenceTargetKind::Element && assessment.target.sacmGid.empty()) {
        error = "Element confidence assessment is missing a SACM gid.";
        return false;
    }
    if (assessment.method == ConfidenceMethod::FixedValue) {
        if (!assessment.fixedValue.has_value()) {
            error = "Fixed confidence assessment is missing fixedValue.";
            return false;
        }
        return ValidateFixedConfidenceValue(assessment.fixedValue.value(), error);
    }
    if (!assessment.josangOpinion.has_value()) {
        error = "Jøsang confidence assessment is missing josangOpinion.";
        return false;
    }
    return ValidateJosangOpinion(assessment.josangOpinion.value(), error);
}

std::string SerializeConfidenceStore(const ConfidenceStore& store) {
    nlohmann::json root;
    root["schema"] = kSchema;
    root["schemaVersion"] = kSchemaVersion;
    root["projectId"] = store.projectId;
    root["sacmSources"] = nlohmann::json::array();
    for (const SacmSource& source : store.sacmSources)
        root["sacmSources"].push_back(ToJson(source));
    root["assessments"] = nlohmann::json::array();
    for (const ConfidenceAssessment& assessment : store.assessments)
        root["assessments"].push_back(ToJson(assessment));
    return root.dump(2) + "\n";
}

bool DeserializeConfidenceStore(const std::string& content, ConfidenceStore& store, std::string& error) {
    store = ConfidenceStore{};
    error.clear();
    try {
        nlohmann::json root = nlohmann::json::parse(content);
        if (!root.is_object()) {
            error = "Confidence file root is not an object.";
            return false;
        }
        if (root.value("schema", "") != kSchema) {
            error = "Confidence file has an unsupported schema.";
            return false;
        }
        if (root.value("schemaVersion", 0) != kSchemaVersion) {
            error = "Confidence file has an unsupported schema version.";
            return false;
        }
        store.schema = kSchema;
        store.schemaVersion = kSchemaVersion;
        store.projectId = root.value("projectId", "");

        const nlohmann::json sources = root.value("sacmSources", nlohmann::json::array());
        if (!sources.is_array()) {
            error = "Confidence file sacmSources field is not an array.";
            return false;
        }
        for (const auto& source : sources) {
            if (source.is_object())
                store.sacmSources.push_back(SacmSourceFromJson(source));
        }

        const nlohmann::json assessments = root.value("assessments", nlohmann::json::array());
        if (!assessments.is_array()) {
            error = "Confidence file assessments field is not an array.";
            return false;
        }
        for (const auto& entry : assessments) {
            ConfidenceAssessment assessment;
            std::string assessment_error;
            if (!AssessmentFromJson(entry, assessment, assessment_error)) {
                error = assessment_error;
                return false;
            }
            store.assessments.push_back(std::move(assessment));
        }
    } catch (const nlohmann::json::exception& e) {
        error = std::string("Confidence JSON parse failed: ") + e.what();
        return false;
    }
    return true;
}

std::string DisplaySacmType(const parser::SacmElement& element) {
    const std::string type = ToLower(element.type);
    if (type == "claim")
        return "Claim";
    if (type == "argumentreasoning")
        return "ArgumentReasoning";
    if (type == "artifactreference")
        return "ArtifactReference";
    if (type == "artifact")
        return "Artifact";
    if (type == "assertedinference")
        return "AssertedInference";
    if (type == "assertedcontext")
        return "AssertedContext";
    if (type == "assertedevidence")
        return "AssertedEvidence";
    return element.type;
}

std::string FingerprintElement(const parser::SacmElement& element) {
    std::ostringstream normalized;
    normalized << "type=" << DisplaySacmType(element) << '\n';
    normalized << "gid=" << element.gid << '\n';
    normalized << "name=" << element.name << '\n';
    normalized << "description=" << element.description << '\n';
    normalized << "content=" << element.content << '\n';
    normalized << "assertionDeclaration=" << element.assertion_declaration << '\n';
    normalized << "undeveloped=" << (element.undeveloped ? "true" : "false") << '\n';
    AddLangMap(normalized, "name", element.name_langs);
    AddLangMap(normalized, "description", element.description_langs);
    AddLangMap(normalized, "content", element.content_langs);
    return HashNormalized(normalized.str());
}

std::string NextAssessmentId(const ConfidenceStore& store) {
    int max_id = 0;
    for (const ConfidenceAssessment& assessment : store.assessments) {
        const std::string prefix = "conf-";
        if (assessment.id.rfind(prefix, 0) != 0)
            continue;
        try {
            max_id = std::max(max_id, std::stoi(assessment.id.substr(prefix.size())));
        } catch (...) {
        }
    }
    std::ostringstream out;
    out << "conf-" << std::setw(6) << std::setfill('0') << (max_id + 1);
    return out.str();
}

const ConfidenceAssessment* FindActiveAssessment(const ConfidenceStore& store,
                                                 const std::string& source_id,
                                                 const std::string& sacm_gid) {
    for (const ConfidenceAssessment& assessment : store.assessments) {
        if (assessment.status == ConfidenceStatus::Active && assessment.target.kind == ConfidenceTargetKind::Element &&
            assessment.target.sourceId == source_id && assessment.target.sacmGid == sacm_gid) {
            return &assessment;
        }
    }
    return nullptr;
}

ConfidenceAssessment* FindActiveAssessment(ConfidenceStore& store,
                                           const std::string& source_id,
                                           const std::string& sacm_gid) {
    for (ConfidenceAssessment& assessment : store.assessments) {
        if (assessment.status == ConfidenceStatus::Active && assessment.target.kind == ConfidenceTargetKind::Element &&
            assessment.target.sourceId == source_id && assessment.target.sacmGid == sacm_gid) {
            return &assessment;
        }
    }
    return nullptr;
}

bool RemoveActiveAssessment(ConfidenceStore& store, const std::string& source_id, const std::string& sacm_gid) {
    const auto old_size = store.assessments.size();
    store.assessments.erase(std::remove_if(store.assessments.begin(),
                                           store.assessments.end(),
                                           [&](const ConfidenceAssessment& assessment) {
                                               return assessment.status == ConfidenceStatus::Active &&
                                                      assessment.target.kind == ConfidenceTargetKind::Element &&
                                                      assessment.target.sourceId == source_id &&
                                                      assessment.target.sacmGid == sacm_gid;
                                           }),
                            store.assessments.end());
    return store.assessments.size() != old_size;
}

void UpsertSacmSource(ConfidenceStore& store, SacmSource source) {
    if (source.sourceId.empty())
        source.sourceId = "main";
    auto found = std::find_if(store.sacmSources.begin(), store.sacmSources.end(), [&](const SacmSource& existing) {
        return existing.sourceId == source.sourceId;
    });
    if (found == store.sacmSources.end()) {
        store.sacmSources.push_back(std::move(source));
    } else {
        *found = std::move(source);
    }
}

bool RefreshStaleFlags(ConfidenceStore& store, const std::string& source_id, const parser::AssuranceCase& model) {
    std::unordered_map<std::string, const parser::SacmElement*> by_gid;
    for (const parser::SacmElement& element : model.elements) {
        if (!element.gid.empty())
            by_gid[element.gid] = &element;
    }

    bool changed = false;
    for (ConfidenceAssessment& assessment : store.assessments) {
        if (assessment.target.kind != ConfidenceTargetKind::Element || assessment.target.sourceId != source_id ||
            assessment.target.sacmGid.empty()) {
            continue;
        }
        const auto found = by_gid.find(assessment.target.sacmGid);
        if (found == by_gid.end())
            continue;
        const std::string current = FingerprintElement(*found->second);
        const bool stale = !assessment.targetFingerprint.empty() && assessment.targetFingerprint != current;
        if (assessment.stale != stale) {
            assessment.stale = stale;
            changed = true;
        }
        if (assessment.target.sacmType.empty())
            assessment.target.sacmType = DisplaySacmType(*found->second);
    }
    return changed;
}

} // namespace core::confidence