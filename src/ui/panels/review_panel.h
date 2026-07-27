#pragma once

#include "core/reviews/review_item.h"
#include "core/reviews/review_proposal.h"
#include "core/problems/problem_item.h"

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ui::panels {

struct ReviewGuidelineOption {
    std::string id;
    std::string category;
    std::string title;
};

struct ProposalTextChangePreview {
    std::string field;
    std::string old_value;
    std::string new_value;
};

// One change an AI client is building or has submitted, as the reviewer sees it.
struct AgentChangeSetRow {
    std::string id;
    std::string title;
    std::string summary;
    // Why the agent says it is making this change. A reviewer is being asked to
    // accept a change to a safety argument and needs the reasoning, not only the
    // diff.
    std::string intent;
    std::string client_label;
    // "building" while the agent is still working, "ready" once it has asked for
    // a decision. Accepting is offered either way -- the user is not obliged to
    // wait for an agent to declare itself finished.
    std::string state;
    int         added_count    = 0;
    int         modified_count = 0;
    int         removed_count  = 0;
    int         operation_count = 0;
    // False when the argument has moved under the change set, in which case
    // `problem` says how and accepting is refused.
    bool        applies = true;
    std::string problem;
    // True for the one currently drawn on the canvas.
    bool        shown_on_canvas = false;
    // SCCG findings against what this change set would produce. Advisory: they
    // never block acceptance, because the reviewer is the authority on a safety
    // argument and only a named, mechanically-decidable subset of SCCG can be
    // checked at all.
    std::vector<std::string> sccg_findings;
};

struct ReviewPanelModel {
    std::string selected_element_id;
    // Change sets connected AI clients are building against this project.
    std::vector<AgentChangeSetRow> agent_change_sets;
    // Clients attached right now, so something reading the project is never
    // invisible to the person responsible for it.
    std::vector<std::string>       connected_agents;
    std::string focus_review_item_id;
    std::vector<core::reviews::ReviewItem> review_items;
    std::vector<core::ProblemItem> problem_items;
    std::vector<ReviewGuidelineOption> guideline_options;
    std::string guideline_status;
    std::map<std::string, core::reviews::ProposalValidityResult> proposal_validity;
    std::map<std::string, std::vector<ProposalTextChangePreview>> proposal_text_changes;
    std::string review_status_text;
    std::string review_status_detail;
    std::string active_proposal_review_item_id;
    size_t active_proposal_operation_count = 0;
    bool active_proposal_can_save = false;
    bool has_project = false;
    bool manual_review_ok = false;
    bool ai_review_ok = false;
    bool ai_review_failed = false;
    bool review_status_passed = false;
};

struct ReviewPanelCallbacks {
    std::function<void(
        const std::string& title, const std::string& message, const std::vector<std::string>& guideline_ids)>
        add_review_item;
    std::function<void(const core::reviews::ReviewItem& item)> create_proposed_change;
    std::function<void(const core::reviews::ReviewItem& item)> save_proposal;
    std::function<void(const core::reviews::ReviewItem& item)> edit_proposal;
    std::function<void(const core::reviews::ReviewItem& item)> preview_proposal;
    std::function<void(const core::reviews::ReviewItem& item)> apply_proposal;
    std::function<void(const core::reviews::ReviewItem& item)> delete_proposal;
    std::function<void(const core::reviews::ReviewItem& item)> resolve_review_item;
    std::function<void(const core::reviews::ReviewItem& item)> delete_review_item;
    std::function<void(const core::ProblemItem& problem)> quick_fix_problem;
    std::function<void(const core::ProblemItem& problem)> delete_problem;
    std::function<void(bool manual_ok)> set_manual_review_ok;
    // Accepting is the only path from an agent's staged work to the safety case,
    // and it is a person's action. There is no tool an agent can call to reach
    // it.
    std::function<void(const std::string& change_set_id)> accept_agent_change_set;
    std::function<void(const std::string& change_set_id)> reject_agent_change_set;
};

void ShowReviewPanel(const ReviewPanelModel& model, const ReviewPanelCallbacks& callbacks);

} // namespace ui::panels