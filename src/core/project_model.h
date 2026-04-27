#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace core {

enum class ProjectFileRole {
    SacmArgument,
    EvidenceRegister,
    J3377CaeRegister,
    ConformanceSheet,
    ExportedReport,
    Unknown
};

enum class ProjectFileState {
    Clean,
    ModifiedOutsideAssuranceForge,
    ModifiedButCompatible,
    ModifiedWithBrokenReferences,
    Missing,
    Moved,
    ParseError,
    UnsupportedVersion,
    GeneratedFileOutdated
};

struct ProjectFileEntry {
    std::string id;
    std::filesystem::path relativePath;
    ProjectFileRole role = ProjectFileRole::Unknown;
    ProjectFileState state = ProjectFileState::Clean;

    std::string hashAlgorithm = "sha256";
    std::string rawHash;
    std::string semanticHash;
    std::string elementIndexHash;
    std::string relationshipGraphHash;

    std::string parseStatus = "notParsed";
    std::string lastError;
};

struct AssuranceProject {
    std::string id;
    std::string name;
    std::string description;
    std::string formatVersion = "0.1.0";

    std::string createdUtc;
    std::string modifiedUtc;
    std::string createdWith = "Assurance Forge";
    std::string lastOpenedWith = "Assurance Forge";
    std::string defaultLanguage = "en";
    std::string validationMode = "permissive";

    std::filesystem::path rootPath;
    std::vector<ProjectFileEntry> files;
};

enum class ProjectLoadStepStatus {
    Passed,
    Failed,
    Warning
};

struct ProjectLoadStep {
    std::string label;
    ProjectLoadStepStatus status = ProjectLoadStepStatus::Passed;
    std::string message;
};

struct ProjectLoadReport {
    std::vector<ProjectLoadStep> steps;
    std::vector<std::string> warnings;
    bool showPopup = false;

    bool has_failures() const;
};

const char* ProjectFileRoleToString(ProjectFileRole role);
const char* ProjectFileRoleToDisplayString(ProjectFileRole role);
ProjectFileRole ProjectFileRoleFromString(const std::string& value);

const char* ProjectFileStateToString(ProjectFileState state);
const char* ProjectFileStateToDisplayString(ProjectFileState state);

}  // namespace core