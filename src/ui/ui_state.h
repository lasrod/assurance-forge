#pragma once

#include "core/changesets/change_set.h"
#include "core/problems/problem_attention.h"
#include "core/sacm_model.h"
#include "ui/confidence_model.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ui {

enum class CenterView {
    ProjectOverview,
    GsnCanvas,
    CseRegister,
    EvidenceRegister,
    PackageDetails,
    TerminologyPackage,
};

// Which version of the argument the canvas draws while a draft workspace holds
// unaccepted changes.
enum class DraftViewMode {
    // The accepted argument with every active change group applied. What an
    // agent reads and what the checks run over.
    WorkingDraft,
    // The argument exactly as accepted, so a reviewer can see what they have
    // now rather than what is being proposed.
    AcceptedBaseline,
    // The working draft restricted to what the draft touches, plus enough
    // surrounding argument to understand it.
    ChangesOnly,
};

enum class ProblemFilter {
    All,
    Validation,
    Review,
    Warnings,
    Info,
};

struct ProposalTextChangePreview {
    std::string field;
    std::string old_value;
    std::string new_value;
};

struct UiState {
    std::string selected_element_id;
    std::string selected_acp_id;
    std::string selected_relationship_id;
    std::string selected_relationship_edge_key;

    // Language toggle for GSN canvas
    bool show_secondary_language = false;
    std::string active_secondary_lang = "ja";
    bool model_has_translations = false; // set when tree is built/rebuilt

    // Set to true when the canvas should center on the selected element
    bool center_on_selection = false;

    // Nodes pending confirmation of removal. The canvas tints these red.
    std::unordered_set<std::string> marked_for_removal;

    // Per-element alert-badge summary derived from the current ProblemsManager
    // snapshot. The ProblemsManager is the single source of truth: every badge
    // visible on the canvas, tree views, and inspector reflects an entry here.
    // Updated by workbench_area after every Sync* call.
    std::unordered_map<std::string, core::ElementBadgeSummary> element_badge_summaries;

    // Elements that currently have an AI review running. Rendered as a small
    // spinner overlay distinct from the problem alert badge.
    std::unordered_set<std::string> ai_review_running_element_ids;

    // Session-only confidence prototype state. Persistence and propagation are
    // intentionally deferred until the feature model is proven in the UI.
    std::unordered_map<std::string, ElementConfidence> confidence_states;

    std::unordered_set<std::string> ai_review_scope_element_ids;
    std::string ai_review_primary_element_id;

    // Proposal preview/creator highlighting. When enabled, nodes outside this
    // set are rendered subdued so proposed changes stand out.
    std::unordered_set<std::string> proposal_highlight_ids;
    std::unordered_map<std::string, std::vector<ProposalTextChangePreview>> proposal_text_changes;
    bool dim_non_proposal_nodes = false;

    // True while any proposal canvas mode (creator or preview) is active.
    // Used by the GSN canvas to apply a distinct background tint.
    bool proposal_canvas_active = false;

    // What a connected AI client's open change set does to each element, so the
    // canvas can show the work as the agent builds it rather than only once it
    // is finished. Empty when no change set is open, which is the ordinary case
    // and costs nothing to check.
    //
    // While this is non-empty the canvas is drawing a *preview* model, so a node
    // here may not exist in the saved argument yet -- and a node marked Removed
    // is drawn from the committed model, because the preview no longer holds it.
    std::unordered_map<std::string, core::changesets::ElementChange> agent_change_status;
    // The change set those statuses belong to, so the canvas legend and the
    // Review panel can agree on which one is being shown.
    std::string agent_change_set_id;
    std::string agent_change_set_title;

    // Which version of the argument the canvas is showing while a working draft
    // is active (ADR 0009).
    //
    // The user must always be able to answer "is this the accepted argument or a
    // proposal?" without inferring it from badges, so the draft banner names the
    // mode in words and this switches it. Defaults to the working draft: a draft
    // exists because someone is working on it, and hiding it would make the
    // banner the only evidence anything is pending.
    DraftViewMode draft_view_mode = DraftViewMode::WorkingDraft;

    // Set to true when the canvas should fit-to-view the marked_for_removal set.
    bool center_on_marked = false;

    // One-shot request to fit the whole argument into the viewport. The live
    // renderers are owned per tab by the workbench, so the toolbar asks through
    // here rather than reaching across to them, matching center_on_selection.
    bool fit_canvas_pending = false;

    // Active center panel view
    CenterView center_view = CenterView::GsnCanvas;

    // Active lower Problems panel state
    ProblemFilter active_problem_filter = ProblemFilter::All;
    std::string selected_problem_id;
    std::string selected_problem_element_id;

    // One-shot focus request: when set, the Problems panel will scroll to and
    // select the row matching `selected_problem_id`, then clear the flag.
    bool problems_panel_focus_pending = false;
    // One-shot request to make the Problems panel visible (e.g. raised when
    // the user clicks an element badge that should reveal the panel).
    bool problems_panel_open_pending = false;

    // Performance analysis overlay (Phase 3 of the perf-analysis plan). When
    // true, the perf overlay window is shown with profiler buckets, render
    // stats, and feature toggles.
    bool show_perf_overlay = false;

    // Reveals the diagnostic surfaces — the menu-bar frame/cull counters and the
    // AI Debug tab. Off by default: they are instrumentation for developing the
    // tool, not part of building a safety argument, and shipping them in the
    // primary chrome makes the application read as unfinished. Persisted as a
    // user preference.
    bool show_developer_tools = false;
};

// Global shared UI state accessible from all panels.
UiState& GetUiState();

// Returns true if any element in the assurance case has a non-empty secondary language entry.
bool ModelHasTranslations(const parser::AssuranceCase& ac, const std::string& secondary_lang = "ja");

// Marks `element_id` as having an AI review in progress; tracks the scope
// (used for highlighting all elements involved in the run) and the primary
// element for badge placement.
void BeginAiReviewSpinner(UiState& ui_state,
                          const std::string& element_id,
                          std::unordered_set<std::string> review_scope_element_ids = {});

// Clears the AI-running flag for `element_id`. The resulting alert badge (if
// any) is driven entirely by the ProblemsManager snapshot.
void EndAiReviewSpinner(UiState& ui_state, const std::string& element_id);

// Requests that the Problems panel scroll to and select the row that matches
// `problem_id`. Sets selection fields and the focus / open one-shot flags.
// `element_id` is optional and stored for the inspector context.
void FocusProblemInPanel(UiState& ui_state, const std::string& problem_id, const std::string& element_id = {});

} // namespace ui
