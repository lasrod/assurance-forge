#pragma once

#include "app/app_events.h"
#include "app/app_runtime_state.h"
#include "core/app_state.h"
#include "core/sacm_model.h"
#include "core/terminology_package_service.h"
#include "parser/xml_parser.h"

#include <filesystem>
#include <string>

namespace app {

struct AppRuntimeState;

namespace actions::detail {

inline void SetStatus(AppRuntimeState& state, const std::string& message) {
    state.events.Emit(StatusMessageEvent{message});
}

std::string TerminologySuggestionKey(const std::string& element_id, const std::string& term_value);

// Absolute path to the per-project sidecar storing ignored terminology suggestions
// (empty when no project is open). Serializes app-private state outside the SACM XML.
std::filesystem::path IgnoredTerminologyFilePath(const AppRuntimeState& state);

// Persists the current in-memory ignore set (state.terminology.ignored_suggestion_keys)
// to the project sidecar. No-op when no project is open.
void SaveIgnoredSuggestions(const AppRuntimeState& state);

void CopyTerminologyPackageToEditor(AppRuntimeState& state, const sacm::TerminologyPackage& package);
void ClearTermEditorBuffers(AppRuntimeState& state);
void CopyTermToEditor(AppRuntimeState& state, const sacm::Term& term);
core::TerminologyTermDraft TermDraftFromEditor(const AppRuntimeState& state);
void ClearCategoryEditorBuffers(AppRuntimeState& state);
void CopyCategoryToEditor(AppRuntimeState& state, const sacm::Category& category);
core::TerminologyCategoryDraft CategoryDraftFromEditor(const AppRuntimeState& state);
bool CategoryNameExists(const sacm::TerminologyPackage& package, const std::string& name);

bool HasTerminologyPackageRef(const core::TerminologyPackageRef& package_ref);
bool TerminologyPackageMatchesRef(const sacm::TerminologyPackage& package,
                                  const core::TerminologyPackageRef& package_ref);
core::TerminologyPackageRef TerminologyPackageRefFor(const sacm::TerminologyPackage& package);

bool ArgumentPackageContainsElement(const sacm::ArgumentPackage& argument_package, const std::string& element_id);
const sacm::ArgumentPackage* FindContainingArgumentPackage(const sacm::AssuranceCasePackage& package,
                                                           const std::string& element_id);
bool IsAssuranceCaseTerminologyPackage(const sacm::AssuranceCasePackage& package,
                                       const core::TerminologyPackageRef& package_ref);
bool IsArgumentTerminologyPackage(const sacm::ArgumentPackage& argument_package,
                                  const core::TerminologyPackageRef& package_ref);

core::TerminologyPackageRef ResolveQuickDefineTargetPackage(const AppRuntimeState& state,
                                                            const std::string& element_id);

struct QuickDefineTargetPackageResult {
    core::TerminologyPackageRef package_ref;
    bool created = false;
    std::string error;
};

QuickDefineTargetPackageResult EnsureQuickDefineTargetPackage(AppRuntimeState& state, const std::string& element_id);

void InvalidateSacmPackageTreeCache(AppRuntimeState& state, const std::filesystem::path& relative_path);

bool CanSwitchProjectSacmFile(const core::AppState& app_state, const core::ProjectFileEntry& entry);
bool IsActiveProjectSacmFile(const core::AppState& app_state, const core::ProjectFileEntry& entry);
bool EnsureProjectSacmFileOpen(AppRuntimeState& state, const core::ProjectFileEntry& entry, bool require_loaded_case);

bool RefreshVisibleTerminologyContextProjection(core::AppState& app_state);
bool SyncVisibleTerminologyContextToParser(core::AppState& app_state,
                                           const core::TerminologyContextAssociationResult& result);

std::string TermStatusLabel(const sacm::AssuranceCasePackage& package,
                            const core::TerminologyPackageRef& package_ref,
                            const core::TerminologyTermRef& term_ref);

struct TerminologyTermQuickFixPayload {
    core::TerminologyPackageRef package_ref;
    core::TerminologyTermRef term_ref;
    std::string term_value;
};

bool DecodeTerminologyTermQuickFixPayload(const std::string& payload, TerminologyTermQuickFixPayload& decoded);
bool OpenTerminologyProblemTerm(AppRuntimeState& state,
                                const core::TerminologyPackageRef& package_ref,
                                const core::TerminologyTermRef& term_ref,
                                const std::string& filter_value);

} // namespace actions::detail
} // namespace app
