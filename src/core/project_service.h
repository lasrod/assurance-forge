#pragma once

#include "core/project_model.h"

#include <filesystem>
#include <string>

namespace core {

class ProjectService {
public:
    static bool CreateEmptyProject(const std::string& project_name,
                                   const std::filesystem::path& parent_location,
                                   AssuranceProject& project,
                                   ProjectLoadReport& report,
                                   std::string& error);

    static bool OpenProject(const std::filesystem::path& project_or_manifest_path,
                            AssuranceProject& project,
                            ProjectLoadReport& report,
                            std::string& error);

    static bool AddSacmFile(AssuranceProject& project,
                            const std::string& requested_file_name,
                            ProjectFileEntry& entry,
                            std::string& error);

    static bool AddEvidenceRegister(AssuranceProject& project,
                                    const std::string& requested_file_name,
                                    ProjectFileEntry& entry,
                                    std::string& error);

    static bool AddJ3377CaeRegister(AssuranceProject& project,
                                    const std::string& requested_file_name,
                                    ProjectFileEntry& entry,
                                    std::string& error);

    static bool WriteManifestSafely(const AssuranceProject& project, std::string& error);
    static std::filesystem::path ManifestPath(const AssuranceProject& project);
    static ProjectLoadReport RefreshFileStatus(AssuranceProject& project);
};

}  // namespace core