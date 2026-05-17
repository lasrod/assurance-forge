#include "app/areas/inspector_area.h"

#include "app/app_runtime_state.h"
#include "app/frame/app_layout_regions.h"
#include "core/confidence/confidence_store.h"
#include "core/sacm_identity.h"
#include "ui/panels/element_panel.h"

namespace app::areas {

void RenderInspectorArea(AppRuntimeState& state,
                         const frame::AppLayoutRegion& region,
                         ImGuiWindowFlags panel_flags,
                         const InspectorAreaCallbacks& callbacks) {
    ImGui::SetNextWindowPos(region.pos);
    ImGui::SetNextWindowSize(region.size);
    ImGui::Begin("Element Properties", nullptr, panel_flags);

    if (state.proposal_controller->creator_active) {
        if (callbacks.render_proposal_element_editor)
            callbacks.render_proposal_element_editor();
    } else if (state.proposal_controller->preview_active) {
        ImGui::TextWrapped("Proposal preview is active. Exit preview before editing element properties.");
    } else {
        parser::AssuranceCase* loaded_case =
            state.app_state.loaded_case.has_value() ? &state.app_state.loaded_case.value() : nullptr;
        sacm::AssuranceCasePackage* sacm_package =
            state.app_state.sacm_package.has_value() ? &state.app_state.sacm_package.value() : nullptr;
        ui::panels::ElementTerminologyAssistCallbacks terminology_callbacks;
        terminology_callbacks.define_term = callbacks.define_terminology_term;
        terminology_callbacks.link_existing_term = callbacks.link_existing_terminology_term;
        terminology_callbacks.use_term_for_element = callbacks.use_terminology_term_for_element;
        terminology_callbacks.ignore_term = callbacks.ignore_terminology_suggestion;
        terminology_callbacks.is_ignored = callbacks.is_terminology_suggestion_ignored;
        terminology_callbacks.focus_review_tab = callbacks.focus_review_tab;
        ui::panels::ElementConfidenceAssistCallbacks confidence_callbacks;
        confidence_callbacks.model_for_element = [&](const parser::SacmElement& element) {
            ui::panels::ConfidencePanelModel model;
            model.element_id = element.id;
            if (!state.confidence_controller)
                return model;
            if (state.confidence_controller->HasStorageError()) {
                model.storage_warning = state.confidence_controller->StorageError();
                return model;
            }
            const auto confidence = state.confidence_controller->ConfidenceForElement(element);
            if (!confidence.has_value())
                return model;
            model.has_assessment = true;
            model.confidence = confidence.value();
            if (const auto* assessment = state.confidence_controller->FindForElement(element)) {
                model.stale = assessment->stale;
                model.expected_confidence = static_cast<float>(assessment->derived.expectedConfidence);
                model.method_label = assessment->method == core::confidence::ConfidenceMethod::JosangOpinion
                                         ? "Jøsang opinion"
                                         : "Fixed value";
                model.status_label = assessment->status == core::confidence::ConfidenceStatus::Archived ? "Archived"
                                                                                                         : "Active";
            }
            return model;
        };
        confidence_callbacks.save_confidence = [&](parser::SacmElement& element,
                                                   const ui::ElementConfidence& confidence) {
            if (!state.confidence_controller)
                return false;
            if (!state.app_state.current_project.has_value()) {
                state.events.Emit(StatusMessageEvent{"Open or create a project before saving confidence."});
                return false;
            }
            bool generated_gid = false;
            if (element.gid.empty()) {
                if (!loaded_case || !sacm_package) {
                    state.events.Emit(StatusMessageEvent{"Could not assign a SACM gid for confidence storage."});
                    return false;
                }
                std::string error;
                generated_gid = core::EnsureElementGid(*loaded_case, sacm_package, element, error);
                if (!error.empty()) {
                    state.events.Emit(StatusMessageEvent{"Confidence save failed: " + error});
                    return false;
                }
            }
            std::string error;
            if (!state.confidence_controller->UpsertElementConfidence(element, confidence, error)) {
                state.events.Emit(StatusMessageEvent{"Confidence save failed: " + error});
                return generated_gid;
            }
            return generated_gid;
        };
        confidence_callbacks.clear_confidence = [&](parser::SacmElement& element) {
            if (!state.confidence_controller)
                return false;
            std::string error;
            if (!state.confidence_controller->ClearElementConfidence(element, error) && !error.empty())
                state.events.Emit(StatusMessageEvent{"Confidence clear failed: " + error});
            return false;
        };
        confidence_callbacks.mark_reviewed = [&](parser::SacmElement& element) {
            if (!state.confidence_controller)
                return false;
            std::string error;
            if (!state.confidence_controller->MarkElementReviewed(element, error) && !error.empty())
                state.events.Emit(StatusMessageEvent{"Confidence update failed: " + error});
            return false;
        };
        confidence_callbacks.backup_invalid_and_reset = [&]() {
            if (!state.confidence_controller)
                return false;
            std::string error;
            if (!state.confidence_controller->BackupInvalidAndStartNew(error)) {
                state.events.Emit(StatusMessageEvent{"Confidence reset failed: " + error});
                return false;
            }
            state.events.Emit(StatusMessageEvent{"Backed up invalid confidence file; new confidence storage will be saved with the project."});
            return true;
        };
        if (ui::panels::ShowElementPanel(loaded_case, sacm_package, &terminology_callbacks, &confidence_callbacks)) {
            if (callbacks.mark_element_modified)
                callbacks.mark_element_modified();
        }
    }

    ImGui::End();
}

} // namespace app::areas
