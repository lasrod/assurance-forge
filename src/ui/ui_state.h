#pragma once

#include "parser/xml_parser.h"

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace ui {

enum class CenterView {
    GsnCanvas,
    CseRegister,
    EvidenceRegister,
    PackageDetails,
};

enum class ProblemFilter {
    All,
    Validation,
    Review,
    Warnings,
    Info,
};

enum class ElementReviewVisualStatus {
    None,
    AiRunning,
    AiOk,
    ManualOk,
    Failed,
};

struct ElementReviewVisualState {
    bool ai_running = false;
    bool ai_ok = false;
    bool manual_ok = false;
    bool failed = false;
    std::string review_profile_id;
    std::string review_profile_name;
    std::string last_review_message;
};

struct UiState {
    std::string selected_element_id;

    // Language toggle for GSN canvas
    bool show_secondary_language = false;
    std::string active_secondary_lang = "ja";
    bool model_has_translations = false; // set when tree is built/rebuilt

    // Set to true when the canvas should center on the selected element
    bool center_on_selection = false;

    // Nodes pending confirmation of removal. The canvas tints these red.
    std::unordered_set<std::string> marked_for_removal;

    // Nodes with element-scoped problems that need user attention.
    std::unordered_set<std::string> attention_element_ids;

    // Session-only visual review state for GSN nodes. Persistence needs a
    // dedicated review-result model rather than SACM metadata.
    std::unordered_map<std::string, ElementReviewVisualState> review_visual_states;
    std::unordered_set<std::string> ai_review_scope_element_ids;
    std::string ai_review_primary_element_id;

    // Proposal preview/creator highlighting. When enabled, nodes outside this
    // set are rendered subdued so proposed changes stand out.
    std::unordered_set<std::string> proposal_highlight_ids;
    bool dim_non_proposal_nodes = false;

    // True while any proposal canvas mode (creator or preview) is active.
    // Used by the GSN canvas to apply a distinct background tint.
    bool proposal_canvas_active = false;

    // Set to true when the canvas should fit-to-view the marked_for_removal set.
    bool center_on_marked = false;

    // Active center panel view
    CenterView center_view = CenterView::GsnCanvas;

    // Active lower Problems panel state
    ProblemFilter active_problem_filter = ProblemFilter::All;
    std::string selected_problem_id;
    std::string selected_problem_element_id;
};

// Global shared UI state accessible from all panels.
UiState& GetUiState();

// Returns true if any element in the assurance case has a non-empty secondary language entry.
bool ModelHasTranslations(const parser::AssuranceCase& ac, const std::string& secondary_lang = "ja");

ElementReviewVisualStatus ResolveElementReviewVisualStatus(const ElementReviewVisualState& state);
ElementReviewVisualStatus ResolveElementReviewVisualStatus(const UiState& ui_state, const std::string& element_id);
const ElementReviewVisualState* FindElementReviewVisualState(const UiState& ui_state, const std::string& element_id);

void MarkAiReviewRunning(UiState& ui_state,
                         const std::string& element_id,
                         const std::string& review_profile_id = {},
                         const std::string& review_profile_name = {},
                         std::unordered_set<std::string> review_scope_element_ids = {});
void MarkAiReviewNoFindings(UiState& ui_state,
                            const std::string& element_id,
                            const std::string& review_profile_id = {},
                            const std::string& review_profile_name = {});
void MarkAiReviewFindings(UiState& ui_state, const std::string& element_id);
void MarkAiReviewFailed(UiState& ui_state,
                        const std::string& element_id,
                        const std::string& message = {},
                        const std::string& review_profile_id = {},
                        const std::string& review_profile_name = {});
void MarkReviewOkManually(UiState& ui_state, const std::string& element_id);

} // namespace ui
