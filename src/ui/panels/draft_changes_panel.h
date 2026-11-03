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

    // Problem findings the author acknowledged at submit rather than resolved.
    // Provenance, shown as stored: the reviewer must see exactly what the
    // author waved through, and the live findings above already carry the
    // translated current state.
    std::vector<std::string> acknowledged_findings;

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

// One contributor's touch on an element, as the reviewer reads it.
struct DraftContributionLine {
    // Who: the contributor's own label where it has one, otherwise the kind of
    // source it was. Already localized by the caller.
    std::string source_label;
    // Why, in the author's own words. Often empty -- a human edit carries no
    // rationale unless one was asked for -- and the panel omits the line then
    // rather than showing an empty quote.
    std::string rationale;
};

// One CHANGED ELEMENT in a document-backed working draft (ADR 0016).
//
// The unit differs from `DraftChangeRow` because the draft does: a document has
// no change groups, so there is no "several operations one author submitted
// together" to be a row. The reviewable thing is the element, and who touched it
// is read from its own provenance rather than from the group that carried it.
//
// It carries no dependency or per-row accept fields on purpose. Accepting a
// document-backed draft accepts all of it, and selective review is the inverse
// gesture -- rejecting individual changes in the draft and accepting what
// remains -- which is not yet built.
struct DraftElementRow {
    std::string element_id;
    // The element's own words, for a reviewer scanning the list.
    std::string label;
    // "Claim", "Strategy", ... and "Added" / "Changed" / "Removed". Localized by
    // the caller, because `core` stores no display text.
    std::string kind_label;
    std::string change_label;
    // Which parts of the element differ, named rather than counted: "modified"
    // alone does not distinguish a reworded claim from a re-pointed
    // relationship. Localized field names.
    std::vector<std::string> changed_fields;
    // Every contributor that touched it, in the order the tags give (document
    // order, not time). More than one is the ordinary case once an agent has
    // drafted something a person then edits. Empty for a removed element, which
    // is not in the draft to carry a tag.
    std::vector<DraftContributionLine> contributions;
    // Findings against this element. Advisory, as everywhere else here.
    std::vector<std::string> findings;
    bool selected = false;
};

struct DraftChangesPanelModel {
    // False when there is no draft at all, which is the ordinary state.
    bool has_workspace = false;
    // Group rows, from the pre-ADR-0016 workspace store.
    std::vector<DraftChangeRow> rows;
    // Element rows, from the draft document. A model carries one list or the
    // other: this one whenever the draft document differs from the accepted
    // argument. The group list goes when the workspace store does.
    std::vector<DraftElementRow> element_rows;
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
    // Focuses one changed element on the canvas and selects it.
    std::function<void(const std::string& element_id)> select_element;
};

void RenderDraftChangesPanel(const DraftChangesPanelModel& model, const DraftChangesPanelCallbacks& callbacks);

} // namespace ui::panels
