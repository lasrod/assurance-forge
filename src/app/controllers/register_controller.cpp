#include "app/controllers/register_controller.h"

#include "core/project_file_io.h"
#include "core/project_service.h"

#include <expected>
#include <filesystem>
#include <system_error>

namespace app::controllers {

RegisterController::RegisterController(AppEvents& events) : events_(events) {}

bool RegisterController::ConfigureStorage(const std::filesystem::path& register_path, std::string& error) {
    error.clear();

    // Re-configuring the same path while edits are pending would discard them:
    // this runs on every project-open and project-create path, including ones
    // reached while a register tab is open.
    if (dirty_ && !file_path_.empty() && file_path_ == register_path)
        return true;

    file_path_ = register_path;
    store_ = core::registers::RegisterStore{};
    storage_error_.clear();
    dirty_ = false;

    if (file_path_.empty())
        return true;

    std::error_code ec;
    if (!std::filesystem::exists(file_path_, ec))
        return true;

    std::expected<std::string, std::string> content = core::ReadTextFile(file_path_);
    if (!content) {
        storage_error_ = content.error();
        error = content.error();
        return false;
    }

    if (!core::registers::DeserializeRegisterStore(*content, store_, error)) {
        storage_error_ = error;
        store_ = core::registers::RegisterStore{};
        return false;
    }
    return true;
}

void RegisterController::ClearStorage() {
    file_path_.clear();
    store_ = core::registers::RegisterStore{};
    storage_error_.clear();
    dirty_ = false;
}

bool RegisterController::SaveIfDirty(core::AssuranceProject& project, std::string& error) {
    if (!dirty_)
        return true;
    if (!storage_error_.empty()) {
        // The file on disk holds assessments we could not read. Writing the
        // (empty) in-memory store over it would delete them for good.
        error = "register assessment storage has not been recovered: " + storage_error_;
        return false;
    }

    core::ProjectFileEntry entry;
    if (!core::ProjectService::SaveRegisterAssessmentsFile(
            project, core::registers::SerializeRegisterStore(store_), entry, error))
        return false;

    file_path_ = project.rootPath / entry.relativePath;
    dirty_ = false;
    return true;
}

bool RegisterController::IsDirty() const {
    return dirty_;
}

void RegisterController::ClearDirty() {
    dirty_ = false;
}

void RegisterController::MarkDirty() {
    dirty_ = true;
    events_.Emit(RegisterAssessmentsDirtyEvent{});
}

const std::filesystem::path& RegisterController::FilePath() const {
    return file_path_;
}

const std::string& RegisterController::StorageError() const {
    return storage_error_;
}

bool RegisterController::HasStorageError() const {
    return !storage_error_.empty();
}

const core::registers::RegisterStore& RegisterController::Store() const {
    return store_;
}

core::registers::RegisterStore& RegisterController::MutableStore() {
    return store_;
}

bool RegisterController::DiscardCseAssessment(const std::string& cse_id) {
    if (store_.cse.erase(cse_id) == 0)
        return false;
    MarkDirty();
    return true;
}

bool RegisterController::DiscardEvidenceAssessment(const std::string& evidence_id) {
    if (store_.evidence.erase(evidence_id) == 0)
        return false;
    MarkDirty();
    return true;
}

} // namespace app::controllers
