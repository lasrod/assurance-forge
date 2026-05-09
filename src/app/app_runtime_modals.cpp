#include "app/app_runtime.h"
#include "app/app_runtime_state.h"
#include "app/project_workflow.h"
#include "app/recent_projects.h"
#include "core/element_factory.h"
#include "imgui.h"
#include "ui/panels/welcome_modal.h"
#include "ui/ui_state.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace app {
namespace {

void CopyToBuffer(char* buffer, size_t buffer_size, const std::string& value) {
    if (!buffer || buffer_size == 0)
        return;
    const size_t count = std::min(buffer_size - 1, value.size());
    std::memcpy(buffer, value.data(), count);
    buffer[count] = '\0';
}

std::string TrimWhitespace(const std::string& value) {
    auto begin = value.begin();
    while (begin != value.end() && std::isspace(static_cast<unsigned char>(*begin)))
        ++begin;
    auto end = value.end();
    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1))))
        --end;
    return std::string(begin, end);
}

bool SameTermRef(const sacm::Term& term, const core::TerminologyTermRef& term_ref) {
    if (!term_ref.id.empty() && term.id == term_ref.id)
        return true;
    if (!term_ref.gid.empty() && term.gid == term_ref.gid)
        return true;
    return false;
}

bool HasTerminologyPackageRef(const core::TerminologyPackageRef& package_ref) {
    return !package_ref.id.empty() || !package_ref.gid.empty();
}

bool SameTerminologyPackageRef(const core::TerminologyPackageRef& left, const core::TerminologyPackageRef& right) {
    if (!left.id.empty() && !right.id.empty() && left.id == right.id)
        return true;
    if (!left.gid.empty() && !right.gid.empty() && left.gid == right.gid)
        return true;
    return false;
}

bool TermValueHasDuplicate(const AppRuntimeState& state,
                           const core::TerminologyPackageRef& package_ref,
                           const std::string& value,
                           bool editing_existing_term,
                           const core::TerminologyTermRef& selected_term_ref) {
    if (value.empty() || !state.app_state.sacm_package.has_value())
        return false;
    const sacm::TerminologyPackage* package =
        core::FindTerminologyPackage(state.app_state.sacm_package.value(), package_ref);
    if (!package)
        return false;
    for (const auto& term : package->terms) {
        if (editing_existing_term && SameTermRef(term, selected_term_ref))
            continue;
        if (term.value == value)
            return true;
    }
    return false;
}

bool CurrentTermValueHasDuplicate(const AppRuntimeState& state, const std::string& value) {
    return TermValueHasDuplicate(state,
                                 state.selected_terminology_package_ref,
                                 value,
                                 state.editing_existing_terminology_term,
                                 state.selected_terminology_term_ref);
}

std::vector<std::string> SplitCategoryRefs(const std::string& raw) {
    std::string normalized = raw;
    std::replace(normalized.begin(), normalized.end(), ',', ' ');
    std::stringstream stream(normalized);
    std::vector<std::string> refs;
    std::string item;
    while (stream >> item) {
        item = TrimWhitespace(item);
        if (!item.empty() && item.front() == '#')
            item.erase(item.begin());
        if (!item.empty() && std::find(refs.begin(), refs.end(), item) == refs.end())
            refs.push_back(item);
    }
    return refs;
}

std::string JoinCategoryRefs(const std::vector<std::string>& refs) {
    std::string result;
    for (const auto& ref : refs) {
        if (ref.empty())
            continue;
        if (!result.empty())
            result += ", ";
        result += ref;
    }
    return result;
}

bool ContainsCategoryRef(const std::vector<std::string>& refs, const std::string& ref) {
    return std::find(refs.begin(), refs.end(), ref) != refs.end();
}

void SetCategoryChecked(AppRuntimeState& state, const sacm::Category& category, bool checked) {
    std::vector<std::string> refs = SplitCategoryRefs(state.term_categories_buf);
    const std::string ref = !category.id.empty() ? category.id : category.gid;
    if (checked) {
        if (!ContainsCategoryRef(refs, ref))
            refs.push_back(ref);
    } else {
        refs.erase(std::remove(refs.begin(), refs.end(), ref), refs.end());
        if (!category.id.empty())
            refs.erase(std::remove(refs.begin(), refs.end(), category.id), refs.end());
        if (!category.gid.empty())
            refs.erase(std::remove(refs.begin(), refs.end(), category.gid), refs.end());
    }
    CopyToBuffer(state.term_categories_buf, sizeof(state.term_categories_buf), JoinCategoryRefs(refs));
}

void RenderTermCategoryPickerForPackage(AppRuntimeState& state, const core::TerminologyPackageRef& package_ref) {
    const sacm::TerminologyPackage* package = nullptr;
    if (state.app_state.sacm_package.has_value()) {
        package = core::FindTerminologyPackage(state.app_state.sacm_package.value(), package_ref);
    }

    ImGui::TextUnformatted("Categories");
    if (!package || package->categories.empty()) {
        ImGui::TextDisabled("No categories are available in this terminology package.");
        return;
    }

    std::vector<std::string> refs = SplitCategoryRefs(state.term_categories_buf);
    const float list_height = ImGui::GetTextLineHeightWithSpacing() * 5.0f;
    if (ImGui::BeginChild("##term_category_picker", ImVec2(460.0f, list_height), true)) {
        for (const auto& category : package->categories) {
            const std::string ref = !category.id.empty() ? category.id : category.gid;
            bool selected = ContainsCategoryRef(refs, ref) || ContainsCategoryRef(refs, category.gid);
            const std::string label = (category.name.empty() ? ref : category.name) + "##" + ref;
            if (ImGui::Checkbox(label.c_str(), &selected))
                SetCategoryChecked(state, category, selected);
        }
    }
    ImGui::EndChild();
}

void RenderTermCategoryPicker(AppRuntimeState& state) {
    RenderTermCategoryPickerForPackage(state, state.selected_terminology_package_ref);
}

struct TerminologyPackageChoice {
    core::TerminologyPackageRef ref;
    std::string label;
};

core::TerminologyPackageRef TerminologyPackageRefFor(const sacm::TerminologyPackage& package) {
    return core::TerminologyPackageRef{package.id, package.gid};
}

std::string PackageDisplayLabel(const sacm::TerminologyPackage& package, const std::string& scope_label) {
    const std::string fallback = !package.id.empty() ? package.id : package.gid;
    const std::string name = package.name.empty() ? fallback : package.name;
    return scope_label.empty() ? name : scope_label + ": " + name;
}

std::vector<TerminologyPackageChoice> BuildTerminologyPackageChoices(const AppRuntimeState& state) {
    std::vector<TerminologyPackageChoice> choices;
    if (!state.app_state.sacm_package.has_value())
        return choices;

    const sacm::AssuranceCasePackage& package = state.app_state.sacm_package.value();
    for (const auto& terminology_package : package.terminologyPackages) {
        choices.push_back({TerminologyPackageRefFor(terminology_package),
                           PackageDisplayLabel(terminology_package, "Assurance case")});
    }
    for (const auto& argument_package : package.argumentPackages) {
        const std::string argument_label =
            argument_package.name.empty() ? (!argument_package.id.empty() ? argument_package.id : argument_package.gid)
                                          : argument_package.name;
        for (const auto& terminology_package : argument_package.terminologyPackages) {
            choices.push_back({TerminologyPackageRefFor(terminology_package),
                               PackageDisplayLabel(terminology_package, argument_label)});
        }
    }
    return choices;
}

int FindTerminologyPackageChoiceIndex(const std::vector<TerminologyPackageChoice>& choices,
                                      const core::TerminologyPackageRef& package_ref) {
    for (std::size_t index = 0; index < choices.size(); ++index) {
        if (SameTerminologyPackageRef(choices[index].ref, package_ref))
            return static_cast<int>(index);
    }
    return -1;
}

std::string PackageChoiceWidgetLabel(const TerminologyPackageChoice& choice, std::size_t index) {
    const std::string ref = !choice.ref.id.empty() ? choice.ref.id : choice.ref.gid;
    return choice.label + "##quick_define_target_package_" + ref + "_" + std::to_string(index);
}

} // namespace

void AppRuntime::RenderNotImplementedModal() {
    if (!impl_->modal_coordinator->show_not_implemented_modal)
        return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("##not_implemented_modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s is not implemented yet.", impl_->modal_coordinator->not_implemented_feature.c_str());
        ImGui::Spacing();
        ImGui::Spacing();

        float button_width = 100.0f;
        float modal_width = ImGui::GetWindowWidth();
        float center_x = (modal_width - button_width) * 0.5f;
        ImGui::SetCursorPosX(center_x);
        if (ImGui::Button("OK", ImVec2(button_width, 0))) {
            impl_->modal_coordinator->show_not_implemented_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (impl_->modal_coordinator->show_not_implemented_modal) {
        ImGui::OpenPopup("##not_implemented_modal");
    }
}

void AppRuntime::RenderRemoveConfirmModal() {
    if (!impl_->element_edit_controller->ShouldShowRemoveConfirm())
        return;

    auto cancel = [&]() {
        impl_->element_edit_controller->CancelPendingRemoval();
        ui::GetUiState().marked_for_removal.clear();
    };

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("##remove_confirm_modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const int n = static_cast<int>(impl_->element_edit_controller->PendingRemoveIds().size());
        const char* mode_label = impl_->element_edit_controller->PendingRemoveMode() == core::RemoveMode::NodeOnly
                                     ? "this node and its attachments"
                                     : "this node and its descendants";
        ImGui::Text("Remove %s?", mode_label);
        ImGui::Text("%d element%s will be deleted (highlighted in red).", n, n == 1 ? "" : "s");
        ImGui::Spacing();
        ImGui::Spacing();

        const float button_width = 110.0f;
        const float spacing = 10.0f;
        const float total_width = button_width * 2.0f + spacing;
        const float center_x = (ImGui::GetWindowWidth() - total_width) * 0.5f;
        ImGui::SetCursorPosX(center_x);

        if (ImGui::Button("Remove", ImVec2(button_width, 0))) {
            ImGui::CloseCurrentPopup();
            if (impl_->app_state.loaded_case.has_value()) {
                parser::AssuranceCase& ac = impl_->app_state.loaded_case.value();
                sacm::AssuranceCasePackage* pkg =
                    impl_->app_state.sacm_package.has_value() ? &impl_->app_state.sacm_package.value() : nullptr;
                impl_->element_edit_controller->ConfirmPendingRemoval(ac, pkg);
            }
            ui::GetUiState().marked_for_removal.clear();
        }
        ImGui::SameLine(0.0f, spacing);
        if (ImGui::Button("Cancel", ImVec2(button_width, 0))) {
            cancel();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (impl_->element_edit_controller->ShouldShowRemoveConfirm()) {
        ImGui::OpenPopup("##remove_confirm_modal");
    }
}

void AppRuntime::RenderDeleteReviewItemConfirmModal() {
    if (!impl_->review_controller->ShouldShowDeleteConfirm())
        return;

    auto cancel = [&]() { impl_->review_controller->CancelDeleteReviewItem(); };

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Delete Review Comment", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const core::reviews::ReviewItem& item = impl_->review_controller->PendingDeleteReviewItem();
        ImGui::TextWrapped("Delete this review comment?");
        ImGui::TextWrapped("The attached proposal will also be deleted.");
        if (item.proposal_id.has_value()) {
            ImGui::TextDisabled("Proposal: %s", item.proposal_id->c_str());
        }
        ImGui::Spacing();
        ImGui::Spacing();

        const float button_width = 130.0f;
        const float spacing = 10.0f;
        const float total_width = button_width * 2.0f + spacing;
        const float center_x = (ImGui::GetWindowWidth() - total_width) * 0.5f;
        ImGui::SetCursorPosX(center_x);

        if (ImGui::Button("Delete Both", ImVec2(button_width, 0))) {
            core::reviews::ReviewItem pending = impl_->review_controller->PendingDeleteReviewItem();
            cancel();
            ImGui::CloseCurrentPopup();
            DeleteReviewItem(pending);
        }
        ImGui::SameLine(0.0f, spacing);
        if (ImGui::Button("Cancel", ImVec2(button_width, 0))) {
            cancel();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (impl_->review_controller->ShouldShowDeleteConfirm()) {
        ImGui::OpenPopup("Delete Review Comment");
    }
}

void AppRuntime::RenderStartupProjectWindow() {
    ui::panels::WelcomeModalCallbacks callbacks{
        [this]() {
            BeginCreateProject();
            if (impl_->project_controller->show_create_project_modal) {
                impl_->project_controller->show_startup_project_window = false;
            }
        },
        []() {},
        [this]() { BeginOpenProject(); },
        []() {},
        []() {},
        []() {},
        []() {},
        [this](const ui::panels::RecentProjectEntry& entry) {
            if (!TryOpenProjectManifest(entry.path)) {
                impl_->project_controller->RemoveRecentProjectByPath(entry.path);
            }
        },
    };
    ui::panels::ShowWelcomeModal(
        impl_->project_controller->show_startup_project_window, impl_->project_controller->recent_projects, callbacks);
}

void AppRuntime::RenderCreateProjectModal() {
    if (!impl_->project_controller->show_create_project_modal)
        return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Create Empty Assurance Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Project name");
        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputText("##project_name",
                         impl_->project_controller->project_name_buf,
                         sizeof(impl_->project_controller->project_name_buf));

        ImGui::TextUnformatted("Parent location");
        ImGui::TextDisabled("%s", impl_->project_controller->project_parent_buf);

        ImGui::Spacing();

        if (ImGui::Button("Create", ImVec2(110.0f, 0.0f))) {
            if (impl_->app_state.create_empty_project(impl_->project_controller->project_name_buf,
                                                      impl_->project_controller->project_parent_buf)) {
                impl_->document_dirty = false;
                impl_->review_controller->ClearDirty();
                if (impl_->app_state.current_project.has_value()) {
                    impl_->proposal_controller->manager.SetProjectRoot(impl_->app_state.current_project->rootPath);
                    EnsureReviewItemStorage();
                }
                OpenFirstProjectSacmFile();
                TouchCurrentProjectRecent();
                impl_->project_controller->show_create_project_modal = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f))) {
            impl_->project_controller->show_create_project_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (impl_->project_controller->show_create_project_modal) {
        ImGui::OpenPopup("Create Empty Assurance Project");
    }
}

void AppRuntime::RenderProjectFileNameModal() {
    if (!impl_->project_controller->show_project_file_name_modal)
        return;

    const char* title = ProjectFileCreateTitle(impl_->project_controller->pending_project_file_kind);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("File name");
        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputText("##project_file_name",
                         impl_->project_controller->project_file_name_buf,
                         sizeof(impl_->project_controller->project_file_name_buf));
        ImGui::Spacing();

        if (ImGui::Button("Create", ImVec2(110.0f, 0.0f))) {
            bool created = false;
            if (impl_->project_controller->pending_project_file_kind == ProjectFileCreateKind::Sacm) {
                created = impl_->app_state.create_project_sacm_file(impl_->project_controller->project_file_name_buf);
            } else if (impl_->project_controller->pending_project_file_kind ==
                       ProjectFileCreateKind::EvidenceRegister) {
                created =
                    impl_->app_state.create_project_evidence_register(impl_->project_controller->project_file_name_buf);
            } else {
                created = impl_->app_state.create_project_j3377_cae_register(
                    impl_->project_controller->project_file_name_buf);
            }
            if (created) {
                impl_->project_controller->show_project_file_name_modal = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f))) {
            impl_->project_controller->show_project_file_name_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (impl_->project_controller->show_project_file_name_modal) {
        ImGui::OpenPopup(title);
    }
}

void AppRuntime::RenderProjectLoadReportModal() {
    auto& report = impl_->app_state.last_project_load_report;
    if (!report.showPopup)
        return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Project Loading Status", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        for (const auto& step : report.steps) {
            const char* mark = "[OK]";
            if (step.status == core::ProjectLoadStepStatus::Failed)
                mark = "[FAIL]";
            if (step.status == core::ProjectLoadStepStatus::Warning)
                mark = "[WARN]";
            ImGui::Text("%s %s", mark, step.label.c_str());
            if (!step.message.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", step.message.c_str());
            }
        }
        if (!report.warnings.empty()) {
            ImGui::Separator();
            ImGui::TextUnformatted("External changes detected");
            for (const auto& warning : report.warnings) {
                ImGui::BulletText("%s", warning.c_str());
            }
        }
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(100.0f, 0.0f))) {
            report.showPopup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (report.showPopup) {
        ImGui::OpenPopup("Project Loading Status");
    }
}

void AppRuntime::RenderSaveBeforeExitModal(bool& done) {
    if (!impl_->modal_coordinator->show_save_before_exit_modal)
        return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("You have unsaved changes. Save before closing?");
        ImGui::Spacing();

        if (ImGui::Button("Save", ImVec2(100.0f, 0.0f))) {
            bool saved = false;
            if (impl_->app_state.current_project.has_value()) {
                saved = SaveProject();
            } else {
                saved = impl_->app_state.save_current_document();
            }

            if (saved) {
                impl_->modal_coordinator->show_save_before_exit_modal = false;
                done = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(100.0f, 0.0f))) {
            impl_->modal_coordinator->show_save_before_exit_modal = false;
            done = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            impl_->modal_coordinator->show_save_before_exit_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (impl_->modal_coordinator->show_save_before_exit_modal) {
        ImGui::OpenPopup("Unsaved Changes");
    }
}

void AppRuntime::RenderCreateTerminologyPackageModal() {
    if (!impl_->show_create_terminology_package_modal)
        return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Create Terminology Package", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputText(
            "Package name", impl_->new_terminology_package_name_buf, sizeof(impl_->new_terminology_package_name_buf));
        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputTextMultiline("Package description",
                                  impl_->new_terminology_package_description_buf,
                                  sizeof(impl_->new_terminology_package_description_buf),
                                  ImVec2(420.0f, 96.0f));
        ImGui::Spacing();

        const bool can_create = !TrimWhitespace(impl_->new_terminology_package_name_buf).empty();
        if (!can_create)
            ImGui::BeginDisabled();
        if (ImGui::Button("Create", ImVec2(100.0f, 0.0f))) {
            ConfirmAddTerminologyPackage();
            if (!impl_->show_create_terminology_package_modal)
                ImGui::CloseCurrentPopup();
        }
        if (!can_create)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            impl_->show_create_terminology_package_modal = false;
            impl_->pending_terminology_package_parent_entry.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (impl_->show_create_terminology_package_modal) {
        ImGui::OpenPopup("Create Terminology Package");
    }
}

void AppRuntime::RenderDeleteTerminologyPackageModal() {
    if (!impl_->show_delete_terminology_package_modal)
        return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Delete Terminology Package", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Delete this terminology package?");
        ImGui::Spacing();
        if (ImGui::Button("Delete", ImVec2(100.0f, 0.0f))) {
            ConfirmDeleteTerminologyPackage();
            if (!impl_->show_delete_terminology_package_modal)
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            impl_->show_delete_terminology_package_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (impl_->show_delete_terminology_package_modal) {
        ImGui::OpenPopup("Delete Terminology Package");
    }
}

void AppRuntime::RenderTerminologyTermEditorModal() {
    if (!impl_->show_terminology_term_editor_modal)
        return;

    const char* title = impl_->editing_existing_terminology_term ? "Edit Term" : "Create Term";
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(460.0f);
        ImGui::InputText("Term", impl_->term_value_buf, sizeof(impl_->term_value_buf));
        ImGui::SetNextItemWidth(460.0f);
        ImGui::InputText("Full Name / Display Name", impl_->term_name_buf, sizeof(impl_->term_name_buf));
        ImGui::SetNextItemWidth(460.0f);
        ImGui::InputTextMultiline(
            "Definition", impl_->term_definition_buf, sizeof(impl_->term_definition_buf), ImVec2(460.0f, 110.0f));
        RenderTermCategoryPicker(*impl_);
        ImGui::SetNextItemWidth(460.0f);
        ImGui::InputText(
            "External Reference", impl_->term_external_reference_buf, sizeof(impl_->term_external_reference_buf));
        ImGui::SetNextItemWidth(460.0f);
        ImGui::InputText("Origin", impl_->term_origin_buf, sizeof(impl_->term_origin_buf));

        const std::string value = TrimWhitespace(impl_->term_value_buf);
        const bool can_save = !value.empty();
        if (!can_save)
            ImGui::TextColored(ImVec4(0.9f, 0.25f, 0.2f, 1.0f), "Term value is required.");
        if (CurrentTermValueHasDuplicate(*impl_, value))
            ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.15f, 1.0f), "Duplicate term value exists in this package.");
        if (TrimWhitespace(impl_->term_definition_buf).empty())
            ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.15f, 1.0f), "Concrete term has no description.");
        if (TrimWhitespace(impl_->term_categories_buf).empty())
            ImGui::TextDisabled("Term has no category.");

        ImGui::Spacing();
        if (!can_save)
            ImGui::BeginDisabled();
        if (ImGui::Button(impl_->editing_existing_terminology_term ? "Save" : "Create", ImVec2(100.0f, 0.0f))) {
            ConfirmTerminologyTermEdit();
            if (!impl_->show_terminology_term_editor_modal)
                ImGui::CloseCurrentPopup();
        }
        if (!can_save)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            impl_->show_terminology_term_editor_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (impl_->show_terminology_term_editor_modal) {
        ImGui::OpenPopup(title);
    }
}

void AppRuntime::RenderQuickDefineTermModal() {
    if (!impl_->show_quick_define_term_modal)
        return;

    std::vector<TerminologyPackageChoice> package_choices = BuildTerminologyPackageChoices(*impl_);
    int selected_package_index =
        FindTerminologyPackageChoiceIndex(package_choices, impl_->quick_define_target_package_ref);
    if (selected_package_index < 0 && !package_choices.empty()) {
        selected_package_index = 0;
        impl_->quick_define_target_package_ref = package_choices.front().ref;
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Create Term##quick_define_term", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(460.0f);
        ImGui::InputText("Term", impl_->term_value_buf, sizeof(impl_->term_value_buf));
        ImGui::SetNextItemWidth(460.0f);
        ImGui::InputText("Full Name / Display Name", impl_->term_name_buf, sizeof(impl_->term_name_buf));
        ImGui::SetNextItemWidth(460.0f);
        ImGui::InputTextMultiline(
            "Definition", impl_->term_definition_buf, sizeof(impl_->term_definition_buf), ImVec2(460.0f, 110.0f));

        ImGui::TextUnformatted("Store in");
        if (package_choices.empty()) {
            ImGui::TextColored(ImVec4(0.9f, 0.25f, 0.2f, 1.0f), "No TerminologyPackage is available.");
        } else {
            const char* preview = package_choices[static_cast<std::size_t>(selected_package_index)].label.c_str();
            ImGui::SetNextItemWidth(460.0f);
            if (ImGui::BeginCombo("##quick_define_target_package", preview)) {
                for (std::size_t index = 0; index < package_choices.size(); ++index) {
                    const bool selected = static_cast<int>(index) == selected_package_index;
                    const std::string selectable_label = PackageChoiceWidgetLabel(package_choices[index], index);
                    if (ImGui::Selectable(selectable_label.c_str(), selected)) {
                        selected_package_index = static_cast<int>(index);
                        impl_->quick_define_target_package_ref = package_choices[index].ref;
                        CopyToBuffer(impl_->term_categories_buf, sizeof(impl_->term_categories_buf), "");
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        RenderTermCategoryPickerForPackage(*impl_, impl_->quick_define_target_package_ref);
        ImGui::SetNextItemWidth(460.0f);
        ImGui::InputText(
            "External Reference", impl_->term_external_reference_buf, sizeof(impl_->term_external_reference_buf));

        const std::string value = TrimWhitespace(impl_->term_value_buf);
        const bool has_target_package =
            HasTerminologyPackageRef(impl_->quick_define_target_package_ref) &&
            impl_->app_state.sacm_package.has_value() &&
            core::FindTerminologyPackage(impl_->app_state.sacm_package.value(), impl_->quick_define_target_package_ref);
        const bool can_create = !value.empty() && has_target_package;
        if (value.empty())
            ImGui::TextColored(ImVec4(0.9f, 0.25f, 0.2f, 1.0f), "Term value is required.");
        if (!has_target_package)
            ImGui::TextColored(ImVec4(0.9f, 0.25f, 0.2f, 1.0f), "Choose a target TerminologyPackage.");
        if (TermValueHasDuplicate(*impl_, impl_->quick_define_target_package_ref, value, false, {}))
            ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.15f, 1.0f), "Duplicate term value exists in this package.");
        if (TrimWhitespace(impl_->term_definition_buf).empty())
            ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.15f, 1.0f), "Concrete term has no description.");
        if (TrimWhitespace(impl_->term_categories_buf).empty())
            ImGui::TextDisabled("Term has no category.");

        ImGui::Spacing();
        if (!can_create)
            ImGui::BeginDisabled();
        if (ImGui::Button("Create", ImVec2(100.0f, 0.0f))) {
            ConfirmQuickDefineTerminologyTerm(false);
            if (!impl_->show_quick_define_term_modal)
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Create + Add as Context", ImVec2(185.0f, 0.0f))) {
            ConfirmQuickDefineTerminologyTerm(true);
            if (!impl_->show_quick_define_term_modal)
                ImGui::CloseCurrentPopup();
        }
        if (!can_create)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            impl_->show_quick_define_term_modal = false;
            impl_->quick_define_element_id.clear();
            impl_->quick_define_source_text.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (impl_->show_quick_define_term_modal) {
        ImGui::OpenPopup("Create Term##quick_define_term");
    }
}

void AppRuntime::RenderDeleteTerminologyTermModal() {
    if (!impl_->show_delete_terminology_term_modal)
        return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Delete Term", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Delete this term?");
        if (impl_->pending_delete_terminology_term_usage_count > 0) {
            ImGui::TextWrapped("This term value appears %d time(s) in the current SACM model.",
                               impl_->pending_delete_terminology_term_usage_count);
        }
        ImGui::Spacing();
        if (ImGui::Button("Delete", ImVec2(100.0f, 0.0f))) {
            ConfirmDeleteTerminologyTerm();
            if (!impl_->show_delete_terminology_term_modal)
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            impl_->show_delete_terminology_term_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (impl_->show_delete_terminology_term_modal) {
        ImGui::OpenPopup("Delete Term");
    }
}

void AppRuntime::RenderTerminologyCategoryEditorModal() {
    if (!impl_->show_terminology_category_editor_modal)
        return;

    const char* title = impl_->editing_existing_terminology_category ? "Edit Category" : "Create Category";
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputText("Category name", impl_->category_name_buf, sizeof(impl_->category_name_buf));
        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputTextMultiline("Category description",
                                  impl_->category_description_buf,
                                  sizeof(impl_->category_description_buf),
                                  ImVec2(420.0f, 96.0f));

        const bool can_save = !TrimWhitespace(impl_->category_name_buf).empty();
        if (!can_save)
            ImGui::TextColored(ImVec4(0.9f, 0.25f, 0.2f, 1.0f), "Category name is required.");

        ImGui::Spacing();
        if (!can_save)
            ImGui::BeginDisabled();
        if (ImGui::Button(impl_->editing_existing_terminology_category ? "Save" : "Create", ImVec2(100.0f, 0.0f))) {
            ConfirmTerminologyCategoryEdit();
            if (!impl_->show_terminology_category_editor_modal)
                ImGui::CloseCurrentPopup();
        }
        if (!can_save)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            impl_->show_terminology_category_editor_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (impl_->show_terminology_category_editor_modal) {
        ImGui::OpenPopup(title);
    }
}

void AppRuntime::RenderDeleteTerminologyCategoryModal() {
    if (!impl_->show_delete_terminology_category_modal)
        return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Delete Category", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Delete this category?");
        if (impl_->pending_delete_terminology_category_term_count > 0) {
            ImGui::TextWrapped("This category is assigned to %d term(s). Remove those assignments before deleting it.",
                               impl_->pending_delete_terminology_category_term_count);
        }
        ImGui::Spacing();
        if (impl_->pending_delete_terminology_category_term_count > 0)
            ImGui::BeginDisabled();
        if (ImGui::Button("Delete", ImVec2(100.0f, 0.0f))) {
            ConfirmDeleteTerminologyCategory();
            if (!impl_->show_delete_terminology_category_modal)
                ImGui::CloseCurrentPopup();
        }
        if (impl_->pending_delete_terminology_category_term_count > 0)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            impl_->show_delete_terminology_category_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (impl_->show_delete_terminology_category_modal) {
        ImGui::OpenPopup("Delete Category");
    }
}

void AppRuntime::RenderReviewerNamePromptModal() {
    if (!impl_->modal_coordinator->show_reviewer_name_prompt)
        return;
    if (impl_->project_controller->show_startup_project_window)
        return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Reviewer Name", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Enter the name to use for review comments.");
        ImGui::SetNextItemWidth(360.0f);
        ImGui::InputText("##startup_reviewer_name", impl_->reviewer_name_buf, sizeof(impl_->reviewer_name_buf));
        ImGui::Spacing();

        const std::string draft = TrimWhitespace(impl_->reviewer_name_buf);
        if (draft.empty())
            ImGui::BeginDisabled();
        if (ImGui::Button("Save", ImVec2(100.0f, 0.0f))) {
            impl_->reviewer_name = draft;
            CopyToBuffer(impl_->reviewer_name_buf, sizeof(impl_->reviewer_name_buf), impl_->reviewer_name);
            impl_->modal_coordinator->show_reviewer_name_prompt = false;
            ImGui::CloseCurrentPopup();
        }
        if (draft.empty())
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Later", ImVec2(100.0f, 0.0f))) {
            impl_->modal_coordinator->show_reviewer_name_prompt = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (impl_->modal_coordinator->show_reviewer_name_prompt) {
        ImGui::OpenPopup("Reviewer Name");
    }
}

} // namespace app
