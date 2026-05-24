#include "app/areas/inspector_area.h"

#include "app/app_runtime_state.h"
#include "app/confidence_problem_sync.h"
#include "app/frame/app_layout_regions.h"
#include "core/audit/audit_baseline.h"
#include "core/audit/audit_diff.h"
#include "core/audit/event_store.h"
#include "core/confidence/confidence_store.h"
#include "core/sacm_identity.h"
#include "ui/panels/acp_panel.h"
#include "ui/panels/element_panel.h"
#include "ui/panels/relationship_panel.h"
#include "ui/ui_state.h"

#include <algorithm>
#include <string>

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
        if (!ui::GetUiState().selected_acp_id.empty()) {
            ui::panels::AcpPanelCallbacks acp_callbacks;
            acp_callbacks.upsert_acp = [&](const parser::AcpRecord& acp) {
                return state.acp_controller && loaded_case &&
                       state.acp_controller->UpsertAcp(*loaded_case, sacm_package, acp);
            };
            acp_callbacks.remove_acp = [&](const std::string& acp_id) {
                return state.acp_controller && loaded_case &&
                       state.acp_controller->RemoveAcp(*loaded_case, sacm_package, acp_id);
            };
            acp_callbacks.create_confidence_argument_tree = [&](const std::string& acp_id) {
                return state.acp_controller && loaded_case &&
                       state.acp_controller->CreateConfidenceArgumentTreeForAcp(*loaded_case, sacm_package, acp_id);
            };
            acp_callbacks.open_confidence_argument_tree = [&](const std::string& acp_id) {
                return state.acp_controller && loaded_case &&
                       state.acp_controller->OpenConfidenceArgumentTreeForAcp(*loaded_case, acp_id);
            };
            acp_callbacks.navigate_to_element = [&](const std::string& element_id) {
                state.events.Emit(CenterRequestEvent{CenterViewRequest::GsnCanvas, true, false, true});
                state.events.Emit(SelectionChangedEvent{element_id, true});
            };
            ui::panels::ShowAcpPanel(loaded_case, sacm_package, &acp_callbacks);
            ImGui::End();
            return;
        }
        if (!ui::GetUiState().selected_relationship_id.empty()) {
            ui::panels::RelationshipPanelCallbacks relationship_callbacks;
            relationship_callbacks.add_acp = [&](const std::string& relationship_id) {
                return state.acp_controller && loaded_case &&
                       state.acp_controller->AddRelationshipAcp(*loaded_case, sacm_package, relationship_id);
            };
            relationship_callbacks.open_acp = [](const std::string&) {};
            ui::panels::ShowRelationshipPanel(loaded_case, &relationship_callbacks);
            ImGui::End();
            return;
        }
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
                model.status_label =
                    assessment->status == core::confidence::ConfidenceStatus::Inactive ? "Inactive" : "Active";
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
                const core::EnsureGidResult gid_result =
                    core::EnsureElementGid(*loaded_case, sacm_package, element, error);
                if (gid_result == core::EnsureGidResult::Failed) {
                    state.events.Emit(StatusMessageEvent{"Confidence save failed: " + error});
                    return false;
                }
                generated_gid = gid_result == core::EnsureGidResult::Generated;
            }
            std::string error;
            if (!state.confidence_controller->UpsertElementConfidence(element, confidence, error)) {
                state.events.Emit(StatusMessageEvent{"Confidence save failed: " + error});
                return generated_gid;
            }
            return generated_gid;
        };
        confidence_callbacks.set_confidence_active = [&](parser::SacmElement& element, bool active) {
            if (!state.confidence_controller)
                return false;
            std::string error;
            if (!state.confidence_controller->SetElementConfidenceActive(element, active, error) && !error.empty())
                state.events.Emit(StatusMessageEvent{"Confidence update failed: " + error});
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
            state.events.Emit(StatusMessageEvent{
                "Backed up invalid confidence file; new confidence storage will be saved with the project."});
            return true;
        };
        ui::panels::ElementTextEditCallbacks text_edit_callbacks;
        text_edit_callbacks.commit_text_edit = callbacks.commit_element_text_edit;

        ui::panels::ElementHistoryCallbacks history_callbacks;
        if (state.app_state.current_project.has_value()) {
            const std::filesystem::path project_root = state.app_state.current_project.value().rootPath;
            history_callbacks.model_for_element = [project_root](const std::string& element_id) {
                ui::panels::ElementHistoryModel hm;
                if (element_id.empty())
                    return hm;
                std::string err;
                auto store = core::audit::EventStore::Open(project_root, err);
                if (!store)
                    return hm;
                hm.available = true;
                const std::vector<core::audit::AuditTransaction> txs = store->Transactions();

                // Determine the latest baseline (if any) by transaction_sequence.
                std::vector<core::audit::BaselineMetadata> baselines =
                    core::audit::ListBaselines(project_root, nullptr);
                std::optional<std::uint64_t> baseline_seq;
                if (!baselines.empty()) {
                    auto it = std::max_element(baselines.begin(), baselines.end(),
                                               [](const core::audit::BaselineMetadata& a,
                                                  const core::audit::BaselineMetadata& b) {
                                                   return a.transaction_sequence < b.transaction_sequence;
                                               });
                    baseline_seq = it->transaction_sequence;
                    hm.has_baseline = true;
                    hm.baseline_label = it->name.empty() ? it->baseline_id : it->name;
                }

                const core::audit::ElementHistorySummary summary =
                    core::audit::SummarizeElementHistory(element_id, txs, baseline_seq);
                hm.ever_seen = summary.ever_seen;
                hm.change_count = summary.change_count;
                hm.last_sequence = summary.last_sequence;
                hm.last_changed_at = summary.last_changed_at;
                hm.last_changed_by = summary.last_changed_by;
                hm.changed_since_baseline = summary.changed_since_baseline;
                return hm;
            };
        }
        if (callbacks.open_element_history) {
            history_callbacks.open_element_history = callbacks.open_element_history;
        }

        // Detect historical preview on the active canvas tab — when the
        // user is scrubbing the timeline the inspector must render its
        // editable fields visually disabled.
        bool inspector_read_only = false;
        if (!state.workbench.active_argument_package_canvas_key.empty()) {
            for (const auto& tab : state.workbench.argument_package_canvas_tabs) {
                if (tab.key == state.workbench.active_argument_package_canvas_key) {
                    inspector_read_only = tab.timeline.preview_sequence.has_value();
                    break;
                }
            }
        }

        if (ui::panels::ShowElementPanel(loaded_case, sacm_package, &terminology_callbacks, &confidence_callbacks,
                                          &text_edit_callbacks, &history_callbacks, inspector_read_only)) {
            if (state.confidence_controller && loaded_case) {
                const bool confidence_changed = state.confidence_controller->RefreshStaleFlags(*loaded_case);
                if (confidence_changed) {
                    app::SyncConfidenceProblems(state.problems_manager,
                                                loaded_case,
                                                &state.confidence_controller->Store(),
                                                state.confidence_controller->ActiveSourceId());
                }
                if (confidence_changed && state.confidence_controller->LastInactivatedCount() > 0) {
                    const int count = state.confidence_controller->LastInactivatedCount();
                    state.events.Emit(StatusMessageEvent{
                        std::to_string(count) +
                        " confidence assessment(s) were marked inactive because their target elements changed."});
                }
            }
            if (callbacks.mark_element_modified)
                callbacks.mark_element_modified();
        }
    }

    ImGui::End();
}

} // namespace app::areas
