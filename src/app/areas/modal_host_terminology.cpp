#include "app/areas/modal_host_internal.h"

#include "app/app_runtime_state.h"
#include "core/string_utils.h"
#include "core/terminology_text_utils.h"
#include "imgui.h"
#include "ui/i18n/localization.h"
#include "ui/imgui_buffer_utils.h"
#include "ui/theme.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace app::areas {
namespace {

using core::TrimWhitespace;
using ui::CopyToBuffer;

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

bool TermDefinitionHasDuplicate(const AppRuntimeState& state,
                                const core::TerminologyPackageRef& package_ref,
                                const std::string& value,
                                const std::string& description,
                                bool editing_existing_term,
                                const core::TerminologyTermRef& selected_term_ref) {
    if (value.empty() || description.empty() || !state.app_state.sacm_package.has_value())
        return false;
    const sacm::TerminologyPackage* package =
        core::FindTerminologyPackage(state.app_state.sacm_package.value(), package_ref);
    if (!package)
        return false;
    for (const auto& term : package->terms) {
        if (editing_existing_term && SameTermRef(term, selected_term_ref))
            continue;
        if (TrimWhitespace(term.value) == value && TrimWhitespace(term.description) == description)
            return true;
    }
    return false;
}

bool CurrentTermDefinitionHasDuplicate(const AppRuntimeState& state,
                                       const std::string& value,
                                       const std::string& description) {
    return TermDefinitionHasDuplicate(state,
                                      state.terminology.selected_package_ref,
                                      value,
                                      description,
                                      state.terminology.editing_existing_term,
                                      state.terminology.selected_term_ref);
}

bool ContainsCategoryRef(const std::vector<std::string>& refs, const std::string& ref) {
    return std::find(refs.begin(), refs.end(), ref) != refs.end();
}

void SetCategoryChecked(AppRuntimeState& state, const sacm::Category& category, bool checked) {
    std::vector<std::string> refs = core::SplitNormalizedCategoryRefs(state.terminology.term_categories_buf);
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
    CopyToBuffer(state.terminology.term_categories_buf,
                 sizeof(state.terminology.term_categories_buf),
                 core::JoinCategoryRefs(refs));
}

void RenderTermCategoryPickerForPackage(AppRuntimeState& state, const core::TerminologyPackageRef& package_ref) {
    const sacm::TerminologyPackage* package = nullptr;
    if (state.app_state.sacm_package.has_value()) {
        package = core::FindTerminologyPackage(state.app_state.sacm_package.value(), package_ref);
    }

    ImGui::TextUnformatted(AF_TR("Categories").c_str());
    if (!package || package->categories.empty()) {
        ImGui::TextDisabled("%s", AF_TR("No categories are available in this terminology package.").c_str());
        return;
    }

    std::vector<std::string> refs = core::SplitNormalizedCategoryRefs(state.terminology.term_categories_buf);
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
    RenderTermCategoryPickerForPackage(state, state.terminology.selected_package_ref);
}

void RenderTermTextFields(AppRuntimeState& state) {
    ImGui::SetNextItemWidth(460.0f);
    ImGui::InputText(AF_TR("Term").c_str(), state.terminology.term_value_buf, sizeof(state.terminology.term_value_buf));
    ImGui::SetNextItemWidth(460.0f);
    ImGui::InputText(
        AF_TR("Full Name / Display Name").c_str(), state.terminology.term_name_buf, sizeof(state.terminology.term_name_buf));
    ImGui::SetNextItemWidth(460.0f);
    ImGui::InputTextMultiline(AF_TR("Definition").c_str(),
                              state.terminology.term_definition_buf,
                              sizeof(state.terminology.term_definition_buf),
                              ImVec2(460.0f, 110.0f));
}

void RenderTermExternalReferenceField(AppRuntimeState& state) {
    ImGui::SetNextItemWidth(460.0f);
    ImGui::InputText(AF_TR("External Reference").c_str(),
                     state.terminology.term_external_reference_buf,
                     sizeof(state.terminology.term_external_reference_buf));
}

void RenderTerminologyTermValidationMessages(bool missing_value,
                                             bool missing_target_package,
                                             bool duplicate_definition,
                                             bool missing_definition,
                                             bool missing_category) {
    if (missing_value)
        ImGui::TextColored(ui::GetErrorColor(), "%s", AF_TR("Term value is required.").c_str());
    if (missing_target_package)
        ImGui::TextColored(ui::GetErrorColor(), "%s", AF_TR("Choose a target TerminologyPackage.").c_str());
    if (duplicate_definition)
        ImGui::TextColored(
            ui::GetWarningColor(), "%s", AF_TR("Duplicate term value and definition exist in this package.").c_str());
    if (missing_definition)
        ImGui::TextColored(ui::GetWarningColor(), "%s", AF_TR("Concrete term has no description.").c_str());
    if (missing_category)
        ImGui::TextDisabled("%s", AF_TR("Term has no category.").c_str());
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
                           PackageDisplayLabel(terminology_package, AF_TR("Assurance case"))});
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

void ModalHost::RenderCreateTerminologyPackageModal() {
    if (!state_.terminology.show_create_package_modal)
        return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal((AF_TR("Create Terminology Package") + "###Create Terminology Package").c_str(),
                               nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputText(AF_TR("Package name").c_str(),
                         state_.terminology.new_package_name_buf,
                         sizeof(state_.terminology.new_package_name_buf));
        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputTextMultiline(AF_TR("Package description").c_str(),
                                  state_.terminology.new_package_description_buf,
                                  sizeof(state_.terminology.new_package_description_buf),
                                  ImVec2(420.0f, 96.0f));
        ImGui::Spacing();

        const bool can_create = !TrimWhitespace(state_.terminology.new_package_name_buf).empty();
        if (!can_create)
            ImGui::BeginDisabled();
        if (ImGui::Button(AF_TR("Create").c_str(), ImVec2(100.0f, 0.0f))) {
            callbacks_.confirm_add_terminology_package();
            if (!state_.terminology.show_create_package_modal)
                ImGui::CloseCurrentPopup();
        }
        if (!can_create)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(AF_TR("Cancel").c_str(), ImVec2(100.0f, 0.0f))) {
            state_.terminology.show_create_package_modal = false;
            state_.terminology.pending_package_parent_entry.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.terminology.show_create_package_modal) {
        ImGui::OpenPopup((AF_TR("Create Terminology Package") + "###Create Terminology Package").c_str());
    }
}

void ModalHost::RenderDeleteTerminologyPackageModal() {
    if (!state_.terminology.show_delete_package_modal)
        return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal((AF_TR("Delete Terminology Package") + "###Delete Terminology Package").c_str(),
                               nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", AF_TR("Delete this terminology package?").c_str());
        ImGui::Spacing();
        if (ImGui::Button(AF_TR("Delete").c_str(), ImVec2(100.0f, 0.0f))) {
            callbacks_.confirm_delete_terminology_package();
            if (!state_.terminology.show_delete_package_modal)
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(AF_TR("Cancel").c_str(), ImVec2(100.0f, 0.0f))) {
            state_.terminology.show_delete_package_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.terminology.show_delete_package_modal) {
        ImGui::OpenPopup((AF_TR("Delete Terminology Package") + "###Delete Terminology Package").c_str());
    }
}

void ModalHost::RenderTerminologyTermEditorModal() {
    if (!state_.terminology.show_term_editor_modal)
        return;

    const char* title_key = state_.terminology.editing_existing_term ? "Edit Term" : "Create Term";
    const std::string title = AF_TR(title_key) + "###" + title_key;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        RenderTermTextFields(state_);
        RenderTermCategoryPicker(state_);
        RenderTermExternalReferenceField(state_);
        ImGui::SetNextItemWidth(460.0f);
        ImGui::InputText(
            AF_TR("Origin").c_str(), state_.terminology.term_origin_buf, sizeof(state_.terminology.term_origin_buf));

        const std::string value = TrimWhitespace(state_.terminology.term_value_buf);
        const std::string description = TrimWhitespace(state_.terminology.term_definition_buf);
        const bool can_save = !value.empty();
        RenderTerminologyTermValidationMessages(!can_save,
                                                false,
                                                CurrentTermDefinitionHasDuplicate(state_, value, description),
                                                description.empty(),
                                                TrimWhitespace(state_.terminology.term_categories_buf).empty());

        ImGui::Spacing();
        if (!can_save)
            ImGui::BeginDisabled();
        if (ImGui::Button((state_.terminology.editing_existing_term ? AF_TR("Save") : AF_TR("Create")).c_str(),
                          ImVec2(100.0f, 0.0f))) {
            callbacks_.confirm_terminology_term_edit();
            if (!state_.terminology.show_term_editor_modal)
                ImGui::CloseCurrentPopup();
        }
        if (!can_save)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(AF_TR("Cancel").c_str(), ImVec2(100.0f, 0.0f))) {
            state_.terminology.show_term_editor_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.terminology.show_term_editor_modal) {
        ImGui::OpenPopup(title.c_str());
    }
}

void ModalHost::RenderQuickDefineTermModal() {
    if (!state_.terminology.show_quick_define_term_modal)
        return;

    std::vector<TerminologyPackageChoice> package_choices = BuildTerminologyPackageChoices(state_);
    int selected_package_index =
        FindTerminologyPackageChoiceIndex(package_choices, state_.terminology.quick_define_target_package_ref);
    if (selected_package_index < 0 && !package_choices.empty()) {
        selected_package_index = 0;
        state_.terminology.quick_define_target_package_ref = package_choices.front().ref;
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal((AF_TR("Create Term") + "###quick_define_term").c_str(),
                               nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        RenderTermTextFields(state_);

        ImGui::TextUnformatted(AF_TR("Store in").c_str());
        if (package_choices.empty()) {
            ImGui::TextColored(ui::GetErrorColor(), "%s", AF_TR("No TerminologyPackage is available.").c_str());
        } else {
            const char* preview = package_choices[static_cast<std::size_t>(selected_package_index)].label.c_str();
            ImGui::SetNextItemWidth(460.0f);
            if (ImGui::BeginCombo("##quick_define_target_package", preview)) {
                for (std::size_t index = 0; index < package_choices.size(); ++index) {
                    const bool selected = static_cast<int>(index) == selected_package_index;
                    const std::string selectable_label = PackageChoiceWidgetLabel(package_choices[index], index);
                    if (ImGui::Selectable(selectable_label.c_str(), selected)) {
                        selected_package_index = static_cast<int>(index);
                        state_.terminology.quick_define_target_package_ref = package_choices[index].ref;
                        CopyToBuffer(
                            state_.terminology.term_categories_buf, sizeof(state_.terminology.term_categories_buf), "");
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        RenderTermCategoryPickerForPackage(state_, state_.terminology.quick_define_target_package_ref);
        RenderTermExternalReferenceField(state_);

        const std::string value = TrimWhitespace(state_.terminology.term_value_buf);
        const std::string description = TrimWhitespace(state_.terminology.term_definition_buf);
        const bool has_target_package =
            HasTerminologyPackageRef(state_.terminology.quick_define_target_package_ref) &&
            state_.app_state.sacm_package.has_value() &&
            core::FindTerminologyPackage(state_.app_state.sacm_package.value(),
                                         state_.terminology.quick_define_target_package_ref);
        const bool can_create = !value.empty() && has_target_package;
        RenderTerminologyTermValidationMessages(
            value.empty(),
            !has_target_package,
            TermDefinitionHasDuplicate(
                state_, state_.terminology.quick_define_target_package_ref, value, description, false, {}),
            description.empty(),
            TrimWhitespace(state_.terminology.term_categories_buf).empty());

        ImGui::Spacing();
        if (!can_create)
            ImGui::BeginDisabled();
        if (ImGui::Button(AF_TR("Create").c_str(), ImVec2(100.0f, 0.0f))) {
            callbacks_.confirm_quick_define_terminology_term(false);
            if (!state_.terminology.show_quick_define_term_modal)
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(AF_TR("Create + Add as Context").c_str(), ImVec2(185.0f, 0.0f))) {
            callbacks_.confirm_quick_define_terminology_term(true);
            if (!state_.terminology.show_quick_define_term_modal)
                ImGui::CloseCurrentPopup();
        }
        if (!can_create)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(AF_TR("Cancel").c_str(), ImVec2(100.0f, 0.0f))) {
            state_.terminology.show_quick_define_term_modal = false;
            state_.terminology.quick_define_element_id.clear();
            state_.terminology.quick_define_source_text.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.terminology.show_quick_define_term_modal) {
        ImGui::OpenPopup((AF_TR("Create Term") + "###quick_define_term").c_str());
    }
}

void ModalHost::RenderDeleteTerminologyTermModal() {
    if (!state_.terminology.show_delete_term_modal)
        return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal((AF_TR("Delete Term") + "###Delete Term").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", AF_TR("Delete this term?").c_str());
        if (state_.terminology.pending_delete_term_usage_count > 0) {
            ImGui::TextWrapped("%s",
                               ui::i18n::trnf("This term value appears {0} time in the current SACM model.",
                                              "This term value appears {0} times in the current SACM model.",
                                              state_.terminology.pending_delete_term_usage_count,
                                              state_.terminology.pending_delete_term_usage_count)
                                   .c_str());
        }
        ImGui::Spacing();
        if (ImGui::Button(AF_TR("Delete").c_str(), ImVec2(100.0f, 0.0f))) {
            callbacks_.confirm_delete_terminology_term();
            if (!state_.terminology.show_delete_term_modal)
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(AF_TR("Cancel").c_str(), ImVec2(100.0f, 0.0f))) {
            state_.terminology.show_delete_term_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.terminology.show_delete_term_modal) {
        ImGui::OpenPopup((AF_TR("Delete Term") + "###Delete Term").c_str());
    }
}

void ModalHost::RenderTerminologyCategoryEditorModal() {
    if (!state_.terminology.show_category_editor_modal)
        return;

    const char* title_key = state_.terminology.editing_existing_category ? "Edit Category" : "Create Category";
    const std::string title = AF_TR(title_key) + "###" + title_key;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputText(AF_TR("Category name").c_str(),
                         state_.terminology.category_name_buf,
                         sizeof(state_.terminology.category_name_buf));
        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputTextMultiline(AF_TR("Category description").c_str(),
                                  state_.terminology.category_description_buf,
                                  sizeof(state_.terminology.category_description_buf),
                                  ImVec2(420.0f, 96.0f));

        const bool can_save = !TrimWhitespace(state_.terminology.category_name_buf).empty();
        if (!can_save)
            ImGui::TextColored(ui::GetErrorColor(), "%s", AF_TR("Category name is required.").c_str());

        ImGui::Spacing();
        if (!can_save)
            ImGui::BeginDisabled();
        if (ImGui::Button((state_.terminology.editing_existing_category ? AF_TR("Save") : AF_TR("Create")).c_str(),
                          ImVec2(100.0f, 0.0f))) {
            callbacks_.confirm_terminology_category_edit();
            if (!state_.terminology.show_category_editor_modal)
                ImGui::CloseCurrentPopup();
        }
        if (!can_save)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(AF_TR("Cancel").c_str(), ImVec2(100.0f, 0.0f))) {
            state_.terminology.show_category_editor_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.terminology.show_category_editor_modal) {
        ImGui::OpenPopup(title.c_str());
    }
}

void ModalHost::RenderDeleteTerminologyCategoryModal() {
    if (!state_.terminology.show_delete_category_modal)
        return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal((AF_TR("Delete Category") + "###Delete Category").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", AF_TR("Delete this category?").c_str());
        if (state_.terminology.pending_delete_category_term_count > 0) {
            ImGui::TextWrapped(
                "%s",
                ui::i18n::trnf(
                    "This category is assigned to {0} term. Remove those assignments before deleting it.",
                    "This category is assigned to {0} terms. Remove those assignments before deleting it.",
                    state_.terminology.pending_delete_category_term_count,
                    state_.terminology.pending_delete_category_term_count)
                    .c_str());
        }
        ImGui::Spacing();
        if (state_.terminology.pending_delete_category_term_count > 0)
            ImGui::BeginDisabled();
        if (ImGui::Button(AF_TR("Delete").c_str(), ImVec2(100.0f, 0.0f))) {
            callbacks_.confirm_delete_terminology_category();
            if (!state_.terminology.show_delete_category_modal)
                ImGui::CloseCurrentPopup();
        }
        if (state_.terminology.pending_delete_category_term_count > 0)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(AF_TR("Cancel").c_str(), ImVec2(100.0f, 0.0f))) {
            state_.terminology.show_delete_category_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.terminology.show_delete_category_modal) {
        ImGui::OpenPopup((AF_TR("Delete Category") + "###Delete Category").c_str());
    }
}

} // namespace app::areas
