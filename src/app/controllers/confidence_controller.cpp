#include "app/controllers/confidence_controller.h"

#include "core/project_service.h"
#include "core/time_utils.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace app::controllers {
namespace {

std::string ReadTextFile(const std::filesystem::path& path, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        error = "Could not open " + path.string();
        return {};
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (!file.good() && !file.eof()) {
        error = "Could not read " + path.string();
        return {};
    }
    return buffer.str();
}

std::string BackupTimestamp() {
    std::string timestamp = core::NowUtcString();
    timestamp.erase(std::remove(timestamp.begin(), timestamp.end(), '-'), timestamp.end());
    timestamp.erase(std::remove(timestamp.begin(), timestamp.end(), ':'), timestamp.end());
    timestamp.erase(std::remove(timestamp.begin(), timestamp.end(), 'T'), timestamp.end());
    timestamp.erase(std::remove(timestamp.begin(), timestamp.end(), 'Z'), timestamp.end());
    return timestamp;
}

std::string SourceIdOrMain(const std::string& source_id) {
    return source_id.empty() ? "main" : source_id;
}

ui::ElementConfidence ToUiConfidence(const core::confidence::ConfidenceAssessment& assessment) {
    ui::ElementConfidence confidence;
    confidence.enabled = assessment.status == core::confidence::ConfidenceStatus::Active;
    if (assessment.method == core::confidence::ConfidenceMethod::JosangOpinion && assessment.josangOpinion.has_value()) {
        confidence.mode = ui::ConfidenceInputMode::OpinionTriangle;
        confidence.opinion.belief = static_cast<float>(assessment.josangOpinion->belief);
        confidence.opinion.disbelief = static_cast<float>(assessment.josangOpinion->disbelief);
        confidence.opinion.uncertainty = static_cast<float>(assessment.josangOpinion->uncertainty);
        confidence.opinion.base_rate = static_cast<float>(assessment.josangOpinion->baseRate);
        ui::NormalizeOpinion(confidence.opinion);
    } else if (assessment.fixedValue.has_value()) {
        confidence.mode = ui::ConfidenceInputMode::DirectValue;
        confidence.direct_value = static_cast<float>(assessment.fixedValue->value);
    }
    return confidence;
}

core::confidence::JosangOpinion ToCoreOpinion(ui::SubjectiveOpinion opinion) {
    ui::NormalizeOpinion(opinion);
    core::confidence::JosangOpinion result;
    result.belief = opinion.belief;
    result.disbelief = opinion.disbelief;
    result.uncertainty = opinion.uncertainty;
    result.baseRate = opinion.base_rate;
    return result;
}

core::confidence::FixedConfidenceValue ToCoreFixed(float value) {
    core::confidence::FixedConfidenceValue fixed;
    fixed.value = ui::ClampConfidenceValue(value);
    return fixed;
}

} // namespace

ConfidenceController::ConfidenceController(AppEvents& events) : events_(events) {
    active_source_.sourceId = "main";
}

bool ConfidenceController::ConfigureStorage(const std::filesystem::path& confidence_path,
                                            const std::string& project_id,
                                            std::string& error) {
    error.clear();
    file_path_ = confidence_path;
    store_ = core::confidence::ConfidenceStore{};
    store_.projectId = project_id;
    storage_error_.clear();
    dirty_ = false;

    if (file_path_.empty())
        return true;

    std::error_code ec;
    if (!std::filesystem::exists(file_path_, ec))
        return true;

    std::string read_error;
    std::string content = ReadTextFile(file_path_, read_error);
    if (!read_error.empty()) {
        storage_error_ = read_error;
        error = read_error;
        return false;
    }

    if (!core::confidence::DeserializeConfidenceStore(content, store_, error)) {
        storage_error_ = error;
        store_ = core::confidence::ConfidenceStore{};
        store_.projectId = project_id;
        return false;
    }
    if (store_.projectId.empty())
        store_.projectId = project_id;
    return true;
}

void ConfidenceController::ClearStorage() {
    file_path_.clear();
    store_ = core::confidence::ConfidenceStore{};
    storage_error_.clear();
    dirty_ = false;
    active_source_ = core::confidence::SacmSource{};
    active_source_.sourceId = "main";
}

bool ConfidenceController::SaveIfDirty(core::AssuranceProject& project, std::string& error) {
    if (!dirty_)
        return true;
    if (!storage_error_.empty()) {
        error = "confidence storage has not been recovered: " + storage_error_;
        return false;
    }
    if (store_.projectId.empty())
        store_.projectId = project.id;
    if (!active_source_.sourceId.empty())
        core::confidence::UpsertSacmSource(store_, active_source_);

    core::ProjectFileEntry entry;
    if (!core::ProjectService::SaveConfidenceFile(project, core::confidence::SerializeConfidenceStore(store_), entry, error))
        return false;

    file_path_ = project.rootPath / entry.relativePath;
    dirty_ = false;
    return true;
}

bool ConfidenceController::BackupInvalidAndStartNew(std::string& error) {
    if (file_path_.empty()) {
        error = "confidence storage path is not set.";
        return false;
    }
    std::error_code ec;
    if (std::filesystem::exists(file_path_, ec)) {
        const std::filesystem::path backup_path = file_path_.parent_path() /
            ("confidence.af.invalid-" + BackupTimestamp() + ".json");
        std::filesystem::copy_file(file_path_, backup_path, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "Could not back up invalid confidence file: " + ec.message();
            return false;
        }
    }
    store_ = core::confidence::ConfidenceStore{};
    storage_error_.clear();
    dirty_ = true;
    events_.Emit(ConfidenceDirtyEvent{});
    return true;
}

bool ConfidenceController::IsDirty() const {
    return dirty_;
}

void ConfidenceController::ClearDirty() {
    dirty_ = false;
}

void ConfidenceController::MarkDirty() {
    dirty_ = true;
    events_.Emit(ConfidenceDirtyEvent{});
}

const std::filesystem::path& ConfidenceController::FilePath() const {
    return file_path_;
}

const std::string& ConfidenceController::StorageError() const {
    return storage_error_;
}

bool ConfidenceController::HasStorageError() const {
    return !storage_error_.empty();
}

void ConfidenceController::SetActiveSource(std::string source_id, std::string path, std::string file_hash) {
    active_source_.sourceId = SourceIdOrMain(source_id);
    active_source_.path = std::move(path);
    active_source_.fileHash = std::move(file_hash);
}

const std::string& ConfidenceController::ActiveSourceId() const {
    return active_source_.sourceId;
}

bool ConfidenceController::RefreshStaleFlags(const parser::AssuranceCase& model) {
    if (storage_error_.empty() && core::confidence::RefreshStaleFlags(store_, active_source_.sourceId, model)) {
        MarkDirty();
        return true;
    }
    return false;
}

const core::confidence::ConfidenceAssessment* ConfidenceController::FindForElement(
    const parser::SacmElement& element) const {
    if (element.gid.empty() || !storage_error_.empty())
        return nullptr;
    return core::confidence::FindActiveAssessment(store_, active_source_.sourceId, element.gid);
}

std::optional<ui::ElementConfidence> ConfidenceController::ConfidenceForElement(const parser::SacmElement& element) const {
    const core::confidence::ConfidenceAssessment* assessment = FindForElement(element);
    if (!assessment)
        return std::nullopt;
    return ToUiConfidence(*assessment);
}

bool ConfidenceController::UpsertElementConfidence(const parser::SacmElement& element,
                                                   ui::ElementConfidence confidence,
                                                   std::string& error) {
    if (!storage_error_.empty()) {
        error = "confidence storage has not been recovered: " + storage_error_;
        return false;
    }
    if (element.gid.empty()) {
        error = "selected element does not have a SACM gid.";
        return false;
    }

    core::confidence::ConfidenceAssessment* assessment =
        core::confidence::FindActiveAssessment(store_, active_source_.sourceId, element.gid);
    const std::string now = core::NowUtcString();
    if (!assessment) {
        core::confidence::ConfidenceAssessment created;
        created.id = core::confidence::NextAssessmentId(store_);
        created.createdAt = now;
        created.target.kind = core::confidence::ConfidenceTargetKind::Element;
        created.target.sourceId = active_source_.sourceId;
        created.target.sacmGid = element.gid;
        store_.assessments.push_back(std::move(created));
        assessment = &store_.assessments.back();
    }

    assessment->target.sacmType = core::confidence::DisplaySacmType(element);
    assessment->targetFingerprint = core::confidence::FingerprintElement(element);
    assessment->stale = false;
    assessment->status = core::confidence::ConfidenceStatus::Active;
    assessment->updatedAt = now;
    if (assessment->createdAt.empty())
        assessment->createdAt = now;

    confidence.enabled = true;
    if (confidence.mode == ui::ConfidenceInputMode::OpinionTriangle) {
        const core::confidence::JosangOpinion opinion = ToCoreOpinion(confidence.opinion);
        if (!core::confidence::ValidateJosangOpinion(opinion, error))
            return false;
        assessment->method = core::confidence::ConfidenceMethod::JosangOpinion;
        assessment->josangOpinion = opinion;
        assessment->fixedValue.reset();
        assessment->derived = core::confidence::DerivedFor(opinion);
    } else {
        const core::confidence::FixedConfidenceValue fixed = ToCoreFixed(confidence.direct_value);
        if (!core::confidence::ValidateFixedConfidenceValue(fixed, error))
            return false;
        assessment->method = core::confidence::ConfidenceMethod::FixedValue;
        assessment->fixedValue = fixed;
        assessment->josangOpinion.reset();
        assessment->derived = core::confidence::DerivedFor(fixed);
    }
    core::confidence::UpsertSacmSource(store_, active_source_);
    MarkDirty();
    return true;
}

bool ConfidenceController::ClearElementConfidence(const parser::SacmElement& element, std::string& error) {
    error.clear();
    if (!storage_error_.empty()) {
        error = "confidence storage has not been recovered: " + storage_error_;
        return false;
    }
    if (element.gid.empty())
        return true;
    if (core::confidence::RemoveActiveAssessment(store_, active_source_.sourceId, element.gid))
        MarkDirty();
    return true;
}

bool ConfidenceController::MarkElementReviewed(const parser::SacmElement& element, std::string& error) {
    error.clear();
    if (!storage_error_.empty()) {
        error = "confidence storage has not been recovered: " + storage_error_;
        return false;
    }
    if (element.gid.empty()) {
        error = "selected element does not have a SACM gid.";
        return false;
    }
    core::confidence::ConfidenceAssessment* assessment =
        core::confidence::FindActiveAssessment(store_, active_source_.sourceId, element.gid);
    if (!assessment)
        return true;
    assessment->targetFingerprint = core::confidence::FingerprintElement(element);
    assessment->target.sacmType = core::confidence::DisplaySacmType(element);
    assessment->stale = false;
    assessment->updatedAt = core::NowUtcString();
    MarkDirty();
    return true;
}

} // namespace app::controllers