#pragma once

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
    GsnCanvas,
    CseRegister,
    EvidenceRegister,
    PackageDetails,
    TerminologyPackage,
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

    // Set to true when the canvas should fit-to-view the marked_for_removal set.
    bool center_on_marked = false;

    // Relationship panel multiplicity-cardinality editor buffer (ADR-0006). The
    // buffer is (re)loaded from the selected relationship's stored display
    // expression whenever the selection changes to a different relationship.
    std::string pattern_cardinality_edit_rel_id;
    char pattern_cardinality_buf[64] = {};

    // Choice-diamond cardinality editor buffer (ADR-0006), reloaded when the
    // active choice popup targets a different group.
    std::string choice_cardinality_edit_group_id;
    char choice_cardinality_buf[64] = {};

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
