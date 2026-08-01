#include "app/areas/review_panel_area.h"

#include "app/app_runtime_state.h"
#include "app/controllers/review_controller.h"
#include "core/guideline_catalog.h"
#include "core/sccg/staged_checks.h"
#include "core/problems/problem_utils.h"
#include "core/time_utils.h"
#include "ui/panels/review_panel.h"
#include "ui/ui_state.h"

#include <chrono>
#include <optional>
#include <sstream>
#include <unordered_set>
#include <utility>
#include <vector>

namespace app::areas {
namespace {

using core::NowUtcString;

std::string GenerateReviewItemId() {
    static unsigned long long counter = 0;
    auto ticks = std::chrono::system_clock::now().time_since_epoch().count();
    std::ostringstream out;
    out << "review-" << std::hex << ticks << "-" << ++counter;
    return out.str();
}

void EnsureReviewGuidelineCatalogLoaded(AppRuntimeState& state) {
    if (state.guideline_catalog_load_attempted)
        return;

    core::GuidelineCatalog catalog;
    std::string error;
    if (core::LoadGuidelineCatalog(catalog, error)) {
        state.guideline_catalog = std::move(catalog);
        state.guideline_catalog_error.clear();
    } else {
        state.guideline_catalog.reset();
        state.guideline_catalog_error = error;
    }
    state.guideline_catalog_load_attempted = true;
}

void SetStatus(const ReviewPanelAreaCallbacks& callbacks, const std::string& message) {
    if (callbacks.set_status)
        callbacks.set_status(message);
}

std::vector<ui::panels::ProposalTextChangePreview>
CollectProposalTextChanges(const core::reviews::ReviewProposal& proposal) {
    std::vector<ui::panels::ProposalTextChangePreview> changes;
    for (const core::reviews::PatchOperation& operation : proposal.operations) {
        if (operation.type != core::reviews::PatchOperationType::UpdateElementText &&
            operation.type != core::reviews::PatchOperationType::UpdateElementName) {
            continue;
        }
        if (operation.old_value == operation.new_value)
            continue;

        changes.push_back(ui::panels::ProposalTextChangePreview{
            operation.field.empty() ? "text" : operation.field,
            operation.old_value,
            operation.new_value,
        });
    }
    return changes;
}

void AddManualReviewItem(AppRuntimeState& state,
                         const ReviewPanelAreaCallbacks& callbacks,
                         const std::string& title,
                         const std::string& message,
                         const std::vector<std::string>& guideline_ids) {
    if (state.proposal_controller->creator_active) {
        SetStatus(callbacks, "Save or discard the active proposal before adding more review comments.");
        return;
    }
    if (!state.app_state.current_project.has_value()) {
        SetStatus(callbacks, "Open or create a project before adding review comments.");
        return;
    }
    if (callbacks.ensure_review_item_storage && !callbacks.ensure_review_item_storage()) {
        return;
    }
    if (state.reviewer_name.empty()) {
        state.modal_coordinator->show_reviewer_name_prompt = true;
        SetStatus(callbacks, "Enter a reviewer name before adding review comments.");
        return;
    }
    const std::string element_id = ui::GetUiState().selected_element_id;
    if (element_id.empty()) {
        SetStatus(callbacks, "Select an element before adding a review comment.");
        return;
    }

    std::vector<std::string> validated_guideline_ids;
    if (!guideline_ids.empty()) {
        EnsureReviewGuidelineCatalogLoaded(state);
        if (!state.guideline_catalog.has_value()) {
            SetStatus(callbacks, "SCCG guidelines are not available: " + state.guideline_catalog_error);
            return;
        }

        std::unordered_set<std::string> seen_guideline_ids;
        for (const std::string& guideline_id : guideline_ids) {
            if (guideline_id.empty() || seen_guideline_ids.count(guideline_id) > 0)
                continue;
            if (state.guideline_catalog->ids.count(guideline_id) == 0) {
                SetStatus(callbacks, "Unknown SCCG guideline id: " + guideline_id);
                return;
            }
            validated_guideline_ids.push_back(guideline_id);
            seen_guideline_ids.insert(guideline_id);
        }
    }

    core::reviews::ReviewItem item;
    item.id = GenerateReviewItemId();
    item.element_id = element_id;
    item.title = title;
    item.message = message;
    item.severity = "warning";
    item.reviewer_name = state.reviewer_name;
    item.guideline_ids = std::move(validated_guideline_ids);
    item.source = core::reviews::ReviewItemSource::Manual;
    item.status = core::reviews::ReviewItemStatus::Open;
    item.created_utc = NowUtcString();
    item.updated_utc = item.created_utc;

    state.review_controller->AddManualItem(std::move(item));
}

ui::panels::ReviewPanelModel BuildReviewPanelModel(AppRuntimeState& state) {
    const ui::UiState& ui_state = ui::GetUiState();
    ui::panels::ReviewPanelModel model;
    auto& proposals = *state.proposal_controller;
    model.selected_element_id =
        proposals.creator_active ? proposals.draft.anchor_element_id : ui_state.selected_element_id;
    model.focus_review_item_id = state.workbench.focus_review_item_id;
    model.has_project = state.app_state.current_project.has_value();

    // What connected AI clients are proposing. Assembled here rather than in the
    // panel so the panel stays a renderer: it receives rows, not a change-set
    // store and a model to diff against.
    if (state.agent_bridge != nullptr) {
        for (const controllers::AgentConnection& connection : state.agent_bridge->connections()) {
            model.connected_agents.push_back(connection.client_label);
        }
    }
    for (const core::changesets::ChangeSet* change_set : state.agent_change_sets.Open()) {
        ui::panels::AgentChangeSetRow row;
        row.id = change_set->id;
        row.title = change_set->title;
        row.summary = change_set->summary;
        row.intent = change_set->intent;
        row.client_label = change_set->client_label;
        row.state = core::changesets::ChangeSetStateToString(change_set->state);
        row.operation_count = static_cast<int>(change_set->proposal.operations.size());
        row.shown_on_canvas = ui_state.agent_change_set_id == change_set->id;
        row.argument_file = change_set->argument_file.filename().generic_string();
        row.argument_file_is_open =
            core::changesets::ChangeSetTargetsArgumentFile(*change_set, state.app_state.loaded_file_path);

        if (state.app_state.loaded_case.has_value()) {
            // The same check acceptance runs, run every frame, so the reason a
            // change set will not accept is on the change set before the user
            // reaches for the button rather than in the status bar afterwards.
            const core::changesets::ChangeSetAcceptability acceptability =
                core::changesets::EvaluateChangeSetAcceptability(
                    *change_set, state.app_state.loaded_file_path, state.app_state.loaded_case.value());
            row.applies = acceptability.can_accept;
            row.problem = acceptability.reason;

            // Counting what it would do only means something against the
            // document it was written for. Against another of the project's
            // arguments the operations land on ids that merely happen to match.
            if (row.argument_file_is_open) {
                const core::changesets::ChangeSetDiff diff =
                    core::changesets::ComputeChangeSetDiff(*change_set, state.app_state.loaded_case.value());
                row.added_count = diff.added_count;
                row.modified_count = diff.modified_count;
                row.removed_count = diff.removed_count;
                row.sccg_findings = DescribeStagedSccgFindings(diff);
            }
        }
        model.agent_change_sets.push_back(std::move(row));
    }
    EnsureReviewGuidelineCatalogLoaded(state);
    if (state.guideline_catalog.has_value()) {
        for (const core::GuidelineCatalogEntry& entry : state.guideline_catalog->entries) {
            model.guideline_options.push_back(ui::panels::ReviewGuidelineOption{
                entry.id,
                entry.category,
                entry.title,
            });
        }
    } else {
        model.guideline_status = state.guideline_catalog_error;
    }
    if (proposals.creator_active) {
        model.active_proposal_review_item_id = proposals.draft.review_item_id;
        model.active_proposal_operation_count = proposals.ActiveOperationCount();
        model.active_proposal_can_save = proposals.CanSaveActiveDraft();
    }
    if (model.selected_element_id.empty()) {
        return model;
    }

    model.review_items = state.review_controller->ItemsForElement(model.selected_element_id);
    bool has_blocking_problem = false;
    for (const core::ProblemItem& problem : state.problems_manager.GetProblems()) {
        if (problem.element_id != model.selected_element_id)
            continue;
        if (core::IsReviewDerivedProblem(problem))
            continue;
        model.problem_items.push_back(problem);
        has_blocking_problem = true;
    }
    const core::reviews::ElementReviewState element_review_state =
        state.review_controller->ElementReviewStateForElement(model.selected_element_id);
    model.manual_review_ok = element_review_state.manual_ok;
    model.ai_review_ok = element_review_state.ai_ok;
    model.ai_review_failed = element_review_state.failed;
    const controllers::ElementReviewStatus review_status =
        state.review_controller->StatusForElement(model.selected_element_id, has_blocking_problem);
    model.review_status_passed = review_status == controllers::ElementReviewStatus::Passed;
    switch (review_status) {
    case controllers::ElementReviewStatus::Passed:
        model.review_status_text = "Passed";
        model.review_status_detail = element_review_state.manual_ok ? "Manual review OK." : "AI review OK.";
        break;
    case controllers::ElementReviewStatus::Failed:
        model.review_status_text = "Not OK";
        model.review_status_detail = element_review_state.last_review_message.empty()
                                         ? "AI review failed."
                                         : element_review_state.last_review_message;
        break;
    case controllers::ElementReviewStatus::OpenItems:
        model.review_status_text = "Not OK";
        model.review_status_detail =
            has_blocking_problem ? "Open problems require attention." : "Open review comments require attention.";
        break;
    case controllers::ElementReviewStatus::NotReviewed:
        model.review_status_text = "Not reviewed";
        model.review_status_detail = "Set Manual review OK or run an AI review with no findings.";
        break;
    }
    for (const core::reviews::ReviewItem& item : model.review_items) {
        if (!item.proposal_id.has_value())
            continue;
        std::string error;
        std::optional<core::reviews::ReviewProposal> proposal =
            proposals.manager.LoadProposal(item.proposal_id.value(), error);
        if (!proposal.has_value()) {
            model.proposal_validity[item.proposal_id.value()] = {core::reviews::ProposalValidity::Broken, error};
        } else {
            std::vector<ui::panels::ProposalTextChangePreview> text_changes = CollectProposalTextChanges(*proposal);
            if (!text_changes.empty()) {
                model.proposal_text_changes[item.proposal_id.value()] = std::move(text_changes);
            }
            if (state.app_state.loaded_case.has_value()) {
                model.proposal_validity[item.proposal_id.value()] = core::reviews::EvaluateReviewProposalValidity(
                    proposal.value(), state.app_state.loaded_case.value());
            } else {
                model.proposal_validity[item.proposal_id.value()] = {core::reviews::ProposalValidity::Broken,
                                                                     "No SACM model is loaded."};
            }
        }
    }

    return model;
}

} // namespace

// The SCCG findings against what a change set would produce, as the reviewer
// reads them.
//
// The agent has been getting these in the result of every staging call since the
// checks were written; the reviewer was not, because nothing ever filled the row
// the panel renders them from. So the panel had a findings section that could
// not appear, and a person deciding whether to accept an agent's change to a
// safety argument saw less about it than the agent did.
//
// Only what this change set touched: findings about the rest of the argument are
// the user's own business and not this proposal's to answer for.
std::vector<std::string> DescribeStagedSccgFindings(const core::changesets::ChangeSetDiff& diff) {
    std::vector<std::string> touched;
    for (const std::pair<const std::string, core::changesets::ElementChange>& entry : diff.status_by_id) {
        if (entry.second != core::changesets::ElementChange::Unchanged &&
            entry.second != core::changesets::ElementChange::Removed) {
            touched.push_back(entry.first);
        }
    }

    std::vector<std::string> described;
    for (const core::sccg::StagedFinding& finding : core::sccg::CheckStagedArgument(diff.preview_model, touched)) {
        std::string line = finding.guideline_id;
        if (!finding.element_id.empty()) {
            line += " " + finding.element_id;
        }
        described.push_back(line + ": " + finding.detail);
    }
    return described;
}

void RenderReviewPanelContent(AppRuntimeState& state, const ReviewPanelAreaCallbacks& callbacks) {
    ui::panels::ReviewPanelModel model = BuildReviewPanelModel(state);

    ui::panels::ReviewPanelCallbacks panel_callbacks;
    panel_callbacks.add_review_item = [&state, &callbacks](const std::string& title,
                                                           const std::string& message,
                                                           const std::vector<std::string>& guideline_ids) {
        AddManualReviewItem(state, callbacks, title, message, guideline_ids);
    };
    panel_callbacks.create_proposed_change = [&callbacks](const core::reviews::ReviewItem& item) {
        if (callbacks.create_proposed_change)
            callbacks.create_proposed_change(item);
    };
    panel_callbacks.save_proposal = [&callbacks](const core::reviews::ReviewItem& item) {
        if (callbacks.save_proposal)
            callbacks.save_proposal(item);
    };
    panel_callbacks.edit_proposal = [&callbacks](const core::reviews::ReviewItem& item) {
        if (callbacks.edit_proposal)
            callbacks.edit_proposal(item);
    };
    panel_callbacks.preview_proposal = [&callbacks](const core::reviews::ReviewItem& item) {
        if (!item.proposal_id.has_value()) {
            SetStatus(callbacks, "This review comment has no proposed change to preview.");
            return;
        }
        if (callbacks.preview_proposal_by_id)
            callbacks.preview_proposal_by_id(item.proposal_id.value());
    };
    panel_callbacks.apply_proposal = [&callbacks](const core::reviews::ReviewItem& item) {
        if (callbacks.apply_proposal)
            callbacks.apply_proposal(item);
    };
    panel_callbacks.delete_proposal = [&callbacks](const core::reviews::ReviewItem& item) {
        if (callbacks.delete_proposal)
            callbacks.delete_proposal(item);
    };
    panel_callbacks.resolve_review_item = [&callbacks](const core::reviews::ReviewItem& item) {
        if (callbacks.resolve_review_item)
            callbacks.resolve_review_item(item);
    };
    panel_callbacks.delete_review_item = [&callbacks](const core::reviews::ReviewItem& item) {
        if (callbacks.delete_review_item)
            callbacks.delete_review_item(item);
    };
    panel_callbacks.quick_fix_problem = [&callbacks](const core::ProblemItem& problem) {
        if (callbacks.quick_fix_problem)
            callbacks.quick_fix_problem(problem);
    };
    panel_callbacks.delete_problem = [&state, &callbacks](const core::ProblemItem& problem) {
        // Translation-review problems are rebuilt from the persisted pending set,
        // so simply removing the manager entry would let them reappear on the
        // next sync. Clear the underlying flag instead (same as "Mark reviewed").
        if (problem.type == "TranslationReviewNeeded" && callbacks.accept_translation_review) {
            callbacks.accept_translation_review(problem.element_id);
            SetStatus(callbacks, "Problem resolved.");
            return;
        }
        state.problems_manager.RemoveProblem(problem.id);
        SetStatus(callbacks, "Problem deleted.");
    };
    panel_callbacks.set_manual_review_ok = [&callbacks, element_id = model.selected_element_id](bool manual_ok) {
        if (callbacks.set_manual_review_ok)
            callbacks.set_manual_review_ok(element_id, manual_ok);
    };
    panel_callbacks.accept_agent_change_set = [&callbacks](const std::string& change_set_id) {
        if (callbacks.accept_agent_change_set)
            callbacks.accept_agent_change_set(change_set_id);
    };
    panel_callbacks.reject_agent_change_set = [&callbacks](const std::string& change_set_id) {
        if (callbacks.reject_agent_change_set)
            callbacks.reject_agent_change_set(change_set_id);
    };
    ui::panels::ShowReviewPanel(model, panel_callbacks);
    state.workbench.focus_review_item_id.clear();
}

} // namespace app::areas
