#include "app/areas/draft_changes_area.h"

#include "app/app_runtime_state.h"
#include "core/drafts/draft_dependency_graph.h"
#include "core/drafts/draft_promotion_service.h"
#include "parser/model_utils.h"
#include "ui/i18n/localization.h"
#include "ui/ui_state.h"

#include <algorithm>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace app::areas {
namespace {

std::string SourceKindLabel(core::drafts::DraftSource source) {
    switch (source) {
    case core::drafts::DraftSource::Human:
        return AF_TR("person");
    case core::drafts::DraftSource::Mcp:
        return AF_TR("AI client");
    case core::drafts::DraftSource::SccgAiReview:
        return AF_TR("SCCG AI review");
    case core::drafts::DraftSource::ImportedLegacyProposal:
        return AF_TR("imported proposal");
    }
    return AF_TR("unknown");
}

std::string GroupStateLabel(core::drafts::DraftGroupState state) {
    switch (state) {
    case core::drafts::DraftGroupState::Building:
        return AF_TR("being written");
    case core::drafts::DraftGroupState::Ready:
        return AF_TR("ready for a decision");
    case core::drafts::DraftGroupState::NeedsAttention:
        return AF_TR("needs attention");
    case core::drafts::DraftGroupState::Rejected:
        return AF_TR("rejected");
    }
    return AF_TR("unknown");
}

// Why the whole draft is held back, when it is. Distinct from a per-row reason:
// in these states no single group is the explanation, and offering a per-row
// one would send the user looking for a problem in the wrong place.
std::string WorkspaceBlockedReason(const core::drafts::DraftWorkspace& workspace,
                                   const core::drafts::DraftMaterializationResult* materialization) {
    switch (workspace.state) {
    case core::drafts::DraftWorkspaceState::NeedsRebase:
        return AF_TR("The argument changed since this draft was written, so none of it is being applied. "
                     "Inspect or discard it before continuing.");
    case core::drafts::DraftWorkspaceState::Promoting:
        return AF_TR("Promotion is recorded, but the accepted SACM file is not yet confirmed. "
                     "The draft is retained and cannot be edited or accepted.");
    case core::drafts::DraftWorkspaceState::Blocked:
        return AF_TR("This draft cannot be shown because one of its changes no longer applies.");
    case core::drafts::DraftWorkspaceState::Active:
    case core::drafts::DraftWorkspaceState::Closed:
        break;
    }
    if (materialization != nullptr && !materialization->success) {
        return materialization->error.empty()
                   ? AF_TR("This draft could not be applied to the accepted argument.")
                   : ui::i18n::trf("This draft could not be applied: {0}", materialization->error);
    }
    return {};
}

// What each group did, counted from the change index rather than from its
// operation list.
//
// The index records the model immediately before and after each group, so it
// reports what an operation actually produced. Counting operation types would
// report what was asked for -- and an `AddSupportedBy` that materializes as one
// relationship on the first sub-goal and an extension of the same relationship
// on the next would be counted twice.
struct GroupCounts {
    int elements_added = 0;
    int elements_modified = 0;
    int elements_removed = 0;
    int relationships_added = 0;
    int relationships_removed = 0;
};

bool IsRelationshipId(const parser::AssuranceCase& working,
                      const core::drafts::DraftChangeIndex& index,
                      const std::string& element_id) {
    for (const core::SacmElement& element : working.elements) {
        if (element.id == element_id)
            return parser::IsRelationshipElement(element);
    }
    // Removed elements are absent from the working model by definition, so the
    // index keeps them for exactly this reason.
    for (const core::SacmElement& element : index.removed) {
        if (element.id == element_id)
            return parser::IsRelationshipElement(element);
    }
    return false;
}

std::map<std::string, GroupCounts> CountContributionsByGroup(const core::drafts::DraftMaterializationResult& result) {
    std::map<std::string, GroupCounts> counts;
    for (const auto& [element_id, entry] : result.change_index.elements) {
        const bool relationship = IsRelationshipId(result.working_model, result.change_index, element_id);
        for (const core::drafts::DraftElementContribution& contribution : entry.contributions) {
            GroupCounts& group = counts[contribution.group_id];
            switch (contribution.change) {
            case core::drafts::DraftElementChange::Added:
                if (relationship)
                    ++group.relationships_added;
                else
                    ++group.elements_added;
                break;
            case core::drafts::DraftElementChange::Modified:
                // A relationship is never "modified" in this vocabulary --
                // the operations address it by endpoints and replace it --
                // so this only ever counts elements.
                ++group.elements_modified;
                break;
            case core::drafts::DraftElementChange::Removed:
                if (relationship)
                    ++group.relationships_removed;
                else
                    ++group.elements_removed;
                break;
            case core::drafts::DraftElementChange::Unchanged:
                break;
            }
        }
    }
    return counts;
}

// Findings attributable to one group: those against an element it contributed
// to. An author should not be shown problems about parts of the argument they
// did not write.
std::vector<std::string> FindingsForGroup(const core::drafts::DraftMaterializationResult& result,
                                          const std::string& group_id) {
    std::vector<std::string> findings;
    for (const core::sccg::StagedFinding& finding : result.sccg_findings) {
        const std::vector<std::string> contributors = result.change_index.ContributingGroupIds(finding.element_id);
        if (std::find(contributors.begin(), contributors.end(), group_id) == contributors.end())
            continue;
        // Joined rather than translated: a guideline id and a detail with a
        // colon between them is not a sentence a translator can improve.
        findings.push_back(finding.guideline_id.empty() ? finding.detail
                                                        : finding.guideline_id + ": " + finding.detail);
    }
    return findings;
}

std::string TitleOf(const core::drafts::DraftWorkspace& workspace, const std::string& group_id) {
    const core::drafts::DraftChangeGroup* group = workspace.FindGroup(group_id);
    if (group == nullptr || group->title.empty())
        return group_id;
    return group->title;
}

// An element in the working model, or among those the draft removed. A category
// a term names can be either: created by this group, already accepted, or gone.
const core::SacmElement* FindElementInGroupView(const core::drafts::DraftMaterializationResult& result,
                                                const std::string& element_id) {
    for (const core::SacmElement& candidate : result.working_model.elements) {
        if (candidate.id == element_id)
            return &candidate;
    }
    for (const core::SacmElement& removed : result.change_index.removed) {
        if (removed.id == element_id)
            return &removed;
    }
    return nullptr;
}

// The terms a group touches, with their full text. A term has no canvas node
// -- glossary content is deliberately not drawn as argument -- so the row is
// the one place a reviewer can read what a staged definition actually says
// before accepting it. Post-state for adds and edits (what accepting produces),
// the removed term's own text for removals.
std::vector<std::string> GlossaryLinesForGroup(const core::drafts::DraftMaterializationResult& result,
                                               const std::string& group_id) {
    std::vector<std::string> lines;
    for (const std::string& element_id : result.change_index.ChangedElementIds()) {
        const std::vector<std::string> contributors = result.change_index.ContributingGroupIds(element_id);
        if (std::find(contributors.begin(), contributors.end(), group_id) == contributors.end())
            continue;
        const core::drafts::DraftElementEntry* entry = result.change_index.Find(element_id);
        if (entry == nullptr)
            continue;

        const core::SacmElement* element = nullptr;
        if (entry->change == core::drafts::DraftElementChange::Removed) {
            for (const core::SacmElement& removed : result.change_index.removed) {
                if (removed.id == element_id) {
                    element = &removed;
                    break;
                }
            }
        } else {
            for (const core::SacmElement& candidate : result.working_model.elements) {
                if (candidate.id == element_id) {
                    element = &candidate;
                    break;
                }
            }
        }
        if (element == nullptr || element->type != "term")
            continue;

        std::string line = element->content.empty() ? element->id : element->content;
        if (!element->description.empty())
            line += ": " + element->description;
        // Classification and source, or a group that only categorizes terms
        // would list them with their definitions and show nothing of what it
        // actually changed. Names where the category resolves, so the reviewer
        // reads "Regulatory terms" rather than an id.
        if (!element->category_refs.empty()) {
            std::string categories;
            for (const std::string& category_ref : element->category_refs) {
                const core::SacmElement* category = FindElementInGroupView(result, category_ref);
                const std::string label =
                    (category != nullptr && !category->name.empty()) ? category->name : category_ref;
                if (!categories.empty())
                    categories += ", ";
                categories += label;
            }
            line += " [" + categories + "]";
        }
        if (!element->external_reference.empty())
            line += " (" + element->external_reference + ")";
        if (entry->change == core::drafts::DraftElementChange::Removed)
            line = ui::i18n::trf("{0} (removed)", line);
        lines.push_back(std::move(line));
    }
    return lines;
}

// The accepted terminology package holding `term_id`, or nothing when the term
// is not in the accepted glossary -- which is the ordinary case for a term this
// draft created and nobody has promoted yet.
//
// Searches the case-level packages and each argument package's, mirroring where
// the terminology panel itself looks for one.
bool FindAcceptedTerm(const sacm::AssuranceCasePackage& package,
                      const std::string& term_id,
                      core::TerminologyPackageRef& out_package_ref,
                      core::TerminologyTermRef& out_term_ref) {
    const core::TerminologyTermRef wanted{term_id, {}};
    const auto search = [&](const std::vector<sacm::TerminologyPackage>& packages) {
        for (const sacm::TerminologyPackage& terminology : packages) {
            if (core::FindTerminologyTerm(terminology, wanted) == nullptr)
                continue;
            out_package_ref = core::TerminologyPackageRef{terminology.id, terminology.gid};
            out_term_ref = wanted;
            return true;
        }
        return false;
    };

    if (search(package.terminologyPackages))
        return true;
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        if (search(argument_package.terminologyPackages))
            return true;
    }
    return false;
}

} // namespace

DraftGroupFocus ResolveDraftGroupFocus(const AppRuntimeState& state, const std::string& group_id) {
    DraftGroupFocus focus;
    const core::drafts::DraftMaterializationResult* materialization = state.draft_frame_materialization.get();
    if (materialization == nullptr)
        return focus;

    // In materialization order, so "the first element this group changed" is
    // stable between frames rather than whichever the map happened to yield.
    for (const std::string& element_id : materialization->change_index.ChangedElementIds()) {
        const std::vector<std::string> contributors = materialization->change_index.ContributingGroupIds(element_id);
        if (std::find(contributors.begin(), contributors.end(), group_id) == contributors.end())
            continue;

        const core::SacmElement* element = FindElementInGroupView(*materialization, element_id);
        if (element != nullptr && element->type == "term") {
            if (state.app_state.has_projected_package() &&
                FindAcceptedTerm(state.app_state.projected_package(), element_id, focus.package_ref, focus.term_ref)) {
                focus.kind = DraftGroupFocusKind::Terminology;
                focus.element_id = element_id;
            }
            // Otherwise the term exists only in the draft, and the row is the
            // only place it can be read. Leave the user on it.
            return focus;
        }
        if (element != nullptr && element->type == "category") {
            // A category is glossary structure with no view of its own until it
            // is accepted; the row names it.
            return focus;
        }

        // A removed element is not on the canvas to be centred on, but the
        // presentation view keeps it as a tombstone, so selecting it still lands
        // somewhere the user can see.
        focus.kind = DraftGroupFocusKind::Canvas;
        focus.element_id = element_id;
        return focus;
    }
    return focus;
}

ui::panels::DraftChangesPanelModel BuildDraftChangesPanelModel(AppRuntimeState& state) {
    ui::panels::DraftChangesPanelModel model;
    const core::drafts::DraftWorkspace* workspace = state.draft_workspace.workspace();
    if (workspace == nullptr)
        return model;

    const core::drafts::DraftMaterializationResult* materialization = state.draft_frame_materialization.get();
    model.has_workspace = true;
    model.workspace_blocked_reason = WorkspaceBlockedReason(*workspace, materialization);
    model.accept_error = ui::DraftAcceptError(ui::GetUiState(), workspace->working_revision);
    model.selected_group_id = ui::GetUiState().selected_draft_group_id;

    const std::map<std::string, GroupCounts> counts =
        materialization != nullptr ? CountContributionsByGroup(*materialization) : std::map<std::string, GroupCounts>{};

    for (const core::drafts::DraftChangeGroup& group : workspace->groups) {
        if (!group.recoverable())
            continue;

        ui::panels::DraftChangeRow row;
        row.group_id = group.id;
        row.title = group.title;
        row.summary = group.summary;
        row.rationale = group.rationale;
        row.source_label = group.source_label;
        row.source_session_id = group.source_session_id;
        row.source_kind_label = SourceKindLabel(group.source);
        row.state_label = GroupStateLabel(group.state);
        row.operation_count = static_cast<int>(group.operations.size());
        row.guideline_ids = group.guideline_ids;
        row.review_item_ids = group.review_item_ids;
        row.needs_attention = group.state == core::drafts::DraftGroupState::NeedsAttention;
        row.selected = group.id == model.selected_group_id;

        const auto found = counts.find(group.id);
        if (found != counts.end()) {
            row.elements_added = found->second.elements_added;
            row.elements_modified = found->second.elements_modified;
            row.elements_removed = found->second.elements_removed;
            row.relationships_added = found->second.relationships_added;
            row.relationships_removed = found->second.relationships_removed;
        }

        for (const std::string& dependency : group.depends_on_group_ids)
            row.depends_on_titles.push_back(TitleOf(*workspace, dependency));

        if (materialization != nullptr) {
            row.glossary_lines = GlossaryLinesForGroup(*materialization, group.id);
            row.findings = FindingsForGroup(*materialization, group.id);
        }

        // Promotability is answered by planning the promotion, not by guessing
        // at it. The plan materializes the selection against the accepted
        // baseline *and* the remainder against the result, which is the only
        // thing that can tell whether accepting this row alone would strand the
        // rest of the draft.
        if (!model.workspace_blocked_reason.empty()) {
            row.promotable = false;
        } else if (row.needs_attention) {
            row.promotable = false;
            row.blocked_reason = AF_TR("A change this one was built on was rejected, so it no longer applies.");
        } else if (state.app_state.loaded_case.has_value()) {
            // Rehearsed exactly as PromoteDraftGroups will run it: accepting
            // the user's own edits is their submission, so a human Building
            // group must not show as blocked when the accept would succeed.
            core::drafts::DraftWorkspace rehearsal = *workspace;
            for (core::drafts::DraftChangeGroup& candidate : rehearsal.groups) {
                if (candidate.state == core::drafts::DraftGroupState::Building &&
                    candidate.source == core::drafts::DraftSource::Human)
                    candidate.state = core::drafts::DraftGroupState::Ready;
            }
            const core::drafts::DraftPromotionPlan plan =
                core::drafts::PlanDraftPromotion(rehearsal,
                                                 state.app_state.loaded_case.value(),
                                                 {group.id},
                                                 core::drafts::DraftPromotionAuthor(*workspace, {group.id}),
                                                 state.draft_workspace.authoritative_identities());
            row.promotable = plan.ok;
            row.blocked_reason = plan.error;
            for (const std::string& added : plan.added_by_closure)
                row.also_accepts_titles.push_back(TitleOf(*workspace, added));
        }

        model.rows.push_back(std::move(row));
    }

    // Sequence order, which is materialization order. The list a reviewer reads
    // has to be the order the changes were applied in, or a group that depends
    // on another can appear above it.
    std::sort(model.rows.begin(), model.rows.end(), [workspace](const auto& left, const auto& right) {
        const core::drafts::DraftChangeGroup* left_group = workspace->FindGroup(left.group_id);
        const core::drafts::DraftChangeGroup* right_group = workspace->FindGroup(right.group_id);
        if (left_group == nullptr || right_group == nullptr)
            return false;
        return left_group->sequence < right_group->sequence;
    });
    return model;
}

void RenderDraftChangesAreaContent(AppRuntimeState& state, const DraftChangesAreaCallbacks& callbacks) {
    const ui::panels::DraftChangesPanelModel model = BuildDraftChangesPanelModel(state);

    ui::panels::DraftChangesPanelCallbacks panel_callbacks;
    panel_callbacks.select_group = [&callbacks](const std::string& group_id) {
        ui::UiState& ui_state = ui::GetUiState();
        ui_state.selected_draft_group_id = group_id;
        ui_state.center_on_draft_group_pending = true;
        if (callbacks.focus_group)
            callbacks.focus_group(group_id);
    };
    panel_callbacks.accept_group = callbacks.accept_group;
    panel_callbacks.reject_group = callbacks.reject_group;
    panel_callbacks.accept_all = callbacks.accept_all;

    ui::panels::RenderDraftChangesPanel(model, panel_callbacks);
}

} // namespace app::areas
