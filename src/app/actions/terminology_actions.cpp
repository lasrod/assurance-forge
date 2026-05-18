#include "app/actions/terminology_actions.h"

#include "app/actions/terminology_actions_internal.h"
#include "app/app_events.h"
#include "app/app_runtime_state.h"
#include "core/string_utils.h"
#include "core/terminology_text_utils.h"
#include "parser/model_utils.h"
#include "parser/xml_parser.h"
#include "ui/imgui_buffer_utils.h"
#include "ui/ui_state.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <utility>

namespace app::actions {

using core::TrimWhitespace;
using detail::CanSwitchProjectSacmFile;
using detail::CategoryDraftFromEditor;
using detail::CategoryNameExists;
using detail::ClearCategoryEditorBuffers;
using detail::ClearTermEditorBuffers;
using detail::CopyCategoryToEditor;
using detail::CopyTerminologyPackageToEditor;
using detail::CopyTermToEditor;
using detail::DecodeTerminologyTermQuickFixPayload;
using detail::EnsureProjectSacmFileOpen;
using detail::EnsureQuickDefineTargetPackage;
using detail::HasTerminologyPackageRef;
using detail::InvalidateSacmPackageTreeCache;
using detail::MarkTerminologyDocumentDirty;
using detail::OpenTerminologyProblemTerm;
using detail::QuickDefineTargetPackageResult;
using detail::RefreshVisibleTerminologyContextProjection;
using detail::ResolveQuickDefineTargetPackage;
using detail::SetStatus;
using detail::SyncVisibleTerminologyContextToParser;
using detail::TermDraftFromEditor;
using detail::TerminologyPackageRefFor;
using detail::TerminologySuggestionKey;
using detail::TerminologyTermQuickFixPayload;
using detail::TermStatusLabel;
using ui::CopyToBuffer;

TerminologyActions::TerminologyActions(AppRuntimeState& state) : state_(state) {}

void TerminologyActions::BeginAddPackage(const core::ProjectFileEntry& entry,
                                         const sacm::SacmPackageTreeNode& parent_node) {
    if (parent_node.type != sacm::SacmPackageNodeType::AssuranceCasePackage)
        return;
    if (!CanSwitchProjectSacmFile(state_.app_state, entry)) {
        SetStatus(state_, "Save the current SACM file before adding a terminology package.");
        return;
    }

    state_.terminology.pending_package_parent_entry = entry;
    CopyToBuffer(state_.terminology.new_package_name_buf,
                 sizeof(state_.terminology.new_package_name_buf),
                 "Terminology Package");
    CopyToBuffer(
        state_.terminology.new_package_description_buf, sizeof(state_.terminology.new_package_description_buf), "");
    state_.terminology.show_create_package_modal = true;
}

bool TerminologyActions::ConfirmAddPackage() {
    if (!state_.terminology.pending_package_parent_entry.has_value()) {
        state_.terminology.show_create_package_modal = false;
        return false;
    }

    const core::ProjectFileEntry entry = state_.terminology.pending_package_parent_entry.value();
    if (!CanSwitchProjectSacmFile(state_.app_state, entry)) {
        SetStatus(state_, "Save the current SACM file before adding a terminology package.");
        return false;
    }
    if (!EnsureProjectSacmFileOpen(state_, entry, false))
        return false;
    if (!state_.app_state.sacm_package.has_value()) {
        SetStatus(state_, "Could not load an editable SACM package model.");
        return false;
    }

    core::TerminologyPackageCreateResult result =
        core::CreateTerminologyPackage(state_.app_state.sacm_package.value(),
                                       TrimWhitespace(state_.terminology.new_package_name_buf),
                                       TrimWhitespace(state_.terminology.new_package_description_buf));
    if (!result.success) {
        SetStatus(state_, "Terminology package create failed: " + result.error);
        return false;
    }

    state_.terminology.selected_package_ref = result.package_ref;
    state_.terminology.selected_term_ref = core::TerminologyTermRef{};
    state_.terminology.selected_category_ref = core::TerminologyCategoryRef{};
    CopyToBuffer(state_.terminology.category_filter_buf, sizeof(state_.terminology.category_filter_buf), "");
    state_.terminology.selected_package_file_path = state_.app_state.active_project_file_path;
    if (const sacm::TerminologyPackage* package =
            core::FindTerminologyPackage(state_.app_state.sacm_package.value(), result.package_ref)) {
        CopyTerminologyPackageToEditor(state_, *package);
    }
    MarkTerminologyDocumentDirty(state_);
    InvalidateSacmPackageTreeCache(state_, entry.relativePath);
    state_.terminology.show_create_package_modal = false;
    state_.terminology.pending_package_parent_entry.reset();
    state_.workbench.show_terminology_package_tab = true;
    ui::GetUiState().center_view = ui::CenterView::TerminologyPackage;
    state_.workbench.force_center_tab_selection = true;
    SetStatus(state_, "Added terminology package " + result.package_ref.id + ".");
    return true;
}

bool TerminologyActions::ApplyPackageEdits() {
    if (!state_.app_state.sacm_package.has_value())
        return false;

    std::string error;
    if (!core::UpdateTerminologyPackage(state_.app_state.sacm_package.value(),
                                        state_.terminology.selected_package_ref,
                                        TrimWhitespace(state_.terminology.package_name_buf),
                                        TrimWhitespace(state_.terminology.package_description_buf),
                                        error)) {
        SetStatus(state_, "Terminology package update failed: " + error);
        return false;
    }

    MarkTerminologyDocumentDirty(state_);
    if (state_.app_state.current_project.has_value() && !state_.app_state.active_project_file_path.empty()) {
        const std::filesystem::path relative = std::filesystem::relative(state_.app_state.active_project_file_path,
                                                                         state_.app_state.current_project->rootPath);
        InvalidateSacmPackageTreeCache(state_, relative);
    }
    return true;
}

void TerminologyActions::BeginDeletePackage() {
    state_.terminology.show_delete_package_modal = true;
}

bool TerminologyActions::ConfirmDeletePackage() {
    if (!state_.app_state.sacm_package.has_value())
        return false;

    std::string error;
    if (!core::DeleteTerminologyPackage(
            state_.app_state.sacm_package.value(), state_.terminology.selected_package_ref, error)) {
        SetStatus(state_, "Terminology package delete failed: " + error);
        return false;
    }

    MarkTerminologyDocumentDirty(state_);
    if (state_.app_state.current_project.has_value() && !state_.app_state.active_project_file_path.empty()) {
        const std::filesystem::path relative = std::filesystem::relative(state_.app_state.active_project_file_path,
                                                                         state_.app_state.current_project->rootPath);
        InvalidateSacmPackageTreeCache(state_, relative);
    }
    state_.terminology.selected_package_ref = core::TerminologyPackageRef{};
    state_.terminology.show_delete_package_modal = false;
    state_.workbench.show_terminology_package_tab = false;
    ui::GetUiState().center_view = ui::CenterView::PackageDetails;
    state_.workbench.force_center_tab_selection = true;
    SetStatus(state_, "Deleted terminology package.");
    return true;
}

bool TerminologyActions::OpenTermFromCanvas(const core::TerminologyPackageRef& package_ref,
                                            const core::TerminologyTermRef& term_ref) {
    if (!state_.app_state.sacm_package.has_value()) {
        SetStatus(state_, "Open a SACM model before opening terminology terms.");
        return false;
    }

    const sacm::TerminologyPackage* terminology_package =
        core::FindTerminologyPackage(state_.app_state.sacm_package.value(), package_ref);
    if (!terminology_package) {
        SetStatus(state_, "Terminology package not found.");
        return false;
    }
    const sacm::Term* term = core::FindTerminologyTerm(*terminology_package, term_ref);
    if (!term) {
        SetStatus(state_, "Term not found.");
        return false;
    }

    state_.terminology.selected_package_ref = package_ref;
    state_.terminology.selected_term_ref = term_ref;
    state_.terminology.selected_category_ref = core::TerminologyCategoryRef{};
    state_.terminology.selected_package_file_path = state_.app_state.active_project_file_path;
    CopyTerminologyPackageToEditor(state_, *terminology_package);
    CopyToBuffer(state_.terminology.filter_buf, sizeof(state_.terminology.filter_buf), term->value);
    CopyToBuffer(state_.terminology.category_filter_buf, sizeof(state_.terminology.category_filter_buf), "");
    state_.workbench.show_terminology_package_tab = true;
    ui::GetUiState().center_view = ui::CenterView::TerminologyPackage;
    state_.workbench.force_center_tab_selection = true;
    SetStatus(state_, "Opened term " + term->value + ".");
    return true;
}

bool TerminologyActions::EditTermFromCanvas(const core::TerminologyPackageRef& package_ref,
                                            const core::TerminologyTermRef& term_ref) {
    if (!OpenTermFromCanvas(package_ref, term_ref))
        return false;
    if (!state_.app_state.sacm_package.has_value())
        return false;
    const sacm::Term* term = core::FindTerminologyTerm(state_.app_state.sacm_package.value(), package_ref, term_ref);
    if (!term)
        return false;
    state_.terminology.selected_term_ref = term_ref;
    CopyTermToEditor(state_, *term);
    state_.terminology.editing_existing_term = true;
    state_.terminology.show_term_editor_modal = true;
    return true;
}

bool TerminologyActions::AddTermAsContextFromCanvas(const std::string& element_id,
                                                    const core::TerminologyPackageRef& package_ref,
                                                    const core::TerminologyTermRef& term_ref) {
    if (!state_.app_state.sacm_package.has_value()) {
        SetStatus(state_, "Open a SACM model before associating terminology.");
        return false;
    }

    core::TerminologyContextAssociationResult result = core::AssociateTerminologyTermWithElement(
        state_.app_state.sacm_package.value(), element_id, package_ref, term_ref);
    if (!result.success) {
        SetStatus(state_, "Could not associate term with element: " + result.error);
        return false;
    }

    if (!result.already_associated) {
        MarkTerminologyDocumentDirty(state_);
        state_.events.Emit(DocumentDirtyEvent{});
        if (state_.app_state.current_project.has_value() && !state_.app_state.active_project_file_path.empty()) {
            const std::filesystem::path relative = std::filesystem::relative(
                state_.app_state.active_project_file_path, state_.app_state.current_project->rootPath);
            InvalidateSacmPackageTreeCache(state_, relative);
        }
    }
    SetStatus(state_,
              result.already_associated ? "Term is already associated with this element."
                                        : "Associated term with this element.");
    return true;
}

bool TerminologyActions::AddVisibleTermContextFromCanvas(const std::string& element_id,
                                                         const core::TerminologyPackageRef& package_ref,
                                                         const core::TerminologyTermRef& term_ref) {
    if (!state_.app_state.sacm_package.has_value()) {
        SetStatus(state_, "Open a SACM model before adding terminology context.");
        return false;
    }

    const std::string term_label = TermStatusLabel(state_.app_state.sacm_package.value(), package_ref, term_ref);
    core::TerminologyContextAssociationResult result = core::AddTerminologyTermAsVisibleContext(
        state_.app_state.sacm_package.value(), element_id, package_ref, term_ref);
    if (!result.success) {
        SetStatus(state_, "Could not add term as context: " + result.error);
        return false;
    }

    const bool parser_changed = SyncVisibleTerminologyContextToParser(state_.app_state, result);
    if (!result.already_associated) {
        MarkTerminologyDocumentDirty(state_);
        state_.events.Emit(DocumentDirtyEvent{});
        if (state_.app_state.current_project.has_value() && !state_.app_state.active_project_file_path.empty()) {
            const std::filesystem::path relative = std::filesystem::relative(
                state_.app_state.active_project_file_path, state_.app_state.current_project->rootPath);
            InvalidateSacmPackageTreeCache(state_, relative);
        }
    }
    if (!result.already_associated || parser_changed)
        state_.events.Emit(TreeDirtyEvent{});
    SetStatus(state_,
              result.already_associated ? term_label + " is already attached as context to this element."
                                        : "Added " + term_label + " as context.");
    return true;
}

void TerminologyActions::SelectTerm(const core::TerminologyTermRef& term_ref) {
    state_.terminology.selected_term_ref = term_ref;
}

void TerminologyActions::BeginAddTerm() {
    if (!state_.app_state.sacm_package.has_value()) {
        SetStatus(state_, "Open a terminology package before adding terms.");
        return;
    }
    ClearTermEditorBuffers(state_);
    state_.terminology.editing_existing_term = false;
    state_.terminology.show_term_editor_modal = true;
}

bool TerminologyActions::BeginEditTerm(const core::TerminologyTermRef& term_ref) {
    if (!state_.app_state.sacm_package.has_value())
        return false;
    const sacm::Term* term = core::FindTerminologyTerm(
        state_.app_state.sacm_package.value(), state_.terminology.selected_package_ref, term_ref);
    if (!term) {
        SetStatus(state_, "Term not found.");
        return false;
    }
    state_.terminology.selected_term_ref = term_ref;
    CopyTermToEditor(state_, *term);
    state_.terminology.editing_existing_term = true;
    state_.terminology.show_term_editor_modal = true;
    return true;
}

bool TerminologyActions::ConfirmTermEdit() {
    if (!state_.app_state.sacm_package.has_value())
        return false;

    const core::TerminologyTermDraft draft = TermDraftFromEditor(state_);
    std::string error;
    if (state_.terminology.editing_existing_term) {
        if (!core::UpdateTerminologyTerm(state_.app_state.sacm_package.value(),
                                         state_.terminology.selected_package_ref,
                                         state_.terminology.selected_term_ref,
                                         draft,
                                         error)) {
            SetStatus(state_, "Term update failed: " + error);
            return false;
        }
        SetStatus(state_, "Updated term " + draft.value + ".");
    } else {
        core::TerminologyTermCreateResult result = core::CreateTerminologyTerm(
            state_.app_state.sacm_package.value(), state_.terminology.selected_package_ref, draft);
        if (!result.success) {
            SetStatus(state_, "Term create failed: " + result.error);
            return false;
        }
        state_.terminology.selected_term_ref = result.term_ref;
        SetStatus(state_, "Added term " + draft.value + ".");
    }

    MarkTerminologyDocumentDirty(state_);
    if (RefreshVisibleTerminologyContextProjection(state_.app_state))
        state_.events.Emit(TreeDirtyEvent{});
    state_.terminology.show_term_editor_modal = false;
    return true;
}

void TerminologyActions::BeginDeleteTerm(const core::TerminologyTermRef& term_ref) {
    if (!state_.app_state.sacm_package.has_value())
        return;
    const sacm::Term* term = core::FindTerminologyTerm(
        state_.app_state.sacm_package.value(), state_.terminology.selected_package_ref, term_ref);
    if (!term) {
        SetStatus(state_, "Term not found.");
        return;
    }
    state_.terminology.selected_term_ref = term_ref;
    state_.terminology.pending_delete_term_usage_count =
        core::CountTerminologyTermUsage(state_.app_state.sacm_package.value(), *term);
    state_.terminology.show_delete_term_modal = true;
}

bool TerminologyActions::ConfirmDeleteTerm() {
    if (!state_.app_state.sacm_package.has_value())
        return false;

    std::string error;
    if (!core::DeleteTerminologyTerm(state_.app_state.sacm_package.value(),
                                     state_.terminology.selected_package_ref,
                                     state_.terminology.selected_term_ref,
                                     error)) {
        SetStatus(state_, "Term delete failed: " + error);
        return false;
    }

    state_.terminology.selected_term_ref = core::TerminologyTermRef{};
    state_.terminology.show_delete_term_modal = false;
    MarkTerminologyDocumentDirty(state_);
    if (RefreshVisibleTerminologyContextProjection(state_.app_state))
        state_.events.Emit(TreeDirtyEvent{});
    SetStatus(state_, "Deleted term.");
    return true;
}

void TerminologyActions::SelectCategory(const core::TerminologyCategoryRef& category_ref) {
    state_.terminology.selected_category_ref = category_ref;
}

void TerminologyActions::SetCategoryFilter(const std::string& category_filter) {
    CopyToBuffer(
        state_.terminology.category_filter_buf, sizeof(state_.terminology.category_filter_buf), category_filter);
}

void TerminologyActions::BeginAddCategory() {
    if (!state_.app_state.sacm_package.has_value()) {
        SetStatus(state_, "Open a terminology package before adding categories.");
        return;
    }
    ClearCategoryEditorBuffers(state_);
    state_.terminology.editing_existing_category = false;
    state_.terminology.show_category_editor_modal = true;
}

bool TerminologyActions::BeginEditCategory(const core::TerminologyCategoryRef& category_ref) {
    if (!state_.app_state.sacm_package.has_value())
        return false;
    const sacm::Category* category = core::FindTerminologyCategory(
        state_.app_state.sacm_package.value(), state_.terminology.selected_package_ref, category_ref);
    if (!category) {
        SetStatus(state_, "Category not found.");
        return false;
    }

    state_.terminology.selected_category_ref = category_ref;
    CopyCategoryToEditor(state_, *category);
    state_.terminology.editing_existing_category = true;
    state_.terminology.show_category_editor_modal = true;
    return true;
}

void TerminologyActions::ConfirmCategoryEdit() {
    if (!state_.app_state.sacm_package.has_value())
        return;

    const core::TerminologyCategoryDraft draft = CategoryDraftFromEditor(state_);
    std::string error;
    if (state_.terminology.editing_existing_category) {
        if (!core::UpdateTerminologyCategory(state_.app_state.sacm_package.value(),
                                             state_.terminology.selected_package_ref,
                                             state_.terminology.selected_category_ref,
                                             draft,
                                             error)) {
            SetStatus(state_, "Category update failed: " + error);
            return;
        }
        SetStatus(state_, "Updated category " + draft.name + ".");
    } else {
        core::TerminologyCategoryCreateResult result = core::CreateTerminologyCategory(
            state_.app_state.sacm_package.value(), state_.terminology.selected_package_ref, draft);
        if (!result.success) {
            SetStatus(state_, "Category create failed: " + result.error);
            return;
        }
        state_.terminology.selected_category_ref = result.category_ref;
        SetStatus(state_, "Added category " + draft.name + ".");
    }

    MarkTerminologyDocumentDirty(state_);
    state_.terminology.show_category_editor_modal = false;
}

void TerminologyActions::BeginDeleteCategory(const core::TerminologyCategoryRef& category_ref) {
    if (!state_.app_state.sacm_package.has_value())
        return;
    const sacm::TerminologyPackage* terminology_package =
        core::FindTerminologyPackage(state_.app_state.sacm_package.value(), state_.terminology.selected_package_ref);
    if (!terminology_package) {
        SetStatus(state_, "Terminology package not found.");
        return;
    }
    const sacm::Category* category = core::FindTerminologyCategory(*terminology_package, category_ref);
    if (!category) {
        SetStatus(state_, "Category not found.");
        return;
    }

    state_.terminology.selected_category_ref = category_ref;
    state_.terminology.pending_delete_category_term_count =
        core::CountTermsUsingCategory(*terminology_package, category_ref);
    state_.terminology.show_delete_category_modal = true;
}

void TerminologyActions::ConfirmDeleteCategory() {
    if (!state_.app_state.sacm_package.has_value())
        return;

    std::string error;
    if (!core::DeleteTerminologyCategory(state_.app_state.sacm_package.value(),
                                         state_.terminology.selected_package_ref,
                                         state_.terminology.selected_category_ref,
                                         error)) {
        SetStatus(state_, "Category delete failed: " + error);
        return;
    }

    state_.terminology.selected_category_ref = core::TerminologyCategoryRef{};
    CopyToBuffer(state_.terminology.category_filter_buf, sizeof(state_.terminology.category_filter_buf), "");
    state_.terminology.show_delete_category_modal = false;
    MarkTerminologyDocumentDirty(state_);
    SetStatus(state_, "Deleted category.");
}

void TerminologyActions::SeedRecommendedCategories() {
    if (!state_.app_state.sacm_package.has_value())
        return;

    sacm::TerminologyPackage* terminology_package =
        core::FindTerminologyPackage(state_.app_state.sacm_package.value(), state_.terminology.selected_package_ref);
    if (!terminology_package) {
        SetStatus(state_, "Terminology package not found.");
        return;
    }

    const char* recommended[] = {"Operational Context",
                                 "System",
                                 "Hazard / Risk",
                                 "Evidence",
                                 "Requirement",
                                 "Standard",
                                 "Project Specific",
                                 "Deprecated"};
    int added = 0;
    for (const char* name : recommended) {
        if (CategoryNameExists(*terminology_package, name))
            continue;
        core::TerminologyCategoryDraft draft;
        draft.name = name;
        core::TerminologyCategoryCreateResult result = core::CreateTerminologyCategory(
            state_.app_state.sacm_package.value(), state_.terminology.selected_package_ref, draft);
        if (result.success)
            ++added;
    }

    if (added > 0) {
        MarkTerminologyDocumentDirty(state_);
        SetStatus(state_, "Added recommended terminology categories.");
    } else {
        SetStatus(state_, "Recommended terminology categories already exist.");
    }
}

void TerminologyActions::BeginFindUsages(const core::TerminologyPackageRef& package_ref,
                                         const core::TerminologyTermRef& term_ref) {
    state_.terminology.usages_active = true;
    state_.terminology.focus_usages_tab = true;
    state_.terminology.usage_search_package_ref = package_ref;
    state_.terminology.usage_search_term_ref = term_ref;
    state_.terminology.usage_search_term_value.clear();
    state_.terminology.usage_search_term_name.clear();
    state_.terminology.usage_search_message.clear();
    state_.terminology.usage_search_error.clear();
    state_.terminology.usage_results.clear();
    state_.terminology.selected_usage_index = -1;

    if (!state_.app_state.sacm_package.has_value()) {
        state_.terminology.usage_search_error = "Open a SACM model before finding terminology usages.";
        SetStatus(state_, state_.terminology.usage_search_error);
        return;
    }

    core::TerminologyTermUsageSearchResult result =
        core::FindTerminologyTermUsages(state_.app_state.sacm_package.value(), package_ref, term_ref);
    state_.terminology.usage_search_term_value = result.term_value;
    state_.terminology.usage_search_term_name = result.term_name;
    if (!result.success) {
        state_.terminology.usage_search_error = result.error;
        SetStatus(state_, "Find usages failed: " + result.error);
        return;
    }

    state_.terminology.usage_results = std::move(result.usages);
    if (!state_.terminology.usage_results.empty())
        state_.terminology.selected_usage_index = 0;
    const int usage_count = static_cast<int>(state_.terminology.usage_results.size());
    const std::string label =
        state_.terminology.usage_search_term_value.empty() ? "term" : state_.terminology.usage_search_term_value;
    SetStatus(state_,
              "Found " + std::to_string(usage_count) + " usage" + (usage_count == 1 ? "" : "s") + " of " + label + ".");
}

void TerminologyActions::NavigateToUsage(std::size_t usage_index) {
    if (usage_index >= state_.terminology.usage_results.size())
        return;
    state_.terminology.selected_usage_index = static_cast<int>(usage_index);
    const core::TerminologyTermUsage& usage = state_.terminology.usage_results[usage_index];
    if (usage.element_id.empty()) {
        SetStatus(state_, "The selected usage has no navigable element id.");
        return;
    }
    state_.workbench.show_gsn_tab = true;
    state_.events.Emit(SelectionChangedEvent{usage.element_id, true});
    state_.events.Emit(CenterRequestEvent{CenterViewRequest::GsnCanvas, true, false, true});
}

void TerminologyActions::ChangeMeaningFromCanvas(const std::string& element_id, const std::string& term_value) {
    (void)element_id;
    (void)term_value;
    state_.events.Emit(ModalRequestEvent{ModalKind::NotImplemented, true, "Change linked terminology meaning"});
}

void TerminologyActions::BeginQuickDefineTerm(const std::string& element_id, const std::string& term_value) {
    if (!state_.app_state.sacm_package.has_value()) {
        SetStatus(state_, "Open a SACM model before defining terms.");
        return;
    }

    const QuickDefineTargetPackageResult target = EnsureQuickDefineTargetPackage(state_, element_id);
    if (!HasTerminologyPackageRef(target.package_ref)) {
        SetStatus(state_,
                  target.error.empty() ? "Could not create a TerminologyPackage for the new term."
                                       : "Terminology package create failed: " + target.error);
        return;
    }
    if (target.created) {
        MarkTerminologyDocumentDirty(state_);
        state_.events.Emit(TreeDirtyEvent{});
        state_.events.Emit(DocumentDirtyEvent{});
        if (state_.app_state.current_project.has_value() && !state_.app_state.active_project_file_path.empty()) {
            const std::filesystem::path relative = std::filesystem::relative(
                state_.app_state.active_project_file_path, state_.app_state.current_project->rootPath);
            InvalidateSacmPackageTreeCache(state_, relative);
        }
        SetStatus(state_, "Created a TerminologyPackage for new terms.");
    }

    const std::string trimmed_term = TrimWhitespace(term_value);
    ClearTermEditorBuffers(state_);
    CopyToBuffer(state_.terminology.term_value_buf, sizeof(state_.terminology.term_value_buf), trimmed_term);
    state_.terminology.quick_define_element_id = element_id;
    state_.terminology.quick_define_source_text = trimmed_term;
    state_.terminology.quick_define_target_package_ref = target.package_ref;
    state_.terminology.show_quick_define_term_modal = true;
}

void TerminologyActions::BeginLinkExistingTerm(const std::string& element_id, const std::string& term_value) {
    if (state_.app_state.sacm_package.has_value()) {
        const core::TerminologyPackageRef target_package_ref = ResolveQuickDefineTargetPackage(state_, element_id);
        if (HasTerminologyPackageRef(target_package_ref)) {
            state_.terminology.selected_package_ref = target_package_ref;
            if (const sacm::TerminologyPackage* package =
                    core::FindTerminologyPackage(state_.app_state.sacm_package.value(), target_package_ref)) {
                CopyTerminologyPackageToEditor(state_, *package);
            }
        }
    }

    const std::string trimmed_term = TrimWhitespace(term_value);
    CopyToBuffer(state_.terminology.filter_buf, sizeof(state_.terminology.filter_buf), trimmed_term);
    state_.workbench.show_terminology_package_tab = true;
    ui::GetUiState().center_view = ui::CenterView::TerminologyPackage;
    state_.workbench.force_center_tab_selection = true;
    SetStatus(state_,
              "Filtered the glossary for " + trimmed_term +
                  ". Select a term to link when occurrence binding is available.");
}

bool TerminologyActions::ConfirmQuickDefineTerm(bool add_as_context) {
    if (!state_.app_state.sacm_package.has_value())
        return false;

    const core::TerminologyTermDraft draft = TermDraftFromEditor(state_);
    core::TerminologyTermCreateResult result = core::CreateTerminologyTerm(
        state_.app_state.sacm_package.value(), state_.terminology.quick_define_target_package_ref, draft);
    if (!result.success) {
        SetStatus(state_, "Term create failed: " + result.error);
        return false;
    }

    state_.terminology.selected_package_ref = state_.terminology.quick_define_target_package_ref;
    state_.terminology.selected_term_ref = result.term_ref;
    state_.terminology.selected_category_ref = core::TerminologyCategoryRef{};
    CopyToBuffer(state_.terminology.filter_buf, sizeof(state_.terminology.filter_buf), draft.value);
    CopyToBuffer(state_.terminology.category_filter_buf, sizeof(state_.terminology.category_filter_buf), "");
    state_.terminology.selected_package_file_path = state_.app_state.active_project_file_path;
    if (const sacm::TerminologyPackage* package = core::FindTerminologyPackage(
            state_.app_state.sacm_package.value(), state_.terminology.selected_package_ref)) {
        CopyTerminologyPackageToEditor(state_, *package);
    }

    MarkTerminologyDocumentDirty(state_);
    state_.events.Emit(TreeDirtyEvent{});
    state_.events.Emit(DocumentDirtyEvent{});
    if (state_.app_state.current_project.has_value() && !state_.app_state.active_project_file_path.empty()) {
        const std::filesystem::path relative = std::filesystem::relative(state_.app_state.active_project_file_path,
                                                                         state_.app_state.current_project->rootPath);
        InvalidateSacmPackageTreeCache(state_, relative);
    }
    const std::string context_element_id = state_.terminology.quick_define_element_id;
    if (add_as_context)
        AddVisibleTermContextFromCanvas(
            context_element_id, state_.terminology.quick_define_target_package_ref, result.term_ref);

    state_.terminology.show_quick_define_term_modal = false;
    state_.terminology.quick_define_element_id.clear();
    state_.terminology.quick_define_source_text.clear();
    if (!add_as_context)
        SetStatus(state_, "Added term " + draft.value + ".");
    return true;
}

void TerminologyActions::HandleProblemQuickFix(const core::ProblemItem& problem) {
    if (problem.type == "TerminologyUndefinedAcronym") {
        BeginQuickDefineTerm(problem.element_id, problem.quick_fix_payload);
        return;
    }
    if (problem.type == "TerminologyAmbiguity") {
        BeginLinkExistingTerm(problem.element_id, problem.quick_fix_payload);
        return;
    }
    if (core::StartsWith(problem.type, "TerminologyTerm")) {
        TerminologyTermQuickFixPayload payload;
        if (!DecodeTerminologyTermQuickFixPayload(problem.quick_fix_payload, payload)) {
            SetStatus(state_, "Could not decode terminology quick fix target.");
            return;
        }
        if (!OpenTerminologyProblemTerm(state_, payload.package_ref, payload.term_ref, payload.term_value)) {
            SetStatus(state_, "Terminology quick fix target was not found.");
            return;
        }
        if (problem.type == "TerminologyTermDuplicateDefinition") {
            SetStatus(state_, "Filtered glossary to duplicated term definition " + payload.term_value + ".");
            return;
        }
        BeginEditTerm(payload.term_ref);
        SetStatus(state_, "Opened terminology term editor.");
        return;
    }
    if (!problem.element_id.empty()) {
        state_.events.Emit(SelectionChangedEvent{problem.element_id, true});
        state_.events.Emit(CenterRequestEvent{CenterViewRequest::GsnCanvas, true, false, true});
    }
}

void TerminologyActions::IgnoreSuggestion(const std::string& element_id, const std::string& term_value) {
    const std::string trimmed_term = TrimWhitespace(term_value);
    state_.terminology.ignored_suggestion_keys.insert(TerminologySuggestionKey(element_id, trimmed_term));
    SetStatus(state_, "Ignored terminology suggestion " + trimmed_term + " for this session.");
}

bool TerminologyActions::IsSuggestionIgnored(const std::string& element_id, const std::string& term_value) const {
    const std::string key = TerminologySuggestionKey(element_id, TrimWhitespace(term_value));
    return state_.terminology.ignored_suggestion_keys.count(key) > 0;
}

} // namespace app::actions
