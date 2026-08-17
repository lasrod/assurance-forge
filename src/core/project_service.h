#pragma once

#include "core/project_model.h"

#include <filesystem>
#include <string>

namespace core {

// Why a project cannot be created at `parent_location / project_name`.
//
// Reported as a reason rather than a message because `core` holds no display
// text (the UI layer owns translation). The create refuses on these and the
// create dialog asks the same question while the user types, so what the button
// does and what the dialog says cannot drift apart -- a press that could only
// fail is refused before it is made instead of appearing to do nothing.
enum class CreateProjectObstacle {
    None,
    NameRequired,
    LocationRequired,
    // A file or directory is already at that path. Creating over it would put a
    // new project's manifest beside another project's files.
    FolderExists,
};

class ProjectService {
public:
    // The obstacle to creating this project, or `None` when there is none.
    // Cheap enough to ask while the user types, and the single place the rule
    // lives.
    static CreateProjectObstacle FindCreateProjectObstacle(const std::string& project_name,
                                                           const std::filesystem::path& parent_location);

    // The path `CreateEmptyProject` would use, so a caller can name it in a
    // message without reimplementing the trim.
    static std::filesystem::path PlanProjectRoot(const std::string& project_name,
                                                 const std::filesystem::path& parent_location);

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

    static bool SaveConfidenceFile(AssuranceProject& project,
                                   const std::string& content,
                                   ProjectFileEntry& entry,
                                   std::string& error);

    // Writes the CSE / Evidence register assessments to
    // registers/register-assessments.af.json and tracks it in the manifest.
    static bool SaveRegisterAssessmentsFile(AssuranceProject& project,
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