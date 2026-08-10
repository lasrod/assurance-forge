#include "app/areas/inspector_area.h"

#include "app/app_runtime_state.h"
#include "app/areas/audit_data_cache.h"
#include "app/commands/dispatch.h"
#include "app/confidence_problem_sync.h"
#include "app/frame/app_layout_regions.h"
#include "core/audit/audit_baseline.h"
#include "core/audit/audit_diff.h"
#include "core/audit/event_store.h"
#include "core/commands/gid_commands.h"
#include "core/confidence/confidence_store.h"
#include "parser/model_utils.h"
#include "ui/i18n/localization.h"
#include "ui/panels/acp_panel.h"
#include "ui/panels/element_panel.h"
#include "ui/panels/relationship_panel.h"
#include "ui/ui_state.h"
#include "ui/widgets/panel_header.h"

#include "hello_imgui/icons_font_awesome_4.h"

#include <algorithm>
#include <string>

namespace app::areas {

void RenderInspectorArea(AppRuntimeState& state,
                         const frame::AppLayoutRegion& region,
                         ImGuiWindowFlags panel_flags,
                         const InspectorAreaCallbacks& callbacks) {
    ImGui::SetNextWindowPos(region.pos);
    ImGui::SetNextWindowSize(region.size);
    ImGui::Begin((AF_TR("Element Properties") + "###Element Properties").c_str(),
                 nullptr,
                 panel_flags | ImGuiWindowFlags_NoTitleBar);
    ui::widgets::PanelHeader(ICON_FA_INFO_CIRCLE, AF_TR("Element Properties"));

    if (state.proposal_controller->creator_active) {
        if (callbacks.render_proposal_element_editor)
            callbacks.render_proposal_element_editor();
    } else if (state.proposal_controller->preview_active) {
        ImGui::TextWrapped(
            "%s", AF_TR("Proposal preview is active. Exit preview before editing element properties.").c_str());
    } else {
        parser::AssuranceCase* loaded_case =
            callbacks.inspector_model
                ? callbacks.inspector_model()
                : (state.app_state.loaded_case.has_value() ? &state.app_state.loaded_case.value() : nullptr);
        // Withheld while a draft is active. The panel mirrors a live edit into
        // the package as the user types, and that package is the *accepted*
        // one -- so handing it over would write draft text into accepted
        // assurance content on every keystroke, which is the leak this whole
        // routing exists to close. The edit reaches the model as a staged draft
        // operation when the field loses focus, and nowhere else.
        const bool draft_editing =
            state.draft_workspace.workspace() != nullptr && state.draft_workspace.workspace()->has_active_groups();
        sacm::AssuranceCasePackage* sacm_package = (!draft_editing && state.app_state.sacm_package.has_value())
                                                       ? &state.app_state.sacm_package.value()
                                                       : nullptr;
        if (!ui::GetUiState().selected_acp_id.empty()) {
            ui::panels::AcpPanelCallbacks acp_callbacks;
            acp_callbacks.upsert_acp = [&](const parser::AcpRecord& acp) {
                return state.acp_controller && loaded_case && state.acp_controller->UpsertAcp(state, acp);
            };
            acp_callbacks.remove_acp = [&](const std::string& acp_id) {
                return state.acp_controller && loaded_case && state.acp_controller->RemoveAcp(state, acp_id);
            };
            acp_callbacks.create_confidence_argument_tree = [&](const std::string& acp_id) {
                return state.acp_controller && loaded_case &&
                       state.acp_controller->CreateConfidenceArgumentTreeForAcp(state, acp_id);
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
                       state.acp_controller->AddRelationshipAcp(state, relationship_id);
            };
            relationship_callbacks.open_acp = [](const std::string&) {};
            relationship_callbacks.remove_relationship = [&](const std::string& relationship_id) {
                if (state.element_edit_controller)
                    state.element_edit_controller->RemoveRelationship(state, relationship_id);
            };
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
                                         ? AF_TR("Jøsang opinion")
                                         : AF_TR("Fixed value");
                model.status_label = assessment->status == core::confidence::ConfidenceStatus::Inactive
                                         ? AF_TR("Inactive")
                                         : AF_TR("Active");
            }
            return model;
        };
        confidence_callbacks.save_confidence = [&](parser::SacmElement& element,
                                                   const ui::ElementConfidence& confidence) {
            if (!state.confidence_controller)
                return false;
            if (!state.app_state.current_project.has_value()) {
                state.events.Emit(StatusMessageEvent{AF_TR("Open or create a project before saving confidence.")});
                return false;
            }
            bool generated_gid = false;
            if (element.gid.empty()) {
                if (!loaded_case) {
                    state.events.Emit(StatusMessageEvent{AF_TR("Could not assign a SACM gid for confidence storage.")});
                    return false;
                }
                core::commands::EnsureElementGidCommand cmd(element.id);
                const app::commands::DispatchOutcome outcome = app::commands::DispatchAuditedCommand(state, cmd);
                if (outcome.success) {
                    // Set the gid on the LIVE element so UpsertElementConfidence can key
                    // by it this frame; the library-primary command set it in the library
                    // and the frame-boundary re-derive re-projects the same gid.
                    element.gid = cmd.GeneratedGid();
                    generated_gid = true;
                } else if (!outcome.error.empty()) {
                    state.events.Emit(StatusMessageEvent{ui::i18n::trf("Confidence save failed: {0}", outcome.error)});
                    return false;
                } else {
                    // A false outcome with NO error is the benign no-op the command
                    // reports when the model element already carries a gid (i.e. this
                    // view's copy was stale). Adopt the existing gid rather than failing
                    // the save; only a genuinely gid-less element is unrecoverable.
                    const parser::SacmElement* current = parser::FindElementById(*loaded_case, element.id);
                    if (current == nullptr || current->gid.empty()) {
                        state.events.Emit(
                            StatusMessageEvent{AF_TR("Could not assign a SACM gid for confidence storage.")});
                        return false;
                    }
                    element.gid = current->gid;
                }
            }
            std::string error;
            if (!state.confidence_controller->UpsertElementConfidence(element, confidence, error)) {
                state.events.Emit(StatusMessageEvent{ui::i18n::trf("Confidence save failed: {0}", error)});
                return generated_gid;
            }
            return generated_gid;
        };
        confidence_callbacks.set_confidence_active = [&](parser::SacmElement& element, bool active) {
            if (!state.confidence_controller)
                return false;
            std::string error;
            if (!state.confidence_controller->SetElementConfidenceActive(element, active, error) && !error.empty())
                state.events.Emit(StatusMessageEvent{ui::i18n::trf("Confidence update failed: {0}", error)});
            return false;
        };
        confidence_callbacks.mark_reviewed = [&](parser::SacmElement& element) {
            if (!state.confidence_controller)
                return false;
            std::string error;
            if (!state.confidence_controller->MarkElementReviewed(element, error) && !error.empty())
                state.events.Emit(StatusMessageEvent{ui::i18n::trf("Confidence update failed: {0}", error)});
            return false;
        };
        confidence_callbacks.backup_invalid_and_reset = [&]() {
            if (!state.confidence_controller)
                return false;
            std::string error;
            if (!state.confidence_controller->BackupInvalidAndStartNew(error)) {
                state.events.Emit(StatusMessageEvent{ui::i18n::trf("Confidence reset failed: {0}", error)});
                return false;
            }
            state.events.Emit(StatusMessageEvent{
                AF_TR("Backed up invalid confidence file; new confidence storage will be saved with the project.")});
            return true;
        };
        ui::panels::ElementTextEditCallbacks text_edit_callbacks;
        text_edit_callbacks.commit_text_edit = callbacks.commit_element_text_edit;
        text_edit_callbacks.set_undeveloped = callbacks.set_element_undeveloped;

        ui::panels::ElementTranslationReviewCallbacks translation_review_callbacks;
        translation_review_callbacks.is_pending = callbacks.is_translation_review_pending;
        translation_review_callbacks.accept = callbacks.accept_translation_review;

        ui::panels::ElementHistoryCallbacks history_callbacks;
        if (state.app_state.current_project.has_value()) {
            const std::filesystem::path project_root = state.app_state.current_project.value().rootPath;
            history_callbacks.model_for_element = [project_root](const std::string& element_id) {
                ui::panels::ElementHistoryModel hm;
                if (element_id.empty())
                    return hm;
                std::string err;
                const std::vector<core::audit::AuditTransaction>& txs = GetCachedTransactions(project_root, err);
                if (!err.empty() && txs.empty())
                    return hm;
                hm.available = true;

                // Determine the latest baseline (if any) by transaction_sequence.
                const std::vector<core::audit::BaselineMetadata>& baselines = GetCachedBaselines(project_root, nullptr);
                std::optional<std::uint64_t> baseline_seq;
                if (!baselines.empty()) {
                    auto it = std::max_element(
                        baselines.begin(),
                        baselines.end(),
                        [](const core::audit::BaselineMetadata& a, const core::audit::BaselineMetadata& b) {
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

        ui::panels::ElementDraftCallbacks draft_callbacks;
        draft_callbacks.accept_groups = callbacks.accept_draft_groups;
        draft_callbacks.reject_groups = callbacks.reject_draft_groups;

        if (ui::panels::ShowElementPanel(loaded_case,
                                         sacm_package,
                                         &terminology_callbacks,
                                         &confidence_callbacks,
                                         &text_edit_callbacks,
                                         &history_callbacks,
                                         &translation_review_callbacks,
                                         &draft_callbacks,
                                         inspector_read_only)) {
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
                    state.events.Emit(StatusMessageEvent{ui::i18n::trnf(
                        "{0} confidence assessment was marked inactive because its target element changed.",
                        "{0} confidence assessments were marked inactive because their target elements changed.",
                        count,
                        count)});
                }
            }
            if (callbacks.mark_element_modified)
                callbacks.mark_element_modified();
        }
    }

    ImGui::End();
}

} // namespace app::areas
