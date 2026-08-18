#pragma once

// The list of unaccepted changes in the working draft, and the decisions
// available on each.
//
// This is the surface that replaces the split proposal / change-set views. Those
// showed one source at a time and could not show a combination at all -- an MCP
// client's change set here, a review proposal file there, and nothing anywhere
// holding both. Every group in the workspace is one row here, whatever wrote it.
//
// The panel renders and decides nothing. Whether a group can be promoted right
// now, what its acceptance would drag in, and what the checks say about it are
// all computed against the materialized working model and passed in.

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace ui::panels {

// One change group, as the reviewer sees it.
struct DraftChangeRow {
    std::string group_id;
    std::string title;
    std::string summary;
    // Why, in the author's own words. A reviewer is being asked to accept a
    // change to a safety argument and needs the reasoning, not only the diff.
    std::string rationale;

    // Who wrote it, and in which conversation or review run. Two connected
    // agents have to be tellable apart, and an AI-authored change to a safety
    // argument must never read as a human's.
    std::string source_label;
    std::string source_session_id;
    // Already localized by the caller -- `core` stores no display text.
    std::string source_kind_label;
    std::string state_label;

    // Elements this group adds, changes or removes, and relationships it adds or
    // removes. Relationships are counted separately because a changed support
    // relationship can alter the meaning of an argument more than a reworded
    // claim can, and a single "12 changes" hides that entirely.
    int elements_added = 0;
    int elements_modified = 0;
    int elements_removed = 0;
    int relationships_added = 0;
    int relationships_removed = 0;
    int operation_count = 0;

    // SCCG guidelines this group serves and review items it answers. Ids, shown
    // verbatim: they are catalog identifiers, not prose.
    std::vector<std::string> guideline_ids;
    std::vector<std::string> review_item_ids;

    // Titles of the groups this one cannot be accepted without, so the row
    // explains the closure rather than asserting it.
    std::vector<std::string> depends_on_titles;
    // Titles of the groups accepting this one would also accept -- its closure
    // minus itself. Empty when it stands alone.
    std::vector<std::string> also_accepts_titles;

    // The glossary content this group stages, one line per term: the term, its
    // definition, and a removal marker where it removes one. Full text rather
    // than a count, because a term is deliberately not a GSN node -- the canvas
    // shows an argument change beside its row, but a definition under review is
    // readable nowhere else on this surface.
    std::vector<std::string> glossary_lines;

    // Findings against what this group would produce, scoped to what it touched.
    // Advisory: they never block acceptance, because the reviewer is the
    // authority on a safety argument and only a mechanically-decidable subset of
    // SCCG can be checked at all.
    std::vector<std::string> findings;

    // Whether it can be accepted against what is open right now. Computed every
    // frame rather than when the button is pressed: "Accept does nothing" was the
    // reported defect in the surface this replaces, where the refusal was real,
    // went to the status bar, and was nowhere near the change it was about.
    bool promotable = true;
    std::string blocked_reason;
    // Stranded: something it depended on was rejected. It is not applied to the
    // working model and cannot be accepted until its author retargets it.
    bool needs_attention = false;
    bool selected = false;
};

struct DraftChangesPanelModel {
    // False when there is no draft at all, which is the ordinary state.
    bool has_workspace = false;
    std::vector<DraftChangeRow> rows;
    // Set when the whole draft is held back rather than any single row -- the
    // argument moved underneath it, a promotion is awaiting durable completion,
    // or materialization failed. Rendered above the rows, because in that state
    // no per-row reason is the real explanation.
    std::string workspace_blocked_reason;
    // Why the last accept refused, kept until the draft changes. A refusal that
    // lives only in the status bar leaves a user looking at a draft that says it
    // has unaccepted changes, an Accept button that appears to do nothing, and
    // no way to find out which of the two is wrong.
    std::string accept_error;
    std::string selected_group_id;
};

struct DraftChangesPanelCallbacks {
    // Focuses the group's changes on the canvas and makes it the selected row.
    std::function<void(const std::string& group_id)> select_group;
    std::function<void(const std::string& group_id)> accept_group;
    // May raise the dependent-scope choice rather than rejecting immediately.
    std::function<void(const std::string& group_id)> reject_group;
    std::function<void()> accept_all;
};

void RenderDraftChangesPanel(const DraftChangesPanelModel& model, const DraftChangesPanelCallbacks& callbacks);

} // namespace ui::panels
