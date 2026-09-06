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

    // Creates a project whose first argument is a copy of `source_sacm_path`,
    // an existing SACM XML file, instead of the empty seed. The copy keeps the
    // source's file name (with a `.sacm` extension) under `arguments/`; the
    // source is never touched. The source is read through the SACM library
    // BEFORE anything is scaffolded, so a file that is not an assurance case
    // refuses without leaving an empty project folder behind.
    static bool CreateProjectFromSacm(const std::string& project_name,
                                      const std::filesystem::path& parent_location,
                                      const std::filesystem::path& source_sacm_path,
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

    // Copies an existing SACM XML file into `arguments/` and tracks it as a
    // SacmArgument. `requested_file_name` empty means the source's own file
    // name; the extension is normalized to `.sacm` either way. The bytes are
    // copied as they are -- the file is the argument, and an import that
    // rewrote it would be a silent edit -- but the source must load through
    // the SACM library first, so the project never tracks an argument it
    // cannot open. A name already tracked is refused, not overwritten.
    static bool ImportSacmFile(AssuranceProject& project,
                               const std::filesystem::path& source_sacm_path,
                               const std::string& requested_file_name,
                               ProjectFileEntry& entry,
                               std::string& error);

    // The file name `ImportSacmFile` uses for `source_sacm_path` when none is
    // requested, so a dialog can offer it before the import is made.
    static std::filesystem::path DefaultImportedSacmFileName(const std::filesystem::path& source_sacm_path);

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