#include "core/project_model.h"

namespace core {

bool ProjectLoadReport::has_failures() const {
    for (const auto& step : steps) {
        if (step.status == ProjectLoadStepStatus::Failed) return true;
    }
    return false;
}

const char* ProjectFileRoleToString(ProjectFileRole role) {
    switch (role) {
        case ProjectFileRole::SacmArgument: return "sacm.argument";
        case ProjectFileRole::EvidenceRegister: return "af.evidenceRegister";
        case ProjectFileRole::J3377CaeRegister: return "af.j3377CaeRegister";
        case ProjectFileRole::ConformanceSheet: return "af.conformanceSheet";
        case ProjectFileRole::ExportedReport: return "af.exportedReport";
        case ProjectFileRole::Unknown: return "unknown";
    }
    return "unknown";
}

const char* ProjectFileRoleToDisplayString(ProjectFileRole role) {
    switch (role) {
        case ProjectFileRole::SacmArgument: return "SACM Argument";
        case ProjectFileRole::EvidenceRegister: return "Evidence Register";
        case ProjectFileRole::J3377CaeRegister: return "J3377 CAE Register";
        case ProjectFileRole::ConformanceSheet: return "Conformance Sheet";
        case ProjectFileRole::ExportedReport: return "Exported Report";
        case ProjectFileRole::Unknown: return "Unknown";
    }
    return "Unknown";
}

ProjectFileRole ProjectFileRoleFromString(const std::string& value) {
    if (value == "sacm.argument") return ProjectFileRole::SacmArgument;
    if (value == "af.evidenceRegister") return ProjectFileRole::EvidenceRegister;
    if (value == "af.j3377CaeRegister") return ProjectFileRole::J3377CaeRegister;
    if (value == "af.conformanceSheet") return ProjectFileRole::ConformanceSheet;
    if (value == "af.exportedReport") return ProjectFileRole::ExportedReport;
    return ProjectFileRole::Unknown;
}

const char* ProjectFileStateToString(ProjectFileState state) {
    switch (state) {
        case ProjectFileState::Clean: return "clean";
        case ProjectFileState::ModifiedOutsideAssuranceForge: return "modifiedOutsideAssuranceForge";
        case ProjectFileState::ModifiedButCompatible: return "modifiedButCompatible";
        case ProjectFileState::ModifiedWithBrokenReferences: return "modifiedWithBrokenReferences";
        case ProjectFileState::Missing: return "missing";
        case ProjectFileState::Moved: return "moved";
        case ProjectFileState::ParseError: return "parseError";
        case ProjectFileState::UnsupportedVersion: return "unsupportedVersion";
        case ProjectFileState::GeneratedFileOutdated: return "generatedFileOutdated";
    }
    return "clean";
}

const char* ProjectFileStateToDisplayString(ProjectFileState state) {
    switch (state) {
        case ProjectFileState::Clean: return "Clean";
        case ProjectFileState::ModifiedOutsideAssuranceForge: return "Modified outside Assurance Forge";
        case ProjectFileState::ModifiedButCompatible: return "Modified but compatible";
        case ProjectFileState::ModifiedWithBrokenReferences: return "Modified with broken references";
        case ProjectFileState::Missing: return "Missing";
        case ProjectFileState::Moved: return "Moved";
        case ProjectFileState::ParseError: return "Parse error";
        case ProjectFileState::UnsupportedVersion: return "Unsupported version";
        case ProjectFileState::GeneratedFileOutdated: return "Generated file outdated";
    }
    return "Clean";
}

}  // namespace core