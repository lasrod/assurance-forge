#include "app/commands/dispatch.h"
#include "app/app_runtime.h"

#include "app/actions/element_actions.h"
#include "app/actions/proposal_actions.h"
#include "app/actions/review_actions.h"
#include "app/actions/terminology_actions.h"
#include "app/app_runtime_internal.h"
#include "app/areas/ai_debug_area.h"
#include "core/audit/audit_snapshot.h"
#include "app/areas/argument_navigator_area.h"
#include "app/areas/feedback_dock_area.h"
#include "app/areas/inspector_area.h"
#include "app/areas/modal_host.h"
#include "app/areas/proposal_editor_area.h"
#include "app/areas/project_explorer_area.h"
#include "app/areas/review_panel_area.h"
#include "app/acp_problem_sync.h"
#include "app/areas/workbench_area.h"
#include "app/app_runtime_state.h"
#include "app/confidence_problem_sync.h"
#include "app/frame/app_menu_bar.h"
#include "app/frame/app_shell.h"
#include "app/native_file_dialogs.h"
#include "app/proposal_ui_state.h"
#include "app/project_workflow.h"
#include "app/recent_projects.h"
#include "app/register_problem_sync.h"
#include "app/review_problem_sync.h"
#include "app/terminology_problem_sync.h"
#include "app/structure_problem_sync.h"
#include "core/acp/assurance_claim_point.h"
#include "core/app_state.h"
#include "core/derived_views.h"
#include "core/drafts/draft_dependency_graph.h"
#include "core/drafts/draft_document_diff.h"
#include "core/drafts/draft_promotion_service.h"
#include "core/element_factory.h"
#include "core/problems/problem_attention.h"
#include "core/problems/problems_manager.h"
#include "core/registers/register_model.h"
#include "core/reviews/review_proposal_manager.h"
#include "core/string_utils.h"
#include "core/terminology_package_service.h"
#include "core/terminology_scope_service.h"
#include "core/time_utils.h"
#include "imgui.h"
#include "parser/model_utils.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/document_edit.h"
#include "ui/gsn/gsn_adapter.h"
#include "ui/i18n/localization.h"
#include "ui/gsn/gsn_canvas.h"
#include "ui/imgui_buffer_utils.h"
#include "ui/panels/sacm_viewer_panel.h"
#include "ui/register_views.h"
#include "ui/ui_state.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace app {
namespace {

// Collect every element id/gid that belongs to a confidence argument package. These elements
// (the confidence top-goal claim and any other claims/reasonings/relationships authored inside
// the separate confidence tree) must be hidden from the main GSN tree; they only belong on the
// confidence argument package canvas tab.
void CollectConfidencePackageElementIdentities(const sacm::AssuranceCasePackage& package,
                                               std::unordered_set<std::string>& ids,
                                               std::unordered_set<std::string>& gids) {
    auto add = [&](const sacm::SacmElement& element) {
        if (!element.id.empty())
            ids.insert(element.id);
        if (!element.gid.empty())
            gids.insert(element.gid);
    };
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        if (!core::acp::IsConfidenceArgumentPackage(argument_package))
            continue;
        for (const sacm::Claim& claim : argument_package.claims)
            add(claim);
        for (const sacm::ArgumentReasoning& reasoning : argument_package.argumentReasonings)
            add(reasoning);
        for (const sacm::AssertedInference& inference : argument_package.assertedInferences)
            add(inference);
        for (const sacm::AssertedContext& context : argument_package.assertedContexts)
            add(context);
        for (const sacm::AssertedEvidence& evidence : argument_package.assertedEvidences)
            add(evidence);
        for (const sacm::ArtifactReference& artifact_reference : argument_package.artifactReferences)
            add(artifact_reference);
    }
}

std::optional<parser::AssuranceCase>
FilterConfidencePackageElementsFromMainTree(const parser::AssuranceCase& source,
                                            const sacm::AssuranceCasePackage* package) {
    if (!package)
        return std::nullopt;
    std::unordered_set<std::string> hidden_ids;
    std::unordered_set<std::string> hidden_gids;
    CollectConfidencePackageElementIdentities(*package, hidden_ids, hidden_gids);
    if (hidden_ids.empty() && hidden_gids.empty())
        return std::nullopt;

    parser::AssuranceCase filtered;
    filtered.id = source.id;
    filtered.name = source.name;
    filtered.description = source.description;
    filtered.elements.reserve(source.elements.size());
    for (const parser::SacmElement& element : source.elements) {
        if (hidden_ids.count(element.id) > 0)
            continue;
        if (!element.gid.empty() && hidden_gids.count(element.gid) > 0)
            continue;
        filtered.elements.push_back(element);
    }
    filtered.acps = source.acps;
    if (filtered.elements.size() == source.elements.size())
        return std::nullopt;
    return filtered;
}

} // namespace

std::string ReviewItemIdFromProblemId(const std::string& problem_id) {
    constexpr const char* kReviewPrefix = "review-comment:";
    constexpr const char* kGuidelinePrefix = "guideline-review:";

    if (problem_id.rfind(kReviewPrefix, 0) == 0)
        return problem_id.substr(std::char_traits<char>::length(kReviewPrefix));

    if (problem_id.rfind(kGuidelinePrefix, 0) == 0) {
        const size_t start = std::char_traits<char>::length(kGuidelinePrefix);
        const size_t end = problem_id.find(':', start);
        if (end == std::string::npos)
            return problem_id.substr(start);
        return problem_id.substr(start, end - start);
    }

    return {};
}

ui::ElementContextActions MakeElementContextActions(AppRuntime& runtime) {
    ui::ElementContextActions actions{
        [&runtime](core::NewElementKind kind) { runtime.AddChildToSelected(kind); },
        [&runtime]() { runtime.AddTopGoal(); },
        [&runtime]() { runtime.AddAcpToSelectedElement(); },
        [&runtime](const std::string& relationship_id) { runtime.AddAcpToRelationship(relationship_id); },
        [&runtime](const std::string& acp_id) { runtime.RemoveAcp(acp_id); },
        [&runtime](core::RemoveMode mode) { runtime.RemoveSelected(mode); },
        [&runtime]() { runtime.RenderAiReviewContextMenuForSelected(); },
        [&runtime](const char* feature) {
            if (feature)
                runtime.ShowNotImplementedModal(feature);
        },
    };
    actions.set_status = [&runtime](const std::string& message) { runtime.SetStatus(message); };
    actions.focus_problem = [](const std::string& problem_id, const std::string& element_id) {
        ui::FocusProblemInPanel(ui::GetUiState(), problem_id, element_id);
    };
    actions.add_counter_argument = [&runtime]() { runtime.AddCounterArgumentToSelected(); };
    actions.add_counter_evidence = [&runtime]() { runtime.AddCounterEvidenceToSelected(); };
    actions.add_counter_argument_to_relationship = [&runtime](const std::string& relationship_id) {
        runtime.AddCounterArgumentToRelationship(relationship_id);
    };
    actions.add_counter_evidence_to_relationship = [&runtime](const std::string& relationship_id) {
        runtime.AddCounterEvidenceToRelationship(relationship_id);
    };
    actions.remove_relationship = [&runtime](const std::string& relationship_id) {
        runtime.RemoveRelationship(relationship_id);
    };
    actions.open_evidence_location = [&runtime](const std::string& location) {
        runtime.OpenEvidenceLocation(location);
    };
    return actions;
}

AppRuntime::AppRuntime() : impl_(std::make_unique<AppRuntimeState>()) {
    RegisterAppEventListeners();

    impl_->current_tree = core::AssuranceTree();
    ui::gsn::SetCanvasTree(impl_->current_tree);
    ui::RebuildRegisterViews(nullptr, impl_->register_controller->Store());

    ui::UiState& ui_state = ui::GetUiState();
    ui_state.center_view = ui::CenterView::GsnCanvas;
    ui_state.selected_element_id.clear();
    ui_state.selected_acp_id.clear();
    ui_state.selected_relationship_id.clear();
    ui_state.selected_relationship_edge_key.clear();
    ui_state.selected_problem_id.clear();
    ui_state.selected_problem_element_id.clear();
}

AppRuntime::~AppRuntime() = default;

void AppRuntime::RegisterAppEventListeners() {
    impl_->events.Subscribe<StatusMessageEvent>(
        [this](const StatusMessageEvent& event) { impl_->app_state.status_message = event.message; });
    impl_->events.Subscribe<AutosaveFailedEvent>(
        [this](const AutosaveFailedEvent& event) { impl_->last_autosave_error = event.error; });
    impl_->events.Subscribe<TreeDirtyEvent>([this](const TreeDirtyEvent& event) {
        impl_->tree_needs_rebuild = event.dirty;
        impl_->tree_edit_index_valid = false;
        if (event.focus_root)
            impl_->workbench.pending_focus_root = true;
    });
    impl_->events.Subscribe<DocumentDirtyEvent>([this](const DocumentDirtyEvent& event) {
        // The command bus autosaves, but callers emit this event afterwards and
        // cannot know that. Honouring `mark_app_dirty` unconditionally is what
        // made the app report unsaved work that was already on disk. The
        // revision still bumps, so derived views rebuild either way.
        const bool already_persisted = impl_->autosave_persisted_pending_edit;
        impl_->autosave_persisted_pending_edit = false;
        // document_dirty gates the save-before-open/close prompts and the
        // needs_sacm_save check, so it has to respect the autosave too --
        // otherwise the user is asked to save work the bus already wrote.
        impl_->document_dirty = already_persisted ? false : event.dirty;
        if (event.mark_app_dirty && !already_persisted)
            impl_->app_state.mark_dirty();
        else
            impl_->app_state.bump_case_revision();
    });
    impl_->events.Subscribe<ProjectFilesChangedEvent>(
        [this](const ProjectFilesChangedEvent&) { impl_->sacm_package_tree_cache.clear(); });
    impl_->events.Subscribe<ReviewItemsDirtyEvent>([this](const ReviewItemsDirtyEvent& event) {
        if (event.mark_app_dirty)
            impl_->app_state.mark_dirty();
        impl_->problems_dirty.review = true;
    });
    impl_->events.Subscribe<ConfidenceDirtyEvent>([this](const ConfidenceDirtyEvent& event) {
        if (event.mark_app_dirty)
            impl_->app_state.mark_dirty();
        impl_->problems_dirty.confidence = true;
    });
    impl_->events.Subscribe<RegisterAssessmentsDirtyEvent>([this](const RegisterAssessmentsDirtyEvent& event) {
        if (event.mark_app_dirty)
            impl_->app_state.mark_dirty();
        impl_->problems_dirty.registers = true;
    });
    impl_->events.Subscribe<SelectionChangedEvent>([](const SelectionChangedEvent& event) {
        ui::UiState& ui_state = ui::GetUiState();
        ui_state.selected_element_id = event.element_id;
        ui_state.selected_acp_id.clear();
        ui_state.selected_relationship_id.clear();
        ui_state.selected_relationship_edge_key.clear();
        ui_state.center_on_selection = event.center_on_selection;
    });
    impl_->events.Subscribe<ElementReviewVisualEvent>([](const ElementReviewVisualEvent& event) {
        ui::UiState& ui_state = ui::GetUiState();
        switch (event.kind) {
        case ElementReviewVisualEventKind::AiStarted:
            ui::BeginAiReviewSpinner(ui_state, event.element_id, event.review_scope_element_ids);
            break;
        case ElementReviewVisualEventKind::AiNoFindings:
        case ElementReviewVisualEventKind::AiFindings:
        case ElementReviewVisualEventKind::AiFailed:
            ui::EndAiReviewSpinner(ui_state, event.element_id);
            break;
        case ElementReviewVisualEventKind::ManualOk:
            // No badge change required - problems panel reflects status via
            // the underlying ProblemsManager state, which is the single source
            // of truth for badges.
            break;
        }
    });
    impl_->events.Subscribe<AiReviewProposalSuggestionsEvent>([this](const AiReviewProposalSuggestionsEvent& event) {
        actions::ProposalActions(*impl_).CreateAiGenerated(event);
    });
    impl_->events.Subscribe<ArgumentPackageCanvasRequestEvent>([this](const ArgumentPackageCanvasRequestEvent& event) {
        OpenArgumentPackageCanvas(event.package_id, event.package_gid, event.display_name, event.focus_element_id);
    });
    impl_->events.Subscribe<CenterRequestEvent>([this](const CenterRequestEvent& event) {
        ui::UiState& ui_state = ui::GetUiState();
        switch (event.view) {
        case CenterViewRequest::Preserve:
            break;
        case CenterViewRequest::ProjectOverview:
            ui_state.center_view = ui::CenterView::ProjectOverview;
            break;
        case CenterViewRequest::GsnCanvas:
            ui_state.center_view = ui::CenterView::GsnCanvas;
            break;
        case CenterViewRequest::CseRegister:
            ui_state.center_view = ui::CenterView::CseRegister;
            break;
        case CenterViewRequest::EvidenceRegister:
            ui_state.center_view = ui::CenterView::EvidenceRegister;
            break;
        }
        ui_state.center_on_selection = event.center_on_selection;
        ui_state.center_on_marked = event.center_on_marked;
        if (event.force_tab_selection)
            impl_->workbench.force_center_tab_selection = true;
    });
    impl_->events.Subscribe<ProposalHighlightEvent>([](const ProposalHighlightEvent& event) {
        ui::UiState& ui_state = ui::GetUiState();
        ui_state.proposal_highlight_ids = event.highlight_ids;
        ui_state.proposal_text_changes.clear();
        ui_state.marked_for_removal = event.marked_for_removal;
        ui_state.dim_non_proposal_nodes = event.dim_non_proposal_nodes;
        ui_state.center_on_marked = event.center_on_marked;
    });
    impl_->events.Subscribe<ModalRequestEvent>([this](const ModalRequestEvent& event) {
        impl_->modal_coordinator->ApplyModalRequest(event);
        switch (event.kind) {
        case ModalKind::StartupProject:
            impl_->project_controller->show_startup_project_window = event.open;
            break;
        default:
            break;
        }
    });
}

void AppRuntime::RequestClose() {
    impl_->modal_coordinator->RequestClose();
}

bool AppRuntime::AddChildToSelected(core::NewElementKind kind) {
    // Into the draft when there is one. The canvas is drawing the working model,
    // so an add applied to the accepted model underneath it lands somewhere the
    // user was not looking -- and against a parent the accepted model may not
    // contain at all, because another draft group created it.
    if (AddChildToSelectedAsDraft(kind))
        return true;
    return actions::ElementActions(*impl_).AddChildToSelected(kind);
}

bool AppRuntime::AddTopGoal() {
    if (AddTopGoalAsDraft())
        return true;
    return actions::ElementActions(*impl_).AddTopGoal();
}

bool AppRuntime::AddAcpToSelectedElement() {
    return actions::ElementActions(*impl_).AddAcpToSelectedElement();
}

bool AppRuntime::AddAcpToRelationship(const std::string& relationship_id) {
    return actions::ElementActions(*impl_).AddAcpToRelationship(relationship_id);
}

bool AppRuntime::RemoveAcp(const std::string& acp_id) {
    return actions::ElementActions(*impl_).RemoveAcp(acp_id);
}

bool AppRuntime::AddCounterArgumentToSelected() {
    return actions::ElementActions(*impl_).AddChallengeToSelectedElement(core::ChallengeSourceType::CounterArgument);
}

bool AppRuntime::AddCounterEvidenceToSelected() {
    return actions::ElementActions(*impl_).AddChallengeToSelectedElement(core::ChallengeSourceType::CounterEvidence);
}

bool AppRuntime::AddCounterArgumentToRelationship(const std::string& relationship_id) {
    return actions::ElementActions(*impl_).AddChallengeToRelationship(relationship_id,
                                                                      core::ChallengeSourceType::CounterArgument);
}

bool AppRuntime::AddCounterEvidenceToRelationship(const std::string& relationship_id) {
    return actions::ElementActions(*impl_).AddChallengeToRelationship(relationship_id,
                                                                      core::ChallengeSourceType::CounterEvidence);
}

void AppRuntime::RemoveSelected(core::RemoveMode mode) {
    if (RemoveSelectedAsDraft(mode))
        return;
    actions::ElementActions(*impl_).RemoveSelected(mode);
}

void AppRuntime::LocateElementOnCanvas(const std::string& element_id) {
    if (element_id.empty())
        return;
    ui::UiState& ui_state = ui::GetUiState();
    ui_state.selected_element_id = element_id;
    ui_state.selected_acp_id.clear();
    ui_state.selected_relationship_id.clear();
    ui_state.selected_relationship_edge_key.clear();

    // The canvas tabs are per argument package, so the element's own package
    // is opened (or brought forward) with the element focused. The working
    // document is asked first: an element that exists only in the draft is
    // not in the accepted one.
    const sacm_adapter::LibraryDocument* document = nullptr;
    if (DraftEditingActive())
        document = impl_->draft_document.document();
    if (document == nullptr)
        document = impl_->app_state.library_document.get();
    std::string package_id;
    if (document != nullptr)
        package_id = sacm_adapter::resolve_argument_package_id(*document, element_id);

    std::string package_gid;
    std::string title;
    if (!package_id.empty() && impl_->app_state.has_projected_package()) {
        for (const sacm::ArgumentPackage& package : impl_->app_state.projected_package().argumentPackages) {
            if (package.id != package_id)
                continue;
            package_gid = package.gid;
            title = package.name.empty() ? package.id : package.name;
            break;
        }
    }
    if (!package_id.empty()) {
        OpenArgumentPackageCanvas(package_id, package_gid, title, element_id);
        return;
    }
    // No package could be resolved (a legacy-parsed file): select and centre in
    // whatever canvas is showing, which is all the canvas can do for it.
    impl_->workbench.show_gsn_tab = true;
    ui_state.center_view = ui::CenterView::GsnCanvas;
    ui_state.center_on_selection = true;
    impl_->workbench.force_center_tab_selection = true;
}

void AppRuntime::RemoveEvidence(const std::string& evidence_id) {
    if (evidence_id.empty())
        return;
    // Both removal paths read the selection, and selecting it first also means
    // the canvas shows what went when the user looks.
    ui::UiState& ui_state = ui::GetUiState();
    ui_state.selected_element_id = evidence_id;
    ui_state.selected_acp_id.clear();
    ui_state.selected_relationship_id.clear();
    ui_state.selected_relationship_edge_key.clear();
    // A Solution has no descendants, so NodeOnly and NodeAndDescendants remove
    // the same thing; NodeOnly is the mode whose plan a leaf node needs. A draft
    // removal is staged and reviewable, so it needs no confirmation; an audited
    // one always asks, with the library's preview of what goes with the row.
    if (RemoveSelectedAsDraft(core::RemoveMode::NodeOnly))
        return;
    actions::ElementActions(*impl_).RemoveSelected(core::RemoveMode::NodeOnly,
                                                   controllers::ElementEditController::RemovalConfirmation::Always);
}

bool AppRuntime::SetEvidenceLocation(const std::string& evidence_id, const std::string& location) {
    if (evidence_id.empty())
        return false;
    if (DraftEditingActive()) {
        core::reviews::PatchOperation set;
        set.type = core::reviews::PatchOperationType::SetEvidenceLocation;
        core::reviews::ElementRef element;
        element.existing_id = evidence_id;
        set.element = element;
        set.new_value = location;
        std::string error;
        if (!StageHumanDraftOperations(AF_TR("My edits"), {set}, error)) {
            SetStatus(ui::i18n::trf("Could not record the location in the draft: {0}", error));
            return false;
        }
        return true;
    }
    return impl_->element_edit_controller->SetEvidenceLocation(*impl_, evidence_id, location);
}

bool AppRuntime::SetEvidenceAttribute(const std::string& evidence_id,
                                      core::EvidenceAttribute attribute,
                                      const std::string& value) {
    if (evidence_id.empty())
        return false;
    if (DraftEditingActive()) {
        core::reviews::PatchOperation set;
        set.type = core::reviews::PatchOperationType::SetEvidenceAttribute;
        core::reviews::ElementRef element;
        element.existing_id = evidence_id;
        set.element = element;
        set.field = core::EvidenceAttributeToken(attribute);
        set.new_value = value;
        std::string error;
        if (!StageHumanDraftOperations(AF_TR("My edits"), {set}, error)) {
            SetStatus(ui::i18n::trf("Could not record the evidence attribute in the draft: {0}", error));
            return false;
        }
        return true;
    }
    return impl_->element_edit_controller->SetEvidenceAttribute(*impl_, evidence_id, attribute, value);
}

namespace {

// The project-file assessment fields, mapped onto the register's SACM-backed
// columns. Recency was free text about when the evidence dates from; it
// becomes the Artifact's date, which is what it was standing in for.
std::vector<core::commands::EvidenceAttributeWrite>
EvidenceWritesFromAssessment(const std::string& evidence_id, const core::registers::EvidenceMetadata& metadata) {
    std::vector<core::commands::EvidenceAttributeWrite> writes;
    const auto add = [&](core::EvidenceAttribute attribute, const std::string& value) {
        if (!value.empty())
            writes.push_back(core::commands::EvidenceAttributeWrite{evidence_id, attribute, value});
    };
    add(core::EvidenceAttribute::Owner, metadata.evidence_owner);
    add(core::EvidenceAttribute::Type, metadata.type);
    add(core::EvidenceAttribute::Date, metadata.recency);
    add(core::EvidenceAttribute::Maturity, metadata.maturity);
    add(core::EvidenceAttribute::ControlledEnvironment, metadata.controlled_environment);
    add(core::EvidenceAttribute::Notes, metadata.notes);
    return writes;
}

} // namespace

void AppRuntime::MigrateEvidenceAssessments() {
    const parser::AssuranceCase& argument = CurrentArgumentView();
    std::vector<core::commands::EvidenceAttributeWrite> writes;
    std::vector<std::string> migrated_ids;
    for (const auto& [evidence_id, metadata] : impl_->register_controller->Store().evidence) {
        const parser::SacmElement* element = parser::FindElementById(argument, evidence_id);
        // An assessment whose subject is gone, or is not evidence, stays in the
        // project file, where the Problems panel already reports it.
        if (element == nullptr || element->type != "artifactreference")
            continue;
        std::vector<core::commands::EvidenceAttributeWrite> row = EvidenceWritesFromAssessment(evidence_id, metadata);
        writes.insert(writes.end(), row.begin(), row.end());
        migrated_ids.push_back(evidence_id);
    }
    if (writes.empty() && migrated_ids.empty()) {
        SetStatus(AF_TR("No project-file assessments match evidence in the argument."));
        return;
    }

    if (DraftEditingActive()) {
        std::vector<core::reviews::PatchOperation> operations;
        for (const core::commands::EvidenceAttributeWrite& write : writes) {
            core::reviews::PatchOperation set;
            set.type = core::reviews::PatchOperationType::SetEvidenceAttribute;
            core::reviews::ElementRef element;
            element.existing_id = write.element_id;
            set.element = element;
            set.field = core::EvidenceAttributeToken(write.attribute);
            set.new_value = write.value;
            operations.push_back(std::move(set));
        }
        std::string error;
        if (!operations.empty() && !StageHumanDraftOperations(AF_TR("My edits"), operations, error)) {
            SetStatus(ui::i18n::trf("Could not move assessments into the draft: {0}", error));
            return;
        }
    } else {
        std::size_t applied = 0;
        if (!writes.empty() && !impl_->element_edit_controller->ImportEvidenceAssessments(*impl_, writes, applied))
            return;
    }

    // Only once every write is in the document (or the draft) does the
    // project-file copy go; the file is rewritten at the next project save.
    for (const std::string& evidence_id : migrated_ids)
        impl_->register_controller->DiscardEvidenceAssessment(evidence_id);
    impl_->register_controller->MarkDirty();
    impl_->problems_dirty.registers = true;
    impl_->tree_needs_rebuild = true;
    const int count = static_cast<int>(migrated_ids.size());
    SetStatus(ui::i18n::trnf(
        "Moved {0} assessment into the SACM document.", "Moved {0} assessments into the SACM document.", count, count));
}

void AppRuntime::BrowseEvidenceLocation(const std::string& evidence_id) {
    if (evidence_id.empty())
        return;
    std::string default_path;
    if (impl_->app_state.current_project.has_value())
        default_path = core::PathToUtf8(impl_->app_state.current_project->rootPath);
    std::string selected;
    std::string error;
    const dialogs::DialogResult result = dialogs::BrowseForEvidenceFile(default_path, selected, error);
    if (result == dialogs::DialogResult::Failed) {
        SetStatus(ui::i18n::trf("Browse failed: {0}", error));
        return;
    }
    if (result != dialogs::DialogResult::Selected)
        return;

    const std::filesystem::path root = impl_->app_state.current_project.has_value()
                                           ? impl_->app_state.current_project->rootPath
                                           : std::filesystem::path{};
    const std::string location = core::EvidenceLocationForPickedFile(root, core::PathFromUtf8(selected));
    SetEvidenceLocation(evidence_id, location);
}

void AppRuntime::CreateEvidence(const std::string& text, const std::string& claim_id) {
    if (DraftEditingActive()) {
        core::reviews::PatchOperation create;
        create.type = core::reviews::PatchOperationType::CreateSolution;
        create.create_ref = "$new";
        create.text = core::TrimWhitespace(text);
        std::vector<core::reviews::PatchOperation> operations{create};
        if (!claim_id.empty()) {
            core::reviews::PatchOperation attach;
            attach.type = core::reviews::PatchOperationType::AddSupportedBy;
            core::reviews::ElementRef source;
            source.create_ref = "$new";
            core::reviews::ElementRef target;
            target.existing_id = claim_id;
            attach.source = source;
            attach.target = target;
            operations.push_back(attach);
        }
        std::string error;
        if (!StageHumanDraftOperations(AF_TR("My edits"), operations, error))
            SetStatus(ui::i18n::trf("Could not add to the draft: {0}", error));
        return;
    }
    impl_->element_edit_controller->CreateEvidence(*impl_, claim_id, text);
}

void AppRuntime::LinkEvidence(const std::string& evidence_id, const std::string& claim_id) {
    if (evidence_id.empty() || claim_id.empty())
        return;
    if (DraftEditingActive()) {
        core::reviews::PatchOperation attach;
        attach.type = core::reviews::PatchOperationType::AddSupportedBy;
        core::reviews::ElementRef source;
        source.existing_id = evidence_id;
        core::reviews::ElementRef target;
        target.existing_id = claim_id;
        attach.source = source;
        attach.target = target;
        std::string error;
        if (!StageHumanDraftOperations(AF_TR("My edits"), {attach}, error))
            SetStatus(ui::i18n::trf("Could not link in the draft: {0}", error));
        return;
    }
    impl_->element_edit_controller->LinkEvidence(*impl_, evidence_id, claim_id);
}

void AppRuntime::UnlinkEvidence(const std::string& evidence_id, const std::string& claim_id) {
    if (evidence_id.empty() || claim_id.empty())
        return;
    if (DraftEditingActive()) {
        core::reviews::PatchOperation withdraw;
        withdraw.type = core::reviews::PatchOperationType::RemoveSupportedBy;
        core::reviews::ElementRef source;
        source.existing_id = evidence_id;
        core::reviews::ElementRef target;
        target.existing_id = claim_id;
        withdraw.source = source;
        withdraw.target = target;
        std::string error;
        if (!StageHumanDraftOperations(AF_TR("My edits"), {withdraw}, error))
            SetStatus(ui::i18n::trf("Could not unlink in the draft: {0}", error));
        return;
    }
    // The relationship the register showed: one AssertedEvidence carrying this
    // evidence to this claim. One that also carries other sources is more than
    // this link, and withdrawing one end is the canvas's edge menu's business.
    for (const core::registers::EvidenceCitation& citation :
         core::registers::DeriveEvidenceCitations(CurrentArgumentView(), evidence_id)) {
        if (citation.claim_id != claim_id)
            continue;
        if (citation.shared) {
            SetStatus(AF_TR("That link is part of a relationship that also supports other elements; remove it "
                            "from the canvas."));
            return;
        }
        RemoveRelationship(citation.relationship_id);
        return;
    }
    SetStatus(ui::i18n::trf("There is no link from {0} to {1}.", evidence_id, claim_id));
}

void AppRuntime::OpenEvidenceLocation(const std::string& location) {
    std::string target = location;
    const bool is_url = target.find("://") != std::string::npos;
    if (!is_url && impl_->app_state.current_project.has_value()) {
        const std::filesystem::path path = core::PathFromUtf8(target);
        if (path.is_relative())
            target = core::PathToUtf8(impl_->app_state.current_project->rootPath / path);
    }
    std::string error;
    if (!dialogs::OpenPathOrUrl(target, error))
        SetStatus(ui::i18n::trf("Could not open the evidence location: {0}", error));
}

bool AppRuntime::RemoveRelationship(const std::string& relationship_id) {
    return impl_->element_edit_controller->RemoveRelationship(*impl_, relationship_id);
}

bool AppRuntime::DropRelationshipReference(const std::string& relationship_id, const std::string& reference) {
    return impl_->element_edit_controller->DropRelationshipReference(*impl_, relationship_id, reference);
}

bool AppRuntime::MoveStrategyToReasoning(const std::string& relationship_id, const std::string& strategy_id) {
    return impl_->element_edit_controller->MoveStrategyToReasoning(*impl_, relationship_id, strategy_id);
}

bool AppRuntime::SetElementUndeveloped(const std::string& element_id, bool undeveloped) {
    return impl_->element_edit_controller->SetElementUndeveloped(*impl_, element_id, undeveloped);
}

bool AppRuntime::RenumberGsnIdentifier(const std::string& element_id) {
    return impl_->element_edit_controller->RenumberGsnIdentifier(*impl_, element_id);
}

core::TreeDropValidationResult AppRuntime::ValidateTreeDrop(const std::string& dragged_id,
                                                            const std::string& target_id,
                                                            core::TreeDropMode drop_mode) const {
    return actions::ElementActions(*impl_).ValidateTreeDrop(dragged_id, target_id, drop_mode);
}

bool AppRuntime::PerformTreeDrop(const std::string& dragged_id,
                                 const std::string& target_id,
                                 core::TreeDropMode drop_mode) {
    return actions::ElementActions(*impl_).PerformTreeDrop(dragged_id, target_id, drop_mode);
}

void AppRuntime::SetStatus(const std::string& message) {
    impl_->events.Emit(StatusMessageEvent{message});
}

// Status text is TRANSLATED AT ITS SOURCE and stored translated, so it bakes the
// language in force at the moment it was set. A message already on screen when
// the user switches language therefore stays in the old one until the next
// action replaces it.
//
// That is a decision, not an oversight (#252). The alternative is the hook
// `AppRuntime::RenderFrame` already runs for `ui::i18n::LanguageEpoch()` changes,
// which would clear `status_message` on a language switch -- and clearing it
// would silently discard a message the user may not have read yet, including a
// failure report. A stale-language sentence is recoverable by reading it; a
// deleted one is not. The status line is transient by design and the next action
// overwrites it in the new language.
//
// The other option -- storing the msgid plus its arguments and translating at
// display -- is what `core` would need, since the layer rule keeps `ui/i18n` out
// of it. That is why the 27 sites in `core::AppState` (file and project
// load/save reporting) are still English and are tracked on #252 rather than
// converted here: `app/` may call `ui::i18n` directly, and `core/` may not.

void AppRuntime::ShowNotImplementedModal(const std::string& feature) {
    impl_->events.Emit(ModalRequestEvent{ModalKind::NotImplemented, true, feature});
}

bool AppRuntime::RefreshProposalCreatorPreview() {
    return actions::ProposalActions(*impl_).RefreshCreatorPreview();
}

void AppRuntime::ProcessPendingProposalCreatorPreviewRefresh() {
    actions::ProposalActions(*impl_).ProcessPendingCreatorPreviewRefresh();
}

bool AppRuntime::BeginProposalForReviewItem(const core::reviews::ReviewItem& item) {
    return actions::ProposalActions(*impl_).BeginForReviewItem(item);
}

bool AppRuntime::BeginEditProposalForReviewItem(const core::reviews::ReviewItem& item) {
    return actions::ProposalActions(*impl_).BeginEditForReviewItem(item);
}

bool AppRuntime::BeginEditProposalById(const std::string& proposal_id) {
    return actions::ProposalActions(*impl_).BeginEditById(proposal_id);
}

bool AppRuntime::PreviewProposalById(const std::string& proposal_id) {
    return actions::ProposalActions(*impl_).PreviewById(proposal_id);
}

bool AppRuntime::SaveActiveProposal(const core::reviews::ReviewItem& item) {
    return actions::ProposalActions(*impl_).SaveActive(item);
}

void AppRuntime::CancelActiveProposal() {
    actions::ProposalActions(*impl_).CancelActive();
}

bool AppRuntime::DeleteProposalPatchFile(const std::string& proposal_id, std::string& error) {
    return actions::ReviewActions(*impl_).DeleteProposalPatchFile(proposal_id, error);
}

void AppRuntime::CloseProposalPreviewIfOpen(const std::string& proposal_id) {
    actions::ReviewActions(*impl_).CloseProposalPreviewIfOpen(proposal_id);
}

void AppRuntime::BeginDeleteReviewItem(const core::reviews::ReviewItem& item) {
    actions::ReviewActions(*impl_).BeginDeleteReviewItem(item);
}

bool AppRuntime::DeleteReviewItem(const core::reviews::ReviewItem& item) {
    return actions::ReviewActions(*impl_).DeleteReviewItem(item);
}

bool AppRuntime::ResolveReviewItem(const core::reviews::ReviewItem& item) {
    return actions::ReviewActions(*impl_).ResolveReviewItem(item, core::NowUtcString());
}

bool AppRuntime::AddProposalChildToSelected(core::NewElementKind kind) {
    return actions::ProposalActions(*impl_).AddChildToSelected(kind);
}

bool AppRuntime::AddProposalTopGoal() {
    return actions::ProposalActions(*impl_).AddTopGoal();
}

void AppRuntime::RemoveProposalSelected(core::RemoveMode mode) {
    actions::ProposalActions(*impl_).RemoveSelected(mode);
}

void AppRuntime::ScanDirectory() {
    impl_->project_controller->ScanDirectory();
}

const parser::AssuranceCase& AppRuntime::RefreshAgentChangePreview(const parser::AssuranceCase& committed) {
    ui::UiState& ui_state = ui::GetUiState();

    // Only change sets written against the argument that is open. Element ids
    // repeat across a project's arguments, and without this filter a change set
    // built against `main2.sacm` decorated `main.sacm`'s G1 -- the overlay
    // pointing at an element the agent had never looked at.
    std::vector<const core::changesets::ChangeSet*> open;
    for (const core::changesets::ChangeSet* candidate : impl_->agent_change_sets.Open()) {
        if (core::changesets::ChangeSetTargetsArgumentFile(*candidate, impl_->app_state.loaded_file_path)) {
            open.push_back(candidate);
        }
    }
    if (open.empty()) {
        ui_state.agent_change_status.clear();
        ui_state.agent_change_set_id.clear();
        ui_state.agent_change_set_title.clear();
        impl_->agent_preview_case.reset();
        impl_->agent_preview_added_ids.clear();
        return committed;
    }

    // One canvas can only show one proposal legibly. Two connected clients are
    // supported, but the newest open change set is the one drawn; the Review
    // panel lists them all so the other is not hidden.
    const core::changesets::ChangeSet& shown = *open.back();
    const core::changesets::ChangeSetDiff diff = core::changesets::ComputeChangeSetDiff(shown, committed);
    if (!diff.success) {
        // The argument moved under the change set. Showing a preview that cannot
        // be applied would invite the user to accept something that will be
        // refused, so fall back to the committed model; the Review panel reports
        // why.
        ui_state.agent_change_status.clear();
        impl_->agent_preview_case.reset();
        impl_->agent_preview_added_ids.clear();
        // Nothing of this change set is on the canvas, so nothing may claim it
        // is. These two fields are how the Review panel decides to print "Shown
        // on the canvas as you watch it build", and setting them here told the
        // reviewer a change set was being drawn while the canvas showed the
        // committed argument. The panel says why it does not apply from the
        // acceptability check, which does not go through here.
        ui_state.agent_change_set_id.clear();
        ui_state.agent_change_set_title.clear();
        return committed;
    }

    ui_state.agent_change_set_id = shown.id;
    ui_state.agent_change_set_title = shown.title;
    ui_state.agent_change_status.clear();
    impl_->agent_preview_added_ids.clear();
    for (const std::pair<const std::string, core::changesets::ElementChange>& entry : diff.status_by_id) {
        if (entry.second != core::changesets::ElementChange::Unchanged) {
            ui_state.agent_change_status[entry.first] = entry.second;
        }
        if (entry.second == core::changesets::ElementChange::Added) {
            impl_->agent_preview_added_ids.push_back(entry.first);
        }
    }

    impl_->agent_preview_case = diff.preview_model;
    // A removed element is absent from the preview by definition, so it is put
    // back for display only. A reviewer being asked to approve a deletion should
    // be able to see what is being deleted, in place, rather than infer it from
    // a gap.
    for (const parser::SacmElement& removed : diff.removed) {
        impl_->agent_preview_case->elements.push_back(removed);
    }
    return impl_->agent_preview_case.value();
}

const core::drafts::DraftWorkspace* AppRuntime::CurrentDraftWorkspace() const {
    return impl_->draft_workspace.workspace();
}

const parser::AssuranceCase& AppRuntime::CurrentArgumentView() {
    static const parser::AssuranceCase kEmpty;
    if (!impl_->app_state.loaded_case.has_value()) {
        return kEmpty;
    }
    const parser::AssuranceCase& accepted = impl_->app_state.loaded_case.value();

    // The draft document first (ADR 0016). It is the working argument whenever
    // it differs from the accepted one; a draft that has changed nothing is
    // indistinguishable from no draft, which is why the comparison rather than
    // the store's existence decides.
    //
    // Cached against the store's revision so the projection and the comparison
    // do not run on every caller in a frame -- this is asked several times per
    // frame by the canvas, the navigator and the inspector.
    if (impl_->DraftDocumentHasChanges())
        return impl_->draft_document_view;

    const core::drafts::DraftWorkspace* workspace = impl_->draft_workspace.workspace();
    if (workspace == nullptr || !workspace->has_active_groups()) {
        return accepted;
    }

    const std::shared_ptr<const core::drafts::DraftMaterializationResult> result =
        impl_->draft_workspace.MaterializeSnapshot(accepted, impl_->app_state.case_revision);
    // The canvas publishes a raw pointer for the duration of the frame. Keep
    // the immutable owner here so accepting or discarding the workspace from a
    // banner rendered earlier in that frame cannot free the model underneath
    // the remaining panels.
    impl_->draft_frame_materialization = result;
    if (!result->success) {
        // A draft that cannot be materialized shows the accepted argument, not a
        // partially applied one. The failure is reported against the group that
        // caused it; what must not happen is a half-applied safety argument
        // rendered as though it were the proposal.
        return accepted;
    }
    return result->working_model;
}

const parser::AssuranceCase& AppRuntime::CurrentCanvasView() {
    const parser::AssuranceCase& working = CurrentArgumentView();
    const core::drafts::DraftWorkspace* workspace = impl_->draft_workspace.workspace();
    const bool has_draft = impl_->DraftDocumentHasChanges() || (workspace != nullptr && workspace->has_active_groups());
    if (!has_draft || !impl_->app_state.loaded_case.has_value()) {
        return working;
    }

    switch (ui::GetUiState().draft_view_mode) {
    case ui::DraftViewMode::AcceptedBaseline:
        return impl_->app_state.loaded_case.value();
    case ui::DraftViewMode::ChangesOnly: {
        const core::drafts::DraftChangeIndex& index = CurrentDraftChangeIndex();
        impl_->draft_presentation_view = working;
        impl_->draft_presentation_view.elements.insert(
            impl_->draft_presentation_view.elements.end(), index.removed.begin(), index.removed.end());
        // Rebuilt only when the draft or the accepted model moved, which is the
        // same condition that invalidates the tree below it.
        impl_->draft_changes_only_view = core::drafts::BuildChangesOnlyView(impl_->draft_presentation_view, index);
        return impl_->draft_changes_only_view;
    }
    case ui::DraftViewMode::WorkingDraft: {
        const core::drafts::DraftChangeIndex& index = CurrentDraftChangeIndex();
        impl_->draft_presentation_view = working;
        impl_->draft_presentation_view.elements.insert(
            impl_->draft_presentation_view.elements.end(), index.removed.begin(), index.removed.end());
        return impl_->draft_presentation_view;
    }
    }
    return working;
}

void AppRuntime::RefreshDraftDecorations() {
    ui::UiState& ui_state = ui::GetUiState();
    ui_state.draft_element_status.clear();
    ui_state.draft_edge_status.clear();
    impl_->draft_added_ids.clear();

    const core::drafts::DraftWorkspace* workspace = impl_->draft_workspace.workspace();
    const bool document_backed = impl_->DraftDocumentHasChanges();
    if (!document_backed && (workspace == nullptr || !workspace->has_active_groups()))
        return;
    // In "accepted baseline" the canvas is deliberately showing what the user
    // has now. Marking it up with what is proposed would contradict the mode
    // they just selected.
    if (ui::GetUiState().draft_view_mode == ui::DraftViewMode::AcceptedBaseline)
        return;

    const core::drafts::DraftChangeIndex& index = CurrentDraftChangeIndex();
    const parser::AssuranceCase& working = CurrentArgumentView();

    for (const std::string& element_id : index.ChangedElementIds()) {
        const core::drafts::DraftElementEntry* entry = index.Find(element_id);
        if (entry == nullptr)
            continue;

        // Empty for a document-backed draft: the comparison records what changed,
        // not who changed it, so the decoration carries no source label until
        // provenance is read from the element's tagged values.
        const std::vector<std::string> contributors = index.ContributingGroupIds(element_id);
        ui::DraftNodeDecoration decoration;
        decoration.change = entry->change;
        decoration.multiple_contributions = contributors.size() > 1;
        if (!contributors.empty() && workspace != nullptr) {
            const core::drafts::DraftChangeGroup* group = workspace->FindGroup(contributors.back());
            if (group != nullptr) {
                decoration.source_label = group->source_label.empty()
                                              ? std::string(core::drafts::DraftSourceToString(group->source))
                                              : group->source_label;
            }
        }

        // Relationships are elements in the model, so they arrive here mixed in
        // with the claims. Split them out: an edge is marked on the edge, keyed
        // by its endpoints, because its own id is not stable across a rebuild.
        const parser::SacmElement* element = nullptr;
        for (const parser::SacmElement& candidate : working.elements) {
            if (candidate.id == element_id) {
                element = &candidate;
                break;
            }
        }
        if (element == nullptr && entry->change == core::drafts::DraftElementChange::Removed) {
            for (const parser::SacmElement& candidate : index.removed) {
                if (candidate.id == element_id) {
                    element = &candidate;
                    break;
                }
            }
        }
        const bool is_relationship =
            element != nullptr && (element->type == "assertedinference" || element->type == "assertedcontext" ||
                                   element->type == "assertedevidence");
        if (entry->change == core::drafts::DraftElementChange::Added)
            impl_->draft_added_ids.push_back(element_id);

        if (!is_relationship) {
            ui_state.draft_element_status.emplace(element_id, std::move(decoration));
            continue;
        }
        if (entry->change != core::drafts::DraftElementChange::Added &&
            entry->change != core::drafts::DraftElementChange::Removed)
            continue;

        ui::DraftEdgeDecoration edge;
        edge.change = entry->change;
        edge.contextual = element->type == "assertedcontext";
        edge.source_label = decoration.source_label;
        // The renderer keys an edge by parent-then-child, and SACM puts the
        // premise in `source_refs` and the conclusion in `target_refs`, so the
        // parent is the target.
        const std::string child = !element->source_refs.empty() ? element->source_refs.front() : element->reasoning_ref;
        if (child.empty() || element->target_refs.empty())
            continue;
        ui_state.draft_edge_status.emplace(element->target_refs.front() + "\x1f" + child, edge);
    }
}

namespace {

const parser::SacmElement* FindElementById(const parser::AssuranceCase& model, const std::string& element_id) {
    for (const parser::SacmElement& element : model.elements) {
        if (element.id == element_id)
            return &element;
    }
    return nullptr;
}

// The fields that actually differ, rather than one field chosen in advance.
//
// An element has a name, a content and a description, and a draft may touch any
// of them. Picking one to display showed "accepted" and "working draft" as the
// same text whenever the edit was to a different field -- which reads as the
// panel being broken rather than as the edit being elsewhere.
void CollectFieldChanges(const parser::SacmElement* accepted,
                         const parser::SacmElement* working,
                         std::vector<ui::DraftFieldChangeView>& out) {
    struct Field {
        const char* label;
        std::string parser::SacmElement::* member;
    };
    const Field fields[] = {
        {"Name", &parser::SacmElement::name},
        {"Content", &parser::SacmElement::content},
        {"Description", &parser::SacmElement::description},
    };
    for (const Field& field : fields) {
        const std::string before = accepted != nullptr ? accepted->*(field.member) : std::string{};
        const std::string after = working != nullptr ? working->*(field.member) : std::string{};
        if (before == after)
            continue;
        ui::DraftFieldChangeView change;
        change.field_label = AF_TR(field.label);
        change.accepted = before;
        change.working = after;
        out.push_back(std::move(change));
    }
}

} // namespace

void AppRuntime::RefreshSelectedDraftDetail() {
    ui::UiState& ui_state = ui::GetUiState();
    ui_state.draft_selected_detail = ui::DraftElementDetailView{};

    const core::drafts::DraftWorkspace* workspace = impl_->draft_workspace.workspace();
    const std::string& selected = ui_state.selected_element_id;
    if (workspace == nullptr || !workspace->has_active_groups() || selected.empty() ||
        !impl_->app_state.loaded_case.has_value()) {
        return;
    }

    const core::drafts::DraftChangeIndex& index = CurrentDraftChangeIndex();
    const core::drafts::DraftElementEntry* entry = index.Find(selected);
    if (entry == nullptr || entry->change == core::drafts::DraftElementChange::Unchanged)
        return;

    ui::DraftElementDetailView& detail = ui_state.draft_selected_detail;
    detail.present = true;
    detail.element_id = selected;
    detail.change = entry->change;
    CollectFieldChanges(FindElementById(impl_->app_state.loaded_case.value(), selected),
                        FindElementById(CurrentArgumentView(), selected),
                        detail.field_changes);

    for (const core::drafts::DraftElementContribution& contribution : entry->contributions) {
        const core::drafts::DraftChangeGroup* group = workspace->FindGroup(contribution.group_id);
        if (group == nullptr)
            continue;
        ui::DraftContributionView view;
        view.group_id = group->id;
        view.title = group->title;
        view.source_label = group->source_label.empty() ? std::string(core::drafts::DraftSourceToString(group->source))
                                                        : group->source_label;
        view.rationale = group->rationale;
        view.change = contribution.change;
        detail.contributions.push_back(std::move(view));
    }

    // What accepting this element would actually promote. Computed here, not at
    // the moment the button is pressed, so the user sees the real acceptance set
    // before they commit rather than after.
    const std::vector<std::string> contributors = index.ContributingGroupIds(selected);
    detail.contributing_group_ids = contributors;
    detail.closure_group_ids = core::drafts::DependencyClosure(*workspace, contributors);
    for (const std::string& group_id : detail.closure_group_ids) {
        if (std::find(contributors.begin(), contributors.end(), group_id) != contributors.end())
            continue;
        const core::drafts::DraftChangeGroup* group = workspace->FindGroup(group_id);
        if (group != nullptr)
            detail.also_accepts_titles.push_back(group->title.empty() ? group->id : group->title);
    }

    if (workspace->state == core::drafts::DraftWorkspaceState::NeedsRebase) {
        detail.blocked_reason = AF_TR("The argument changed since this draft was written.");
    } else if (workspace->state == core::drafts::DraftWorkspaceState::Promoting) {
        detail.blocked_reason = AF_TR("Promotion is awaiting durable SACM completion.");
    } else {
        const core::drafts::DraftPromotionPlan plan =
            core::drafts::PlanDraftPromotion(*workspace,
                                             impl_->app_state.loaded_case.value(),
                                             contributors,
                                             "preview",
                                             impl_->draft_workspace.authoritative_identities());
        if (!plan.ok)
            detail.blocked_reason = plan.error;
    }
}

const core::drafts::DraftChangeIndex& AppRuntime::CurrentDraftChangeIndex() {
    static const core::drafts::DraftChangeIndex kEmpty;
    // A document-backed draft answers this from the comparison (ADR 0016), which
    // is derived from the two documents and so cannot disagree with what is on
    // the canvas. Cached on the same key as the comparison itself.
    if (impl_->DraftDocumentHasChanges()) {
        if (impl_->draft_document_index_revision != impl_->draft_document_view_revision ||
            impl_->draft_document_index_case_revision != impl_->draft_document_view_case_revision) {
            impl_->draft_document_index = core::drafts::ChangeIndexFromDiff(impl_->draft_document_changes);
            impl_->draft_document_index_revision = impl_->draft_document_view_revision;
            impl_->draft_document_index_case_revision = impl_->draft_document_view_case_revision;
        }
        return impl_->draft_document_index;
    }
    const core::drafts::DraftWorkspace* workspace = impl_->draft_workspace.workspace();
    if (workspace == nullptr || !workspace->has_active_groups() || !impl_->app_state.loaded_case.has_value()) {
        return kEmpty;
    }
    const core::drafts::DraftMaterializationResult& result =
        impl_->draft_workspace.Materialize(impl_->app_state.loaded_case.value(), impl_->app_state.case_revision);
    return result.success ? result.change_index : kEmpty;
}

void AppRuntime::SyncDraftWorkspace() {
    const std::filesystem::path root = impl_->app_state.current_project.has_value()
                                           ? impl_->app_state.current_project->rootPath
                                           : std::filesystem::path{};
    const std::filesystem::path argument =
        impl_->app_state.loaded_case.has_value() ? impl_->app_state.loaded_file_path : std::filesystem::path{};

    std::unordered_set<std::string> authoritative_identities;
    if (impl_->app_state.library_document) {
        for (const sacm_adapter::DocumentElement& element :
             sacm_adapter::list_document_elements(*impl_->app_state.library_document)) {
            if (!element.id.empty())
                authoritative_identities.insert(element.id);
        }
    }
    impl_->draft_workspace.SetAuthoritativeIdentities(std::move(authoritative_identities));

    if (root == impl_->draft_workspace_root && argument == impl_->draft_workspace_argument) {
        return;
    }

    impl_->draft_workspace_root = root;
    impl_->draft_workspace_argument = argument;
    impl_->draft_workspace.SetProjectRoot(root);

    // A different argument is a different draft; a leftover "Changes only"
    // mode from the previous one would silently show a subset of the new
    // case. Every argument starts on the full working view.
    ui::GetUiState().draft_view_mode = ui::DraftViewMode::WorkingDraft;
    // And a refusal from the previous argument's draft says nothing about this
    // one. Revisions are per workspace, so the stamp alone would not expire it.
    ui::GetUiState().draft_accept_error.clear();
    ui::GetUiState().draft_accept_error_revision = 0;

    if (argument.empty() || !impl_->app_state.loaded_case.has_value()) {
        // Forgets the workspace without touching what is on disk, so the draft
        // is still there when the argument is opened again. Closing the
        // application is not a decision about unaccepted work.
        impl_->draft_workspace.Close();
        impl_->draft_document.Close();
        return;
    }

    std::string error;
    if (!impl_->draft_workspace.Open(argument, impl_->app_state.loaded_case.value(), error)) {
        // Recovery data that cannot be read is reported rather than deleted. The
        // work in it may be hours of an agent's conversation, and the file is the
        // only copy.
        impl_->app_state.status_message =
            ui::i18n::trf("Warning: could not read the draft for this argument: {0}", error);
    }

    // The draft document (ADR 0016). Opened from the accepted LIBRARY document
    // rather than the projection, because that document is what a draft is a
    // copy of -- projecting and re-serializing would drop whatever the flat
    // model has no field for, and the draft would differ from the argument it
    // was copied from before anyone edited it.
    //
    // Absent a library document there is nothing to copy, so the draft stays
    // closed rather than being faked from the projection.
    impl_->draft_document.Close();
    if (impl_->app_state.library_document != nullptr) {
        std::string draft_error;
        if (!impl_->draft_document.Open(root, argument, *impl_->app_state.library_document, draft_error)) {
            impl_->app_state.status_message =
                ui::i18n::trf("Warning: could not read the working draft for this argument: {0}", draft_error);
        }
    }

    // Snapshots accumulate one per promotion and are only consumed by an undo,
    // so a project that has accepted many drafts and undone none carries them
    // all. Opening is the natural moment to clear what the undo stack can no
    // longer reach -- the audit store and its baselines are loaded by then, and
    // the boundary is what decides.
    PrunePromotionSnapshots();
}

void AppRuntime::RebuildDerivedViewsIfNeeded() {
    // A library-primary (flipped) command committed its edit to the library but
    // deliberately did NOT rebuild the live loaded_case/sacm_package inside the
    // command bus -- doing so mid-dispatch replaces containers the canvas is still
    // rendering from this frame (it holds &loaded_case across the frame, and a
    // context-menu edit dispatches mid-render). Re-derive them here, at the top of
    // the frame before any panel renders -- the same deferred-to-next-frame remedy
    // `pending_reconcile_audit_store` uses for the same class of hazard. This runs
    // even when the proposal canvas is active (the edit is committed regardless).
    app::commands::ApplyPendingLibraryRederive(*impl_);

    if (impl_->IsProposalCanvasActive()) {
        return;
    }

    if (impl_->tree_needs_rebuild && !impl_->app_state.loaded_case.has_value()) {
        ui::RebuildRegisterViews(nullptr, impl_->register_controller->Store());
        // No argument means nothing to be orphaned against; drop the warnings
        // rather than leave them standing against a model that is gone.
        impl_->problems_dirty.registers = true;
        impl_->tree_edit_index = core::TreeEditIndex();
        impl_->tree_edit_index_valid = false;
        impl_->tree_needs_rebuild = false;
        return;
    }

    if (!impl_->app_state.loaded_case.has_value() || !impl_->tree_needs_rebuild) {
        return;
    }

    // Everything derived below is built from the *working* argument: the
    // accepted case with every active draft group applied. That is what makes a
    // proposal visible where it lands rather than as a list of operations beside
    // the diagram, and it is why two proposals are evaluated against each other
    // rather than each against the accepted model (ADR 0009).
    //
    // The accepted case is untouched; nothing here is saved.
    //
    // The change-set preview still layers on top, because MCP writes change sets
    // until phase 3 moves it onto draft groups. Stacking it on the working model
    // rather than on the accepted one means a connected client's staged work is
    // at least drawn against the draft it is really landing in.
    // The whole working argument, never the view-mode narrowing.
    //
    // Everything derived here is shared: the Argument Navigator, the tree edit
    // index, the register views and the Problems pipeline all read it. Feeding
    // them `CurrentCanvasView()` meant selecting "changes only" collapsed the
    // navigator to a handful of nodes and the project counts with it -- the
    // application believing the safety case had shrunk to whatever the canvas
    // happened to be showing. A view mode is a statement about the canvas, not
    // about the argument.
    const parser::AssuranceCase& working = CurrentArgumentView();
    RefreshDraftDecorations();
    const parser::AssuranceCase& ac = RefreshAgentChangePreview(working);

    const sacm::AssuranceCasePackage* sacm_package =
        impl_->app_state.has_projected_package() ? &impl_->app_state.projected_package() : nullptr;
    const std::optional<parser::AssuranceCase> filtered_case =
        FilterConfidencePackageElementsFromMainTree(ac, sacm_package);
    impl_->current_tree =
        ui::gsn::BuildAssuranceTree(filtered_case ? *filtered_case : ac, ui::GetUiState().active_secondary_lang);
    core::ApplyTreeDisplayOrder(impl_->current_tree, impl_->tree_display_order);
    impl_->tree_edit_index = core::BuildTreeEditIndex(ac);
    impl_->tree_edit_index_valid = true;
    ui::gsn::SetCanvasTree(impl_->current_tree);
    ui::RebuildRegisterViews(&ac, impl_->register_controller->Store());
    ui::GetUiState().model_has_translations = ui::ModelHasTranslations(ac);
    impl_->problems_dirty.terminology = true;
    impl_->problems_dirty.acp = true;
    impl_->problems_dirty.translation = true;

    // The Project Explorer's SACM package tree is cached per file, and for a
    // long time only the terminology actions invalidated it -- their edits were
    // the only thing that changed the package structure. Promotion can now
    // bring a TerminologyPackage into being (a glossary accepted from a draft),
    // and every model mutation funnels through this rebuild, so the loaded
    // file's cached tree is dropped here. Without this, an accepted glossary
    // existed in the saved file and the terminology panel's data but the
    // explorer -- the way a user navigates to it -- still showed the tree from
    // before the promotion. Only the loaded file's entry: the other files'
    // trees are rebuilt from disk, and this rebuild says nothing about them.
    if (impl_->app_state.current_project.has_value() && !impl_->app_state.loaded_file_path.empty()) {
        std::error_code relative_error;
        const std::filesystem::path relative = std::filesystem::relative(
            impl_->app_state.loaded_file_path, impl_->app_state.current_project->rootPath, relative_error);
        if (!relative_error && !relative.empty())
            impl_->sacm_package_tree_cache.erase(relative.generic_string());
    }

    // The accepted model is current again, so a promotion's machine-written
    // translations can now be found and flagged.
    if (!impl_->translation_review_marks_pending_rebuild.empty()) {
        const std::vector<std::string> marks = std::move(impl_->translation_review_marks_pending_rebuild);
        impl_->translation_review_marks_pending_rebuild.clear();
        for (const std::string& element_id : marks)
            MarkTranslationReviewPending(element_id);
    }
    // Any edit to the support structure can create or break a cycle, and this
    // rebuild runs on exactly those edits.
    impl_->problems_dirty.structure = true;
    // The same edits move claim-evidence pairings in and out of the argument,
    // which is what makes a stored assessment orphaned or whole again.
    impl_->problems_dirty.registers = true;

    if (impl_->workbench.pending_focus_root && impl_->current_tree.root) {
        ui::UiState& ui_state = ui::GetUiState();
        ui_state.selected_element_id = impl_->current_tree.root->id;
        ui_state.center_on_selection = true;
        ui_state.center_view = ui::CenterView::GsnCanvas;
        impl_->workbench.force_center_tab_selection = true;
        impl_->workbench.pending_focus_root = false;
    }

    impl_->tree_needs_rebuild = false;
}

void AppRuntime::RenderSacmViewerPanel(float left_w, float sacm_h, float top_y) {
    ui::panels::SacmViewerPanelModel model{
        impl_->app_state,
        impl_->project_controller->dir_path_buf,
        sizeof(impl_->project_controller->dir_path_buf),
        impl_->project_controller->file_path_buf,
        sizeof(impl_->project_controller->file_path_buf),
        impl_->project_controller->xml_files,
        impl_->project_controller->selected_file_idx,
        impl_->project_controller->show_overwrite_confirm,
    };
    ui::panels::SacmViewerPanelCallbacks callbacks{
        [this]() { ScanDirectory(); },
        [this]() {
            impl_->tree_needs_rebuild = true;
            impl_->workbench.pending_focus_root = true;
        },
        [this]() {
            impl_->current_tree = core::AssuranceTree();
            ui::gsn::SetCanvasTree(impl_->current_tree);
            ui::RebuildRegisterViews(nullptr, impl_->register_controller->Store());
            ui::GetUiState().selected_element_id.clear();
        },
    };
    ui::panels::ShowSacmViewerPanel(left_w, sacm_h, top_y, kPanelFlags, model, callbacks);
}

areas::WorkbenchAreaCallbacks AppRuntime::MakeWorkbenchAreaCallbacks() {
    return areas::WorkbenchAreaCallbacks{
        [this]() { return MakeElementContextActions(*this); },
        [this](core::NewElementKind kind) { AddProposalChildToSelected(kind); },
        [this]() { AddProposalTopGoal(); },
        [this](core::RemoveMode mode) { RemoveProposalSelected(mode); },
        [this](const char* feature) {
            if (feature)
                ShowNotImplementedModal(feature);
        },
        [this](const std::string& proposal_id) { return BeginEditProposalById(proposal_id); },
        [this](bool was_creator) {
            impl_->proposal_controller->ClearActiveState();
            ClearProposalHighlightState(ui::GetUiState());
            if (impl_->app_state.loaded_case.has_value()) {
                impl_->current_tree = ui::gsn::BuildAssuranceTree(impl_->app_state.loaded_case.value(),
                                                                  ui::GetUiState().active_secondary_lang);
                ui::gsn::SetCanvasTree(impl_->current_tree);
            } else {
                impl_->tree_needs_rebuild = true;
            }
            if (was_creator)
                SetStatus(AF_TR("Discarded proposal draft."));
        },
        [this](const core::TerminologyPackageRef& package_ref, const core::TerminologyTermRef& term_ref) {
            OpenTerminologyTermFromCanvas(package_ref, term_ref);
        },
        [this](const core::TerminologyPackageRef& package_ref, const core::TerminologyTermRef& term_ref) {
            EditTerminologyTermFromCanvas(package_ref, term_ref);
        },
        [this](const std::string& element_id, const std::string& term_value) {
            BeginQuickDefineTerminologyTerm(element_id, term_value);
        },
        [this](const std::string& element_id,
               const core::TerminologyPackageRef& package_ref,
               const core::TerminologyTermRef& term_ref) {
            AddTerminologyTermAsContextFromCanvas(element_id, package_ref, term_ref);
        },
        [this](const std::string& element_id,
               const core::TerminologyPackageRef& package_ref,
               const core::TerminologyTermRef& term_ref) {
            AddVisibleTerminologyTermContextFromCanvas(element_id, package_ref, term_ref);
        },
        [this](const core::TerminologyPackageRef& package_ref, const core::TerminologyTermRef& term_ref) {
            FindTerminologyUsagesFromCanvas(package_ref, term_ref);
        },
        [this](const std::string& element_id, const std::string& term_value) {
            ChangeTerminologyMeaningFromCanvas(element_id, term_value);
        },
        [this]() { ApplyTerminologyPackageEdits(); },
        [this]() { BeginDeleteTerminologyPackage(); },
        [this]() { BeginAddTerminologyTerm(); },
        [this](const core::TerminologyTermRef& term_ref) { SelectTerminologyTerm(term_ref); },
        [this](const core::TerminologyTermRef& term_ref) { BeginEditTerminologyTerm(term_ref); },
        [this](const core::TerminologyTermRef& term_ref) { BeginDeleteTerminologyTerm(term_ref); },
        [this](const core::TerminologyTermRef& term_ref) {
            BeginFindTerminologyUsages(impl_->terminology.selected_package_ref, term_ref);
        },
        [this](const std::string& category_filter) { SetTerminologyCategoryFilter(category_filter); },
        [this]() { BeginAddTerminologyCategory(); },
        [this](const core::TerminologyCategoryRef& category_ref) { SelectTerminologyCategory(category_ref); },
        [this](const core::TerminologyCategoryRef& category_ref) { BeginEditTerminologyCategory(category_ref); },
        [this](const core::TerminologyCategoryRef& category_ref) { BeginDeleteTerminologyCategory(category_ref); },
        [this]() { SeedRecommendedTerminologyCategories(); },
        [this]() {
            std::vector<ui::panels::IgnoredTerminologyEntry> entries;
            for (const actions::IgnoredSuggestionView& view : ListIgnoredTerminologySuggestions())
                entries.push_back(ui::panels::IgnoredTerminologyEntry{view.element_id, view.term});
            return entries;
        },
        [this](const std::string& element_id, const std::string& term) {
            RestoreTerminologySuggestion(element_id, term);
        },
        [this]() { impl_->pending_reconcile_audit_store = true; },
        [this]() {
            std::string summary;
            std::string error;
            if (PromoteWorkingDraft(summary, error)) {
                SetStatus(summary);
            } else {
                SetStatus(ui::i18n::trf("Could not accept the working draft: {0}", error));
            }
        },
        [this]() {
            std::string error;
            if (DiscardWorkingDraft(error)) {
                SetStatus(AF_TR("Discarded the working draft. The accepted argument is unchanged."));
            } else {
                SetStatus(ui::i18n::trf("Could not discard the working draft: {0}", error));
            }
        },
        [this](const std::string& element_id) { LocateElementOnCanvas(element_id); },
        [this](const std::string& evidence_id) { RemoveEvidence(evidence_id); },
        [this](const std::string& evidence_id, const std::string& location) {
            SetEvidenceLocation(evidence_id, location);
        },
        [this](const std::string& location) { OpenEvidenceLocation(location); },
        [this](const std::string& evidence_id, core::EvidenceAttribute attribute, const std::string& value) {
            SetEvidenceAttribute(evidence_id, attribute, value);
        },
        [this]() { MigrateEvidenceAssessments(); },
        [this](const std::string& evidence_id) { BrowseEvidenceLocation(evidence_id); },
        [this](const std::string& text, const std::string& claim_id) { CreateEvidence(text, claim_id); },
        [this](const std::string& evidence_id, const std::string& claim_id) { LinkEvidence(evidence_id, claim_id); },
        [this](const std::string& evidence_id, const std::string& claim_id) { UnlinkEvidence(evidence_id, claim_id); },
    };
}

void AppRuntime::RefreshDirtyProblems() {
    ProblemSyncDirty& dirty = impl_->problems_dirty;
    if (dirty.review) {
        SyncReviewProblems();
        dirty.review = false;
    }
    if (dirty.terminology) {
        SyncTerminologyProblems();
        dirty.terminology = false;
    }
    if (dirty.confidence) {
        SyncConfidenceProblems();
        dirty.confidence = false;
    }
    if (dirty.acp) {
        SyncAcpProblems();
        dirty.acp = false;
    }
    if (dirty.translation) {
        SyncTranslationReviewProblems();
        dirty.translation = false;
    }
    if (dirty.structure) {
        SyncStructureProblems();
        dirty.structure = false;
    }
    if (dirty.registers) {
        SyncRegisterProblems();
        dirty.registers = false;
    }
}

void AppRuntime::SyncReviewProblems() {
    app::SyncReviewProblems(impl_->problems_manager, impl_->review_controller->Items());
}

void AppRuntime::SyncTerminologyProblems() {
    // Over the working argument and its glossary (ADR 0016), as the structure
    // checks already are: a term an AI client defined in the draft answers a
    // finding about an undefined term, and a check reading the accepted glossary
    // kept reporting the finding until the accept.
    const parser::AssuranceCase* model = impl_->app_state.loaded_case.has_value() ? &CurrentArgumentView() : nullptr;
    const sacm::AssuranceCasePackage* package = impl_->WorkingPackage();
    app::SyncTerminologyProblems(
        impl_->problems_manager, model, package, [this](const std::string& element_id, const std::string& term_value) {
            return IsTerminologySuggestionIgnored(element_id, term_value);
        });
}

void AppRuntime::SyncConfidenceProblems() {
    const parser::AssuranceCase* model =
        impl_->app_state.loaded_case.has_value() ? &impl_->app_state.loaded_case.value() : nullptr;
    const core::confidence::ConfidenceStore* store =
        impl_->confidence_controller ? &impl_->confidence_controller->Store() : nullptr;
    const std::string source_id =
        impl_->confidence_controller ? impl_->confidence_controller->ActiveSourceId() : std::string{};
    app::SyncConfidenceProblems(impl_->problems_manager, model, store, source_id);
}

void AppRuntime::SyncAcpProblems() {
    const parser::AssuranceCase* model =
        impl_->app_state.loaded_case.has_value() ? &impl_->app_state.loaded_case.value() : nullptr;
    const sacm::AssuranceCasePackage* package =
        impl_->app_state.has_projected_package() ? &impl_->app_state.projected_package() : nullptr;
    app::SyncAcpProblems(impl_->problems_manager, model, package);
}

void AppRuntime::SyncStructureProblems() {
    // Structural validation, cycle detection and GSN well-formedness run over the
    // *working* argument, not the accepted one (ADR 0009).
    //
    // This is the half of the design that pays for itself: a strategy left
    // developing into nothing because one proposal removed the only child
    // another proposal added is invisible in either proposal alone. Checking the
    // combination is how it is seen before someone accepts it rather than after.
    const parser::AssuranceCase* model = impl_->app_state.loaded_case.has_value() ? &CurrentArgumentView() : nullptr;
    app::SyncStructureProblems(impl_->problems_manager, model);
}

void AppRuntime::SyncRegisterProblems() {
    const parser::AssuranceCase* model =
        impl_->app_state.loaded_case.has_value() ? &impl_->app_state.loaded_case.value() : nullptr;
    const core::registers::RegisterStore* store =
        impl_->register_controller ? &impl_->register_controller->Store() : nullptr;
    app::SyncRegisterProblems(impl_->problems_manager, model, store);
}

bool AppRuntime::SetManualReviewOk(const std::string& element_id, bool manual_ok) {
    if (element_id.empty()) {
        SetStatus(AF_TR("Select an element before changing manual review status."));
        return false;
    }
    if (!impl_->app_state.current_project.has_value()) {
        SetStatus(AF_TR("Open or create a project before changing review status."));
        return false;
    }
    if (!EnsureReviewItemStorage())
        return false;
    if (impl_->reviewer_name.empty()) {
        impl_->modal_coordinator->show_reviewer_name_prompt = true;
        SetStatus(AF_TR("Enter a reviewer name before changing review status."));
        return false;
    }
    if (!impl_->review_controller->SetManualReviewOk(
            element_id, manual_ok, impl_->reviewer_name, core::NowUtcString())) {
        SetStatus(AF_TR("Could not update manual review status."));
        return false;
    }
    return true;
}

void AppRuntime::RequestExit(bool& done) {
    // Commit any in-progress inspector text edit as an audited transaction
    // before anything else. A focused field that never saw ImGui's
    // deactivation transition (the user closed the window mid-edit, or
    // navigated away programmatically) has already mutated the model; if it
    // only reached disk via the un-audited SaveProject path below it would be
    // flagged as an audit-log divergence on the next open. Doing it here also
    // ensures the edit is persisted even when nothing else marked the project
    // dirty.
    FlushPendingTextEdits();

    // Stop serving AI clients before anything is flushed or torn down. An
    // operation arriving mid-shutdown would run against half-closed state, and
    // the endpoint record must not outlive the process that published it -- a
    // stale one sends the next adapter to a pipe nobody is listening on.
    if (impl_->agent_bridge != nullptr) {
        impl_->agent_bridge->Stop();
    }

    // Autosave on close: best-effort flush before exiting so the on-disk SACM
    // matches the user's latest in-memory edits. CommandBus already autosaves
    // per-transaction; SaveProject covers the remaining bypass paths (review
    // controller, confidence controller, and any mutation that only sets
    // document_dirty / has_unsaved_changes). If the flush fails, fall back to
    // the legacy save-before-exit modal so the user can decide what to do
    // instead of silently losing data.
    if (impl_->app_state.has_unsaved_changes) {
        if (!SaveProject()) {
            impl_->last_autosave_error = "Auto-flush on close failed: " + impl_->app_state.status_message;
            impl_->modal_coordinator->show_save_before_exit_modal = true;
            return;
        }
    }

    // Take a close-time snapshot so the next session has a natural boundary
    // for undo / history navigation at "this session's close." CreateUserSnapshot
    // derives its id from the latest transaction sequence, so a session that
    // produced no new transactions reuses the existing snapshot id and the call
    // is naturally a no-op. Failures here are non-fatal — close still proceeds.
    if (impl_->app_state.current_project.has_value()) {
        core::audit::SnapshotMetadata snapshot;
        std::string snapshot_error;
        const std::string created_by = impl_->reviewer_name.empty() ? std::string("system") : impl_->reviewer_name;
        (void)core::audit::CreateUserSnapshot(impl_->app_state.current_project->rootPath,
                                              "automatic close snapshot",
                                              created_by,
                                              snapshot,
                                              snapshot_error);
    }

    done = true;
}

const parser::AssuranceCase* AppRuntime::GetLoadedCase() const {
    if (!impl_->app_state.loaded_case.has_value())
        return nullptr;
    return &impl_->app_state.loaded_case.value();
}

void AppRuntime::LoadRecentProjectsPreference(const std::string& content) {
    impl_->project_controller->LoadRecentProjectsPreference(content);
}

std::string AppRuntime::RecentProjectsPreferenceJson() const {
    return impl_->project_controller->RecentProjectsPreferenceJson();
}

void AppRuntime::LoadReviewerNamePreference(const std::string& content) {
    impl_->reviewer_name = core::TrimWhitespace(content);
    ui::CopyToBuffer(impl_->reviewer_name_buf, sizeof(impl_->reviewer_name_buf), impl_->reviewer_name);
    impl_->modal_coordinator->show_reviewer_name_prompt = impl_->reviewer_name.empty();
}

std::string AppRuntime::ReviewerNamePreference() const {
    return impl_->reviewer_name;
}

} // namespace app
