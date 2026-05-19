#pragma once

#include "app/app_events.h"
#include "core/confidence/confidence_store.h"
#include "core/project_model.h"
#include "parser/xml_parser.h"
#include "ui/confidence_model.h"

#include <filesystem>
#include <optional>
#include <string>

namespace app::controllers {

class ConfidenceController {
public:
    explicit ConfidenceController(AppEvents& events);

    bool
    ConfigureStorage(const std::filesystem::path& confidence_path, const std::string& project_id, std::string& error);
    void ClearStorage();
    bool SaveIfDirty(core::AssuranceProject& project, std::string& error);
    bool BackupInvalidAndStartNew(std::string& error);

    bool IsDirty() const;
    void ClearDirty();
    void MarkDirty();

    const std::filesystem::path& FilePath() const;
    const std::string& StorageError() const;
    bool HasStorageError() const;
    const core::confidence::ConfidenceStore& Store() const;

    void SetActiveSource(std::string source_id, std::string path, std::string file_hash = {});
    const std::string& ActiveSourceId() const;
    bool RefreshStaleFlags(const parser::AssuranceCase& model);
    int LastInactivatedCount() const;

    const core::confidence::ConfidenceAssessment* FindForElement(const parser::SacmElement& element) const;
    std::optional<ui::ElementConfidence> ConfidenceForElement(const parser::SacmElement& element) const;
    bool
    UpsertElementConfidence(const parser::SacmElement& element, ui::ElementConfidence confidence, std::string& error);
    bool SetElementConfidenceActive(const parser::SacmElement& element, bool active, std::string& error);
    bool MarkElementReviewed(const parser::SacmElement& element, std::string& error);

private:
    AppEvents& events_;
    std::filesystem::path file_path_;
    core::confidence::ConfidenceStore store_;
    core::confidence::SacmSource active_source_;
    std::string storage_error_;
    bool dirty_ = false;
    bool persistence_dirty_ = false;
    int last_inactivated_count_ = 0;
};

} // namespace app::controllers