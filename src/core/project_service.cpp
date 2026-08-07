#include "core/project_service.h"

#include "core/project_file_io.h"
#include "core/project_manifest.h"
#include "core/reviews/review_item.h"
#include "core/sha256.h"
#include "core/string_utils.h"
#include "parser/xml_parser.h"
#include "sacm_adapter/library_load.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <unordered_set>
#include <utility>
#include <vector>

namespace core {
namespace {

// The format name and version constants live in project_manifest.cpp, which is
// what reads and validates them. The copies here were never referenced.
constexpr const char* kManifestFileName = "af.proj";

const std::array<const char*, 11> kProjectDirectories = {
    "arguments",
    "registers",
    "reviews",
    "reviews/proposals",
    "conformance",
    "exports",
    ".af/cache",
    ".af/backups",
    ".af/snapshots",
    ".af/history",
    ".af/drafts",
};

// `.af/` holds machine-local working state: caches, backups, snapshots, replay
// history, and -- since ADR 0010 -- unaccepted draft work.
//
// None of it is accepted assurance content and none of it should reach a
// colleague through version control. It always could: the scaffold has created
// these directories from the beginning and never wrote an ignore file, so a
// project under git has been carrying its own caches and backups all along. That
// was untidy. With drafts in there it would mean committing AI-authored claims
// that no human has accepted, which is a different order of problem, and ADR
// 0008 refused to put transient AI state in the project directory for exactly
// this reason.
//
// Written on create *and* on open, so existing projects are covered too.
constexpr const char* kInternalDirectoryIgnoreFile = R"(# Assurance Forge internal working state.
#
# Caches, backups, snapshots, replay history and unaccepted draft work. None of
# it is accepted assurance-case content, and drafts hold argument text no human
# has accepted yet. Not for version control.
*
)";

void EnsureInternalDirectoryIgnored(const std::filesystem::path& root) {
    if (root.empty())
        return;
    std::error_code ec;
    const std::filesystem::path internal = root / ".af";
    if (!std::filesystem::exists(internal, ec))
        return;
    const std::filesystem::path ignore_path = internal / ".gitignore";
    if (std::filesystem::exists(ignore_path, ec))
        return;
    // Best effort. A project on read-only media, or one whose owner deleted the
    // file deliberately, is not a reason to refuse to open it.
    (void)WriteTextFile(ignore_path, kInternalDirectoryIgnoreFile);
}

bool MakeUtcTime(std::time_t time, std::tm& utc) {
#if defined(_WIN32)
    return gmtime_s(&utc, &time) == 0;
#else
    return gmtime_r(&time, &utc) != nullptr;
#endif
}

std::string NowUtc() {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    if (!MakeUtcTime(time, utc)) {
        return "1970-01-01T00:00:00Z";
    }
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string BackupTimestampUtc() {
    auto timestamp = NowUtc();
    timestamp.erase(std::remove(timestamp.begin(), timestamp.end(), '-'), timestamp.end());
    timestamp.erase(std::remove(timestamp.begin(), timestamp.end(), ':'), timestamp.end());
    return timestamp;
}

std::string GenerateId(const char* prefix) {
    static std::mt19937_64 rng{std::random_device{}()};
    static unsigned long long counter = 0;
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    std::ostringstream out;
    out << prefix << "-" << std::hex << now << "-" << ++counter << "-" << rng();
    return out.str();
}

std::filesystem::path
NormalizeFileName(const std::string& requested_file_name, const char* default_name, const char* extension) {
    std::string trimmed = TrimWhitespace(requested_file_name);
    if (trimmed.empty())
        trimmed = default_name;
    std::filesystem::path name(trimmed);
    std::string actual_extension = name.extension().string();
    std::string expected_extension = extension;
    actual_extension = ToLower(std::move(actual_extension));
    expected_extension = ToLower(std::move(expected_extension));
    if (actual_extension != expected_extension) {
        name += extension;
    }
    return name.filename();
}

std::filesystem::path NormalizeProposalPatchName(const std::string& requested_file_name) {
    std::string trimmed = TrimWhitespace(requested_file_name);
    if (trimmed.empty())
        trimmed = "proposal-0001.afpatch.json";
    const std::string suffix = ".afpatch.json";
    bool has_suffix = false;
    if (trimmed.size() >= suffix.size()) {
        std::string actual_tail = ToLower(trimmed.substr(trimmed.size() - suffix.size()));
        has_suffix = (actual_tail == suffix);
    }
    if (!has_suffix) {
        trimmed += suffix;
    }
    return std::filesystem::path(trimmed).filename();
}

std::string ReviewItemsJson() {
    return reviews::SerializeReviewItems({});
}

// The seed for a new SACM file, built through the library and serialized in
// strict mode so a new project starts as conformant SACM 2.3 XMI.
//
// This used to be a hand-written string in the SACM *2.2* namespace using `id=`
// attributes rather than `xmi:id`. The tolerant reader accepts that dialect, so
// nothing broke visibly -- but every new project began life as a document no
// strict SACM consumer would take, and the first save silently rewrote it into
// a different dialect than the one on disk. Generating it through the same
// writer that performs every save removes both the nonconformance and the
// discontinuity.
//
// Returns an empty string if the library declines to build or serialize the
// seed; the caller reports that rather than falling back to a dialect we no
// longer intend to produce.
std::string MinimalSacmXml(const std::string& case_name) {
    const sacm_adapter::SaveOutcome seed = sacm_adapter::new_case_document_xmi(case_name);
    return seed.ok ? seed.xml : std::string{};
}

std::string EvidenceRegisterJson() {
    return "{\n"
           "  \"format\": \"assurance-forge-evidence-register\",\n"
           "  \"formatVersion\": \"0.1.0\",\n"
           "  \"entries\": []\n"
           "}\n";
}

std::string J3377RegisterJson() {
    return "{\n"
           "  \"format\": \"assurance-forge-j3377-cae-register\",\n"
           "  \"formatVersion\": \"0.1.0\",\n"
           "  \"entries\": []\n"
           "}\n";
}

bool AddTrackedFile(AssuranceProject& project,
                    const std::filesystem::path& folder,
                    const std::filesystem::path& file_name,
                    ProjectFileRole role,
                    const std::string& content,
                    ProjectFileEntry& entry,
                    std::string& error) {
    std::filesystem::path relative_path = folder / file_name;
    if (!IsSafeRelativePath(relative_path)) {
        error = "Invalid project file path";
        return false;
    }

    std::filesystem::path absolute_path = project.rootPath / relative_path;
    std::error_code ec;
    if (!std::filesystem::create_directories(absolute_path.parent_path(), ec) && ec) {
        error = "Could not create project directory: " + absolute_path.parent_path().string();
        return false;
    }
    if (std::filesystem::exists(absolute_path, ec)) {
        error = "File already exists: " + relative_path.generic_string();
        return false;
    }

    if (auto r = WriteTextFile(absolute_path, content); !r) {
        error = std::move(r.error());
        return false;
    }

    ProjectFileEntry new_entry;
    new_entry.id = GenerateId("af-file");
    new_entry.relativePath = relative_path;
    new_entry.role = role;
    new_entry.hashAlgorithm = "sha256";
    if (auto r = RefreshEntryHashes(project, new_entry, false); !r) {
        error = std::move(r.error());
        std::filesystem::remove(absolute_path, ec);
        return false;
    }

    project.files.push_back(new_entry);
    project.modifiedUtc = NowUtc();

    if (!ProjectService::WriteManifestSafely(project, error)) {
        project.files.pop_back();
        std::filesystem::remove(absolute_path, ec);
        return false;
    }

    entry = new_entry;
    return true;
}

// Writes `content` to a tracked project file, adding the manifest entry the
// first time and refreshing its hashes on every write. Unlike AddTrackedFile
// this overwrites an existing file, so it is the save path for the files the
// app owns one of per project (review items, confidence assessments, register
// assessments). Shared so those saves cannot drift apart in tracking or
// hashing behaviour.
bool WriteTrackedFile(AssuranceProject& project,
                      const std::filesystem::path& relative_path,
                      ProjectFileRole role,
                      const char* role_mismatch_description,
                      const std::string& content,
                      ProjectFileEntry& entry,
                      std::string& error) {
    if (!IsSafeRelativePath(relative_path)) {
        error = "Invalid project file path";
        return false;
    }

    auto found = std::find_if(project.files.begin(), project.files.end(), [&](const ProjectFileEntry& candidate) {
        return candidate.relativePath.generic_string() == relative_path.generic_string();
    });

    // Refuse before writing when something else already owns the path: an
    // overwrite here would destroy a file of a different kind.
    if (found != project.files.end() && found->role != role) {
        error = std::string("Tracked file is not ") + role_mismatch_description + ": " + relative_path.generic_string();
        return false;
    }

    const std::filesystem::path absolute_path = project.rootPath / relative_path;
    std::error_code ec;
    if (!std::filesystem::create_directories(absolute_path.parent_path(), ec) && ec) {
        error = "Could not create project directory: " + absolute_path.parent_path().string();
        return false;
    }
    if (auto r = WriteTextFile(absolute_path, content); !r) {
        error = std::move(r.error());
        return false;
    }

    if (found == project.files.end()) {
        ProjectFileEntry new_entry;
        new_entry.id = GenerateId("af-file");
        new_entry.relativePath = relative_path;
        new_entry.role = role;
        new_entry.hashAlgorithm = "sha256";
        if (auto r = RefreshEntryHashes(project, new_entry, false); !r) {
            error = std::move(r.error());
            return false;
        }
        project.files.push_back(new_entry);
        found = project.files.end() - 1;
    } else if (auto r = RefreshEntryHashes(project, *found, false); !r) {
        error = std::move(r.error());
        return false;
    }

    project.modifiedUtc = NowUtc();
    if (!ProjectService::WriteManifestSafely(project, error))
        return false;

    entry = *found;
    return true;
}

void AddStep(ProjectLoadReport& report,
             const std::string& label,
             ProjectLoadStepStatus status,
             const std::string& message = {}) {
    report.steps.push_back(ProjectLoadStep{label, status, message});
}

} // namespace

std::filesystem::path ProjectService::ManifestPath(const AssuranceProject& project) {
    return project.rootPath / kManifestFileName;
}

bool ProjectService::CreateEmptyProject(const std::string& project_name,
                                        const std::filesystem::path& parent_location,
                                        AssuranceProject& project,
                                        ProjectLoadReport& report,
                                        std::string& error) {
    std::string clean_name = TrimWhitespace(project_name);
    if (clean_name.empty()) {
        error = "Project name is required.";
        return false;
    }
    if (parent_location.empty()) {
        error = "Project location is required.";
        return false;
    }

    std::filesystem::path root = parent_location / clean_name;
    std::error_code ec;
    if (std::filesystem::exists(root, ec)) {
        error = "Project folder already exists: " + root.string();
        return false;
    }

    for (const char* directory : kProjectDirectories) {
        if (!std::filesystem::create_directories(root / directory, ec) && ec) {
            error = "Could not create project directory: " + (root / directory).string();
            return false;
        }
    }

    EnsureInternalDirectoryIgnored(root);

    project = AssuranceProject{};
    project.id = GenerateId("af-project");
    project.name = clean_name;
    project.rootPath = root;
    project.createdUtc = NowUtc();
    project.modifiedUtc = project.createdUtc;

    if (!WriteManifestSafely(project, error))
        return false;

    ProjectFileEntry main_entry;
    if (!AddSacmFile(project, "main.sacm", main_entry, error))
        return false;

    ProjectFileEntry review_entry;
    if (!AddReviewItemsFile(project, "review-items.af.json", review_entry, error))
        return false;

    report = RefreshFileStatus(project);
    return true;
}

bool ProjectService::OpenProject(const std::filesystem::path& project_or_manifest_path,
                                 AssuranceProject& project,
                                 ProjectLoadReport& report,
                                 std::string& error) {
    std::filesystem::path manifest = project_or_manifest_path;
    std::error_code ec;
    if (std::filesystem::is_directory(manifest, ec)) {
        manifest /= kManifestFileName;
    }

    report = ProjectLoadReport{};
    auto manifest_text = ReadTextFile(manifest);
    if (!manifest_text) {
        error = std::move(manifest_text.error());
        AddStep(report, "Load af.proj", ProjectLoadStepStatus::Failed, error);
        report.showPopup = true;
        return false;
    }

    AssuranceProject loaded;
    if (!ParseManifest(*manifest_text, manifest.parent_path(), loaded, error)) {
        AddStep(report, "Load af.proj", ProjectLoadStepStatus::Failed, error);
        report.showPopup = true;
        return false;
    }
    AddStep(report, "Load af.proj", ProjectLoadStepStatus::Passed, manifest.string());

    loaded.lastOpenedWith = "Assurance Forge";
    // Covers projects created before the ignore file existed. Without this, the
    // only protected projects would be ones scaffolded by this version.
    EnsureInternalDirectoryIgnored(loaded.rootPath);
    report = RefreshFileStatus(loaded);
    report.steps.insert(report.steps.begin(),
                        ProjectLoadStep{"Load af.proj", ProjectLoadStepStatus::Passed, manifest.string()});

    project = std::move(loaded);
    return true;
}

bool ProjectService::AddSacmFile(AssuranceProject& project,
                                 const std::string& requested_file_name,
                                 ProjectFileEntry& entry,
                                 std::string& error) {
    std::filesystem::path file_name = NormalizeFileName(requested_file_name, "main.sacm", ".sacm");
    const std::string seed = MinimalSacmXml(project.name);
    if (seed.empty()) {
        error = "Could not generate a SACM 2.3 seed document";
        return false;
    }
    return AddTrackedFile(project, "arguments", file_name, ProjectFileRole::SacmArgument, seed, entry, error);
}

bool ProjectService::AddEvidenceRegister(AssuranceProject& project,
                                         const std::string& requested_file_name,
                                         ProjectFileEntry& entry,
                                         std::string& error) {
    std::filesystem::path file_name = NormalizeFileName(requested_file_name, "evidence-register.af.json", ".json");
    return AddTrackedFile(
        project, "registers", file_name, ProjectFileRole::EvidenceRegister, EvidenceRegisterJson(), entry, error);
}

bool ProjectService::AddJ3377CaeRegister(AssuranceProject& project,
                                         const std::string& requested_file_name,
                                         ProjectFileEntry& entry,
                                         std::string& error) {
    std::filesystem::path file_name = NormalizeFileName(requested_file_name, "j3377-cae-register.af.json", ".json");
    return AddTrackedFile(
        project, "registers", file_name, ProjectFileRole::J3377CaeRegister, J3377RegisterJson(), entry, error);
}

bool ProjectService::AddReviewItemsFile(AssuranceProject& project,
                                        const std::string& requested_file_name,
                                        ProjectFileEntry& entry,
                                        std::string& error) {
    std::filesystem::path file_name = NormalizeFileName(requested_file_name, "review-items.af.json", ".json");
    return AddTrackedFile(project, "reviews", file_name, ProjectFileRole::ReviewItems, ReviewItemsJson(), entry, error);
}

bool ProjectService::SaveReviewItemsFile(AssuranceProject& project,
                                         const std::string& requested_file_name,
                                         const std::string& content,
                                         ProjectFileEntry& entry,
                                         std::string& error) {
    std::filesystem::path file_name = NormalizeFileName(requested_file_name, "review-items.af.json", ".json");
    const std::filesystem::path relative_path = std::filesystem::path("reviews") / file_name;
    return WriteTrackedFile(
        project, relative_path, ProjectFileRole::ReviewItems, "a review item file", content, entry, error);
}

bool ProjectService::AddReviewProposalFile(AssuranceProject& project,
                                           const std::string& requested_file_name,
                                           const std::string& content,
                                           ProjectFileEntry& entry,
                                           std::string& error) {
    std::filesystem::path file_name = NormalizeProposalPatchName(requested_file_name);
    return AddTrackedFile(
        project, "reviews/proposals", file_name, ProjectFileRole::ReviewProposal, content, entry, error);
}

bool ProjectService::SaveReviewProposalFile(AssuranceProject& project,
                                            const std::string& requested_file_name,
                                            const std::string& content,
                                            ProjectFileEntry& entry,
                                            std::string& error) {
    std::filesystem::path file_name = NormalizeProposalPatchName(requested_file_name);
    std::filesystem::path relative_path = std::filesystem::path("reviews") / "proposals" / file_name;
    if (!IsSafeRelativePath(relative_path)) {
        error = "Invalid project file path";
        return false;
    }

    auto found = std::find_if(project.files.begin(), project.files.end(), [&](const ProjectFileEntry& candidate) {
        return candidate.relativePath.generic_string() == relative_path.generic_string();
    });

    if (found == project.files.end()) {
        return AddTrackedFile(
            project, "reviews/proposals", file_name, ProjectFileRole::ReviewProposal, content, entry, error);
    }

    if (found->role != ProjectFileRole::ReviewProposal) {
        error = "Tracked file is not a review proposal: " + relative_path.generic_string();
        return false;
    }

    const std::filesystem::path absolute_path = project.rootPath / relative_path;
    if (auto r = WriteTextFile(absolute_path, content); !r) {
        error = std::move(r.error());
        return false;
    }

    if (auto r = RefreshEntryHashes(project, *found, false); !r) {
        error = std::move(r.error());
        return false;
    }
    project.modifiedUtc = NowUtc();
    if (!WriteManifestSafely(project, error))
        return false;

    entry = *found;
    return true;
}

bool ProjectService::SaveConfidenceFile(AssuranceProject& project,
                                        const std::string& content,
                                        ProjectFileEntry& entry,
                                        std::string& error) {
    const std::filesystem::path relative_path = std::filesystem::path("analysis") / "confidence.af.json";
    return WriteTrackedFile(project,
                            relative_path,
                            ProjectFileRole::ConfidenceAssessments,
                            "a confidence assessment file",
                            content,
                            entry,
                            error);
}

bool ProjectService::SaveRegisterAssessmentsFile(AssuranceProject& project,
                                                 const std::string& content,
                                                 ProjectFileEntry& entry,
                                                 std::string& error) {
    // One file per project, next to the register documents it annotates. The
    // path is fixed rather than user-chosen: the assessments are keyed by CSE /
    // evidence id, so a second file would just be a second opinion nobody could
    // reconcile.
    const std::filesystem::path relative_path = std::filesystem::path("registers") / "register-assessments.af.json";
    return WriteTrackedFile(project,
                            relative_path,
                            ProjectFileRole::RegisterAssessments,
                            "a register assessment file",
                            content,
                            entry,
                            error);
}

bool ProjectService::TrackExistingFile(AssuranceProject& project,
                                       const std::filesystem::path& relative_path,
                                       ProjectFileRole role,
                                       ProjectFileEntry& entry,
                                       std::string& error) {
    if (!IsSafeRelativePath(relative_path)) {
        error = "Invalid project file path";
        return false;
    }

    const std::filesystem::path absolute_path = project.rootPath / relative_path;
    std::error_code ec;
    if (!std::filesystem::exists(absolute_path, ec) || ec) {
        error = "File does not exist: " + relative_path.generic_string();
        return false;
    }
    if (!std::filesystem::is_regular_file(absolute_path, ec) || ec) {
        error = "Tracked path is not a file: " + relative_path.generic_string();
        return false;
    }

    auto found = std::find_if(project.files.begin(), project.files.end(), [&](const ProjectFileEntry& candidate) {
        return candidate.relativePath.generic_string() == relative_path.generic_string();
    });

    if (found != project.files.end() && found->role != role) {
        error = "Tracked file has a different role: " + relative_path.generic_string();
        return false;
    }

    const bool new_entry = found == project.files.end();
    ProjectFileEntry previous_entry;
    if (new_entry) {
        ProjectFileEntry tracked;
        tracked.id = GenerateId("af-file");
        tracked.relativePath = relative_path;
        tracked.role = role;
        tracked.hashAlgorithm = "sha256";
        project.files.push_back(tracked);
        found = project.files.end() - 1;
    } else {
        previous_entry = *found;
    }

    if (auto r = RefreshEntryHashes(project, *found, false); !r) {
        error = std::move(r.error());
        if (new_entry)
            project.files.pop_back();
        else
            *found = previous_entry;
        return false;
    }

    project.modifiedUtc = NowUtc();
    if (!WriteManifestSafely(project, error)) {
        if (new_entry)
            project.files.pop_back();
        else
            *found = previous_entry;
        return false;
    }

    entry = *found;
    return true;
}

bool ProjectService::RemoveTrackedFile(AssuranceProject& project,
                                       const std::filesystem::path& relative_path,
                                       bool delete_file,
                                       std::string& error) {
    if (!IsSafeRelativePath(relative_path)) {
        error = "Invalid project file path";
        return false;
    }

    // Capture the path value up front. Callers commonly invoke this with a reference that aliases
    // the relativePath of an entry inside project.files; once we erase that entry below, the
    // original reference dangles and reads turn into reads of the next vector element, which
    // would cause us to delete the WRONG file from disk.
    const std::filesystem::path target_relative_path = relative_path;

    auto found = std::find_if(project.files.begin(), project.files.end(), [&](const ProjectFileEntry& entry) {
        return entry.relativePath.generic_string() == target_relative_path.generic_string();
    });
    if (found == project.files.end()) {
        error = "Tracked file was not found: " + target_relative_path.generic_string();
        return false;
    }

    ProjectFileEntry removed_entry = *found;
    project.files.erase(found);
    project.modifiedUtc = NowUtc();

    std::error_code ec;
    if (delete_file) {
        std::filesystem::remove(project.rootPath / target_relative_path, ec);
        if (ec) {
            project.files.push_back(std::move(removed_entry));
            error = "Could not delete file: " + ec.message();
            return false;
        }
    }

    if (!WriteManifestSafely(project, error)) {
        project.files.push_back(std::move(removed_entry));
        return false;
    }

    return true;
}

bool ProjectService::WriteManifestSafely(const AssuranceProject& project, std::string& error) {
    std::filesystem::path manifest = ManifestPath(project);
    std::filesystem::path temp_manifest = manifest;
    temp_manifest += ".tmp";

    std::string content = SerializeManifest(project);
    AssuranceProject validation_project;
    if (!ParseManifest(content, project.rootPath, validation_project, error)) {
        error = "Generated af.proj did not validate: " + error;
        return false;
    }

    if (auto r = WriteTextFile(temp_manifest, content); !r) {
        error = std::move(r.error());
        return false;
    }

    std::error_code ec;
    if (std::filesystem::exists(manifest, ec)) {
        std::filesystem::path backup_dir = project.rootPath / ".af" / "backups" / BackupTimestampUtc();
        if (!std::filesystem::create_directories(backup_dir, ec) && ec) {
            error = "Could not create backup directory: " + backup_dir.string();
            std::filesystem::remove(temp_manifest, ec);
            return false;
        }
        std::filesystem::copy_file(
            manifest, backup_dir / kManifestFileName, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "Could not back up af.proj: " + ec.message();
            std::filesystem::remove(temp_manifest, ec);
            return false;
        }
        std::filesystem::remove(manifest, ec);
        if (ec) {
            error = "Could not replace af.proj: " + ec.message();
            std::filesystem::remove(temp_manifest, ec);
            return false;
        }
    }

    std::filesystem::rename(temp_manifest, manifest, ec);
    if (ec) {
        error = "Could not install af.proj: " + ec.message();
        std::filesystem::remove(temp_manifest, ec);
        return false;
    }
    return true;
}

ProjectLoadReport ProjectService::RefreshFileStatus(AssuranceProject& project) {
    ProjectLoadReport report;
    AddStep(report,
            "Scan tracked files",
            ProjectLoadStepStatus::Passed,
            std::to_string(project.files.size()) + " tracked file(s)");

    bool raw_hashes_ok = true;
    bool parsing_ok = true;
    bool semantic_hashes_ok = true;
    size_t changed_count = 0;
    size_t missing_count = 0;

    for (auto& entry : project.files) {
        std::string previous_raw_hash = entry.rawHash;
        if (auto r = RefreshEntryHashes(project, entry, true); !r) {
            raw_hashes_ok = false;
            entry.lastError = std::move(r.error());
        }
        if (entry.state == ProjectFileState::Missing) {
            ++missing_count;
            raw_hashes_ok = false;
        }
        if (entry.state == ProjectFileState::ModifiedOutsideAssuranceForge ||
            entry.state == ProjectFileState::ModifiedButCompatible) {
            ++changed_count;
            report.warnings.push_back(entry.relativePath.generic_string() + " was modified outside Assurance Forge.");
        }
        if (entry.state == ProjectFileState::ParseError)
            parsing_ok = false;
        if (entry.role == ProjectFileRole::SacmArgument &&
            (entry.semanticHash.empty() || entry.elementIndexHash.empty() || entry.relationshipGraphHash.empty()) &&
            entry.state != ProjectFileState::ParseError && entry.state != ProjectFileState::Missing) {
            semantic_hashes_ok = false;
        }
        (void)previous_raw_hash;
    }

    AddStep(report,
            "Recalculate raw hashes",
            raw_hashes_ok ? (changed_count > 0 ? ProjectLoadStepStatus::Warning : ProjectLoadStepStatus::Passed)
                          : ProjectLoadStepStatus::Failed,
            changed_count > 0   ? std::to_string(changed_count) + " externally modified file(s)"
            : missing_count > 0 ? std::to_string(missing_count) + " missing file(s)"
                                : "Raw hashes recalculated");
    AddStep(report,
            "Parse changed files",
            parsing_ok ? ProjectLoadStepStatus::Passed : ProjectLoadStepStatus::Failed,
            parsing_ok ? "Changed SACM files parsed" : "One or more SACM files failed to parse");
    AddStep(report,
            "Recalculate semantic hashes",
            semantic_hashes_ok ? ProjectLoadStepStatus::Passed : ProjectLoadStepStatus::Failed,
            semantic_hashes_ok ? "Semantic hashes recalculated" : "Could not calculate one or more semantic hashes");
    AddStep(report,
            "Check project references",
            ProjectLoadStepStatus::Passed,
            "Detailed dependency repair will be implemented later");
    AddStep(report,
            "Report project health",
            report.has_failures()
                ? ProjectLoadStepStatus::Failed
                : (!report.warnings.empty() ? ProjectLoadStepStatus::Warning : ProjectLoadStepStatus::Passed),
            report.has_failures() ? "Project needs attention"
                                  : (!report.warnings.empty() ? "Project loaded with warnings" : "Project is healthy"));
    report.showPopup = report.has_failures() || !report.warnings.empty();
    return report;
}

} // namespace core
