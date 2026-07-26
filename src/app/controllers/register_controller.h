#pragma once

// Owns the CSE / Evidence register assessments for the open project and their
// one file on disk.
//
// The register *structure* is derived from the argument every frame and needs no
// storage. The assessment fields (owners, criteria, status, notes) are the
// reviewer's own judgement: they exist nowhere else, so this is the only thing
// standing between them and the next app close.

#include "app/app_events.h"
#include "core/project_model.h"
#include "core/registers/register_model.h"

#include <filesystem>
#include <string>

namespace app::controllers {

class RegisterController {
public:
    explicit RegisterController(AppEvents& events);

    // Points the controller at `register_path` and loads it when it exists. A
    // file that exists but does not parse is reported and remembered as a
    // storage error; the in-memory store is left empty and saving is refused
    // until the file is recovered, so a load failure cannot overwrite whatever
    // is on disk.
    bool ConfigureStorage(const std::filesystem::path& register_path, std::string& error);
    void ClearStorage();
    bool SaveIfDirty(core::AssuranceProject& project, std::string& error);

    bool IsDirty() const;
    void ClearDirty();
    void MarkDirty();

    const std::filesystem::path& FilePath() const;
    const std::string& StorageError() const;
    bool HasStorageError() const;

    const core::registers::RegisterStore& Store() const;
    // The store the register tables edit in place. Callers must MarkDirty()
    // after an edit; the panel is the only thing that knows whether the user
    // actually changed a cell this frame.
    core::registers::RegisterStore& MutableStore();

    // Drops an assessment the user has decided to let go of — the human
    // decision core::registers::FindOrphanedMetadata refuses to make on its own.
    // Returns false when there was nothing stored under that key. Takes effect
    // on disk at the next project save, so closing without saving restores it.
    bool DiscardCseAssessment(const std::string& cse_id);
    bool DiscardEvidenceAssessment(const std::string& evidence_id);

private:
    AppEvents& events_;
    std::filesystem::path file_path_;
    core::registers::RegisterStore store_;
    std::string storage_error_;
    bool dirty_ = false;
};

} // namespace app::controllers
