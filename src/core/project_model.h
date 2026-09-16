#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace core {

enum class ProjectFileRole {
    SacmArgument,
    EvidenceRegister,
    J3377CaeRegister,
    // What reviewers typed into the CSE and Evidence registers (owners,
    // criteria, assessment status, notes). Distinct from the two register
    // *documents* above: those are register files a user asked for, this is the
    // single project-wide store of assessments keyed by CSE / evidence id.
    RegisterAssessments,
    ReviewItems,
    ReviewProposal,
    ConfidenceAssessments,
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

enum class ProjectLoadStepStatus { Passed, Failed, Warning };

struct ProjectLoadStep {
    std::string label;
    ProjectLoadStepStatus status = ProjectLoadStepStatus::Passed;
    std::string message;
};

// A tracked file whose bytes no longer match the hash `af.proj` records for it.
// Both hashes are kept because the refresh overwrites the recorded one in memory,
// and "this exact change" is what an acknowledgement has to name (#402).
struct ExternalFileChange {
    std::filesystem::path relativePath;
    std::string recordedRawHash;
    std::string observedRawHash;
    // Already recorded as seen on this computer, so not reported as a warning.
    bool acknowledged = false;
};

struct ProjectLoadReport {
    std::vector<ProjectLoadStep> steps;
    std::vector<std::string> warnings;
    // Every external change found, acknowledged or not. `warnings` carries only
    // the unacknowledged ones.
    std::vector<ExternalFileChange> externalChanges;
    bool showPopup = false;

    bool has_failures() const;
};

const char* ProjectFileRoleToString(ProjectFileRole role);
const char* ProjectFileRoleToDisplayString(ProjectFileRole role);
ProjectFileRole ProjectFileRoleFromString(const std::string& value);

const char* ProjectFileStateToString(ProjectFileState state);
const char* ProjectFileStateToDisplayString(ProjectFileState state);

} // namespace core