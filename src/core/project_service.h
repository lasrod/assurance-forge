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

    static bool AddReviewItemsFile(AssuranceProject& project,
                                   const std::string& requested_file_name,
                                   ProjectFileEntry& entry,
                                   std::string& error);

    static bool SaveReviewItemsFile(AssuranceProject& project,
                                    const std::string& requested_file_name,
                                    const std::string& content,
                                    ProjectFileEntry& entry,
                                    std::string& error);

    static bool AddReviewProposalFile(AssuranceProject& project,
                                      const std::string& requested_file_name,
                                      const std::string& content,
                                      ProjectFileEntry& entry,
                                      std::string& error);

    static bool SaveReviewProposalFile(AssuranceProject& project,
                                       const std::string& requested_file_name,
                                       const std::string& content,
                                       ProjectFileEntry& entry,
                                       std::string& error);

    static bool TrackExistingFile(AssuranceProject& project,
                                  const std::filesystem::path& relative_path,
                                  ProjectFileRole role,
                                  ProjectFileEntry& entry,
                                  std::string& error);

    static bool RemoveTrackedFile(AssuranceProject& project,
                                  const std::filesystem::path& relative_path,
                                  bool delete_file,
                                  std::string& error);

    static bool WriteManifestSafely(const AssuranceProject& project, std::string& error);
    static std::filesystem::path ManifestPath(const AssuranceProject& project);
    static ProjectLoadReport RefreshFileStatus(AssuranceProject& project);
};

} // namespace core